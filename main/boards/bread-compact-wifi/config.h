#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include "sdkconfig.h"

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// 如果使用 Duplex I2S 模式，请注释下面一行
#if defined(CONFIG_AUDIO_I2S_METHOD_SIMPLEX)
#define AUDIO_I2S_METHOD_SIMPLEX
#elif !defined(CONFIG_AUDIO_I2S_METHOD_SIMPLEX) && !defined(CONFIG_AUDIO_I2S_METHOD_DUPLEX)
// Default to simplex if not configured via Kconfig
#define AUDIO_I2S_METHOD_SIMPLEX
#endif

#ifdef AUDIO_I2S_METHOD_SIMPLEX

#ifndef AUDIO_I2S_MIC_GPIO_WS
#ifdef CONFIG_AUDIO_I2S_MIC_GPIO_WS
#define AUDIO_I2S_MIC_GPIO_WS   CONFIG_AUDIO_I2S_MIC_GPIO_WS
#else
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#endif
#endif

#ifndef AUDIO_I2S_MIC_GPIO_SCK
#ifdef CONFIG_AUDIO_I2S_MIC_GPIO_SCK
#define AUDIO_I2S_MIC_GPIO_SCK  CONFIG_AUDIO_I2S_MIC_GPIO_SCK
#else
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#endif
#endif

#ifndef AUDIO_I2S_MIC_GPIO_DIN
#ifdef CONFIG_AUDIO_I2S_MIC_GPIO_DIN
#define AUDIO_I2S_MIC_GPIO_DIN  CONFIG_AUDIO_I2S_MIC_GPIO_DIN
#else
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6
#endif
#endif

#ifndef AUDIO_I2S_SPK_GPIO_DOUT
#ifdef CONFIG_AUDIO_I2S_SPK_GPIO_DOUT
#define AUDIO_I2S_SPK_GPIO_DOUT CONFIG_AUDIO_I2S_SPK_GPIO_DOUT
#else
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#endif
#endif

#ifndef AUDIO_I2S_SPK_GPIO_BCLK
#ifdef CONFIG_AUDIO_I2S_SPK_GPIO_BCLK
#define AUDIO_I2S_SPK_GPIO_BCLK CONFIG_AUDIO_I2S_SPK_GPIO_BCLK
#else
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#endif
#endif

#ifndef AUDIO_I2S_SPK_GPIO_LRCK
#ifdef CONFIG_AUDIO_I2S_SPK_GPIO_LRCK
#define AUDIO_I2S_SPK_GPIO_LRCK CONFIG_AUDIO_I2S_SPK_GPIO_LRCK
#else
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16
#endif
#endif

#else

#ifndef AUDIO_I2S_GPIO_WS
#define AUDIO_I2S_GPIO_WS GPIO_NUM_4
#endif
#ifndef AUDIO_I2S_GPIO_BCLK
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_5
#endif
#ifndef AUDIO_I2S_GPIO_DIN
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6
#endif
#ifndef AUDIO_I2S_GPIO_DOUT
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7
#endif

#endif


#ifndef BUILTIN_LED_GPIO
#ifdef CONFIG_BUILTIN_LED_GPIO
#define BUILTIN_LED_GPIO        ((gpio_num_t)CONFIG_BUILTIN_LED_GPIO)
#else
#define BUILTIN_LED_GPIO        GPIO_NUM_48
#endif
#endif

#ifndef BOOT_BUTTON_GPIO
#ifdef CONFIG_BOOT_BUTTON_GPIO
#define BOOT_BUTTON_GPIO        ((gpio_num_t)CONFIG_BOOT_BUTTON_GPIO)
#else
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#endif
#endif

#ifndef TOUCH_BUTTON_GPIO
#ifdef CONFIG_TOUCH_BUTTON_GPIO
#define TOUCH_BUTTON_GPIO       ((gpio_num_t)CONFIG_TOUCH_BUTTON_GPIO)
#else
#define TOUCH_BUTTON_GPIO       GPIO_NUM_47
#endif
#endif

#ifndef VOLUME_UP_BUTTON_GPIO
#ifdef CONFIG_VOLUME_UP_BUTTON_GPIO
#define VOLUME_UP_BUTTON_GPIO   ((gpio_num_t)CONFIG_VOLUME_UP_BUTTON_GPIO)
#else
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40
#endif
#endif

