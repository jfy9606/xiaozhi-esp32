#ifndef BOARD_H
#define BOARD_H

#include <http.h>
#include <web_socket.h>
#include <mqtt.h>
#include <udp.h>
#include <string>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <vector>
#include <functional>
#include <network_interface.h>

#include "led/led.h"
#include "backlight.h"
#include "camera.h"
#include "assets.h"
#include "ext/include/i2c_utils.h"
#include "sdkconfig.h"

// 摄像头信息结构体
typedef struct {
    const char* model;       // 摄像头型号
    const char* name;        // 摄像头名称
    bool is_supported;       // 是否支持
} camera_info_t;

// 板级配置结构体，用于存储板子的引脚配置
typedef struct {
    // 电机引脚
    int ena_pin;
    int enb_pin;
    int in1_pin;
    int in2_pin;
    int in3_pin;
    int in4_pin;
    
    // 舵机引脚
    int* servo_pins;  // 舵机引脚数组
    int servo_count;  // 舵机数量
    
    // 摄像头引脚
    int pwdn_pin;
    int reset_pin;
    int xclk_pin;
    int siod_pin;
    int sioc_pin;
    int y2_pin;
    int y3_pin;
    int y4_pin;
    int y5_pin;
    int y6_pin;
    int y7_pin;
    int y8_pin;
    int y9_pin;
    int vsync_pin;
    int href_pin;
    int pclk_pin;
    int cam_led_pin;

    // 音频引脚
    int audio_i2s_mic_ws;
    int audio_i2s_mic_sck;
    int audio_i2s_mic_din;
    int audio_i2s_spk_dout;
    int audio_i2s_spk_bclk;
    int audio_i2s_spk_lrck;

    // 按钮与LED引脚
    int builtin_led_pin;
    int boot_button_pin;
    int touch_button_pin;
    int volume_up_button_pin;
    int volume_down_button_pin;

    // 摄像头配置与信息
    bool camera_supported;           // 是否支持摄像头
    bool has_camera;                 // 是否已初始化摄像头
    camera_info_t* camera_info;      // 摄像头信息

    // 超声波传感器引脚
    int us_front_trig_pin;
    int us_front_echo_pin;
    int us_rear_trig_pin;
    int us_rear_echo_pin;
} board_config_t;

// 根据电机连接方式选择定义
#if defined(CONFIG_MOTOR_CONNECTION_DIRECT)
// 直接连接模式下的引脚定义
// Default motor pins - using Kconfig values or fallback to defaults
#ifndef MOTOR_ENA_PIN
#ifdef CONFIG_MOTOR_ENA_PIN
#define MOTOR_ENA_PIN             CONFIG_MOTOR_ENA_PIN
#else
#define MOTOR_ENA_PIN             2  // Default ENA pin
#endif
#endif

#ifndef MOTOR_ENB_PIN
#ifdef CONFIG_MOTOR_ENB_PIN
#define MOTOR_ENB_PIN             CONFIG_MOTOR_ENB_PIN
#else
#define MOTOR_ENB_PIN             1  // Default ENB pin
#endif
#endif

#ifndef MOTOR_IN1_PIN
#ifdef CONFIG_MOTOR_IN1_PIN
#define MOTOR_IN1_PIN             CONFIG_MOTOR_IN1_PIN
#else
#define MOTOR_IN1_PIN             47  // Default IN1 pin
#endif
#endif

#ifndef MOTOR_IN2_PIN
#ifdef CONFIG_MOTOR_IN2_PIN
#define MOTOR_IN2_PIN             CONFIG_MOTOR_IN2_PIN
#else
#define MOTOR_IN2_PIN             21  // Default IN2 pin
#endif
#endif

#ifndef MOTOR_IN3_PIN
#ifdef CONFIG_MOTOR_IN3_PIN
#define MOTOR_IN3_PIN             CONFIG_MOTOR_IN3_PIN
#else
#define MOTOR_IN3_PIN             20  // Default IN3 pin
#endif
#endif

#ifndef MOTOR_IN4_PIN
#ifdef CONFIG_MOTOR_IN4_PIN
#define MOTOR_IN4_PIN             CONFIG_MOTOR_IN4_PIN
#else
#define MOTOR_IN4_PIN             19  // Default IN4 pin
#endif
#endif

