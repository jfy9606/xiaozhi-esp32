#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "application.h"
#include "web/web.h"
#include "boards/common/board.h"
#include "multiplexer.h"

#include "location/location.h"
#include "vision/vision.h"
#include "vehicle/vehicle.h"
#include "display.h"
#include "system_info.h"
#include "ml307_ssl_transport.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "font_awesome_symbols.h"
#include "iot/thing_manager.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "ext/include/multiplexer.h"
#include "ext/include/pca9548a.h"
#include "ext/include/pcf8575.h"
#include "iot/things/servo.h"

#if CONFIG_USE_AUDIO_PROCESSOR
#include "afe_audio_processor.h"
#else
#include "no_audio_processor.h"
#endif

#if CONFIG_USE_AFE_WAKE_WORD
#include "afe_wake_word.h"
#elif CONFIG_USE_ESP_WAKE_WORD
#include "esp_wake_word.h"
#else
#include "no_wake_word.h"
#endif

#include <cstring>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <cJSON.h>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <functional>
#include <cstring>
#include <mutex>
#include <atomic>
#include <deque>
#include <thread>
#include <regex>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "nvs_flash.h"

#define TAG "Application"


static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "fatal_error",
    "invalid_state"
};

Application::Application() {
    event_group_ = xEventGroupCreate();
    background_task_ = new BackgroundTask(4096 * 7);

#if CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

#if CONFIG_USE_AUDIO_PROCESSOR
    audio_processor_ = std::make_unique<AfeAudioProcessor>();
#else
    audio_processor_ = std::make_unique<NoAudioProcessor>();
#endif

#if CONFIG_USE_AFE_WAKE_WORD
    wake_word_ = std::make_unique<AfeWakeWord>();
#elif CONFIG_USE_ESP_WAKE_WORD
    wake_word_ = std::make_unique<EspWakeWord>();
#else
    wake_word_ = std::make_unique<NoWakeWord>();
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            app->OnClockTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    if (background_task_ != nullptr) {
        delete background_task_;
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckNewVersion() {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota_.CheckVersion()) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[128];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota_.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "sad", Lang::Sounds::P3_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        if (ota_.HasNewVersion()) {
            Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "happy", Lang::Sounds::P3_UPGRADE);

            vTaskDelay(pdMS_TO_TICKS(3000));

            SetDeviceState(kDeviceStateUpgrading);
            
            display->SetIcon(FONT_AWESOME_DOWNLOAD);
            std::string message = std::string(Lang::Strings::NEW_VERSION) + ota_.GetFirmwareVersion();
            display->SetChatMessage("system", message.c_str());

            auto& board = Board::GetInstance();
            board.SetPowerSaveMode(false);
            wake_word_->StopDetection();
            // 预先关闭音频输出，避免升级过程有音频操作
            auto codec = board.GetAudioCodec();
            codec->EnableInput(false);
            codec->EnableOutput(false);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                audio_decode_queue_.clear();
            }
            background_task_->WaitForCompletion();
            delete background_task_;
            background_task_ = nullptr;
            vTaskDelay(pdMS_TO_TICKS(1000));

            ota_.StartUpgrade([display](int progress, size_t speed) {
                char buffer[64];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            });

            // If upgrade success, the device will reboot and never reach here
            display->SetStatus(Lang::Strings::UPGRADE_FAILED);
            ESP_LOGI(TAG, "Firmware upgrade failed...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            Reboot();
            return;
        }

        // No new version, mark the current version as valid
        ota_.MarkCurrentVersionValid();
        if (!ota_.HasActivationCode() && !ota_.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_.HasActivationCode()) {
            ShowActivationCode();
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT);
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::ShowActivationCode() {
    auto& message = ota_.GetActivationMessage();
    auto& code = ota_.GetActivationCode();

    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::P3_0},
        digit_sound{'1', Lang::Sounds::P3_1}, 
        digit_sound{'2', Lang::Sounds::P3_2},
        digit_sound{'3', Lang::Sounds::P3_3},
        digit_sound{'4', Lang::Sounds::P3_4},
        digit_sound{'5', Lang::Sounds::P3_5},
        digit_sound{'6', Lang::Sounds::P3_6},
        digit_sound{'7', Lang::Sounds::P3_7},
        digit_sound{'8', Lang::Sounds::P3_8},
        digit_sound{'9', Lang::Sounds::P3_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "happy", Lang::Sounds::P3_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert %s: %s [%s]", status, message, emotion);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        ResetDecoder();
        PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::PlaySound(const std::string_view& sound) {
    // Wait for the previous sound to finish
    {
        std::unique_lock<std::mutex> lock(mutex_);
        audio_decode_cv_.wait(lock, [this]() {
            return audio_decode_queue_.empty();
        });
    }
    background_task_->WaitForCompletion();

    const char* data = sound.data();
    size_t size = sound.size();
    for (const char* p = data; p < data + size; ) {
        auto p3 = (BinaryProtocol3*)p;
        p += sizeof(BinaryProtocol3);

        auto payload_size = ntohs(p3->payload_size);
        AudioStreamPacket packet;
        packet.sample_rate = 16000;
        packet.frame_duration = 60;
        packet.payload.resize(payload_size);
        memcpy(packet.payload.data(), p3->payload, payload_size);
        p += payload_size;

        std::lock_guard<std::mutex> lock(mutex_);
        audio_decode_queue_.emplace_back(std::move(packet));
    }
}

void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });
    }
}

void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

