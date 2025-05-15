#ifndef _BOARD_CONFIG_COMMON_H_
#define _BOARD_CONFIG_COMMON_H_

#include <driver/gpio.h>
#include <driver/i2c.h>
#include "sdkconfig.h"

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
} board_config_t;

// 获取板级配置
board_config_t* board_get_config(void);

// Default I2C Multiplexer pins - using Kconfig values or fallback to defaults
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

// Default PCA9548A I2C Multiplexer Configuration
// Using Kconfig values if defined
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
#define PCA9548A_I2C_ADDR           0x70  // Default address
#endif
#endif

#ifndef PCA9548A_RESET_PIN
#ifdef CONFIG_PCA9548A_RESET_PIN
#define PCA9548A_RESET_PIN          (CONFIG_PCA9548A_RESET_PIN >= 0 ? CONFIG_PCA9548A_RESET_PIN : GPIO_NUM_NC)
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

// Default HW-178 Analog Multiplexer Configuration
// Using Kconfig values if defined
#ifndef HW178_S0_PIN
#ifdef CONFIG_HW178_S0_PIN
#define HW178_S0_PIN                CONFIG_HW178_S0_PIN
#else
#define HW178_S0_PIN                GPIO_NUM_NC  // Not connected by default
#endif
#endif

#ifndef HW178_S1_PIN
#ifdef CONFIG_HW178_S1_PIN
#define HW178_S1_PIN                CONFIG_HW178_S1_PIN
#else
#define HW178_S1_PIN                GPIO_NUM_NC  // Not connected by default
#endif
#endif

#ifndef HW178_S2_PIN
#ifdef CONFIG_HW178_S2_PIN
#define HW178_S2_PIN                CONFIG_HW178_S2_PIN
#else
#define HW178_S2_PIN                GPIO_NUM_NC  // Not connected by default
#endif
#endif

#ifndef HW178_S3_PIN
#ifdef CONFIG_HW178_S3_PIN
#define HW178_S3_PIN                CONFIG_HW178_S3_PIN
#else
#define HW178_S3_PIN                GPIO_NUM_NC  // Not connected by default
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

#endif // _BOARD_CONFIG_COMMON_H_