#elif defined(CONFIG_MOTOR_CONNECTION_PCF8575)
// PCF8575连接模式下只定义ENA/ENB引脚
#ifndef MOTOR_ENA_PIN
#ifdef CONFIG_MOTOR_ENA_PIN
#define MOTOR_ENA_PIN             CONFIG_MOTOR_ENA_PIN
#else
#define MOTOR_ENA_PIN             2  // Default ENA pin
#endif
#endif

#ifndef MOTOR_ENB_PIN
#ifdef CONFIG_MOTOR_ENB_PIN
#define MOTOR_ENB_PIN             CONFIG_MOTOR_ENB_PIN
#else
#define MOTOR_ENB_PIN             1  // Default ENB pin
#endif
#endif
#else
// 默认定义所有引脚以保持兼容性
#ifndef MOTOR_ENA_PIN
#ifdef CONFIG_MOTOR_ENA_PIN
#define MOTOR_ENA_PIN             CONFIG_MOTOR_ENA_PIN
#else
#define MOTOR_ENA_PIN             2  // Default ENA pin
#endif
#endif

#ifndef MOTOR_ENB_PIN
#ifdef CONFIG_MOTOR_ENB_PIN
#define MOTOR_ENB_PIN             CONFIG_MOTOR_ENB_PIN
#else
#define MOTOR_ENB_PIN             1  // Default ENB pin
#endif
#endif

#ifndef MOTOR_IN1_PIN
#ifdef CONFIG_MOTOR_IN1_PIN
#define MOTOR_IN1_PIN             CONFIG_MOTOR_IN1_PIN
#else
#define MOTOR_IN1_PIN             47  // Default IN1 pin
#endif
#endif

#ifndef MOTOR_IN2_PIN
#ifdef CONFIG_MOTOR_IN2_PIN
#define MOTOR_IN2_PIN             CONFIG_MOTOR_IN2_PIN
#else
#define MOTOR_IN2_PIN             21  // Default IN2 pin
#endif
#endif

#ifndef MOTOR_IN3_PIN
#ifdef CONFIG_MOTOR_IN3_PIN
#define MOTOR_IN3_PIN             CONFIG_MOTOR_IN3_PIN
#else
#define MOTOR_IN3_PIN             20  // Default IN3 pin
#endif
#endif

#ifndef MOTOR_IN4_PIN
#ifdef CONFIG_MOTOR_IN4_PIN
#define MOTOR_IN4_PIN             CONFIG_MOTOR_IN4_PIN
#else
#define MOTOR_IN4_PIN             19  // Default IN4 pin
#endif
#endif
#endif

// 舵机相关定义
#ifndef SERVO_COUNT
#ifdef CONFIG_SERVO_COUNT
#define SERVO_COUNT               CONFIG_SERVO_COUNT
#else
#define SERVO_COUNT               4  // Default servo count
#endif
#endif

// 根据舵机连接方式选择定义
#if !defined(CONFIG_SERVO_CONNECTION_LU9685)
// 直接连接模式或未定义模式下的引脚定义
#ifndef SERVO_PIN_1
#ifdef CONFIG_SERVO_PIN_1
#define SERVO_PIN_1               CONFIG_SERVO_PIN_1
#else
#define SERVO_PIN_1               -1  // 默认不设置
#endif
#endif

#ifndef SERVO_PIN_2
#ifdef CONFIG_SERVO_PIN_2
#define SERVO_PIN_2               CONFIG_SERVO_PIN_2
#else
#define SERVO_PIN_2               -1  // 默认不设置
#endif
#endif

#ifndef SERVO_PIN_3
#ifdef CONFIG_SERVO_PIN_3
#define SERVO_PIN_3               CONFIG_SERVO_PIN_3
#else
#define SERVO_PIN_3               -1  // 默认不设置
#endif
#endif

#ifndef SERVO_PIN_4
#ifdef CONFIG_SERVO_PIN_4
#define SERVO_PIN_4               CONFIG_SERVO_PIN_4
#else
#define SERVO_PIN_4               -1  // 默认不设置
#endif
#endif

#ifndef SERVO_PIN_5
#ifdef CONFIG_SERVO_PIN_5
#define SERVO_PIN_5               CONFIG_SERVO_PIN_5
#else
#define SERVO_PIN_5               -1  // 默认不设置
#endif
#endif