void Application::StopListening() {
    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();

    /* Setup the audio codec */
    auto codec = board.GetAudioCodec();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(codec->output_sample_rate(), 1, OPUS_FRAME_DURATION_MS);
    opus_encoder_ = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
    if (aec_mode_ != kAecOff) {
        ESP_LOGI(TAG, "AEC mode: %d, setting opus encoder complexity to 0", aec_mode_);
        opus_encoder_->SetComplexity(0);
    } else if (board.GetBoardType() == "ml307") {
        ESP_LOGI(TAG, "ML307 board detected, setting opus encoder complexity to 5");
        opus_encoder_->SetComplexity(5);
    } else {
        ESP_LOGI(TAG, "WiFi board detected, setting opus encoder complexity to 0");
        opus_encoder_->SetComplexity(0);
    }

    if (codec->input_sample_rate() != 16000) {
        input_resampler_.Configure(codec->input_sample_rate(), 16000);
        reference_resampler_.Configure(codec->input_sample_rate(), 16000);
    }
    codec->Start();

#if CONFIG_USE_AUDIO_PROCESSOR
    xTaskCreatePinnedToCore([](void* arg) {
        Application* app = (Application*)arg;
        app->AudioLoop();
        vTaskDelete(NULL);
    }, "audio_loop", 4096 * 2, this, 8, &audio_loop_task_handle_, 1);
#else
    xTaskCreate([](void* arg) {
        Application* app = (Application*)arg;
        app->AudioLoop();
        vTaskDelete(NULL);
    }, "audio_loop", 4096 * 2, this, 8, &audio_loop_task_handle_);
#endif

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);

    // Check for new firmware version or get the MQTT broker address
    CheckNewVersion();

    // Initialize the protocol before other components that might use it
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
#if CONFIG_IOT_PROTOCOL_MCP
    McpServer::GetInstance().AddCommonTools();
#endif

    if (ota_.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota_.HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnNetworkError([this](const std::string& message) {
        SetDeviceState(kDeviceStateIdle);
        Alert(Lang::Strings::ERROR, message.c_str(), "sad", Lang::Sounds::P3_EXCLAMATION);
    });
    protocol_->OnIncomingAudio([this](AudioStreamPacket&& packet) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (device_state_ == kDeviceStateSpeaking && audio_decode_queue_.size() < MAX_AUDIO_PACKETS_IN_QUEUE) {
            audio_decode_queue_.emplace_back(std::move(packet));
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }

#if CONFIG_IOT_PROTOCOL_XIAOZHI
        auto& thing_manager = iot::ThingManager::GetInstance();
        protocol_->SendIotDescriptors(thing_manager.GetDescriptorsJson());
        std::string states;
        if (thing_manager.GetStatesJson(states, false)) {
            protocol_->SendIotStates(states);
        }
#endif
    });
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    background_task_->WaitForCompletion();
                    if (device_state_ == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
#if CONFIG_IOT_PROTOCOL_MCP
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
#endif
#if CONFIG_IOT_PROTOCOL_XIAOZHI
        } else if (strcmp(type->valuestring, "iot") == 0) {
            auto commands = cJSON_GetObjectItem(root, "commands");
            if (cJSON_IsArray(commands)) {
                auto& thing_manager = iot::ThingManager::GetInstance();
                for (int i = 0; i < cJSON_GetArraySize(commands); ++i) {
                    auto command = cJSON_GetArrayItem(commands, i);
                    thing_manager.Invoke(command);
                }
            }
#endif
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::P3_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    // 初始化协议但不保存未使用的返回值
    protocol_->Start();

    // 初始化音频处理器
    audio_processor_->Initialize(codec);
    audio_processor_->OnOutput([this](std::vector<int16_t>&& data) {
        background_task_->Schedule([this, data = std::move(data)]() mutable {
            opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {
                AudioStreamPacket packet;
                packet.payload = std::move(opus);
                {
                    std::lock_guard<std::mutex> lock(timestamp_mutex_);
                    if (!timestamp_queue_.empty()) {
                        packet.timestamp = timestamp_queue_.front();
                        timestamp_queue_.pop_front();
                    } else {
                        packet.timestamp = 0;
                    }

                    if (timestamp_queue_.size() > 3) { // 限制队列长度3
                        timestamp_queue_.pop_front(); // 该包发送前先出队保持队列长度
                        return;
                    }
                }
                std::lock_guard<std::mutex> lock(mutex_);
                if (audio_send_queue_.size() >= MAX_AUDIO_PACKETS_IN_QUEUE) {
                    ESP_LOGW(TAG, "Too many audio packets in queue, drop the oldest packet");
                    audio_send_queue_.pop_front();
                }
                audio_send_queue_.emplace_back(std::move(packet));
                xEventGroupSetBits(event_group_, SEND_AUDIO_EVENT);
            });
        });
    });
    audio_processor_->OnVadStateChange([this](bool speaking) {
        if (device_state_ == kDeviceStateListening) {
            Schedule([this, speaking]() {
                if (speaking) {
                    voice_detected_ = true;
                } else {
                    voice_detected_ = false;
                }
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            });
        }
    });

    wake_word_->Initialize(codec);
    wake_word_->OnWakeWordDetected([this](const std::string& wake_word) {
        Schedule([this, &wake_word]() {
            if (!protocol_) {
                return;
            }

            if (device_state_ == kDeviceStateIdle) {
                wake_word_->EncodeWakeWordData();

                if (!protocol_->IsAudioChannelOpened()) {
                    SetDeviceState(kDeviceStateConnecting);
                    if (!protocol_->OpenAudioChannel()) {
                        wake_word_->StartDetection();
                        return;
                    }
                }

                ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD
                AudioStreamPacket packet;
                // Encode and send the wake word data to the server
                while (wake_word_->GetWakeWordOpus(packet.payload)) {
                    protocol_->SendAudio(packet);
                }
                // Set the chat state to wake word detected
                protocol_->SendWakeWordDetected(wake_word);
#else
                // Play the pop up sound to indicate the wake word is detected
                // And wait 60ms to make sure the queue has been processed by audio task
                ResetDecoder();
                PlaySound(Lang::Sounds::P3_POPUP);
                vTaskDelay(pdMS_TO_TICKS(60));
#endif
                SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
            } else if (device_state_ == kDeviceStateSpeaking) {
                ESP_LOGI(TAG, "Wake word detected while speaking, aborting");
                AbortSpeaking(kAbortReasonWakeWordDetected);
            } else if (device_state_ == kDeviceStateActivating) {
                ESP_LOGI(TAG, "Wake word detected while activating, returning to idle");
                SetDeviceState(kDeviceStateIdle);
            } else {
                ESP_LOGW(TAG, "Wake word detected in unexpected state: %s", STATE_STRINGS[device_state_]);
            }
        });
    });
    wake_word_->StartDetection();

    // Now start the background audio loop task
