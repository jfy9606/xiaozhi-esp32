#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "ml307_ssl_transport.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "font_awesome_symbols.h"
#include "iot/thing_manager.h"
#include "iot/things/us.h"
#include "iot/things/cam.h"
#include "iot/things/imu.h"
#include "iot/things/light.h"
#include "iot/things/motor.h"
#include "iot/things/servo.h"
#include "assets/lang_config.h"
<<<<<<< HEAD
#include "web/web_server.h"
#include "wifi_station.h"
=======
#include "mcp_server.h"
>>>>>>> upstream/main

#if CONFIG_USE_AUDIO_PROCESSOR
#include "afe_audio_processor.h"
#else
#include "dummy_audio_processor.h"
#endif

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>

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

#if CONFIG_ENABLE_XIAOZHI_AI_CORE
#if CONFIG_USE_AUDIO_PROCESSOR
    audio_processor_ = std::make_unique<AfeAudioProcessor>();
#else
    audio_processor_ = std::make_unique<DummyAudioProcessor>();
#endif
#else
    // 即使在AI核心功能禁用时，仍然创建一个基础的音频处理器
    // 以支持基础的音频功能，但不执行AI相关处理
    audio_processor_ = std::make_unique<DummyAudioProcessor>();
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
    esp_timer_start_periodic(clock_timer_handle_, 1000000);
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
#if CONFIG_USE_WAKE_WORD_DETECT
            wake_word_detect_.StopDetection();
#endif
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
                snprintf(buffer, sizeof(buffer), "%d%% %zuKB/s", progress, speed / 1024);
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
    
    // 无论AI核心是否启用，都允许播放基本提示音
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
#if !CONFIG_ENABLE_XIAOZHI_AI_CORE
    // AI核心功能禁用时，仍然支持基础音效播放
    // 但会跳过AI特定的处理流程
    ESP_LOGI(TAG, "PlaySound: Playing sound in basic mode (AI core disabled)");
    
    auto codec = Board::GetInstance().GetAudioCodec();
    codec->EnableOutput(true);
    
    // 简单的音效处理逻辑
    SetDecodeSampleRate(16000, 60);
    const char* data = sound.data();
    size_t size = sound.size();
    
    // 基础音效处理
    for (const char* p = data; p < data + size; ) {
        auto p3 = (BinaryProtocol3*)p;
        p += sizeof(BinaryProtocol3);

        auto payload_size = ntohs(p3->payload_size);
        AudioStreamPacket packet;
        packet.payload.resize(payload_size);
        memcpy(packet.payload.data(), p3->payload, payload_size);
        p += payload_size;

        std::lock_guard<std::mutex> lock(mutex_);
        audio_decode_queue_.emplace_back(std::move(packet));
    }
    
    last_output_time_ = std::chrono::steady_clock::now();
    return;
#endif

    // Wait for the previous sound to finish
    {
        std::unique_lock<std::mutex> lock(mutex_);
        audio_decode_cv_.wait(lock, [this]() {
            return audio_decode_queue_.empty();
        });
    }
    background_task_->WaitForCompletion();

    // The assets are encoded at 16000Hz, 60ms frame duration
    SetDecodeSampleRate(16000, 60);
    const char* data = sound.data();
    size_t size = sound.size();
    for (const char* p = data; p < data + size; ) {
        auto p3 = (BinaryProtocol3*)p;
        p += sizeof(BinaryProtocol3);

        auto payload_size = ntohs(p3->payload_size);
        AudioStreamPacket packet;
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

#if !CONFIG_ENABLE_XIAOZHI_AI_CORE
    // AI核心功能禁用时，显示提示信息并终止执行
    ESP_LOGW(TAG, "ToggleChatState: AI core is disabled");
    Alert(Lang::Strings::INFO, Lang::Strings::AI_CORE_DISABLED, "sad", Lang::Sounds::P3_EXCLAMATION);
    return;
#endif

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        Alert(Lang::Strings::ERROR, "Communication protocol not initialized", "sad", Lang::Sounds::P3_EXCLAMATION);
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                return;
            }

            SetListeningMode(realtime_chat_enabled_ ? kListeningModeRealtime : kListeningModeAutoStop);
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
    