#ifndef SERVO_PIN_6
#ifdef CONFIG_SERVO_PIN_6
#define SERVO_PIN_6               CONFIG_SERVO_PIN_6
#else
#define SERVO_PIN_6               -1  // 默认不设置
#endif
#endif

#ifndef SERVO_PIN_7
#ifdef CONFIG_SERVO_PIN_7
#define SERVO_PIN_7               CONFIG_SERVO_PIN_7
#else
#define SERVO_PIN_7               -1  // 默认不设置
#endif
#endif

#ifndef SERVO_PIN_8
#ifdef CONFIG_SERVO_PIN_8
#define SERVO_PIN_8               CONFIG_SERVO_PIN_8
#else
#define SERVO_PIN_8               -1  // 默认不设置
#endif
#endif
#endif

// I2C多路复用器相关定义
#ifndef I2C_MUX_SDA_PIN
#ifdef CONFIG_PCA9548A_SDA_PIN
#define I2C_MUX_SDA_PIN             CONFIG_PCA9548A_SDA_PIN
#else
#define I2C_MUX_SDA_PIN             8  // Default SDA pin
#endif
#endif

#ifndef I2C_MUX_SCL_PIN
#ifdef CONFIG_PCA9548A_SCL_PIN
#define I2C_MUX_SCL_PIN             CONFIG_PCA9548A_SCL_PIN
#else
#define I2C_MUX_SCL_PIN             9  // Default SCL pin
#endif
#endif

#ifndef I2C_MUX_FREQ_HZ
#ifdef CONFIG_PCA9548A_I2C_FREQ_HZ
#define I2C_MUX_FREQ_HZ             CONFIG_PCA9548A_I2C_FREQ_HZ
#else
#define I2C_MUX_FREQ_HZ             400000       // Default 400KHz frequency
#endif
#endif

#ifndef PCA9548A_I2C_PORT
#ifdef CONFIG_PCA9548A_I2C_PORT
#define PCA9548A_I2C_PORT           CONFIG_PCA9548A_I2C_PORT
#else
#define PCA9548A_I2C_PORT           I2C_NUM_0
#endif
#endif

#ifndef PCA9548A_I2C_ADDR
#ifdef CONFIG_PCA9548A_I2C_ADDR
#define PCA9548A_I2C_ADDR           CONFIG_PCA9548A_I2C_ADDR
#else
#define PCA9548A_I2C_ADDR           I2C_ADDR_PCA9548A_BASE
#endif
#endif

#ifndef PCA9548A_RESET_PIN
#ifdef CONFIG_PCA9548A_RESET_PIN
#define PCA9548A_RESET_PIN          ((gpio_num_t)(CONFIG_PCA9548A_RESET_PIN >= 0 ? CONFIG_PCA9548A_RESET_PIN : GPIO_NUM_NC))
#else
#define PCA9548A_RESET_PIN          GPIO_NUM_NC  // Not connected by default
#endif
#endif

#ifndef PCA9548A_I2C_TIMEOUT_MS
#ifdef CONFIG_PCA9548A_I2C_TIMEOUT_MS
#define PCA9548A_I2C_TIMEOUT_MS     CONFIG_PCA9548A_I2C_TIMEOUT_MS
#else
#define PCA9548A_I2C_TIMEOUT_MS     1000  // Default timeout
#endif
#endif

// HW-178模拟多路复用器相关定义
#ifndef HW178_S0_PIN
#ifdef CONFIG_HW178_S0_PIN
#define HW178_S0_PIN                ((gpio_num_t)CONFIG_HW178_S0_PIN)
#else
#define HW178_S0_PIN                GPIO_NUM_NC  // Not connected by default
#endif
#endif

#ifndef HW178_S1_PIN
#ifdef CONFIG_HW178_S1_PIN
#define HW178_S1_PIN                ((gpio_num_t)CONFIG_HW178_S1_PIN)
#else
#define HW178_S1_PIN                GPIO_NUM_NC  // Not connected by default
#endif
#endif

#ifndef HW178_S2_PIN
#ifdef CONFIG_HW178_S2_PIN
#define HW178_S2_PIN                ((gpio_num_t)CONFIG_HW178_S2_PIN)
#else
#define HW178_S2_PIN                GPIO_NUM_NC  // Not connected by default
#endif
#endif

