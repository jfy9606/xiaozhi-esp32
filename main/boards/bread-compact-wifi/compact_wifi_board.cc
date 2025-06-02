#include "wifi_board.h"
#include "audio_codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "../common/esp32_camera.h"
#include "vision/vision_controller.h"
#include "vision/vision_content.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <freertos/semphr.h>

// 添加多路复用器头文件
#ifdef CONFIG_ENABLE_PCA9548A
#include "include/multiplexer.h"
#endif

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "CompactWifiBoard"

LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_14_1);

// 资源冲突管理实现
static resource_state_t g_resource_state = RESOURCE_IDLE;
static SemaphoreHandle_t g_resource_mutex = NULL;
static bool g_resource_initialized = false;

// 摄像头在 board_config 中定义，这里不需要重复定义

// 初始化资源管理系统
static void init_resource_management() {
    if (!g_resource_initialized) {
        g_resource_mutex = xSemaphoreCreateMutex();
        if (g_resource_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create resource mutex!");
        } else {
            g_resource_initialized = true;
            ESP_LOGI(TAG, "Resource management system initialized");
        }
    }
}

// 为摄像头锁定资源
bool lock_resource_for_camera(void) {
    if (!g_resource_initialized) {
        init_resource_management();
    }
    
    if (g_resource_mutex == NULL) {
        return false;
    }
    
    if (xSemaphoreTake(g_resource_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // 检查当前状态
        if (g_resource_state == RESOURCE_AUDIO_ACTIVE) {
            // 音频正在使用，无法锁定摄像头
            ESP_LOGW(TAG, "Cannot lock resource for camera: audio is active");
            xSemaphoreGive(g_resource_mutex);
            return false;
        }
        
        // 硬件级别的资源处理 - DFRobot风格
        // 重新配置共享引脚为摄像头模式
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << SHARED_PIN_1) | (1ULL << SHARED_PIN_2) | (1ULL << SHARED_PIN_3),
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        
        // 延迟一小段时间，确保引脚状态稳定
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // 锁定资源
        g_resource_state = RESOURCE_CAMERA_ACTIVE;
        ESP_LOGI(TAG, "Resource locked for camera");
        xSemaphoreGive(g_resource_mutex);
        return true;
    }
    
    ESP_LOGE(TAG, "Failed to acquire resource mutex for camera");
    return false;
}

// 为音频锁定资源
bool lock_resource_for_audio(void) {
    if (!g_resource_initialized) {
        init_resource_management();
    }
    
    if (g_resource_mutex == NULL) {
        return false;
    }
    
    if (xSemaphoreTake(g_resource_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // 检查当前状态
        if (g_resource_state == RESOURCE_CAMERA_ACTIVE) {
            // 摄像头正在使用，无法锁定音频
            ESP_LOGW(TAG, "Cannot lock resource for audio: camera is active");
            xSemaphoreGive(g_resource_mutex);
            return false;
        }
        
        // 硬件级别的资源处理 - DFRobot风格
        // 重新配置共享引脚为音频模式
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << SHARED_PIN_1) | (1ULL << SHARED_PIN_2) | (1ULL << SHARED_PIN_3),
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        
        // 延迟一小段时间，确保引脚状态稳定
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // 锁定资源
        g_resource_state = RESOURCE_AUDIO_ACTIVE;
        ESP_LOGI(TAG, "Resource locked for audio");
        xSemaphoreGive(g_resource_mutex);
        return true;
    }
    
    ESP_LOGE(TAG, "Failed to acquire resource mutex for audio");
    return false;
}