#if !CONFIG_ENABLE_XIAOZHI_AI_CORE
    // AI核心功能禁用时，显示提示信息并终止执行
    ESP_LOGW(TAG, "StartListening: AI core is disabled");
    Alert(Lang::Strings::INFO, Lang::Strings::AI_CORE_DISABLED, "sad", Lang::Sounds::P3_EXCLAMATION);
    return;
#endif
    
    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        Alert(Lang::Strings::ERROR, "Communication protocol not initialized", "sad", Lang::Sounds::P3_EXCLAMATION);
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
#if !CONFIG_ENABLE_XIAOZHI_AI_CORE
    // AI核心功能禁用时直接返回，不需要额外提示
    ESP_LOGW(TAG, "StopListening: AI core is disabled");
    return;
#endif

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

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

    /* Setup the display */
    auto display = board.GetDisplay();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the audio codec */
    auto codec = board.GetAudioCodec();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(codec->output_sample_rate(), 1, OPUS_FRAME_DURATION_MS);
    opus_encoder_ = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
    if (realtime_chat_enabled_) {
        ESP_LOGI(TAG, "Realtime chat enabled, setting opus encoder complexity to 0");
        opus_encoder_->SetComplexity(0);
    } else if (board.GetBoardType() == "ml307") {
        ESP_LOGI(TAG, "ML307 board detected, setting opus encoder complexity to 5");
        opus_encoder_->SetComplexity(5);
    } else {
        ESP_LOGI(TAG, "WiFi board detected, setting opus encoder complexity to 3");
        opus_encoder_->SetComplexity(3);
    }

    if (codec->input_sample_rate() != 16000) {
        input_resampler_.Configure(codec->input_sample_rate(), 16000);
        reference_resampler_.Configure(codec->input_sample_rate(), 16000);
    }
    codec->Start();

    /* Wait for the network to be ready */
    board.StartNetwork();

    // 初始化Web组件 - must be after network is ready
#if defined(CONFIG_ENABLE_WEB_SERVER)
    ESP_LOGI(TAG, "Initializing web components");
    WebServer::InitWebComponents();
    ESP_LOGI(TAG, "Web components registered (will start after device enters idle state)");
#endif

    // Check for new firmware version or get the MQTT broker address
    CheckNewVersion();

    // Initialize the protocol before other components that might use it
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

