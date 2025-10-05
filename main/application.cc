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
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "iot/thing_manager.h"
#include "iot/things/imu.h"
#include "iot/things/us.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "ext/include/multiplexer.h"
#include "ext/include/pca9548a.h"
#include "ext/include/pcf8575.h"
#include "hardware/hardware_manager.h"
#include "ai/ai.h"
#include "iot/things/servo.h"
#include "audio/processors/audio_debugger.h"

#if CONFIG_USE_AUDIO_PROCESSOR
#include "audio/processors/afe_audio_processor.h"
#else
#include "no_audio_processor.h"
#endif

#if CONFIG_USE_AFE_WAKE_WORD
#include "audio/wake_words/afe_wake_word.h"
#elif CONFIG_USE_ESP_WAKE_WORD
#include "audio/wake_words/esp_wake_word.h"
#else
#include "no_wake_word.h"
#endif
#include "assets.h"
#include "settings.h"
#include <font_awesome.h>

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
#include <font_awesome.h>

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
    "audio_testing",
    "fatal_error",
    "invalid_state"
};

Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
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
    if (hardware_manager_) {
        delete hardware_manager_;
        hardware_manager_ = nullptr;
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckAssetsVersion() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveMode(false);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [display](int progress, size_t speed) -> void {
            std::thread([display, progress, speed]() {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            }).detach();
        });

        board.SetPowerSaveMode(true);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
    }

    // Apply assets
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion(Ota& ota) {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    auto& board = Board::GetInstance();
    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota.CheckVersion()) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

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

        if (ota.HasNewVersion()) {
            if (UpgradeFirmware(ota)) {
                return; // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation (don't break, just fall through)
        }

        // No new version, mark the current version as valid
        ota.MarkCurrentVersionValid();
        if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota.HasActivationCode()) {
            ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
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

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
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

void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
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
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
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
    if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
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
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();

    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    /* Setup the audio service */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // Start the main event loop task with priority 3
    xTaskCreate([](void* arg) {
        ((Application*)arg)->MainEventLoop();
        vTaskDelete(NULL);
    }, "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);

    // Check for new assets version
    CheckAssetsVersion();

    // Check for new firmware version or get the MQTT broker address
    Ota ota;
    CheckNewVersion(ota);

    // Initialize the protocol before other components that might use it
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    if (ota.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota.HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (device_state_ == kDeviceStateSpeaking) {
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
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
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
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
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    // 初始化协议并跟踪启动状态
    protocol_started_ = protocol_->Start();

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

    // Initialize Hardware Manager after multiplexer initialization
    ESP_LOGI(TAG, "Initializing Hardware Manager");
    hardware_manager_ = new HardwareManager();
    if (hardware_manager_) {
        esp_err_t hw_ret = hardware_manager_->Initialize();
        if (hw_ret == ESP_OK) {
            ESP_LOGI(TAG, "Hardware Manager initialized successfully");
            
            // Try to load configuration file
            esp_err_t config_ret = hardware_manager_->LoadConfiguration("/spiffs/hardware_config.json");
            if (config_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to load hardware configuration, trying default location");
                config_ret = hardware_manager_->LoadConfiguration("main/hardware/hardware_config.json");
                if (config_ret != ESP_OK) {
                    ESP_LOGW(TAG, "No hardware configuration found, creating default");
                    hardware_manager_->CreateDefaultConfiguration("/spiffs/hardware_config.json");
                }
            }
            
            // Set hardware manager for API module
            extern void SetHardwareManager(HardwareManager* manager);
            SetHardwareManager(hardware_manager_);
            ESP_LOGI(TAG, "Hardware Manager set for API module");
        } else {
            ESP_LOGE(TAG, "Hardware Manager initialization failed: %s", esp_err_to_name(hw_ret));
        }
    } else {
        ESP_LOGE(TAG, "Failed to create Hardware Manager instance");
    }

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
    
    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota.HasServerTime();
    if (protocol_started_) {
        std::string message = std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK |
            MAIN_EVENT_ERROR, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
        
            // Print the debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
                // SystemInfo::PrintTaskList();
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

void Application::OnWakeWordDetected() {
    if (!protocol_) {
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        // Play the pop up sound to indicate the wake word is detected
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
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

    // Send the state change event
    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            // Make sure the audio processor is running
            if (!audio_service_.IsAudioProcessorRunning()) {
                // Send the start listening command
                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);
                audio_service_.EnableWakeWordDetection(false);
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(Ota& ota, const std::string& url) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    // Use provided URL or get from OTA object
    std::string upgrade_url = url.empty() ? ota.GetFirmwareUrl() : url;
    std::string version_info = url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)";
    
    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());
    
    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);
    
    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveMode(false);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = ota.StartUpgradeFromUrl(upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            display->SetChatMessage("system", buffer);
        }).detach();
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveMode(true); // Restore power save mode
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
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

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    if (protocol_ == nullptr) {
        return;
    }

    // Make sure you are using main thread to send MCP message
    if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {
        protocol_->SendMcpMessage(payload);
    } else {
        Schedule([this, payload = std::move(payload)]() {
            protocol_->SendMcpMessage(payload);
        });
    }
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
        
        // Schedule component start to avoid blocking
        Schedule([this, component]() {
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
                
                Schedule([this, vision]() {
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

    // 注册AI组件
    ESP_LOGI(TAG, "Registering AI component");
    static AI* ai_component = new AI(web_server);
    if (ai_component && hardware_manager_) {
        ai_component->SetHardwareManager(hardware_manager_);
        ESP_LOGI(TAG, "Hardware manager set for AI component");
    }
    manager.RegisterComponent(ai_component);

    // 初始化车辆控制组件
#ifdef CONFIG_ENABLE_MOTOR_CONTROLLER
    InitVehicleComponent(web_server);
#endif

    // 初始化传感器部分
    // 注册IMU传感器
    iot::RegisterIMU();
    ESP_LOGI(TAG, "IMU sensor registered");

    // 注册超声波传感器
    iot::RegisterUS();
    ESP_LOGI(TAG, "Ultrasonic sensor registered");

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
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
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
void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}