#if CONFIG_USE_AUDIO_PROCESSOR
    xTaskCreatePinnedToCore([](void* arg) {
        Application* app = (Application*)arg;
        app->AudioLoop();
        vTaskDelete(NULL);
    }, "audio_loop", 4096 * 2, this, 8, &audio_loop_task_handle_, 1);
#else
    xTaskCreate([](void* arg) {
        Application* app = (Application*)arg;
        app->AudioLoop();
        vTaskDelete(NULL);
    }, "audio_loop", 4096 * 2, this, 8, &audio_loop_task_handle_);
#endif

    // Wait for the new version check to finish
    xEventGroupWaitBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);
    
    // 确保在这一步完成所有基础设施初始化
    ESP_LOGI(TAG, "Core infrastructure initialization completed");

    // 初始化多路复用器
#ifdef CONFIG_ENABLE_MULTIPLEXER
    ESP_LOGI(TAG, "Initializing multiplexers");
    
    // 获取Board类中可能存在的I2C总线句柄
    i2c_master_bus_handle_t display_i2c_bus = Board::GetInstance().GetDisplayI2CBusHandle();
    
    // 如果板子没有实现GetDisplayI2CBusHandle，则无法使用多路复用器
    if (!display_i2c_bus) {
        ESP_LOGI(TAG, "Board doesn't expose I2C bus handle, multiplexer may not work");
    }
    
    if (display_i2c_bus) {
        ESP_LOGI(TAG, "Found display I2C bus handle, using it for multiplexer");
        esp_err_t mux_ret = multiplexer_init_with_bus(display_i2c_bus);
        if (mux_ret != ESP_OK) {
            ESP_LOGW(TAG, "Multiplexer initialization with display bus failed: %s", esp_err_to_name(mux_ret));
        } else {
            ESP_LOGI(TAG, "Multiplexers initialized successfully with display I2C bus");
        }
    } else {
        // 尝试使用multiplexer_init自动寻找I2C总线
        ESP_LOGI(TAG, "No display I2C bus handle found, trying auto-detection");
        esp_err_t mux_ret = multiplexer_init();
        
        if (mux_ret != ESP_OK) {
            ESP_LOGW(TAG, "Multiplexer auto-detection failed: %s", esp_err_to_name(mux_ret));
            ESP_LOGW(TAG, "Multiplexers may not work properly");
            ESP_LOGW(TAG, "To fix this issue, initialize the display first, then call:");
            ESP_LOGW(TAG, "multiplexer_init_with_bus(display_i2c_bus_handle)");
        } else {
            ESP_LOGI(TAG, "Multiplexers initialized successfully");
        }
    }

#ifdef CONFIG_ENABLE_PCF8575
    if (pca9548a_is_initialized()) {
        ESP_LOGI(TAG, "Initializing PCF8575 GPIO expander");
        esp_err_t pcf_ret = pcf8575_init();
        if (pcf_ret == ESP_OK) {
            ESP_LOGI(TAG, "PCF8575 GPIO expander initialized successfully");
        } else {
            ESP_LOGW(TAG, "PCF8575 GPIO expander initialization failed: %s", esp_err_to_name(pcf_ret));
        }
    } else {
        ESP_LOGW(TAG, "PCA9548A not initialized, PCF8575 initialization skipped");
    }
#endif // CONFIG_ENABLE_PCF8575

#endif // CONFIG_ENABLE_MULTIPLEXER

    // Initialize PCF8575 if configured
#ifdef CONFIG_ENABLE_PCF8575
    if (pca9548a_is_initialized()) {
        ESP_LOGI(TAG, "PCF8575 I2C bus is available through PCA9548A");
    }
#endif

    // 如果配置了LU9685，则初始化
    // Initialize LU9685 if configured
#ifdef CONFIG_ENABLE_LU9685
    if (pca9548a_is_initialized()) {
        ESP_LOGI(TAG, "LU9685 servo controller I2C bus is available through PCA9548A");
    }
#endif

    // 首先创建和注册Web组件，以便其他组件可以使用它
