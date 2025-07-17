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
#include "esp32_camera.h"
#include <network_interface.h>

#include "led/led.h"
#include "backlight.h"
#include "camera.h"
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

    // 摄像头配置与信息
    bool camera_supported;           // 是否支持摄像头
    bool has_camera;                 // 是否已初始化摄像头
    camera_config_t camera_config;   // 摄像头配置
    camera_info_t* camera_info;      // 摄像头信息
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
    virtual const char* GetNetworkStateIcon() = 0;
    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging);
    virtual std::string GetJson();
    virtual void SetPowerSaveMode(bool enabled) = 0;
    virtual std::string GetBoardJson() = 0;
    virtual std::string GetDeviceStatusJson() = 0;
    
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
