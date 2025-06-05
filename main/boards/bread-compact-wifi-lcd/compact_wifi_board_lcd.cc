#include "wifi_board.h"
#include "audio_codecs/no_audio_codec.h"
#include "display/lcd_display.h"
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
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <freertos/semphr.h>

// 添加多路复用器头文件
#ifdef CONFIG_ENABLE_PCA9548A
#include "ext/include/multiplexer.h"
#endif

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif
 
#define TAG "CompactWifiBoardLCD"

LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_16_4);

// 资源冲突管理实现
static resource_state_t g_resource_state = RESOURCE_IDLE;
static SemaphoreHandle_t g_resource_mutex = NULL;
static bool g_resource_initialized = false;

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

class CompactWifiBoardLCD : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    Button boot_button_;
    Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    LcdDisplay* display_;
    Esp32Camera* camera_ = nullptr;

    void InitializeI2c() {
        i2c_master_bus_config_t bus_config = {
            // 使用Kconfig中配置的I2C通道
            .i2c_port = (i2c_port_t)CONFIG_DISPLAY_I2C_PORT,
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
        ESP_LOGI(TAG, "Initializing multiplexer with shared I2C bus on port %d", CONFIG_DISPLAY_I2C_PORT);
        esp_err_t ret = multiplexer_init_with_bus(display_i2c_bus_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize multiplexer: %s", esp_err_to_name(ret));
        }
        #endif
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };        
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        esp_lcd_panel_reset(panel);
 

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_16_4,
                                        .icon_font = &font_awesome_16_4,
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
                                        .emoji_font = font_emoji_32_init(),
#else
                                        .emoji_font = DISPLAY_HEIGHT >= 240 ? font_emoji_64_init() : font_emoji_32_init(),
#endif
                                    });
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

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
#if CONFIG_IOT_PROTOCOL_XIAOZHI
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
        thing_manager.AddThing(iot::CreateThing("Lamp"));
        
        // 根据配置选项添加舵机控制器
        #ifdef CONFIG_ENABLE_SERVO_CONTROLLER
        thing_manager.AddThing(iot::CreateThing("ServoThing"));
        ESP_LOGI(TAG, "Servo controller enabled");
        #endif
        
        // 根据配置选项添加电机控制器
        #ifdef CONFIG_ENABLE_MOTOR_CONTROLLER
        thing_manager.AddThing(iot::CreateThing("Motor"));
        ESP_LOGI(TAG, "Motor controller enabled");
        #endif
        
        // 根据配置选项添加超声波传感器
        #ifdef CONFIG_ENABLE_US_SENSOR
        thing_manager.AddThing(iot::CreateThing("US"));
        ESP_LOGI(TAG, "Ultrasonic sensor enabled");
        #endif
        
        // 添加摄像头支持
        if (camera_ && camera_->IsInitialized()) {
            thing_manager.AddThing(iot::CreateThing("Camera"));
            ESP_LOGI(TAG, "Camera Thing added");
        }
#elif CONFIG_IOT_PROTOCOL_MCP
        static LampController lamp(LAMP_GPIO);
        
        if (camera_ && camera_->IsInitialized()) {
            auto& mcp_server = McpServer::GetInstance();
            
            mcp_server.AddTool("self.camera.take_photo", "拍摄照片", ::PropertyList(), [this](const ::PropertyList& properties) -> ReturnValue {
                if (camera_) {
                    camera_->Capture();
                    return true;
                }
                return false;
            });
            
            if (CAM_LED_PIN >= 0) {
                mcp_server.AddTool("self.camera.flash", "控制闪光灯", ::PropertyList({
                    ::Property("enable", ::kPropertyTypeBoolean)
                }), [this](const ::PropertyList& properties) -> ReturnValue {
                    bool enable = properties["enable"].template value<bool>();
                    gpio_set_level((gpio_num_t)CAM_LED_PIN, enable ? 1 : 0);
                    return true;
                });
            }
            
            mcp_server.AddTool("self.camera.set_config", "设置摄像头参数", ::PropertyList({
                ::Property("param", ::kPropertyTypeString),
                ::Property("value", ::kPropertyTypeInteger, -2, 2)
            }), [this](const ::PropertyList& properties) -> ReturnValue {
                std::string param = properties["param"].template value<std::string>();
                int value = properties["value"].template value<int>();
                
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

    void InitializeCamera() {
        ESP_LOGI(TAG, "初始化摄像头...");
        
        // 在初始化摄像头之前锁定资源
        if (!lock_resource_for_camera()) {
            ESP_LOGE(TAG, "无法锁定资源用于摄像头初始化，可能存在资源冲突");
            return;
        }
        
        // 获取板级配置
        board_config_t* board_config = Board::GetBoardConfig();
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
            
            // 配置闪光灯引脚
            if (CAM_LED_PIN >= 0) {
                gpio_reset_pin((gpio_num_t)CAM_LED_PIN);
                gpio_set_direction((gpio_num_t)CAM_LED_PIN, GPIO_MODE_OUTPUT);
                gpio_set_level((gpio_num_t)CAM_LED_PIN, 0);
                ESP_LOGI(TAG, "摄像头闪光灯引脚已配置: %d", CAM_LED_PIN);
            }
        } else {
            ESP_LOGE(TAG, "无法获取板级配置");
            // 释放资源锁定
            release_resource();
        }
    }

public:
    CompactWifiBoardLCD() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        // 初始化资源管理系统
        init_resource_management();
        
        InitializeI2c();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeIot();
        InitializeCamera();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        
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
                        ESP_LOGI(TAG, "WebServer ready, initializing Vision component");
                        InitVisionComponent(webserver);
                    });
                    
                    ESP_LOGI(TAG, "Vision subsystem initialization scheduled");
                } else {
                    ESP_LOGW(TAG, "WebServer component not found, Vision subsystem not initialized");
                }
            }
        }
        
        // board_config.cc 现在会自动从宏定义读取舵机配置信息
        ESP_LOGI(TAG, "Bread Compact WiFi LCD Board Initialized with Camera, Vision, Servo and Motor support");
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

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }

    void OnFirmwareUpdate() override {
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
    
    void OnWheelRun(int interval_ms) override {
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

DECLARE_BOARD(CompactWifiBoardLCD);