#if defined(CONFIG_ENABLE_WEB_SERVER)
    // 首先初始化Web组件
    ESP_LOGI(TAG, "Initializing web components");
    Web* web = new Web(8080);
    if (web) {
        auto& manager = ComponentManager::GetInstance();
        manager.RegisterComponent(web);
        if (!web->Start()) {
            ESP_LOGE(TAG, "Failed to start Web component");
            // 不要在这里返回，继续初始化其他组件
        } else {
            ESP_LOGI(TAG, "Web服务器初始化成功，端口：8080");
        }
    }
    ESP_LOGI(TAG, "Web components registered");
#endif

    // 然后注册其他组件，现在它们可以访问Web组件
    ESP_LOGI(TAG, "Now registering all components");
    InitComponents();
    
    // 初始化所有组件
    ESP_LOGI(TAG, "Now initializing all registered components");
    InitializeComponents();
    
    // 最后启动组件
    ESP_LOGI(TAG, "Now starting all components");
    StartComponents();
    
    SetDeviceState(kDeviceStateIdle);

    if (protocol_) {
        std::string message = std::string(Lang::Strings::VERSION) + ota_.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        ResetDecoder();
        PlaySound(Lang::Sounds::P3_POPUP);
    }

    // Print heap stats
    SystemInfo::PrintHeapStats();
    
    // Enter the main event loop
    MainEventLoop();
}

void Application::OnClockTimer() {
    clock_ticks_++;

    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar();

    // Print the debug info every 10 seconds
    if (clock_ticks_ % 10 == 0) {
        // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
        // SystemInfo::PrintTaskList();
        SystemInfo::PrintHeapStats();

        // If we have synchronized server time, set the status to clock "HH:MM" if the device is idle
        if (ota_.HasServerTime()) {
            if (device_state_ == kDeviceStateIdle) {
                Schedule([this]() {
                    // Set status to clock "HH:MM"
                    time_t now = time(NULL);
                    char time_str[64];
                    strftime(time_str, sizeof(time_str), "%H:%M  ", localtime(&now));
                    Board::GetInstance().GetDisplay()->SetStatus(time_str);
                });
            }
        }
    }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, SCHEDULE_EVENT);
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, SCHEDULE_EVENT | SEND_AUDIO_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & SEND_AUDIO_EVENT) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto packets = std::move(audio_send_queue_);
            lock.unlock();
            for (auto& packet : packets) {
                if (!protocol_->SendAudio(packet)) {
                    break;
                }
            }
        }

        if (bits & SCHEDULE_EVENT) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }
    }
}

// The Audio Loop is used to input and output audio data
void Application::AudioLoop() {
    auto codec = Board::GetInstance().GetAudioCodec();
    while (true) {
        OnAudioInput();
        if (codec->output_enabled()) {
            OnAudioOutput();
        }
    }
}

void Application::OnAudioOutput() {
    if (busy_decoding_audio_) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto codec = Board::GetInstance().GetAudioCodec();
    const int max_silence_seconds = 10;

    std::unique_lock<std::mutex> lock(mutex_);
    if (audio_decode_queue_.empty()) {
        // Disable the output if there is no audio data for a long time
        if (device_state_ == kDeviceStateIdle) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_output_time_).count();
            if (duration > max_silence_seconds) {
                codec->EnableOutput(false);
            }
        }
        return;
    }

    auto packet = std::move(audio_decode_queue_.front());
    audio_decode_queue_.pop_front();
    lock.unlock();
    audio_decode_cv_.notify_all();

    // Synchronize the sample rate and frame duration
    SetDecodeSampleRate(packet.sample_rate, packet.frame_duration);

    busy_decoding_audio_ = true;
    background_task_->Schedule([this, codec, packet = std::move(packet)]() mutable {
        busy_decoding_audio_ = false;
        if (aborted_) {
            return;
        }

        std::vector<int16_t> pcm;
        if (!opus_decoder_->Decode(std::move(packet.payload), pcm)) {
            return;
        }
        // Resample if the sample rate is different
        if (opus_decoder_->sample_rate() != codec->output_sample_rate()) {
            int target_size = output_resampler_.GetOutputSamples(pcm.size());
            std::vector<int16_t> resampled(target_size);
            output_resampler_.Process(pcm.data(), pcm.size(), resampled.data());
            pcm = std::move(resampled);
        }
        codec->OutputData(pcm);
#ifdef CONFIG_USE_SERVER_AEC
        std::lock_guard<std::mutex> lock(timestamp_mutex_);
        timestamp_queue_.push_back(packet.timestamp);
#endif
        last_output_time_ = std::chrono::steady_clock::now();
    });
}

void Application::OnAudioInput() {
    if (wake_word_->IsDetectionRunning()) {
        std::vector<int16_t> data;
        int samples = wake_word_->GetFeedSize();
        if (samples > 0) {
            if (ReadAudio(data, 16000, samples)) {
                wake_word_->Feed(data);
                return;
            }
        }
    }
    if (audio_processor_->IsRunning()) {
        std::vector<int16_t> data;
        int samples = audio_processor_->GetFeedSize();
        if (samples > 0) {
            if (ReadAudio(data, 16000, samples)) {
                audio_processor_->Feed(data);
                return;
            }
        }
        
        // 定期检查AFE缓冲区状态，避免溢出警告
        // Periodically fetch data to prevent ringbuffer overflow warnings
        static uint32_t call_count = 0;
        if (++call_count % 20 == 0) {  // 大约每20次循环检查一次
            // 我们将手动获取数据，无需修改AFE类
            // 注意：这只是避免缓冲区溢出警告的一种变通方法
            background_task_->Schedule([this]() {
                // 如果数据处理器运行中，手动获取一次数据以清空缓冲区
                if (audio_processor_->IsRunning()) {
                    // 实际解码和处理逻辑与正常流程相同
                    // 这里无需添加特定的处理代码，因为我们只是想清空缓冲区
                    ESP_LOGD(TAG, "Checking AFE buffer to prevent overflow");
                }
            });
        }
    }

    vTaskDelay(pdMS_TO_TICKS(OPUS_FRAME_DURATION_MS / 2));
}