#ifndef VOLUME_DOWN_BUTTON_GPIO
#ifdef CONFIG_VOLUME_DOWN_BUTTON_GPIO
#define VOLUME_DOWN_BUTTON_GPIO ((gpio_num_t)CONFIG_VOLUME_DOWN_BUTTON_GPIO)
#else
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39
#endif
#endif

#ifndef DISPLAY_SDA_PIN
#ifdef CONFIG_DISPLAY_SDA_PIN
#define DISPLAY_SDA_PIN ((gpio_num_t)CONFIG_DISPLAY_SDA_PIN)
#else
#define DISPLAY_SDA_PIN GPIO_NUM_41
#endif
#endif

#ifndef DISPLAY_SCL_PIN
#ifdef CONFIG_DISPLAY_SCL_PIN
#define DISPLAY_SCL_PIN ((gpio_num_t)CONFIG_DISPLAY_SCL_PIN)
#else
#define DISPLAY_SCL_PIN GPIO_NUM_42
#endif
#endif
#define DISPLAY_WIDTH   128

#if CONFIG_OLED_SSD1306_128X32
#define DISPLAY_HEIGHT  32
#elif CONFIG_OLED_SSD1306_128X64
#define DISPLAY_HEIGHT  64
#elif CONFIG_OLED_SH1106_128X64
#define DISPLAY_HEIGHT  64
#define SH1106
#else
#error "未选择 OLED 屏幕类型"
#endif

#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true

// ADC 资源冲突处理配置
// 此开发板通过HW-178多路复用器连接电压传感器监控电池
// 设置尝试顺序：优先ADC2，如果失败再尝试ADC1
#define PREFER_ADC_UNIT ADC_UNIT_2
#define FALLBACK_ADC_UNIT ADC_UNIT_1
// 减少ADC超时和重试次数，避免长时间等待
#define ADC_TIMEOUT_MS 20
#define ADC_MAX_RETRIES 2

// 电机控制引脚定义 - 使用Kconfig中的配置
#ifdef CONFIG_ENABLE_MOTOR_CONTROLLER
#define MOTOR_ENA_PIN CONFIG_MOTOR_ENA_PIN  // 电机A使能
#define MOTOR_ENB_PIN CONFIG_MOTOR_ENB_PIN  // 电机B使能
#define MOTOR_IN1_PIN CONFIG_MOTOR_IN1_PIN  // 电机A输入1
#define MOTOR_IN2_PIN CONFIG_MOTOR_IN2_PIN  // 电机A输入2
#define MOTOR_IN3_PIN CONFIG_MOTOR_IN3_PIN  // 电机B输入1
#define MOTOR_IN4_PIN CONFIG_MOTOR_IN4_PIN  // 电机B输入2
#endif

// 舵机引脚定义 - 使用Kconfig中的配置
#ifdef CONFIG_ENABLE_SERVO_CONTROLLER
#ifndef SERVO_COUNT
#define SERVO_COUNT CONFIG_SERVO_COUNT  // 舵机数量
#endif

// 设置舵机引脚数组
#define SERVO_PINS_ARRAY { \
    CONFIG_SERVO_PIN_1, \
    CONFIG_SERVO_COUNT >= 2 ? CONFIG_SERVO_PIN_2 : -1, \
    CONFIG_SERVO_COUNT >= 3 ? CONFIG_SERVO_PIN_3 : -1, \
    CONFIG_SERVO_COUNT >= 4 ? CONFIG_SERVO_PIN_4 : -1, \
    CONFIG_SERVO_COUNT >= 5 ? CONFIG_SERVO_PIN_5 : -1, \
    CONFIG_SERVO_COUNT >= 6 ? CONFIG_SERVO_PIN_6 : -1, \
    CONFIG_SERVO_COUNT >= 7 ? CONFIG_SERVO_PIN_7 : -1, \
    CONFIG_SERVO_COUNT >= 8 ? CONFIG_SERVO_PIN_8 : -1  \
}

// 向后兼容定义
#define SERVO_PAN_PIN  CONFIG_SERVO_PIN_1  // 水平舵机引脚
#define SERVO_TILT_PIN CONFIG_SERVO_PIN_2  // 垂直舵机引脚
#endif