#if CONFIG_ENABLE_XIAOZHI_AI_CORE
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
        const int max_packets_in_queue = 600 / OPUS_FRAME_DURATION_MS;
        std::lock_guard<std::mutex> lock(mutex_);
        if (audio_decode_queue_.size() < max_packets_in_queue) {
            audio_decode_queue_.emplace_back(std::move(packet));
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
        SetDecodeSampleRate(protocol_->server_sample_rate(), protocol_->server_frame_duration());

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
        }
    });
    // 初始化协议但不保存未使用的返回值
    protocol_->Start();

    // 初始化音频处理器
    audio_processor_->Initialize(codec);
    audio_processor_->OnOutput([this](std::vector<int16_t>&& data) {
        background_task_->Schedule([this, data = std::move(data)]() mutable {
            if (protocol_->IsAudioChannelBusy()) {
                return;
            }
            opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {
                AudioStreamPacket packet;
                packet.payload = std::move(opus);
                uint32_t last_output_timestamp_value = last_output_timestamp_.load();
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
                Schedule([this, packet = std::move(packet)]() {
                    protocol_->SendAudio(packet);
                });
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

#if CONFIG_USE_WAKE_WORD_DETECT
    wake_word_detect_.Initialize(codec);
    wake_word_detect_.OnWakeWordDetected([this](const std::string& wake_word) {
        Schedule([this, wake_word]() {
            if (device_state_ == kDeviceStateIdle) {
                SetDeviceState(kDeviceStateConnecting);
                wake_word_detect_.EncodeWakeWordData();

                if (!protocol_ || !protocol_->OpenAudioChannel()) {
                    wake_word_detect_.StartDetection();
                    return;
                }
                
                AudioStreamPacket packet;
                // Encode and send the wake word data to the server
                while (wake_word_detect_.GetWakeWordOpus(packet.payload)) {
                    protocol_->SendAudio(packet);
                }
                // Set the chat state to wake word detected
                protocol_->SendWakeWordDetected(wake_word);
                ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
                SetListeningMode(realtime_chat_enabled_ ? kListeningModeRealtime : kListeningModeAutoStop);
            } else if (device_state_ == kDeviceStateSpeaking) {
                AbortSpeaking(kAbortReasonWakeWordDetected);
            } else if (device_state_ == kDeviceStateActivating) {
                SetDeviceState(kDeviceStateIdle);
            }
        });
    });
    wake_word_detect_.StartDetection();
#endif

#else
    // AI核心功能禁用时，显示提示信息
    ESP_LOGW(TAG, "AI core functionality is disabled by configuration");
    Alert(Lang::Strings::INFO, Lang::Strings::AI_CORE_DISABLED, "sad", Lang::Sounds::P3_EXCLAMATION);
    // protocol_对象未被初始化 - 这里不创建空实现，而是在使用protocol_的地方做检查
#endif // CONFIG_ENABLE_XIAOZHI_AI_CORE

    // 在Protocol初始化之后再初始化其他组件
    // Initialize all components before starting tasks
    InitializeComponents();
    
    // Start components but not web components yet
    // Web components will be started after network is ready
    StartComponents();
    
    // Now start the background audio loop task
#if CONFIG_ENABLE_XIAOZHI_AI_CORE
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
#else
    // 即使在AI核心功能禁用时，仍然需要音频循环来支持基本音频功能
    xTaskCreate([](void* arg) {
        Application* app = (Application*)arg;
        app->AudioLoop();
        vTaskDelete(NULL);
    }, "audio_loop", 4096 * 2, this, 8, &audio_loop_task_handle_);
#endif

    // Wait for the new version check to finish
    xEventGroupWaitBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);
    SetDeviceState(kDeviceStateIdle);

#if CONFIG_ENABLE_XIAOZHI_AI_CORE
    if (protocol_) {
        std::string message = std::string(Lang::Strings::VERSION) + ota_.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        ResetDecoder();
        PlaySound(Lang::Sounds::P3_SUCCESS);
    }
#else
    // 即使AI核心禁用，也显示版本号
    std::string message = std::string(Lang::Strings::VERSION) + ota_.GetCurrentVersion();
    display->ShowNotification(message.c_str());
    display->SetChatMessage("system", "");
#endif
    // Enter the main event loop
    MainEventLoop();
}

void Application::OnClockTimer() {
    clock_ticks_++;

    // Print the debug info every 10 seconds
    if (clock_ticks_ % 3 == 0) {
        // char buffer[500];
        // vTaskList(buffer);
        // ESP_LOGI(TAG, "Task list: \n%s", buffer);
        // SystemInfo::PrintRealTimeStats(pdMS_TO_TICKS(1000));

        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);

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
        auto bits = xEventGroupWaitBits(event_group_, SCHEDULE_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & SCHEDULE_EVENT) {
            std::unique_lock<std::mutex> lock(mutex_);
            std::list<std::function<void()>> tasks = std::move(main_tasks_);
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
    
#if !CONFIG_ENABLE_XIAOZHI_AI_CORE
    // 当AI核心功能禁用时，仍然提供基础音频处理
    // 但不运行AI相关处理（如唤醒词检测、语音交互等）
    ESP_LOGI(TAG, "Audio loop started without AI features");
    while (true) {
        // 仍然处理基本的音频输出（用于其他组件播放声音）
        if (codec->output_enabled()) {
            OnAudioOutput();
        }
        // 基本延迟，降低CPU使用率
        vTaskDelay(pdMS_TO_TICKS(20));
    }
#else
    // 完整的音频循环，包括AI功能
    while (true) {
        OnAudioInput();
        if (codec->output_enabled()) {
            OnAudioOutput();
        }
    }
#endif
}

void Application::OnAudioOutput() {
#if !CONFIG_ENABLE_XIAOZHI_AI_CORE
    // AI核心功能禁用时，不执行AI相关的音频处理
    // 但保留基础的音频输出功能供其他组件使用
    
    // 如果有其他组件需要使用音频输出，这里不要直接返回
    // 只跳过AI特定的音频处理逻辑
    
    auto codec = Board::GetInstance().GetAudioCodec();
    const int max_silence_seconds = 10;
    auto now = std::chrono::steady_clock::now();
    
    std::unique_lock<std::mutex> lock(mutex_);
    // 处理音频队列为空的情况
    if (audio_decode_queue_.empty()) {
        // 长时间无音频数据时关闭输出
        if (device_state_ == kDeviceStateIdle) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_output_time_).count();
            if (duration > max_silence_seconds) {
                codec->EnableOutput(false);
            }
        }
        return;
    }
    
    // 其余部分仍然保留基础音频处理功能
    return;
#endif

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

    if (device_state_ == kDeviceStateListening) {
        audio_decode_queue_.clear();
        audio_decode_cv_.notify_all();
        return;
    }

    auto packet = std::move(audio_decode_queue_.front());
    audio_decode_queue_.pop_front();
    lock.unlock();
    audio_decode_cv_.notify_all();

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
        {
            std::lock_guard<std::mutex> lock(timestamp_mutex_);
            timestamp_queue_.push_back(packet.timestamp);
            last_output_timestamp_ = packet.timestamp;
        }
        last_output_time_ = std::chrono::steady_clock::now();
    });
}