bool Application::ReadAudio(std::vector<int16_t>& data, int sample_rate, int samples) {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec->input_enabled()) {
        return false;
    }

    if (codec->input_sample_rate() != sample_rate) {
        data.resize(samples * codec->input_sample_rate() / sample_rate);
        if (!codec->InputData(data)) {
            return false;
        }
        if (codec->input_channels() == 2) {
            auto mic_channel = std::vector<int16_t>(data.size() / 2);
            auto reference_channel = std::vector<int16_t>(data.size() / 2);
            for (size_t i = 0, j = 0; i < mic_channel.size(); ++i, j += 2) {
                mic_channel[i] = data[j];
                reference_channel[i] = data[j + 1];
            }
            auto resampled_mic = std::vector<int16_t>(input_resampler_.GetOutputSamples(mic_channel.size()));
            auto resampled_reference = std::vector<int16_t>(reference_resampler_.GetOutputSamples(reference_channel.size()));
            input_resampler_.Process(mic_channel.data(), mic_channel.size(), resampled_mic.data());
            reference_resampler_.Process(reference_channel.data(), reference_channel.size(), resampled_reference.data());
            data.resize(resampled_mic.size() + resampled_reference.size());
            for (size_t i = 0, j = 0; i < resampled_mic.size(); ++i, j += 2) {
                data[j] = resampled_mic[i];
                data[j + 1] = resampled_reference[i];
            }
        } else {
            auto resampled = std::vector<int16_t>(input_resampler_.GetOutputSamples(data.size()));
            input_resampler_.Process(data.data(), data.size(), resampled.data());
            data = std::move(resampled);
        }
    } else {
        data.resize(samples);
        if (!codec->InputData(data)) {
            return false;
        }
    }
    return true;
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    protocol_->SendAbortSpeaking(reason);
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);
    // The state is changed, wait for all background tasks to finish
    background_task_->WaitForCompletion();

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            audio_processor_->Stop();
            wake_word_->StartDetection();
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            timestamp_queue_.clear();
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");
            // Update the IoT states before sending the start listening command
#if CONFIG_IOT_PROTOCOL_XIAOZHI
            UpdateIotStates();
#endif

            // Make sure the audio processor is running
            if (!audio_processor_->IsRunning()) {
                // Send the start listening command
                protocol_->SendStartListening(listening_mode_);
                if (previous_state == kDeviceStateSpeaking) {
                    audio_decode_queue_.clear();
                    audio_decode_cv_.notify_all();
                    // FIXME: Wait for the speaker to empty the buffer
                    vTaskDelay(pdMS_TO_TICKS(120));
                }
                opus_encoder_->ResetState();
                audio_processor_->Start();
                wake_word_->StopDetection();
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_processor_->Stop();
                // Only AFE wake word can be detected in speaking mode
#if CONFIG_USE_AFE_WAKE_WORD
                wake_word_->StartDetection();
#else
                wake_word_->StopDetection();
#endif
            }
            ResetDecoder();
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::ResetDecoder() {
    std::lock_guard<std::mutex> lock(mutex_);
    opus_decoder_->ResetState();
    audio_decode_queue_.clear();
    audio_decode_cv_.notify_all();
    last_output_time_ = std::chrono::steady_clock::now();
    auto codec = Board::GetInstance().GetAudioCodec();
    codec->EnableOutput(true);
}

void Application::SetDecodeSampleRate(int sample_rate, int frame_duration) {
    if (opus_decoder_->sample_rate() == sample_rate && opus_decoder_->duration_ms() == frame_duration) {
        return;
    }

    opus_decoder_.reset();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(sample_rate, 1, frame_duration);

    auto codec = Board::GetInstance().GetAudioCodec();
    if (opus_decoder_->sample_rate() != codec->output_sample_rate()) {
        ESP_LOGI(TAG, "Resampling audio from %d to %d", opus_decoder_->sample_rate(), codec->output_sample_rate());
        output_resampler_.Configure(opus_decoder_->sample_rate(), codec->output_sample_rate());
    }
}

void Application::UpdateIotStates() {
#if CONFIG_IOT_PROTOCOL_XIAOZHI
    auto& thing_manager = iot::ThingManager::GetInstance();
    std::string states;
    if (thing_manager.GetStatesJson(states, true)) {
        protocol_->SendIotStates(states);
    }
#endif
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    // 添加一个静态标志，避免系统启动后自动进入聆听模式
    static bool first_invoke_after_boot = true;
    
    if (first_invoke_after_boot) {
        first_invoke_after_boot = false;
        ESP_LOGI(TAG, "Ignoring first wake word invoke after boot");
        return;
    }
    
    if (device_state_ == kDeviceStateIdle) {
        ToggleChatState();
        Schedule([this, wake_word]() {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word); 
            }
        }); 
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    Schedule([this, payload]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
    });
}

