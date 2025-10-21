#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
// 使用ESP-IDF官方摄像头支持
#include <esp_camera.h>

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>

// 扩展器支持
#if ENABLE_PCA9548A_MUX
#include "ext/include/pca9548a.h"
#endif

#if ENABLE_LU9685_SERVO
#include "ext/include/lu9685.h"
#endif

#if ENABLE_PCF8575_GPIO
#include "ext/include/pcf8575.h"
#endif

#if ENABLE_HW178_ANALOG
#include "ext/include/hw178.h"
#include <esp_adc/adc_oneshot.h>
#endif

// Multiplexer系统
#include "ext/include/multiplexer.h"

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
 
#define TAG "CompactWifiBoardS3Cam"

class CompactWifiBoardS3Cam : public WifiBoard {
private:
 
    Button boot_button_;
    LcdDisplay* display_;
    bool camera_initialized_;
    
    // 扩展器相关
    i2c_master_bus_handle_t i2c_bus_handle_;
    bool i2c_bus_initialized_;
    
#if ENABLE_PCA9548A_MUX
    pca9548a_handle_t pca9548a_handle_;
    bool pca9548a_initialized_;
#endif

#if ENABLE_LU9685_SERVO
    lu9685_handle_t lu9685_handle_;
    bool lu9685_initialized_;
#endif

#if ENABLE_PCF8575_GPIO
    pcf8575_handle_t pcf8575_handle_;
    bool pcf8575_initialized_;
#endif

#if ENABLE_HW178_ANALOG
    hw178_handle_t hw178_handle_;
    adc_oneshot_unit_handle_t adc_handle_;
    bool hw178_initialized_;
#endif

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
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeCamera() {
        ESP_LOGI(TAG, "Initializing ESP-IDF camera system");
        
        camera_config_t config;
        config.ledc_channel = LEDC_CHANNEL_0;
        config.ledc_timer = LEDC_TIMER_0;
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = CAMERA_PIN_SIOD;
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.pixel_format = PIXFORMAT_JPEG;
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        
        // 初始化摄像头
        esp_err_t err = esp_camera_init(&config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
            camera_initialized_ = false;
            return;
        }
        
        camera_initialized_ = true;
        ESP_LOGI(TAG, "ESP-IDF camera system initialized successfully");
    }

    void DeinitializeCamera() {
        if (camera_initialized_) {
            esp_camera_deinit();
            camera_initialized_ = false;
            ESP_LOGI(TAG, "ESP-IDF camera system deinitialized");
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
        
        // Add long press for camera toggle functionality
        boot_button_.OnLongPress([this]() {
            // Simple camera toggle - reinitialize camera
            if (camera_initialized_) {
                DeinitializeCamera();
                GetDisplay()->ShowNotification("Camera Disabled");
            } else {
                InitializeCamera();
                if (camera_initialized_) {
                    GetDisplay()->ShowNotification("Camera Enabled");
                } else {
                    GetDisplay()->ShowNotification("Camera Init Failed");
                }
            }
        });
    }

    void InitializeI2CBus() {
        ESP_LOGI(TAG, "Initializing I2C bus for expanders");
        
        // 初始化I2C总线
        i2c_master_bus_config_t i2c_bus_config = {
            .i2c_port = I2C_EXT_PORT,
            .sda_io_num = I2C_EXT_SDA_PIN,
            .scl_io_num = I2C_EXT_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = true,
            },
        };
        
        esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(ret));
            i2c_bus_initialized_ = false;
            return;
        }
        
        // 初始化multiplexer系统，设置全局I2C句柄
        ret = multiplexer_init_with_bus(i2c_bus_handle_);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to initialize multiplexer system: %s", esp_err_to_name(ret));
        }
        
        i2c_bus_initialized_ = true;
        ESP_LOGI(TAG, "I2C bus initialized successfully (SDA: GPIO%d, SCL: GPIO%d)", 
                 I2C_EXT_SDA_PIN, I2C_EXT_SCL_PIN);
    }