#ifndef HW178_S3_PIN
#ifdef CONFIG_HW178_S3_PIN
#define HW178_S3_PIN                ((gpio_num_t)CONFIG_HW178_S3_PIN)
#else
#define HW178_S3_PIN                GPIO_NUM_NC  // Not connected by default
#endif
#endif

#ifndef HW178_SIG_PIN
#ifdef CONFIG_HW178_SIG_PIN
#define HW178_SIG_PIN               ((gpio_num_t)CONFIG_HW178_SIG_PIN)
#else
#define HW178_SIG_PIN               GPIO_NUM_NC  // Not connected by default
#endif
#endif

#ifndef HW178_EN_PIN
#ifdef CONFIG_HW178_EN_PIN
#define HW178_EN_PIN                (CONFIG_HW178_EN_PIN >= 0 ? CONFIG_HW178_EN_PIN : GPIO_NUM_NC)
#else
#define HW178_EN_PIN                GPIO_NUM_NC  // Not connected by default
#endif
#endif

#ifndef HW178_EN_ACTIVE_HIGH
#ifdef CONFIG_HW178_EN_ACTIVE_HIGH
#define HW178_EN_ACTIVE_HIGH        CONFIG_HW178_EN_ACTIVE_HIGH
#else
#define HW178_EN_ACTIVE_HIGH        true  // Default active high
#endif
#endif

#ifndef HW178_ADC_CHANNEL
#ifdef CONFIG_HW178_ADC_CHANNEL
#define HW178_ADC_CHANNEL           CONFIG_HW178_ADC_CHANNEL
#else
#define HW178_ADC_CHANNEL           -1  // Invalid ADC channel by default
#endif
#endif

// 超声波传感器相关定义
#ifndef US_FRONT_TRIG_PIN
#ifdef CONFIG_US_FRONT_TRIG_PIN
#define US_FRONT_TRIG_PIN           CONFIG_US_FRONT_TRIG_PIN
#else
#define US_FRONT_TRIG_PIN           -1
#endif
#endif

#ifndef US_FRONT_ECHO_PIN
#ifdef CONFIG_US_FRONT_ECHO_PIN
#define US_FRONT_ECHO_PIN           CONFIG_US_FRONT_ECHO_PIN
#else
#define US_FRONT_ECHO_PIN           -1
#endif
#endif

#ifndef US_REAR_TRIG_PIN
#ifdef CONFIG_US_REAR_TRIG_PIN
#define US_REAR_TRIG_PIN            CONFIG_US_REAR_TRIG_PIN
#else
#define US_REAR_TRIG_PIN            -1
#endif
#endif

#ifndef US_REAR_ECHO_PIN
#ifdef CONFIG_US_REAR_ECHO_PIN
#define US_REAR_ECHO_PIN            CONFIG_US_REAR_ECHO_PIN
#else
#define US_REAR_ECHO_PIN            -1
#endif
#endif

// 摄像头引脚相关定义
#ifndef CAMERA_PIN_PWDN
#ifdef CONFIG_CAMERA_PIN_PWDN
#define CAMERA_PIN_PWDN             CONFIG_CAMERA_PIN_PWDN
#else
#define CAMERA_PIN_PWDN             -1
#endif
#endif

#ifndef CAMERA_PIN_RESET
#ifdef CONFIG_CAMERA_PIN_RESET
#define CAMERA_PIN_RESET            CONFIG_CAMERA_PIN_RESET
#else
#define CAMERA_PIN_RESET            -1
#endif
#endif

#ifndef CAMERA_PIN_XCLK
#ifdef CONFIG_CAMERA_PIN_XCLK
#define CAMERA_PIN_XCLK             CONFIG_CAMERA_PIN_XCLK
#else
#define CAMERA_PIN_XCLK             -1
#endif
#endif

#ifndef CAMERA_PIN_SIOD
#ifdef CONFIG_CAMERA_PIN_SIOD
#define CAMERA_PIN_SIOD             CONFIG_CAMERA_PIN_SIOD
#else
#define CAMERA_PIN_SIOD             -1
#endif
#endif

#ifndef CAMERA_PIN_SIOC
#ifdef CONFIG_CAMERA_PIN_SIOC
#define CAMERA_PIN_SIOC             CONFIG_CAMERA_PIN_SIOC
#else
#define CAMERA_PIN_SIOC             -1
#endif
#endif