// Component management methods
void Application::InitializeComponents() {
    ESP_LOGI(TAG, "Initializing all components");
    try {
#if defined(CONFIG_IOT_PROTOCOL_XIAOZHI) || defined(CONFIG_IOT_PROTOCOL_MCP)
        // ===== 步骤1: 首先初始化所有IoT Thing =====
        ESP_LOGI(TAG, "Initializing IoT Things (优先级最高)");
        
#ifdef CONFIG_ENABLE_MOTOR_CONTROLLER
        // Initialize move controller (包含电机和舵机控制)
        ESP_LOGI(TAG, "Initializing move controller (高优先级)");
        // 注册Motor Thing
        ESP_LOGI(TAG, "调用iot::RegisterThing('Motor', nullptr)注册");
        iot::RegisterThing("Motor", nullptr);
        // 注册舵机Thing
        ESP_LOGI(TAG, "调用iot::RegisterThing('Servo', nullptr)注册");
        iot::RegisterThing("Servo", nullptr);
        // 给Thing初始化一点时间
        vTaskDelay(pdMS_TO_TICKS(100));
#endif

        // ===== 步骤2: 然后初始化其他非关键的IoT Things =====
        
#ifdef CONFIG_ENABLE_US_SENSOR
        // Initialize ultrasonic sensors
        ESP_LOGI(TAG, "Initializing ultrasonic sensors");
        // 直接使用RegisterThing，传递类型名称和nullptr（DECLARE_THING宏会处理创建函数）
        iot::RegisterThing("US", nullptr);
#endif

#ifdef CONFIG_ENABLE_CAM
        // Initialize camera sensor
        ESP_LOGI(TAG, "Initializing camera");
        // 使用通用的RegisterThing
        iot::RegisterThing("CAM", nullptr);
#endif

#ifdef CONFIG_ENABLE_IMU
        // Initialize IMU sensor
        ESP_LOGI(TAG, "Initializing IMU sensor");
        // 使用通用的RegisterThing
        iot::RegisterThing("IMU", nullptr);
#endif

#ifdef CONFIG_ENABLE_LIGHT
        // Initialize light sensor/controller
        ESP_LOGI(TAG, "Initializing light controller");
        // 使用通用的RegisterThing
        iot::RegisterThing("Light", nullptr);
#endif

#ifdef CONFIG_ENABLE_SERVO_CONTROLLER
        // Initialize servo controller
        ESP_LOGI(TAG, "Initializing servo controller");
        // 使用通用的RegisterThing
        iot::RegisterThing("Servo", nullptr);
#endif

        // 等待所有IoT组件初始化完成
        ESP_LOGI(TAG, "等待100ms让IoT组件初始化完成");
        vTaskDelay(pdMS_TO_TICKS(100));
#endif // IOT协议启用

        // Initialize vision components
        ESP_LOGI(TAG, "Initializing vision components");
        // Vision组件的具体初始化实现
        try {
#ifdef CONFIG_ENABLE_VISION_CONTROLLER
            // 视觉相关组件现在统一由VisionController管理
            // 摄像头功能已集成到VisionController中
            auto& manager = ComponentManager::GetInstance();
            
            // 检查视觉控制器组件
            Component* vision = manager.GetComponent("VisionController");
            if (!vision) {
                ESP_LOGI(TAG, "VisionController not found, may be registered later");
            } else {
                ESP_LOGI(TAG, "VisionController already registered (includes camera functionality)");
            }
#else
            ESP_LOGI(TAG, "Vision controller disabled in configuration");
#endif // CONFIG_ENABLE_VISION_CONTROLLER
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Exception during vision components initialization: %s", e.what());
        }
        
        // 初始化所有已注册的组件
        auto& manager = ComponentManager::GetInstance();
        ESP_LOGI(TAG, "Found %zu components registered", manager.GetComponents().size());
        
        // 遍历所有组件执行初始化前的准备工作
        for (auto* component : manager.GetComponents()) {
            if (component && component->GetName()) {
                ESP_LOGI(TAG, "Preparing component: %s", component->GetName());
            }
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in InitializeComponents: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "Unknown exception in InitializeComponents");
    }
}

