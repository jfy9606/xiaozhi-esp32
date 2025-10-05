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
#define TOUCH_BUTTON_GPIO       GPIO_NUM_NC
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_42
#define DISPLAY_MOSI_PIN      GPIO_NUM_47
#define DISPLAY_CLK_PIN       GPIO_NUM_21
#define DISPLAY_DC_PIN        GPIO_NUM_40
#define DISPLAY_RST_PIN       GPIO_NUM_45
#define DISPLAY_CS_PIN        GPIO_NUM_41

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
#define SERVO_COUNT CONFIG_SERVO_COUNT  // 舵机数量

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



#ifdef CONFIG_LCD_ST7789_240X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X320_NO_IPS
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_170X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   170
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  35
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_172X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   172
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  34
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X280
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  280
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  20
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X240
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X240_7PIN
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 3
#endif

#ifdef CONFIG_LCD_ST7789_240X135
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  135
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY true
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  40
#define DISPLAY_OFFSET_Y  53
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7735_128X160
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  160
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7735_128X128
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  128
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR  false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  32
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7796_320X480
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  480
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7796_320X480_NO_IPS
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  480
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ILI9341_240X320
#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ILI9341_240X320_NO_IPS
#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_GC9A01_240X240
#define LCD_TYPE_GC9A01_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_CUSTOM
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

// ========== 扩展器配置 ==========
// 主I2C总线配置 (用于所有I2C扩展器)
// 使用Kconfig配置的引脚，支持用户自定义
#ifdef CONFIG_PCA9548A_SDA_PIN
#define I2C_EXT_SDA_PIN           ((gpio_num_t)CONFIG_PCA9548A_SDA_PIN)
#else
#define I2C_EXT_SDA_PIN           GPIO_NUM_8     // 默认I2C数据线 (CAM_Y4 - 可复用)
#endif

#ifdef CONFIG_PCA9548A_SCL_PIN
#define I2C_EXT_SCL_PIN           ((gpio_num_t)CONFIG_PCA9548A_SCL_PIN)
#else
#define I2C_EXT_SCL_PIN           GPIO_NUM_9     // 默认I2C时钟线 (CAM_Y3 - 可复用)
#endif

#ifdef CONFIG_PCA9548A_I2C_FREQ_HZ
#define I2C_EXT_FREQ_HZ           CONFIG_PCA9548A_I2C_FREQ_HZ
#else
#define I2C_EXT_FREQ_HZ           400000         // 默认I2C频率 400KHz
#endif

#ifdef CONFIG_DISPLAY_I2C_PORT
#define I2C_EXT_PORT              ((i2c_port_t)CONFIG_DISPLAY_I2C_PORT)
#else
#define I2C_EXT_PORT              I2C_NUM_0      // 默认I2C端口号
#endif

#ifdef CONFIG_PCA9548A_I2C_TIMEOUT_MS
#define I2C_EXT_TIMEOUT_MS        CONFIG_PCA9548A_I2C_TIMEOUT_MS
#else
#define I2C_EXT_TIMEOUT_MS        1000           // 默认I2C超时时间
#endif

// 1. PCA9548A I2C多路复用器配置
#ifdef CONFIG_PCA9548A_I2C_ADDR
#define PCA9548A_I2C_ADDR         CONFIG_PCA9548A_I2C_ADDR
#else
#define PCA9548A_I2C_ADDR         0x70           // PCA9548A默认地址 (7位格式)
#endif

// PCA9548A_RESET_PIN 已在 common/board.h 中定义

// 2. LU9685舵机扩展器配置 (通过PCA9548A通道1)
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

// 3. PCF8575 I2C GPIO扩展器配置 (通过PCA9548A通道2)
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
#define HW178_S0_PIN              GPIO_NUM_10    // 默认选择引脚S0 (CAM_Y5 - 可复用)
#endif

#ifdef CONFIG_HW178_S1_PIN
#if CONFIG_HW178_S1_PIN >= 0
#define HW178_S1_PIN              ((gpio_num_t)CONFIG_HW178_S1_PIN)
#else
#define HW178_S1_PIN              GPIO_NUM_NC
#endif
#else
#define HW178_S1_PIN              GPIO_NUM_11    // 默认选择引脚S1 (CAM_Y2 - 可复用)
#endif

#ifdef CONFIG_HW178_S2_PIN
#if CONFIG_HW178_S2_PIN >= 0
#define HW178_S2_PIN              ((gpio_num_t)CONFIG_HW178_S2_PIN)
#else
#define HW178_S2_PIN              GPIO_NUM_NC
#endif
#else
#define HW178_S2_PIN              GPIO_NUM_12    // 默认选择引脚S2 (CAM_Y6 - 可复用)
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

// 扩展器功能启用标志 - 使用Kconfig配置
#ifdef CONFIG_ENABLE_PCA9548A
#define ENABLE_PCA9548A_MUX       1              // 启用PCA9548A I2C多路复用器
#else
#define ENABLE_PCA9548A_MUX       0              // 禁用PCA9548A I2C多路复用器
#endif

#ifdef CONFIG_ENABLE_LU9685
#define ENABLE_LU9685_SERVO       1              // 启用LU9685舵机扩展器
#else
#define ENABLE_LU9685_SERVO       0              // 禁用LU9685舵机扩展器
#endif

#ifdef CONFIG_ENABLE_PCF8575
#define ENABLE_PCF8575_GPIO       1              // 启用PCF8575 GPIO扩展器
#else
#define ENABLE_PCF8575_GPIO       0              // 禁用PCF8575 GPIO扩展器
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
#endif // _BOARD_CONFIG_H_