#if ENABLE_PCA9548A_MUX
    void InitializePCA9548A() {
        if (!i2c_bus_initialized_) {
            ESP_LOGE(TAG, "I2C bus not initialized, cannot initialize PCA9548A");
            pca9548a_initialized_ = false;
            return;
        }
        
        ESP_LOGI(TAG, "Initializing PCA9548A I2C multiplexer");
        
        pca9548a_config_t pca9548a_config = {
            .i2c_port = I2C_EXT_PORT,
            .i2c_addr = PCA9548A_I2C_ADDR,
            .i2c_timeout_ms = I2C_EXT_TIMEOUT_MS,
            .reset_pin = PCA9548A_RESET_PIN,
        };
        
        pca9548a_handle_ = pca9548a_create(&pca9548a_config);
        if (pca9548a_handle_ == nullptr) {
            ESP_LOGW(TAG, "PCA9548A device not found (hardware not connected), continuing without multiplexer");
            pca9548a_initialized_ = false;
            return;
        }
        
        // 初始化时关闭所有通道
        esp_err_t ret = pca9548a_select_channels(pca9548a_handle_, PCA9548A_CHANNEL_NONE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize PCA9548A channels: %s", esp_err_to_name(ret));
            pca9548a_initialized_ = false;
            return;
        }
        
        pca9548a_initialized_ = true;
        ESP_LOGI(TAG, "PCA9548A I2C multiplexer initialized successfully at address 0x%02X", PCA9548A_I2C_ADDR);
    }
#endif

#if ENABLE_LU9685_SERVO
    void InitializeLU9685() {
        if (!i2c_bus_initialized_) {
            ESP_LOGE(TAG, "I2C bus not initialized, cannot initialize LU9685");
            lu9685_initialized_ = false;
            return;
        }
        
        ESP_LOGI(TAG, "Initializing LU9685 servo controller");
        
        lu9685_config_t lu9685_config = {
            .i2c_port = i2c_bus_handle_,
            .i2c_addr = LU9685_I2C_ADDR,
            .pwm_freq = LU9685_PWM_FREQ,
            .use_pca9548a = true,
            .pca9548a_channel = LU9685_PCA9548A_CHANNEL,
        };
        
        lu9685_handle_ = lu9685_init(&lu9685_config);
        if (lu9685_handle_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create LU9685 device");
            lu9685_initialized_ = false;
            return;
        }
        
        // 设置所有通道为中位 (90度)
        for (int i = 0; i < 16; i++) {
            lu9685_set_channel_angle(lu9685_handle_, i, 90);
        }
        
        lu9685_initialized_ = true;
        ESP_LOGI(TAG, "LU9685 servo controller initialized successfully at address 0x%02X (via PCA9548A channel %d)", 
                 LU9685_I2C_ADDR, LU9685_PCA9548A_CHANNEL);
    }
#endif

#if ENABLE_PCF8575_GPIO
    void InitializePCF8575() {
        if (!i2c_bus_initialized_) {
            ESP_LOGE(TAG, "I2C bus not initialized, cannot initialize PCF8575");
            pcf8575_initialized_ = false;
            return;
        }
        
        ESP_LOGI(TAG, "Initializing PCF8575 GPIO expander");
        
        pcf8575_config_t pcf8575_config = {
            .i2c_port = i2c_bus_handle_,
            .i2c_addr = PCF8575_I2C_ADDR,
            .i2c_timeout_ms = I2C_EXT_TIMEOUT_MS,
            .all_output = true,  // 设置为输出模式
            .use_pca9548a = true,
            .pca9548a_channel = PCF8575_PCA9548A_CHANNEL,
        };
        
        pcf8575_handle_ = pcf8575_create(&pcf8575_config);
        if (pcf8575_handle_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create PCF8575 device");
            pcf8575_initialized_ = false;
            return;
        }
        
        // 初始化所有引脚为低电平
        esp_err_t ret = pcf8575_write_ports(pcf8575_handle_, 0x0000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize PCF8575 ports: %s", esp_err_to_name(ret));
            pcf8575_initialized_ = false;
            return;
        }
        
        pcf8575_initialized_ = true;
        ESP_LOGI(TAG, "PCF8575 GPIO expander initialized successfully at address 0x%02X (via PCA9548A channel %d)", 
                 PCF8575_I2C_ADDR, PCF8575_PCA9548A_CHANNEL);
    }
#endif