// ========== 级联多路复用器配置 ==========
// 主I2C总线配置 (用于所有I2C级联多路复用器)
// 使用Kconfig配置的引脚，支持用户自定义
#ifdef CONFIG_PCA9548A_SDA_PIN
#define I2C_MUX_SDA_PIN           ((gpio_num_t)CONFIG_PCA9548A_SDA_PIN)
#else
#define I2C_MUX_SDA_PIN           GPIO_NUM_8     // 默认I2C数据线 (CAM_Y4 - 可复用)
#endif

#ifdef CONFIG_PCA9548A_SCL_PIN
#define I2C_MUX_SCL_PIN           ((gpio_num_t)CONFIG_PCA9548A_SCL_PIN)
#else
#define I2C_MUX_SCL_PIN           GPIO_NUM_9     // 默认I2C时钟线 (CAM_Y3 - 可复用)
#endif

#ifdef CONFIG_PCA9548A_I2C_FREQ_HZ
#define I2C_MUX_FREQ_HZ           CONFIG_PCA9548A_I2C_FREQ_HZ
#else
#define I2C_MUX_FREQ_HZ           400000         // 默认I2C频率 400KHz
#endif

#ifdef CONFIG_DISPLAY_I2C_PORT
#define I2C_MUX_PORT              ((i2c_port_t)CONFIG_DISPLAY_I2C_PORT)
#else
#define I2C_MUX_PORT              I2C_NUM_0      // 默认I2C端口号
#endif

#ifdef CONFIG_PCA9548A_I2C_TIMEOUT_MS
#define I2C_MUX_TIMEOUT_MS        CONFIG_PCA9548A_I2C_TIMEOUT_MS
#else
#define I2C_MUX_TIMEOUT_MS        1000           // 默认I2C超时时间
#endif

// 1. PCA9548A I2C多路复用器配置
#ifdef CONFIG_PCA9548A_I2C_ADDR
#define PCA9548A_I2C_ADDR         CONFIG_PCA9548A_I2C_ADDR
#else
#define PCA9548A_I2C_ADDR         0x70           // PCA9548A默认地址 (7位格式)
#endif

// PCA9548A_RESET_PIN 已在 common/board.h 中定义

// 2. LU9685舵机多路复用器配置 (通过PCA9548A通道1)
#ifdef CONFIG_LU9685_I2C_ADDR
#define LU9685_I2C_ADDR           CONFIG_LU9685_I2C_ADDR
#else
#define LU9685_I2C_ADDR           0x40           // LU9685默认地址 (7位格式)
#endif

#define LU9685_PWM_FREQ           50             // PWM频率 50Hz (舵机标准)

#ifdef CONFIG_LU9685_PCA9548A_CHANNEL
#define LU9685_PCA9548A_CHANNEL   CONFIG_LU9685_PCA9548A_CHANNEL
#else
#define LU9685_PCA9548A_CHANNEL   1              // 连接在PCA9548A的通道1
#endif

// 3. PCF8575 I2C GPIO多路复用器配置 (通过PCA9548A通道2)
#ifdef CONFIG_PCF8575_I2C_ADDR
#define PCF8575_I2C_ADDR          CONFIG_PCF8575_I2C_ADDR
#else
#define PCF8575_I2C_ADDR          0x20           // PCF8575默认地址 (7位格式)
#endif

#ifdef CONFIG_PCF8575_PCA9548A_CHANNEL
#define PCF8575_PCA9548A_CHANNEL  CONFIG_PCF8575_PCA9548A_CHANNEL
#else
#define PCF8575_PCA9548A_CHANNEL  2              // 连接在PCA9548A的通道2
#endif

#ifdef CONFIG_PCF8575_I2C_TIMEOUT_MS
#define PCF8575_I2C_TIMEOUT_MS    CONFIG_PCF8575_I2C_TIMEOUT_MS
#else
#define PCF8575_I2C_TIMEOUT_MS    1000           // 默认PCF8575 I2C超时时间
#endif

// 4. HW-178模拟多路复用器配置
// 使用Kconfig配置的引脚，支持用户自定义
#ifdef CONFIG_HW178_S0_PIN
#if CONFIG_HW178_S0_PIN >= 0
#define HW178_S0_PIN              ((gpio_num_t)CONFIG_HW178_S0_PIN)
#else
#define HW178_S0_PIN              GPIO_NUM_NC
#endif
#else
#define HW178_S0_PIN              ((gpio_num_t)104)    // 使用PCF8575 P4 (避免PSRAM冲突)
#endif

