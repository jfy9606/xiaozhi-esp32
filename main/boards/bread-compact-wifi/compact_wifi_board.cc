#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <freertos/semphr.h>

// 多路复用器支持
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

// Multiplexer system
#include "ext/include/multiplexer.h"

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "CompactWifiBoard"

LV_FONT_DECLARE(font_puhui_basic_14_1);
LV_FONT_DECLARE(font_awesome_14_1);



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
    
    // 多路复用器相关
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

    static void HW178SetLevelCallback(int pin, int level, void* user_data) {
        auto self = static_cast<CompactWifiBoard*>(user_data);
        if (pin >= 100 && pin <= 115) {
            // Virtual pins 100-115 map to PCF8575 P0-P15
#if ENABLE_PCF8575_GPIO
            if (self->pcf8575_initialized_ && self->pcf8575_handle_) {
                pcf8575_set_level(self->pcf8575_handle_, pin - 100, level);
            }
#endif
        } else {
            // Normal GPIO pins
            gpio_set_level((gpio_num_t)pin, level);
        }
    }
#endif


    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            // Use I2C channel 0 for display
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
    }

    void InitializeMultiplexerI2CBus() {
        ESP_LOGI(TAG, "Initializing I2C bus for multiplexers");
        
        // 初始化多路复用器专用I2C总线
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
            ESP_LOGE(TAG, "Failed to initialize multiplexer I2C bus: %s", esp_err_to_name(ret));
            i2c_bus_initialized_ = false;
            return;
        }
        
        // 初始化multiplexer系统，设置全局I2C句柄
        ret = multiplexer_init_with_bus(i2c_bus_handle_);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to initialize multiplexer system: %s", esp_err_to_name(ret));
        }
        
        i2c_bus_initialized_ = true;
        ESP_LOGI(TAG, "Multiplexer I2C bus initialized successfully (SDA: GPIO%d, SCL: GPIO%d)", 
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
            ESP_LOGE(TAG, "Failed to create PCA9548A device");
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
        
        ESP_LOGI(TAG, "Initializing PCF8575 GPIO multiplexer");
        
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
        ESP_LOGI(TAG, "PCF8575 GPIO multiplexer initialized successfully at address 0x%02X (via PCA9548A channel %d)", 
                 PCF8575_I2C_ADDR, PCF8575_PCA9548A_CHANNEL);
    }
#endif

#if ENABLE_HW178_ANALOG
    void InitializeHW178() {
        ESP_LOGI(TAG, "Initializing HW-178 analog multiplexer");
        
        // 初始化ADC
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_2,
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
            .set_level_cb = HW178SetLevelCallback,
            .user_data = this,
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

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        touch_button_.OnClick([this]() {
            GetDisplay()->ShowNotification("触摸按钮");
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
        thing_manager.AddThing(iot::CreateThing("UltrasonicSensor"));
        ESP_LOGI(TAG, "Ultrasonic sensor enabled");
#endif
#elif CONFIG_IOT_PROTOCOL_MCP
        static LampController lamp(LAMP_GPIO);
#endif
    }
    
    // 物联网初始化，逐步迁移到 MCP 协议
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
    }

public:
    CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO),
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
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        
        // 显示系统稳定后再初始化多路复用器
        ESP_LOGI(TAG, "Starting multiplexer initialization...");
        InitializeMultiplexerI2CBus();
        
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
        
        ESP_LOGI(TAG, "Multiplexer initialization completed, starting IoT components...");
        
        // 多路复用器初始化完成后再初始化IoT组件
        InitializeIot();

        // 记录多路复用器状态
        ESP_LOGI(TAG, "Multiplexer I2C Bus: %s", i2c_bus_initialized_ ? "Initialized" : "Failed");
        
#if ENABLE_PCA9548A_MUX
        ESP_LOGI(TAG, "PCA9548A I2C Multiplexer: %s", pca9548a_initialized_ ? "Initialized" : "Failed");
#endif

#if ENABLE_LU9685_SERVO
        ESP_LOGI(TAG, "LU9685 Servo Controller: %s", lu9685_initialized_ ? "Initialized" : "Failed");
#endif

#if ENABLE_PCF8575_GPIO
        ESP_LOGI(TAG, "PCF8575 GPIO Multiplexer: %s", pcf8575_initialized_ ? "Initialized" : "Failed");
#endif

#if ENABLE_HW178_ANALOG
        ESP_LOGI(TAG, "HW-178 Analog Multiplexer: %s", hw178_initialized_ ? "Initialized" : "Failed");
#endif

        ESP_LOGI(TAG, "Bread Compact WiFi Board Initialized");
        InitializeTools();
    }

    virtual ~CompactWifiBoard() {
        // 清理多路复用器资源
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

        if (display_i2c_bus_) {
            i2c_del_master_bus(display_i2c_bus_);
        }
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

    virtual i2c_master_bus_handle_t GetDisplayI2CBusHandle() override {
        return display_i2c_bus_;
    }

    void OnFirmwareUpdate() {
        ESP_LOGI(TAG, "固件更新中，执行相关操作");
        // 让无线网络继续保持连接
        // 注意：WifiStation是单例，可直接保持连接状态

        ESP_LOGI(TAG, "系统准备就绪，开始固件更新");
    }

    // 多路复用器访问方法
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
    bool IsPcf8575Available() const {
        return pcf8575_initialized_;
    }

    pcf8575_handle_t GetPcf8575Handle() const {
        return pcf8575_handle_;
    }

    bool IsGPIOMultiplexerAvailable() const {
        return pcf8575_initialized_;
    }
    
    bool SetMultiplexerPin(int pin, bool level) {
        if (!pcf8575_initialized_ || pin < 0 || pin >= 16) {
            return false;
        }
        
        esp_err_t ret = pcf8575_set_level(pcf8575_handle_, pin, level ? 1 : 0);
        return ret == ESP_OK;
    }
    
    bool GetMultiplexerPin(int pin, bool* level) {
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
    
    bool SetMultiplexerPorts(uint16_t value) {
        if (!pcf8575_initialized_) {
            return false;
        }
        
        esp_err_t ret = pcf8575_write_ports(pcf8575_handle_, value);
        return ret == ESP_OK;
    }
    
    bool GetMultiplexerPorts(uint16_t* value) {
        if (!pcf8575_initialized_ || !value) {
            return false;
        }
        
        esp_err_t ret = pcf8575_read_ports(pcf8575_handle_, value);
        return ret == ESP_OK;
    }
    
    bool SetMultiplexerPins(uint16_t pin_mask, uint16_t levels) {
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

    // 获取多路复用器状态信息
    std::string GetMultiplexerStatusJson() const {
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

} // namespace iot

DECLARE_BOARD(iot::CompactWifiBoard);