#if ENABLE_HW178_ANALOG
    void InitializeHW178() {
        ESP_LOGI(TAG, "Initializing HW-178 analog multiplexer");
        
        // 初始化ADC
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,
            .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        
        esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize ADC: %s", esp_err_to_name(ret));
            hw178_initialized_ = false;
            return;
        }
        
        // 配置ADC通道
        adc_oneshot_chan_cfg_t config = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        
        ret = adc_oneshot_config_channel(adc_handle_, HW178_ADC_CHANNEL, &config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
            hw178_initialized_ = false;
            return;
        }
        
        // 初始化HW-178
        hw178_config_t hw178_config = {
            .s0_pin = HW178_S0_PIN,
            .s1_pin = HW178_S1_PIN,
            .s2_pin = HW178_S2_PIN,
            .s3_pin = HW178_S3_PIN,
            .sig_pin = HW178_SIG_PIN,
        };
        
        hw178_handle_ = hw178_create(&hw178_config);
        if (hw178_handle_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create HW-178 device");
            hw178_initialized_ = false;
            return;
        }
        
        hw178_initialized_ = true;
        ESP_LOGI(TAG, "HW-178 analog multiplexer initialized successfully (S0-S3: GPIO%d,%d,%d,%d, SIG: GPIO%d)", 
                 HW178_S0_PIN, HW178_S1_PIN, HW178_S2_PIN, HW178_S3_PIN, HW178_SIG_PIN);
    }
#endif



public:
    CompactWifiBoardS3Cam() :
        boot_button_(BOOT_BUTTON_GPIO),
        camera_initialized_(false),
        i2c_bus_handle_(nullptr),
        i2c_bus_initialized_(false)
#if ENABLE_PCA9548A_MUX
        , pca9548a_handle_(nullptr)
        , pca9548a_initialized_(false)
#endif
#if ENABLE_LU9685_SERVO
        , lu9685_handle_(nullptr)
        , lu9685_initialized_(false)
#endif
#if ENABLE_PCF8575_GPIO
        , pcf8575_handle_(nullptr)
        , pcf8575_initialized_(false)
#endif
#if ENABLE_HW178_ANALOG
        , hw178_handle_(nullptr)
        , adc_handle_(nullptr)
        , hw178_initialized_(false)
#endif
    {
        
        // 先初始化显示系统
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        
        // 显示系统稳定后再初始化扩展器
        ESP_LOGI(TAG, "Starting expander initialization...");
        InitializeI2CBus();
        
#if ENABLE_PCA9548A_MUX
        InitializePCA9548A();
#endif

#if ENABLE_LU9685_SERVO
        InitializeLU9685();
#endif

#if ENABLE_PCF8575_GPIO
        InitializePCF8575();
#endif

#if ENABLE_HW178_ANALOG
        InitializeHW178();
#endif
        
        ESP_LOGI(TAG, "Expander initialization completed, starting camera system...");
        
        // 扩展器初始化完成后再初始化摄像头系统
        InitializeCamera();
        
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        
        // Log initialization status
        ESP_LOGI(TAG, "=== Compact WiFi S3Cam Board Initialization Complete ===");
        ESP_LOGI(TAG, "Camera System: %s", camera_initialized_ ? "Initialized" : "Failed");
        
        ESP_LOGI(TAG, "Audio Mode: %s", 
#ifdef AUDIO_I2S_METHOD_SIMPLEX
                 "Simplex (No Pin Conflicts)"
#else
                 "Duplex (Potential Pin Conflicts)"
#endif
        );
        
        // 记录扩展器状态
        ESP_LOGI(TAG, "I2C Bus: %s", i2c_bus_initialized_ ? "Initialized" : "Failed");
        
#if ENABLE_PCA9548A_MUX
        ESP_LOGI(TAG, "PCA9548A I2C Multiplexer: %s", pca9548a_initialized_ ? "Initialized" : "Failed");
#endif

#if ENABLE_LU9685_SERVO
        ESP_LOGI(TAG, "LU9685 Servo Controller: %s", lu9685_initialized_ ? "Initialized" : "Failed");
#endif

#if ENABLE_PCF8575_GPIO
        ESP_LOGI(TAG, "PCF8575 GPIO Expander: %s", pcf8575_initialized_ ? "Initialized" : "Failed");
#endif

#if ENABLE_HW178_ANALOG
        ESP_LOGI(TAG, "HW-178 Analog Multiplexer: %s", hw178_initialized_ ? "Initialized" : "Failed");
#endif
        
        ESP_LOGI(TAG, "========================================================");
    }

    virtual ~CompactWifiBoardS3Cam() {
        DeinitializeCamera();
        
        // 清理扩展器资源
#if ENABLE_HW178_ANALOG
        if (hw178_handle_) {
            hw178_delete(hw178_handle_);
        }
        if (adc_handle_) {
            adc_oneshot_del_unit(adc_handle_);
        }
#endif

#if ENABLE_PCF8575_GPIO
        if (pcf8575_handle_) {
            pcf8575_delete(&pcf8575_handle_);
        }
#endif

#if ENABLE_LU9685_SERVO
        if (lu9685_handle_) {
            lu9685_deinit(&lu9685_handle_);
        }
#endif

#if ENABLE_PCA9548A_MUX
        if (pca9548a_handle_) {
            pca9548a_delete(pca9548a_handle_);
        }
#endif

        if (i2c_bus_handle_) {
            i2c_del_master_bus(i2c_bus_handle_);
        }
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        // 检查摄像头和音频引脚冲突
        if (camera_initialized_) {
#ifndef AUDIO_I2S_METHOD_SIMPLEX
            ESP_LOGW(TAG, "Camera active - audio duplex mode has pin conflicts");
            ESP_LOGW(TAG, "Consider disabling camera or using simplex audio mode");
            // 返回null以防止引脚冲突
            return nullptr;
#endif
        }

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
        // 使用ESP-IDF官方摄像头API
        if (camera_initialized_) {
            // 返回一个简单的摄像头包装器，或者直接返回nullptr让上层使用esp_camera API
            // 这里暂时返回nullptr，让应用层直接使用esp_camera_fb_get()等API
        }
        return nullptr;
    }

    // 简化的摄像头状态查询
    bool IsCameraEnabled() const {
        return camera_initialized_;
    }

    // 获取摄像头帧缓冲区（使用ESP-IDF官方API）
    camera_fb_t* GetCameraFrameBuffer() {
        if (!camera_initialized_) {
            return nullptr;
        }
        return esp_camera_fb_get();
    }

    // 释放摄像头帧缓冲区
    void ReturnCameraFrameBuffer(camera_fb_t* fb) {
        if (fb) {
            esp_camera_fb_return(fb);
        }
    }

    // 扩展器访问方法
    bool IsI2CBusAvailable() const {
        return i2c_bus_initialized_;
    }