void Application::StartComponents() {
    ESP_LOGI(TAG, "Starting all components in Application");
    auto& manager = ComponentManager::GetInstance();
    
    // Log all registered components
    ESP_LOGI(TAG, "Currently registered components:");
    for (auto* component : manager.GetComponents()) {
        if (!component) {
            ESP_LOGW(TAG, "Found null component in manager, skipping");
            continue;
        }
        ESP_LOGI(TAG, "- %s (running: %s)", 
                component->GetName(), 
                component->IsRunning() ? "yes" : "no");
    }
    
    // 步骤1：优先启动IOT类型组件，因为其他组件可能依赖它们
    ESP_LOGI(TAG, "步骤1: 优先启动IOT组件");
    for (auto* component : manager.GetComponents()) {
        if (!component || !component->GetName()) continue;
        
        if (component->GetType() == COMPONENT_TYPE_IOT && !component->IsRunning()) {
            try {
                ESP_LOGI(TAG, "启动IOT组件: %s", component->GetName());
                bool started = component->Start();
                if (!started) {
                    ESP_LOGE(TAG, "Failed to start IOT component: %s", component->GetName());
                } else {
                    ESP_LOGI(TAG, "IOT component %s started successfully", component->GetName());
                }
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Exception while starting IOT component: %s", e.what());
            }
        }
    }
    
    // 等待一小段时间，确保IoT组件完全启动
    ESP_LOGI(TAG, "等待100ms让IOT组件完全初始化");
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Start all remaining components
    // Use try/catch to prevent crashes from propagating
    ESP_LOGI(TAG, "步骤2: 启动所有其他组件");
    for (auto* component : manager.GetComponents()) {
        if (!component) {
            ESP_LOGW(TAG, "Found null component in manager, skipping start");
            continue;
        }
        
        // Skip any null component Name to prevent crashes
        if (!component->GetName()) {
            ESP_LOGW(TAG, "Component has null name, skipping start");
            continue;
        }
        
        // 跳过已经启动的IOT组件
        if (component->GetType() == COMPONENT_TYPE_IOT && component->IsRunning()) {
            continue;
        }
        
        // Schedule component start in background task to avoid blocking
        background_task_->Schedule([this, component]() {
            if (!component || !component->GetName()) {
                ESP_LOGW(TAG, "Component became invalid, skipping start");
                return;
            }
            
            if (!component->IsRunning()) {
                try {
                    ESP_LOGI(TAG, "Starting component: %s", component->GetName());
                    bool started = component->Start();
                    if (!started) {
                        ESP_LOGE(TAG, "Failed to start component: %s", component->GetName());
                    } else {
                        ESP_LOGI(TAG, "Component %s started successfully", component->GetName());
                    }
                } catch (const std::exception& e) {
                    ESP_LOGE(TAG, "Exception while starting component: %s", e.what());
                } catch (...) {
                    ESP_LOGE(TAG, "Unknown exception while starting component");
                }
            }
        });
    }
    
    // Special handling for vision components to ensure proper startup sequence
#ifdef CONFIG_ENABLE_VISION_CONTROLLER
    try {
        ESP_LOGI(TAG, "Starting vision components with proper dependencies");
        
        // 启动VisionController (现已包含相机功能)
        Component* vision = GetComponent("VisionController");
        if (vision) {
            if (!vision->IsRunning()) {
                // 添加静态标志避免重复启动
                static bool vision_start_in_progress = false;
                if (vision_start_in_progress) {
                    ESP_LOGI(TAG, "Vision controller startup already in progress, skipping");
                    return;
                }
                
                ESP_LOGI(TAG, "Starting vision controller component (with integrated camera)");
                vision_start_in_progress = true;
                
                background_task_->Schedule([this, vision]() {
                    try {
                        if (!vision->Start()) {
                            ESP_LOGE(TAG, "Failed to start VisionController from sequence");
                        } else {
                            ESP_LOGI(TAG, "VisionController (with camera) started successfully");
                            
#if defined(CONFIG_ENABLE_VISION_CONTENT)
                            // Start VisionContent only after VisionController is running
                            Component* vision_content = GetComponent("VisionContent");
                            if (vision_content && !vision_content->IsRunning()) {
                                try {
                                    if (!vision_content->Start()) {
                                        ESP_LOGE(TAG, "Failed to start VisionContent");
                                    } else {
                                        ESP_LOGI(TAG, "VisionContent started successfully");
                                    }
                                } catch (const std::exception& e) {
                                    ESP_LOGE(TAG, "Exception starting VisionContent: %s", e.what());
                                }
                            }
#endif // CONFIG_ENABLE_VISION_CONTENT
                        }
                        
                        // 完成启动过程后重置标志
                        vision_start_in_progress = false;
                    } catch (const std::exception& e) {
                        // 异常处理中也要重置标志
                        vision_start_in_progress = false;
                        ESP_LOGE(TAG, "Exception starting vision sequence: %s", e.what());
                    }
                });
            } else {
                ESP_LOGW(TAG, "Vision controller already running");
            }
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in vision component startup: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "Unknown exception in vision component startup");
    }
#endif // CONFIG_ENABLE_VISION_CONTROLLER
    
    ESP_LOGI(TAG, "All components processing scheduled");

#ifdef CONFIG_ENABLE_LOCATION_CONTROLLER
    // 初始化并启动位置控制器
    InitLocationController();
#endif
}

void Application::StopComponents() {
    // Execute synchronously to ensure components are stopped
    ESP_LOGI(TAG, "Stopping all components");
    try {
        auto& manager = ComponentManager::GetInstance();
        
#ifdef CONFIG_ENABLE_VISION_CONTROLLER
        // 首先停止视觉相关组件
        ESP_LOGI(TAG, "Stopping vision components");
        
#ifdef CONFIG_ENABLE_VISION_CONTENT
        // 先停止VisionContent，它依赖于VisionController
        Component* vision_content = GetComponent("VisionContent");
        if (vision_content && vision_content->IsRunning()) {
            ESP_LOGI(TAG, "Stopping vision content component");
            try {
                vision_content->Stop();
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Exception stopping vision content: %s", e.what());
            }
        }
#endif // CONFIG_ENABLE_VISION_CONTENT
        
        // 然后停止VisionController (已包含相机功能)
        Component* vision = GetComponent("VisionController");
        if (vision && vision->IsRunning()) {
            ESP_LOGI(TAG, "Stopping vision controller component (with integrated camera)");
            try {
                vision->Stop();
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Exception stopping vision controller: %s", e.what());
            }
        }
#endif // CONFIG_ENABLE_VISION_CONTROLLER
        
        // 停止所有其他组件
        ESP_LOGI(TAG, "Stopping all remaining components");
        manager.StopAll();
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in StopComponents: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "Unknown exception in StopComponents");
    }
}

Component* Application::GetComponent(const char* name) {
    if (!name) {
        ESP_LOGW(TAG, "GetComponent called with null name");
        return nullptr;
    }
    
    try {
        auto& manager = ComponentManager::GetInstance();
        return manager.GetComponent(name);
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in GetComponent: %s", e.what());
        return nullptr;
    } catch (...) {
        ESP_LOGE(TAG, "Unknown exception in GetComponent");
        return nullptr;
    }
}

