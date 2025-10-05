#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include "sdkconfig.h"

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// 如果使用 Duplex I2S 模式，请注释下面一行
#define AUDIO_I2S_METHOD_SIMPLEX

#ifdef AUDIO_I2S_METHOD_SIMPLEX

#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

#else

#define AUDIO_I2S_GPIO_WS GPIO_NUM_4
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7

#endif


#define BUILTIN_LED_GPIO        GPIO_NUM_48
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define TOUCH_BUTTON_GPIO       GPIO_NUM_47
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39

#define DISPLAY_SDA_PIN GPIO_NUM_41
#define DISPLAY_SCL_PIN GPIO_NUM_42
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



// ========== 扩展器配置 ==========
// 主I2C总线配置 (用于所有I2C扩展器)
// 避开音频引脚(4-7,15-16)、显示引脚(41-42)、按键引脚(0,39-40,47-48)
#define I2C_EXT_SDA_PIN           GPIO_NUM_8     // I2C数据线 (CAM_Y4 - 可复用)
#define I2C_EXT_SCL_PIN           GPIO_NUM_9     // I2C时钟线 (CAM_Y3 - 可复用)
#define I2C_EXT_FREQ_HZ           400000         // I2C频率 400KHz
#define I2C_EXT_PORT              I2C_NUM_0      // I2C端口号
#define I2C_EXT_TIMEOUT_MS        1000           // I2C超时时间

// 1. PCA9548A I2C多路复用器配置
#ifndef PCA9548A_I2C_ADDR
#define PCA9548A_I2C_ADDR         0x70           // PCA9548A默认地址 (7位格式)
#endif
#ifndef PCA9548A_RESET_PIN
#define PCA9548A_RESET_PIN        GPIO_NUM_NC    // 复位引脚 (不使用)
#endif

// 2. LU9685舵机扩展器配置 (通过PCA9548A通道1)
#define LU9685_I2C_ADDR           0x40           // LU9685默认地址 (7位格式)
#define LU9685_PWM_FREQ           50             // PWM频率 50Hz (舵机标准)
#define LU9685_PCA9548A_CHANNEL   1              // 连接在PCA9548A的通道1

// 3. PCF8575 I2C GPIO扩展器配置 (通过PCA9548A通道2)
#define PCF8575_I2C_ADDR          0x20           // PCF8575默认地址 (7位格式)
#define PCF8575_PCA9548A_CHANNEL  2              // 连接在PCA9548A的通道2

// 4. HW-178模拟多路复用器配置
// 使用可用的GPIO，避开已占用的引脚
#ifndef HW178_S0_PIN
#define HW178_S0_PIN              GPIO_NUM_10    // 选择引脚S0 (CAM_Y5 - 可复用)
#endif
#ifndef HW178_S1_PIN
#define HW178_S1_PIN              GPIO_NUM_11    // 选择引脚S1 (CAM_Y2 - 可复用)
#endif
#ifndef HW178_S2_PIN
#define HW178_S2_PIN              GPIO_NUM_12    // 选择引脚S2 (CAM_Y6 - 可复用)
#endif
#ifndef HW178_S3_PIN
#define HW178_S3_PIN              GPIO_NUM_13    // 选择引脚S3 (CAM_PCLK - 可复用)
#endif
#ifndef HW178_SIG_PIN
#define HW178_SIG_PIN             GPIO_NUM_14    // 信号输出引脚 (ADC2_CH3 - 连接到ADC)
#endif
#ifndef HW178_EN_PIN
#define HW178_EN_PIN              GPIO_NUM_NC    // 使能引脚 (不使用)
#endif
#ifdef HW178_ADC_CHANNEL
#undef HW178_ADC_CHANNEL
#endif
#define HW178_ADC_CHANNEL         ADC_CHANNEL_3  // ADC通道 (GPIO14对应ADC2_CH3)

// 扩展器功能启用标志
#define ENABLE_PCA9548A_MUX       1              // 启用PCA9548A I2C多路复用器
#define ENABLE_LU9685_SERVO       1              // 启用LU9685舵机扩展器
#define ENABLE_PCF8575_GPIO       1              // 启用PCF8575 GPIO扩展器
#define ENABLE_HW178_ANALOG       1              // 启用HW-178模拟多路复用器

// 引入通用板级配置头文件
#include "../common/board.h"

// A MCP Test: Control a lamp
#define LAMP_GPIO GPIO_NUM_18

#endif // _BOARD_H_