#if ENABLE_PCA9548A_MUX
    bool IsPCA9548AAvailable() const {
        return pca9548a_initialized_;
    }
    
    bool SelectI2CChannel(uint8_t channel_mask) {
        if (!pca9548a_initialized_) {
            return false;
        }
        
        esp_err_t ret = pca9548a_select_channels(pca9548a_handle_, channel_mask);
        return ret == ESP_OK;
    }
    
    bool GetSelectedI2CChannels(uint8_t* channels) {
        if (!pca9548a_initialized_ || !channels) {
            return false;
        }
        
        esp_err_t ret = pca9548a_get_selected_channels(pca9548a_handle_, channels);
        return ret == ESP_OK;
    }
#endif

#if ENABLE_LU9685_SERVO
    bool IsServoControllerAvailable() const {
        return lu9685_initialized_;
    }
    
    bool SetServoAngle(int channel, int angle) {
        if (!lu9685_initialized_ || channel < 0 || channel >= 16 || angle < 0 || angle > 180) {
            return false;
        }
        
        esp_err_t ret = lu9685_set_channel_angle(lu9685_handle_, channel, angle);
        return ret == ESP_OK;
    }
    
    bool SetServoPWM(int channel, float duty_percent) {
        if (!lu9685_initialized_ || channel < 0 || channel >= 16 || duty_percent < 0.0 || duty_percent > 100.0) {
            return false;
        }
        
        esp_err_t ret = lu9685_set_duty_percent(lu9685_handle_, channel, duty_percent);
        return ret == ESP_OK;
    }
    
    bool SetAllServosAngle(int angle) {
        if (!lu9685_initialized_ || angle < 0 || angle > 180) {
            return false;
        }
        
        bool success = true;
        for (int i = 0; i < 16; i++) {
            if (lu9685_set_channel_angle(lu9685_handle_, i, angle) != ESP_OK) {
                success = false;
            }
        }
        return success;
    }
    
    bool SetServoFrequency(uint16_t freq_hz) {
        if (!lu9685_initialized_) {
            return false;
        }
        
        esp_err_t ret = lu9685_set_frequency(lu9685_handle_, freq_hz);
        return ret == ESP_OK;
    }
#endif