#ifndef CAMERA_PIN_D7
#ifdef CONFIG_CAMERA_PIN_D7
#define CAMERA_PIN_D7               CONFIG_CAMERA_PIN_D7
#else
#define CAMERA_PIN_D7               -1
#endif
#endif

#ifndef CAMERA_PIN_D6
#ifdef CONFIG_CAMERA_PIN_D6
#define CAMERA_PIN_D6               CONFIG_CAMERA_PIN_D6
#else
#define CAMERA_PIN_D6               -1
#endif
#endif

#ifndef CAMERA_PIN_D5
#ifdef CONFIG_CAMERA_PIN_D5
#define CAMERA_PIN_D5               CONFIG_CAMERA_PIN_D5
#else
#define CAMERA_PIN_D5               -1
#endif
#endif

#ifndef CAMERA_PIN_D4
#ifdef CONFIG_CAMERA_PIN_D4
#define CAMERA_PIN_D4               CONFIG_CAMERA_PIN_D4
#else
#define CAMERA_PIN_D4               -1
#endif
#endif

#ifndef CAMERA_PIN_D3
#ifdef CONFIG_CAMERA_PIN_D3
#define CAMERA_PIN_D3               CONFIG_CAMERA_PIN_D3
#else
#define CAMERA_PIN_D3               -1
#endif
#endif

#ifndef CAMERA_PIN_D2
#ifdef CONFIG_CAMERA_PIN_D2
#define CAMERA_PIN_D2               CONFIG_CAMERA_PIN_D2
#else
#define CAMERA_PIN_D2               -1
#endif
#endif

#ifndef CAMERA_PIN_D1
#ifdef CONFIG_CAMERA_PIN_D1
#define CAMERA_PIN_D1               CONFIG_CAMERA_PIN_D1
#else
#define CAMERA_PIN_D1               -1
#endif
#endif

#ifndef CAMERA_PIN_D0
#ifdef CONFIG_CAMERA_PIN_D0
#define CAMERA_PIN_D0               CONFIG_CAMERA_PIN_D0
#else
#define CAMERA_PIN_D0               -1
#endif
#endif

#ifndef CAMERA_PIN_VSYNC
#ifdef CONFIG_CAMERA_PIN_VSYNC
#define CAMERA_PIN_VSYNC            CONFIG_CAMERA_PIN_VSYNC
#else
#define CAMERA_PIN_VSYNC            -1
#endif
#endif

#ifndef CAMERA_PIN_HREF
#ifdef CONFIG_CAMERA_PIN_HREF
#define CAMERA_PIN_HREF             CONFIG_CAMERA_PIN_HREF
#else
#define CAMERA_PIN_HREF             -1
#endif
#endif

#ifndef CAMERA_PIN_PCLK
#ifdef CONFIG_CAMERA_PIN_PCLK
#define CAMERA_PIN_PCLK             CONFIG_CAMERA_PIN_PCLK
#else
#define CAMERA_PIN_PCLK             -1
#endif
#endif

// 显示屏引脚相关定义
#ifndef DISPLAY_MOSI_PIN
#ifdef CONFIG_DISPLAY_MOSI_PIN
#define DISPLAY_MOSI_PIN            CONFIG_DISPLAY_MOSI_PIN
#else
#define DISPLAY_MOSI_PIN            -1
#endif
#endif

#ifndef DISPLAY_CLK_PIN
#ifdef CONFIG_DISPLAY_CLK_PIN
#define DISPLAY_CLK_PIN             CONFIG_DISPLAY_CLK_PIN
#else
#define DISPLAY_CLK_PIN             -1
#endif
#endif

#ifndef DISPLAY_CS_PIN
#ifdef CONFIG_DISPLAY_CS_PIN
#define DISPLAY_CS_PIN              CONFIG_DISPLAY_CS_PIN
#else
#define DISPLAY_CS_PIN              -1
#endif
#endif

#ifndef DISPLAY_DC_PIN
#ifdef CONFIG_DISPLAY_DC_PIN
#define DISPLAY_DC_PIN              CONFIG_DISPLAY_DC_PIN
#else
#define DISPLAY_DC_PIN              -1
#endif
#endif