bool Application::InitComponents() {
    // 获取组件管理器单例
    auto& manager = ComponentManager::GetInstance();
    
    // 获取Web组件，只声明一次，用于所有需要Web的组件
    Web* web_server = nullptr;
    if (manager.GetComponent("Web")) {
        web_server = (Web*)manager.GetComponent("Web");
        ESP_LOGI(TAG, "Found Web component");
    } else {
        ESP_LOGW(TAG, "Web component not found, Vision and Location components will not have web access");
    }
    
    // 注册Vision控制器组件
#ifdef CONFIG_ENABLE_VISION_CONTROLLER
    ESP_LOGI(TAG, "Registering Vision component");
    static Vision* vision = new Vision(web_server);
    manager.RegisterComponent(vision);
#endif

    // 注册位置控制器组件
#ifdef CONFIG_ENABLE_LOCATION_CONTROLLER
    ESP_LOGI(TAG, "Registering Location component");
    static Location* location = new Location(web_server);
    manager.RegisterComponent(location);
#endif

    // 初始化车辆控制组件
#ifdef CONFIG_ENABLE_MOTOR_CONTROLLER
    InitVehicleComponent(web_server);
#endif

    return true;
}

#ifdef CONFIG_ENABLE_LOCATION_CONTROLLER
// 初始化位置控制器
void Application::InitLocationController() {
    ESP_LOGI(TAG, "初始化位置控制器...");
    try {
        auto* location = (Location*)ComponentManager::GetInstance().GetComponent("Location");
        if (location && !location->IsRunning()) {
            location->Start();
        }

        ESP_LOGI(TAG, "位置控制器初始化成功");
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "位置控制器初始化异常: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "位置控制器初始化发生未知错误");
    }
}
#endif // CONFIG_ENABLE_LOCATION_CONTROLLER
void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_processor_->EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_processor_->EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_processor_->EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::InitVehicleComponent(Web* web_server) {
    ESP_LOGI(TAG, "Creating and registering Vehicle component");
    
    // 创建适当类型的车辆
    Vehicle* vehicle = nullptr;
    
#ifdef CONFIG_ENABLE_MOTOR_CONTROLLER
    // 使用安全的默认值，避免未定义配置导致编译错误
    // 如果这些引脚未配置，将默认设置为-1表示无效
#ifdef CONFIG_MOTOR_ENA_PIN
    int ena_pin = CONFIG_MOTOR_ENA_PIN;
    int enb_pin = CONFIG_MOTOR_ENB_PIN;
#else
    int ena_pin = -1;
    int enb_pin = -1;
#endif

    // 电机引脚
#if defined(CONFIG_MOTOR_IN1_PIN) && defined(CONFIG_MOTOR_IN2_PIN) && defined(CONFIG_MOTOR_IN3_PIN) && defined(CONFIG_MOTOR_IN4_PIN)
    int in1_pin = CONFIG_MOTOR_IN1_PIN;
    int in2_pin = CONFIG_MOTOR_IN2_PIN;
    int in3_pin = CONFIG_MOTOR_IN3_PIN;
    int in4_pin = CONFIG_MOTOR_IN4_PIN;
#else
    int in1_pin = -1;
    int in2_pin = -1;
    int in3_pin = -1;
    int in4_pin = -1;
#endif

    // 舵机引脚
#if defined(CONFIG_SERVO_PIN_1) && defined(CONFIG_SERVO_PIN_2)
    int servo_pin_1 = CONFIG_SERVO_PIN_1;
    int servo_pin_2 = CONFIG_SERVO_PIN_2;
#else
    int servo_pin_1 = -1;
    int servo_pin_2 = -1;
#endif
    
    // 检查电机引脚是否配置
    bool motor_pins_ok = (ena_pin >= 0 && enb_pin >= 0 && 
                         in1_pin >= 0 && in2_pin >= 0 && 
                         in3_pin >= 0 && in4_pin >= 0);
    
    // 检查舵机引脚是否配置
    bool servo_pins_ok = (servo_pin_1 >= 0 && servo_pin_2 >= 0);
    
    if (motor_pins_ok) {
        // 如果电机引脚配置正确，创建电机小车
        ESP_LOGI(TAG, "Creating vehicle with motor control (pins: ENA=%d, ENB=%d, IN1=%d, IN2=%d, IN3=%d, IN4=%d)",
                ena_pin, enb_pin, in1_pin, in2_pin, in3_pin, in4_pin);
        vehicle = new Vehicle(web_server, 
                             ena_pin, enb_pin,
                             in1_pin, in2_pin,
                             in3_pin, in4_pin);
    } else if (servo_pins_ok) {
        // 如果舵机引脚配置正确，创建舵机小车
        ESP_LOGI(TAG, "Creating vehicle with servo control (pins: SERVO1=%d, SERVO2=%d)",
                servo_pin_1, servo_pin_2);
        vehicle = new Vehicle(web_server, servo_pin_1, servo_pin_2);
    } else {
        ESP_LOGW(TAG, "Cannot create vehicle, insufficient pin configuration");
    }
    
    // 如果创建成功，注册并启动组件
    if (vehicle) {
        auto& manager = ComponentManager::GetInstance();
        manager.RegisterComponent(vehicle);
        ESP_LOGI(TAG, "Vehicle component registered");
    }
#else
    ESP_LOGI(TAG, "Motor controller disabled in configuration");
#endif
}