#if ENABLE_PCF8575_GPIO
    bool IsGPIOExpanderAvailable() const {
        return pcf8575_initialized_;
    }
    
    bool SetExpanderPin(int pin, bool level) {
        if (!pcf8575_initialized_ || pin < 0 || pin >= 16) {
            return false;
        }
        
        esp_err_t ret = pcf8575_set_level(pcf8575_handle_, pin, level ? 1 : 0);
        return ret == ESP_OK;
    }
    
    bool GetExpanderPin(int pin, bool* level) {
        if (!pcf8575_initialized_ || pin < 0 || pin >= 16 || !level) {
            return false;
        }
        
        uint32_t pin_level;
        esp_err_t ret = pcf8575_get_level(pcf8575_handle_, pin, &pin_level);
        if (ret == ESP_OK) {
            *level = (pin_level != 0);
            return true;
        }
        return false;
    }
    
    bool SetExpanderPorts(uint16_t value) {
        if (!pcf8575_initialized_) {
            return false;
        }
        
        esp_err_t ret = pcf8575_write_ports(pcf8575_handle_, value);
        return ret == ESP_OK;
    }
    
    bool GetExpanderPorts(uint16_t* value) {
        if (!pcf8575_initialized_ || !value) {
            return false;
        }
        
        esp_err_t ret = pcf8575_read_ports(pcf8575_handle_, value);
        return ret == ESP_OK;
    }
    
    bool SetExpanderPins(uint16_t pin_mask, uint16_t levels) {
        if (!pcf8575_initialized_) {
            return false;
        }
        
        esp_err_t ret = pcf8575_set_pins(pcf8575_handle_, pin_mask, levels);
        return ret == ESP_OK;
    }
#endif

#if ENABLE_HW178_ANALOG
    bool IsAnalogMuxAvailable() const {
        return hw178_initialized_;
    }
    
    bool SelectAnalogChannel(int channel) {
        if (!hw178_initialized_ || channel < 0 || channel >= HW178_CHANNEL_COUNT) {
            return false;
        }
        
        esp_err_t ret = hw178_select_channel(hw178_handle_, (hw178_channel_t)channel);
        return ret == ESP_OK;
    }
    
    bool ReadAnalogValue(int* value) {
        if (!hw178_initialized_ || !value) {
            return false;
        }
        
        esp_err_t ret = adc_oneshot_read(adc_handle_, HW178_ADC_CHANNEL, value);
        return ret == ESP_OK;
    }
    
    bool ReadAnalogChannel(int channel, int* value) {
        if (!SelectAnalogChannel(channel)) {
            return false;
        }
        
        // 等待通道切换稳定
        vTaskDelay(pdMS_TO_TICKS(1));
        
        return ReadAnalogValue(value);
    }
    
    int GetCurrentAnalogChannel() {
        if (!hw178_initialized_) {
            return -1;
        }
        
        hw178_channel_t channel;
        esp_err_t ret = hw178_get_selected_channel(hw178_handle_, &channel);
        return (ret == ESP_OK) ? (int)channel : -1;
    }
    
    // 扫描所有模拟通道
    bool ScanAllAnalogChannels(int values[HW178_CHANNEL_COUNT]) {
        if (!hw178_initialized_ || !values) {
            return false;
        }
        
        bool success = true;
        for (int i = 0; i < HW178_CHANNEL_COUNT; i++) {
            if (!ReadAnalogChannel(i, &values[i])) {
                success = false;
                values[i] = -1; // 标记失败的通道
            }
        }
        return success;
    }
#endif

    // 获取扩展器状态信息
    std::string GetExpanderStatusJson() const {
        std::string status = "{";
        status += "\"i2c_bus\":" + std::string(i2c_bus_initialized_ ? "true" : "false");
        
#if ENABLE_PCA9548A_MUX
        status += ",\"pca9548a\":" + std::string(pca9548a_initialized_ ? "true" : "false");
#endif

#if ENABLE_LU9685_SERVO
        status += ",\"lu9685\":" + std::string(lu9685_initialized_ ? "true" : "false");
#endif

#if ENABLE_PCF8575_GPIO
        status += ",\"pcf8575\":" + std::string(pcf8575_initialized_ ? "true" : "false");
#endif

#if ENABLE_HW178_ANALOG
        status += ",\"hw178\":" + std::string(hw178_initialized_ ? "true" : "false");
#endif

        status += "}";
        return status;
    }


};

DECLARE_BOARD(CompactWifiBoardS3Cam);