// 释放资源
void release_resource(void) {
    if (!g_resource_initialized || g_resource_mutex == NULL) {
        return;
    }
    
    if (xSemaphoreTake(g_resource_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // 记录旧状态用于日志
        resource_state_t old_state = g_resource_state;
        
        // 重置资源状态
        g_resource_state = RESOURCE_IDLE;
        
        // 硬件级别的资源重置 - DFRobot风格
        if (old_state != RESOURCE_IDLE) {
            // 重置共享引脚到默认状态
            for (int pin : {SHARED_PIN_1, SHARED_PIN_2, SHARED_PIN_3}) {
                if (pin >= 0) {
                    gpio_reset_pin((gpio_num_t)pin);
                }
            }
            
            // 延迟一小段时间，确保引脚状态稳定
            vTaskDelay(pdMS_TO_TICKS(10));
            
            ESP_LOGI(TAG, "Shared pins reset to default state");
        }
        
        ESP_LOGI(TAG, "Resource released from %s state", 
                old_state == RESOURCE_CAMERA_ACTIVE ? "camera" : 
                old_state == RESOURCE_AUDIO_ACTIVE ? "audio" : "idle");
        
        xSemaphoreGive(g_resource_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire resource mutex for release");
    }
}

// 获取资源状态
resource_state_t get_resource_state(void) {
    resource_state_t state = RESOURCE_IDLE;
    
    if (!g_resource_initialized || g_resource_mutex == NULL) {
        return state;
    }
    
    if (xSemaphoreTake(g_resource_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        state = g_resource_state;
        xSemaphoreGive(g_resource_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire resource mutex for state check");
    }
    
    return state;
}

namespace iot {

class CompactWifiBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Camera* camera_ = nullptr;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            // Use I2C channel 0 for display since camera is not initialized yet
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
        
        // 初始化共享同一个I2C总线的多路复用器
        #ifdef CONFIG_ENABLE_PCA9548A
        ESP_LOGI(TAG, "Initializing multiplexer with shared I2C bus");
        esp_err_t ret = multiplexer_init_with_bus(display_i2c_bus_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize multiplexer: %s", esp_err_to_name(ret));
        }
        #endif
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
            {&font_puhui_14_1, &font_awesome_14_1});
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
        touch_button_.OnClick([this]() {
            if (camera_) {
                camera_->Capture();
                GetDisplay()->ShowNotification("拍照成功");
            }
        });
        touch_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        touch_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeCamera() {
        // 检查资源锁定
        if (!lock_resource_for_camera()) {
            ESP_LOGE(TAG, "无法锁定摄像头资源");
            return;
        }
        
        // 获取板级配置
        board_config_t* board_config = board_config_get();
        if (board_config) {
            // 配置摄像头引脚
            camera_config_t config = {};
            config.pin_pwdn = CAM_PWDN_PIN;
            config.pin_reset = CAM_RESET_PIN;
            config.pin_xclk = CAM_XCLK_PIN;
            config.pin_sccb_sda = CAM_SIOD_PIN;
            config.pin_sccb_scl = CAM_SIOC_PIN;
            
            config.pin_d0 = CAM_Y2_PIN;
            config.pin_d1 = CAM_Y3_PIN;
            config.pin_d2 = CAM_Y4_PIN;
            config.pin_d3 = CAM_Y5_PIN;
            config.pin_d4 = CAM_Y6_PIN;
            config.pin_d5 = CAM_Y7_PIN;
            config.pin_d6 = CAM_Y8_PIN;
            config.pin_d7 = CAM_Y9_PIN;
            
            config.pin_vsync = CAM_VSYNC_PIN;
            config.pin_href = CAM_HREF_PIN;
            config.pin_pclk = CAM_PCLK_PIN;
            
                    // 摄像头设置
        config.xclk_freq_hz = 15000000;
        config.ledc_timer = LEDC_TIMER_0;
        config.ledc_channel = LEDC_CHANNEL_0;
        
        // DFRobot风格 - 使用专用I2C端口并动态分配以避免与显示屏冲突
        // 注意: I2C0端口已用于显示，使用I2C1给摄像头
        config.sccb_i2c_port = 1;
        
        config.pixel_format = PIXFORMAT_JPEG;
            config.frame_size = FRAMESIZE_SVGA;
            config.jpeg_quality = 15;
            config.fb_count = 2;
            config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
            config.fb_location = CAMERA_FB_IN_PSRAM;
            
            // 创建摄像头对象
            camera_ = new Esp32Camera(config);
            if (!camera_) {
                ESP_LOGE(TAG, "摄像头初始化失败：无法分配内存");
                return;
            }
            
            board_config->has_camera = camera_ != nullptr;
            ESP_LOGI(TAG, "摄像头初始化完成");
        }
    }

    void InitializeIot() {
#if CONFIG_IOT_PROTOCOL_XIAOZHI
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Lamp"));
        
        if (camera_ && camera_->IsInitialized()) {
            thing_manager.AddThing(iot::CreateThing("Camera"));
            ESP_LOGI(TAG, "Camera Thing added");
        }
#elif CONFIG_IOT_PROTOCOL_MCP
        static LampController lamp(LAMP_GPIO);
        
        if (camera_ && camera_->IsInitialized()) {
            auto& mcp_server = McpServer::GetInstance();
            
            mcp_server.AddTool("self.camera.take_photo", "拍摄照片", PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
                if (camera_) {
                    camera_->Capture();
                    return true;
                }
                return false;
            });
            
            if (CAM_LED_PIN >= 0) {
                mcp_server.AddTool("self.camera.flash", "控制闪光灯", PropertyList({
                    Property("enable", kPropertyTypeBoolean)
                }), [this](const PropertyList& properties) -> ReturnValue {
                    bool enable = properties["enable"].value<bool>();
                    gpio_set_level((gpio_num_t)CAM_LED_PIN, enable ? 1 : 0);
                    return true;
                });
            }
            
            mcp_server.AddTool("self.camera.set_config", "设置摄像头参数", PropertyList({
                Property("param", kPropertyTypeString),
                Property("value", kPropertyTypeInteger, -2, 2)
            }), [this](const PropertyList& properties) -> ReturnValue {
                std::string param = properties["param"].value<std::string>();
                int value = properties["value"].value<int>();
                
                if (camera_) {
                    if (param == "brightness") {
                        camera_->SetBrightness(value);
                    } else if (param == "contrast") {
                        camera_->SetContrast(value);
                    } else if (param == "saturation") {
                        camera_->SetSaturation(value);
                    } else if (param == "hmirror") {
                        camera_->SetHMirror(value > 0);
                    } else if (param == "vflip") {
                        camera_->SetVFlip(value > 0);
                    } else {
                        return false;
                    }
                    return true;
                }
                return false;
            });
        }
#endif
    }

public:
    CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        
        // 初始化资源管理系统
        init_resource_management();
        
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeCamera();
        InitializeIot();

        // 初始化Vision子系统（如果摄像头初始化成功）
        if (camera_ && camera_->IsInitialized()) {
            // 检查是否已经有VisionController实例
            auto& manager = ComponentManager::GetInstance();
            if (!manager.GetComponent("VisionController")) {
                ESP_LOGI(TAG, "Initializing Vision subsystem...");
                
                // 在Web Server启动后，自动添加Vision支持
                auto webserver_component = manager.GetComponent("WebServer");
                if (webserver_component) {
                    WebServer* webserver = static_cast<WebServer*>(webserver_component);
                    
                    // 注册Vision准备就绪回调
                    webserver->RegisterReadyCallback([webserver]() {
                        // 延迟初始化Vision组件，以确保WebServer已完全启动
                        ESP_LOGI(TAG, "WebServer ready, initializing Vision components");
                        InitVisionComponents(webserver);
                    });
                    
                    ESP_LOGI(TAG, "Vision subsystem initialization scheduled");
                } else {
                    ESP_LOGW(TAG, "WebServer component not found, Vision subsystem not initialized");
                }
            }
        }

        ESP_LOGI(TAG, "Bread Compact WiFi Board Initialized with Camera and Vision support");
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }

    virtual i2c_master_bus_handle_t GetDisplayI2CBusHandle() override {
        return display_i2c_bus_;
    }

    void OnFirmwareUpdate() {
        ESP_LOGI(TAG, "固件更新中，执行相关操作");
        // 暂时关闭摄像头
        if (camera_) {
            delete camera_;
            camera_ = nullptr;
        }

        // 让无线网络继续保持连接
        // 注意：WifiStation是单例，可直接保持连接状态

        ESP_LOGI(TAG, "系统准备就绪，开始固件更新");
    }
    
    void OnWheelRun(int interval_ms) {
        static unsigned long last_frame_time = 0;
        unsigned long current_time = esp_timer_get_time() / 1000;
        
        // 每3秒拍一张照片
        if (current_time - last_frame_time > interval_ms) {
            if (camera_) {
                ESP_LOGI(TAG, "自动拍照");
                camera_->Capture();
            }
            last_frame_time = current_time;
        }
    }
};

} // namespace iot

DECLARE_BOARD(iot::CompactWifiBoard);