#ifndef DISPLAY_RST_PIN
#ifdef CONFIG_DISPLAY_RST_PIN
#define DISPLAY_RST_PIN             CONFIG_DISPLAY_RST_PIN
#else
#define DISPLAY_RST_PIN             -1
#endif
#endif

#ifndef DISPLAY_BACKLIGHT_PIN
#ifdef CONFIG_DISPLAY_BACKLIGHT_PIN
#define DISPLAY_BACKLIGHT_PIN       CONFIG_DISPLAY_BACKLIGHT_PIN
#else
#define DISPLAY_BACKLIGHT_PIN       -1
#endif
#endif

// 音频相关定义
#ifndef AUDIO_I2S_MIC_GPIO_WS
#ifdef CONFIG_AUDIO_I2S_MIC_GPIO_WS
#define AUDIO_I2S_MIC_GPIO_WS       CONFIG_AUDIO_I2S_MIC_GPIO_WS
#else
#define AUDIO_I2S_MIC_GPIO_WS       -1
#endif
#endif

#ifndef AUDIO_I2S_MIC_GPIO_SCK
#ifdef CONFIG_AUDIO_I2S_MIC_GPIO_SCK
#define AUDIO_I2S_MIC_GPIO_SCK      CONFIG_AUDIO_I2S_MIC_GPIO_SCK
#else
#define AUDIO_I2S_MIC_GPIO_SCK      -1
#endif
#endif

#ifndef AUDIO_I2S_MIC_GPIO_DIN
#ifdef CONFIG_AUDIO_I2S_MIC_GPIO_DIN
#define AUDIO_I2S_MIC_GPIO_DIN      CONFIG_AUDIO_I2S_MIC_GPIO_DIN
#else
#define AUDIO_I2S_MIC_GPIO_DIN      -1
#endif
#endif

#ifndef AUDIO_I2S_SPK_GPIO_DOUT
#ifdef CONFIG_AUDIO_I2S_SPK_GPIO_DOUT
#define AUDIO_I2S_SPK_GPIO_DOUT     CONFIG_AUDIO_I2S_SPK_GPIO_DOUT
#else
#define AUDIO_I2S_SPK_GPIO_DOUT     -1
#endif
#endif

#ifndef AUDIO_I2S_SPK_GPIO_BCLK
#ifdef CONFIG_AUDIO_I2S_SPK_GPIO_BCLK
#define AUDIO_I2S_SPK_GPIO_BCLK     CONFIG_AUDIO_I2S_SPK_GPIO_BCLK
#else
#define AUDIO_I2S_SPK_GPIO_BCLK     -1
#endif
#endif

#ifndef AUDIO_I2S_SPK_GPIO_LRCK
#ifdef CONFIG_AUDIO_I2S_SPK_GPIO_LRCK
#define AUDIO_I2S_SPK_GPIO_LRCK     CONFIG_AUDIO_I2S_SPK_GPIO_LRCK
#else
#define AUDIO_I2S_SPK_GPIO_LRCK     -1
#endif
#endif

// 按钮与LED相关定义
#ifndef BUILTIN_LED_GPIO
#ifdef CONFIG_BUILTIN_LED_GPIO
#define BUILTIN_LED_GPIO            CONFIG_BUILTIN_LED_GPIO
#else
#define BUILTIN_LED_GPIO            -1
#endif
#endif

#ifndef BOOT_BUTTON_GPIO
#ifdef CONFIG_BOOT_BUTTON_GPIO
#define BOOT_BUTTON_GPIO            CONFIG_BOOT_BUTTON_GPIO
#else
#define BOOT_BUTTON_GPIO            -1
#endif
#endif

#ifndef TOUCH_BUTTON_GPIO
#ifdef CONFIG_TOUCH_BUTTON_GPIO
#define TOUCH_BUTTON_GPIO           CONFIG_TOUCH_BUTTON_GPIO
#else
#define TOUCH_BUTTON_GPIO           -1
#endif
#endif

#ifndef VOLUME_UP_BUTTON_GPIO
#ifdef CONFIG_VOLUME_UP_BUTTON_GPIO
#define VOLUME_UP_BUTTON_GPIO       CONFIG_VOLUME_UP_BUTTON_GPIO
#else
#define VOLUME_UP_BUTTON_GPIO       -1
#endif
#endif