void Application::OnAudioInput() {
#if !CONFIG_ENABLE_XIAOZHI_AI_CORE
    // AI核心功能禁用时，不执行唤醒词检测和音频处理器相关操作
    // 但仍可提供基础的音频输入支持给其他组件使用
    vTaskDelay(pdMS_TO_TICKS(20));
    return;
#endif

#if CONFIG_USE_WAKE_WORD_DETECT
    if (wake_word_detect_.IsDetectionRunning()) {
        std::vector<int16_t> data;
        int samples = wake_word_detect_.GetFeedSize();
        if (samples > 0) {
            ReadAudio(data, 16000, samples);
            wake_word_detect_.Feed(data);
            return;
        }
    }
#endif
    if (audio_processor_->IsRunning()) {
        std::vector<int16_t> data;
        int samples = audio_processor_->GetFeedSize();
        if (samples > 0) {
            ReadAudio(data, 16000, samples);
            audio_processor_->Feed(data);
            return;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(30));
}

void Application::ReadAudio(std::vector<int16_t>& data, int sample_rate, int samples) {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec->input_sample_rate() != sample_rate) {
        data.resize(samples * codec->input_sample_rate() / sample_rate);
        if (!codec->InputData(data)) {
            return;
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
            return;
        }
    }
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    
#if !CONFIG_ENABLE_XIAOZHI_AI_CORE
    ESP_LOGW(TAG, "AbortSpeaking: AI core is disabled");
    return;
#endif

    if (!protocol_) {
        ESP_LOGW(TAG, "Cannot abort speaking: protocol not initialized");
        return;
    }
    
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
    
#if !CONFIG_ENABLE_XIAOZHI_AI_CORE
    // AI核心功能禁用时，限制可以进入的状态
    if (state == kDeviceStateConnecting || 
        state == kDeviceStateListening || 
        state == kDeviceStateSpeaking) {
        ESP_LOGW(TAG, "Cannot enter state %s: AI core is disabled", STATE_STRINGS[state]);
        Alert(Lang::Strings::INFO, Lang::Strings::AI_CORE_DISABLED, "sad", Lang::Sounds::P3_EXCLAMATION);
        // 如果已经在空闲状态，直接返回；否则转为空闲状态
        if (device_state_ == kDeviceStateIdle) {
            return;
        }
        state = kDeviceStateIdle;
    }
#endif
    
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
            
#if CONFIG_USE_WAKE_WORD_DETECT && CONFIG_ENABLE_XIAOZHI_AI_CORE
            wake_word_detect_.StartDetection();
#endif

            // 设备进入空闲状态，且WiFi已连接，启动Web服务器组件
#if defined(CONFIG_ENABLE_WEB_SERVER)
            // 使用Schedule方法在后台执行启动Web服务器的操作
            Schedule([this]() {
                // 检查WiFi是否已连接
                if (WifiStation::GetInstance().IsConnected()) {
                    ESP_LOGI(TAG, "Device entered idle state and WiFi is connected, starting Web components");
                    
                    // 获取WebServer组件
                    Component* web_server = GetComponent("WebServer");
                    if (web_server) {
                        if (!web_server->IsRunning()) {
                            // 使用静态方法启动Web服务器及相关组件
                            if (WebServer::StartWebComponents()) {
                                ESP_LOGI(TAG, "Web server components started successfully");
                            } else {
                                ESP_LOGW(TAG, "Failed to start Web server components");
                            }
                        } else {
                            ESP_LOGI(TAG, "Web server already running");
                        }
                    } else {
                        ESP_LOGW(TAG, "WebServer component not found");
                    }
                } else {
                    ESP_LOGW(TAG, "WiFi not connected, skipping Web server startup");
                }
            });
#endif // CONFIG_ENABLE_WEB_SERVER

            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            timestamp_queue_.clear();
            last_output_timestamp_ = 0;
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

#if CONFIG_ENABLE_XIAOZHI_AI_CORE
            // Update the IoT states before sending the start listening command
#if CONFIG_IOT_PROTOCOL_XIAOZHI
            UpdateIotStates();
#endif

            // Make sure the audio processor is running
            if (!audio_processor_->IsRunning()) {
                // 只在协议对象存在时执行发送操作
                if (protocol_) {
                    // Send the start listening command
                    protocol_->SendStartListening(listening_mode_);
                    if (listening_mode_ == kListeningModeAutoStop && previous_state == kDeviceStateSpeaking) {
                        // FIXME: Wait for the speaker to empty the buffer
                        vTaskDelay(pdMS_TO_TICKS(120));
                    }
                }
                opus_encoder_->ResetState();
#if CONFIG_USE_WAKE_WORD_DETECT
                wake_word_detect_.StopDetection();
#endif
                audio_processor_->Start();
            }
#else
            // AI核心功能禁用时
            display->SetChatMessage("system", Lang::Strings::AI_CORE_DISABLED);
            // 这里不应该进入到这个分支，因为在前面已经限制了状态转换
            // 但为了防止未知情况，仍旧处理
            SetDeviceState(kDeviceStateIdle);
#endif
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

#if CONFIG_ENABLE_XIAOZHI_AI_CORE
            if (listening_mode_ != kListeningModeRealtime) {
                audio_processor_->Stop();
#if CONFIG_USE_WAKE_WORD_DETECT
                wake_word_detect_.StartDetection();
#endif
            }
            ResetDecoder();
#else
            // AI核心功能禁用时
            display->SetChatMessage("system", Lang::Strings::AI_CORE_DISABLED);
            // 这里不应该进入到这个分支，因为在前面已经限制了状态转换
            // 但为了防止未知情况，仍旧处理
            SetDeviceState(kDeviceStateIdle);
#endif
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::ResetDecoder() {
#if !CONFIG_ENABLE_XIAOZHI_AI_CORE
    // AI核心功能禁用时，执行基础音频解码器初始化
    // 确保其他组件能够使用音频功能
    
    // 确保解码器已初始化
    if (opus_decoder_) {
        opus_decoder_->ResetState();
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    audio_decode_queue_.clear();
    audio_decode_cv_.notify_all();
    last_output_time_ = std::chrono::steady_clock::now();
    
    auto codec = Board::GetInstance().GetAudioCodec();
    codec->EnableOutput(true);
    return;
#endif

    std::lock_guard<std::mutex> lock(mutex_);
    opus_decoder_->ResetState();
    audio_decode_queue_.clear();
    audio_decode_cv_.notify_all();
    last_output_time_ = std::chrono::steady_clock::now();
    
    auto codec = Board::GetInstance().GetAudioCodec();
    codec->EnableOutput(true);
}

void Application::SetDecodeSampleRate(int sample_rate, int frame_duration) {
#if !CONFIG_ENABLE_XIAOZHI_AI_CORE
    // AI核心功能禁用时，仍支持基础的音频解码功能
    // 这对其他需要使用音频的组件很重要
    
    if (!opus_decoder_) {
        // 如果解码器尚未初始化，创建一个基础解码器
        opus_decoder_ = std::make_unique<OpusDecoderWrapper>(sample_rate, 1, frame_duration);
        
        auto codec = Board::GetInstance().GetAudioCodec();
        if (opus_decoder_->sample_rate() != codec->output_sample_rate()) {
            ESP_LOGI(TAG, "Resampling audio from %d to %d", opus_decoder_->sample_rate(), codec->output_sample_rate());
            output_resampler_.Configure(opus_decoder_->sample_rate(), codec->output_sample_rate());
        }
        return;
    }
    
    // 如果解码器已存在但参数不同，重新配置
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
    return;
#endif

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
<<<<<<< HEAD
#if !CONFIG_ENABLE_XIAOZHI_AI_CORE
    ESP_LOGW(TAG, "UpdateIotStates: AI core is disabled");
    return;
#endif

    if (!protocol_) {
        ESP_LOGW(TAG, "Cannot update IoT states: protocol not initialized");
        return;
    }

=======
#if CONFIG_IOT_PROTOCOL_XIAOZHI
>>>>>>> upstream/main
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
#if !CONFIG_ENABLE_XIAOZHI_AI_CORE
    // AI核心功能禁用时，显示提示信息并终止执行
    ESP_LOGW(TAG, "WakeWordInvoke: AI core is disabled");
    Alert(Lang::Strings::INFO, Lang::Strings::AI_CORE_DISABLED, "sad", Lang::Sounds::P3_EXCLAMATION);
    return;
#endif

    if (device_state_ == kDeviceStateIdle) {
        ToggleChatState();
        if (protocol_) {
            Schedule([this, wake_word]() {
                protocol_->SendWakeWordDetected(wake_word); 
            }); 
        }
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {   
        if (protocol_) {
            Schedule([this]() {
                protocol_->CloseAudioChannel();
            });
        }
    }
}

bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }

#if CONFIG_ENABLE_XIAOZHI_AI_CORE
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }
#endif

    // Now it is safe to enter sleep mode
    return true;
}