#ifdef CONFIG_HW178_S1_PIN
#if CONFIG_HW178_S1_PIN >= 0
#define HW178_S1_PIN              ((gpio_num_t)CONFIG_HW178_S1_PIN)
#else
#define HW178_S1_PIN              GPIO_NUM_NC
#endif
#else
#define HW178_S1_PIN              ((gpio_num_t)105)    // 使用PCF8575 P5 (避免PSRAM冲突)
#endif

#ifdef CONFIG_HW178_S2_PIN
#if CONFIG_HW178_S2_PIN >= 0
#define HW178_S2_PIN              ((gpio_num_t)CONFIG_HW178_S2_PIN)
#else
#define HW178_S2_PIN              GPIO_NUM_NC
#endif
#else
#define HW178_S2_PIN              ((gpio_num_t)106)    // 使用PCF8575 P6 (避免PSRAM冲突)
#endif

#ifdef CONFIG_HW178_S3_PIN
#if CONFIG_HW178_S3_PIN >= 0
#define HW178_S3_PIN              ((gpio_num_t)CONFIG_HW178_S3_PIN)
#else
#define HW178_S3_PIN              GPIO_NUM_NC
#endif
#else
#define HW178_S3_PIN              GPIO_NUM_13    // 默认选择引脚S3 (CAM_PCLK - 可复用)
#endif

// 重新定义HW178_SIG_PIN，覆盖common/board.h中的默认值
#ifdef CONFIG_HW178_SIG_PIN
#undef HW178_SIG_PIN
#define HW178_SIG_PIN             ((gpio_num_t)CONFIG_HW178_SIG_PIN)
#else
#undef HW178_SIG_PIN
#define HW178_SIG_PIN             GPIO_NUM_3     // 默认信号输出引脚 (ADC1_CH2)
#endif

// 重新定义HW178_EN_PIN，覆盖common/board.h中的默认值
#ifdef CONFIG_HW178_EN_PIN
#if CONFIG_HW178_EN_PIN >= 0
#undef HW178_EN_PIN
#define HW178_EN_PIN              ((gpio_num_t)CONFIG_HW178_EN_PIN)
#else
#undef HW178_EN_PIN
#define HW178_EN_PIN              GPIO_NUM_NC
#endif
#else
#undef HW178_EN_PIN
#define HW178_EN_PIN              GPIO_NUM_NC    // 默认使能引脚 (不使用)
#endif

// 重新定义HW178_ADC_CHANNEL，覆盖common/board.h中的默认值
#ifdef CONFIG_HW178_ADC_CHANNEL
#undef HW178_ADC_CHANNEL
#define HW178_ADC_CHANNEL         ((adc_channel_t)CONFIG_HW178_ADC_CHANNEL)
#else
#undef HW178_ADC_CHANNEL
#define HW178_ADC_CHANNEL         ADC_CHANNEL_2  // 默认ADC通道 (GPIO3对应ADC1_CH2)
#endif

// 多路复用器功能启用标志 - 使用Kconfig配置
#ifdef CONFIG_ENABLE_PCA9548A
#define ENABLE_PCA9548A_MUX       1              // 启用PCA9548A I2C多路复用器
#else
#define ENABLE_PCA9548A_MUX       0              // 禁用PCA9548A I2C多路复用器
#endif

#ifdef CONFIG_ENABLE_LU9685
#define ENABLE_LU9685_SERVO       1              // 启用LU9685舵机多路复用器
#else
#define ENABLE_LU9685_SERVO       0              // 禁用LU9685舵机多路复用器
#endif

#ifdef CONFIG_ENABLE_PCF8575
#define ENABLE_PCF8575_GPIO       1              // 启用PCF8575 GPIO多路复用器
#else
#define ENABLE_PCF8575_GPIO       0              // 禁用PCF8575 GPIO多路复用器
#endif

#ifdef CONFIG_ENABLE_HW178
#define ENABLE_HW178_ANALOG       1              // 启用HW-178模拟多路复用器
#else
#define ENABLE_HW178_ANALOG       0              // 禁用HW-178模拟多路复用器
#endif

// 引入通用板级配置头文件
#include "../common/board.h"

// A MCP Test: Control a lamp
#define LAMP_GPIO GPIO_NUM_18

#endif // _BOARD_H_