#ifndef VOLUME_DOWN_BUTTON_GPIO
#ifdef CONFIG_VOLUME_DOWN_BUTTON_GPIO
#define VOLUME_DOWN_BUTTON_GPIO     CONFIG_VOLUME_DOWN_BUTTON_GPIO
#else
#define VOLUME_DOWN_BUTTON_GPIO     -1
#endif
#endif

#ifndef CAMERA_FLASH_PIN
#ifdef CONFIG_CAMERA_FLASH_PIN
#define CAMERA_FLASH_PIN            CONFIG_CAMERA_FLASH_PIN
#else
#define CAMERA_FLASH_PIN            -1
#endif
#endif
/**
 * Network events for unified callback
 */
enum class NetworkEvent {
    Scanning,              // Network is scanning (WiFi scanning, etc.)
    Connecting,            // Network is connecting (data: SSID/network name)
    Connected,             // Network connected successfully (data: SSID/network name)
    Disconnected,          // Network disconnected
    WifiConfigModeEnter,   // Entered WiFi configuration mode
    WifiConfigModeExit,    // Exited WiFi configuration mode
    // Cellular modem specific events
    ModemDetecting,        // Detecting modem (baud rate, module type)
    ModemErrorNoSim,       // No SIM card detected
    ModemErrorRegDenied,   // Network registration denied
    ModemErrorInitFailed,  // Modem initialization failed
    ModemErrorTimeout      // Operation timeout
};

// Power save level enumeration
enum class PowerSaveLevel {
    LOW_POWER,    // Maximum power saving (lowest power consumption)
    BALANCED,     // Medium power saving (balanced)
    PERFORMANCE,  // No power saving (maximum power consumption / full performance)
};

// Network event callback type (event, data)
// data contains additional info like SSID for Connecting/Connected events
using NetworkEventCallback = std::function<void(NetworkEvent event, const std::string& data)>;

void* create_board();
class AudioCodec;
class Display;
class Board {
private:
    Board(const Board&) = delete; // 禁用拷贝构造函数
    Board& operator=(const Board&) = delete; // 禁用赋值操作

protected:
    Board();
    std::string GenerateUuid();

    // 软件生成的设备唯一标识
    std::string uuid_;
    
    // 板级配置
    static board_config_t board_config_;
    static bool config_initialized_;

public:
    static Board& GetInstance() {
        static Board* instance = static_cast<Board*>(create_board());
        return *instance;
    }

    virtual ~Board() = default;
    virtual std::string GetBoardType() = 0;
    virtual std::string GetUuid() { return uuid_; }
    virtual Backlight* GetBacklight() { return nullptr; }
    virtual Led* GetLed();
    virtual AudioCodec* GetAudioCodec() = 0;
    virtual bool GetTemperature(float& esp32temp);
    virtual Display* GetDisplay();
    virtual i2c_master_bus_handle_t GetDisplayI2CBusHandle() { return nullptr; }
    virtual Camera* GetCamera();
    virtual NetworkInterface* GetNetwork() = 0;
    virtual void StartNetwork() = 0;
    virtual void SetNetworkEventCallback(NetworkEventCallback callback) { (void)callback; }
    virtual const char* GetNetworkStateIcon() = 0;
    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging);
    virtual std::string GetSystemInfoJson();
    virtual void SetPowerSaveLevel(PowerSaveLevel level) = 0;
    virtual std::string GetBoardJson() = 0;
    virtual std::string GetDeviceStatusJson() = 0;
    virtual Assets* GetAssets();
    
    // 板级配置管理方法
    static board_config_t* GetBoardConfig();
    static void InitBoardConfig();
    
    // 硬件工具方法
    static int GetDefaultI2CPort();
    static bool IsI2CDeviceConnected(int port, uint8_t addr);
};

#define DECLARE_BOARD(BOARD_CLASS_NAME) \
void* create_board() { \
    return new BOARD_CLASS_NAME(); \
}

#ifdef __cplusplus
extern "C" {
#endif

// 全局函数用于获取I2C总线句柄
i2c_master_bus_handle_t board_get_i2c_bus_handle(void);

// 全局函数用于获取板级配置
board_config_t* board_get_config(void);

#ifdef __cplusplus
}
#endif

#endif // BOARD_H