<<<<<<< HEAD
// Component management methods
void Application::InitializeComponents() {
    ESP_LOGI(TAG, "Initializing all components");
    try {
#ifdef CONFIG_ENABLE_US_SENSOR
        // Initialize ultrasonic sensors
        ESP_LOGI(TAG, "Initializing ultrasonic sensors");
        iot::RegisterUS();
#endif

#ifdef CONFIG_ENABLE_CAM
        // Initialize camera sensor
        ESP_LOGI(TAG, "Initializing camera");
        iot::RegisterCAM();
#endif

#ifdef CONFIG_ENABLE_IMU
        // Initialize IMU sensor
        ESP_LOGI(TAG, "Initializing IMU sensor");
        iot::RegisterIMU();
#endif

#ifdef CONFIG_ENABLE_LIGHT
        // Initialize light sensor/controller
        ESP_LOGI(TAG, "Initializing light controller");
        iot::RegisterLight();
#endif

#ifdef CONFIG_ENABLE_MOTOR_CONTROLLER
        // Initialize motor controller
        ESP_LOGI(TAG, "Initializing motor controller");
        iot::RegisterMotor();
#endif

#ifdef CONFIG_ENABLE_SERVO
        // Initialize servo controller
        ESP_LOGI(TAG, "Initializing servo controller");
        iot::RegisterServo();
#endif

        // Initialize vision components
        ESP_LOGI(TAG, "Initializing vision components");
        // Vision组件的具体初始化实现
        try {
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
    
    // Start all components
    // Use try/catch to prevent crashes from propagating
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
    try {
        ESP_LOGI(TAG, "Starting vision components with proper dependencies");
        
        // 启动VisionController (现已包含相机功能)
        Component* vision = GetComponent("VisionController");
        if (vision) {
            if (!vision->IsRunning()) {
                ESP_LOGI(TAG, "Starting vision controller component (with integrated camera)");
                background_task_->Schedule([this, vision]() {
                    try {
                        if (!vision->Start()) {
                            ESP_LOGE(TAG, "Failed to start VisionController from sequence");
                        } else {
                            ESP_LOGI(TAG, "VisionController (with camera) started successfully");
                            
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
                        }
                    } catch (const std::exception& e) {
                        ESP_LOGE(TAG, "Exception starting vision sequence: %s", e.what());
                    }
                });
            }
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in vision component startup: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "Unknown exception in vision component startup");
    }
    
    ESP_LOGI(TAG, "All components processing scheduled");
}

void Application::StopComponents() {
    // Execute synchronously to ensure components are stopped
    ESP_LOGI(TAG, "Stopping all components");
    try {
        auto& manager = ComponentManager::GetInstance();
        
        // 首先停止视觉相关组件
        ESP_LOGI(TAG, "Stopping vision components");
        
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
=======
void Application::SendMcpMessage(const std::string& payload) {
    Schedule([this, payload]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
    });
>>>>>>> upstream/main
}
