#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

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
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_42
#endif
#endif

#ifndef AUDIO_I2S_MIC_GPIO_SCK
#ifdef CONFIG_AUDIO_I2S_MIC_GPIO_SCK
#define AUDIO_I2S_MIC_GPIO_SCK  CONFIG_AUDIO_I2S_MIC_GPIO_SCK
#else
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_41
#endif
#endif

#ifndef AUDIO_I2S_MIC_GPIO_DIN
#ifdef CONFIG_AUDIO_I2S_MIC_GPIO_DIN
#define AUDIO_I2S_MIC_GPIO_DIN  CONFIG_AUDIO_I2S_MIC_GPIO_DIN
#else
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_2
#endif
#endif

#ifndef AUDIO_I2S_SPK_GPIO_DOUT
#ifdef CONFIG_AUDIO_I2S_SPK_GPIO_DOUT
#define AUDIO_I2S_SPK_GPIO_DOUT CONFIG_AUDIO_I2S_SPK_GPIO_DOUT
#else
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_43
#endif
#endif

#ifndef AUDIO_I2S_SPK_GPIO_BCLK
#ifdef CONFIG_AUDIO_I2S_SPK_GPIO_BCLK
#define AUDIO_I2S_SPK_GPIO_BCLK CONFIG_AUDIO_I2S_SPK_GPIO_BCLK
#else
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_40
#endif
#endif

#ifndef AUDIO_I2S_SPK_GPIO_LRCK
#ifdef CONFIG_AUDIO_I2S_SPK_GPIO_LRCK
#define AUDIO_I2S_SPK_GPIO_LRCK CONFIG_AUDIO_I2S_SPK_GPIO_LRCK
#else
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_39
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
#define BUILTIN_LED_GPIO        ((gpio_num_t)101)    // 使用PCF8575 P1
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
#define TOUCH_BUTTON_GPIO       GPIO_NUM_NC
#endif
#endif

#ifndef VOLUME_UP_BUTTON_GPIO
#ifdef CONFIG_VOLUME_UP_BUTTON_GPIO
#define VOLUME_UP_BUTTON_GPIO   ((gpio_num_t)CONFIG_VOLUME_UP_BUTTON_GPIO)
#else
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#endif
#endif

#ifndef VOLUME_DOWN_BUTTON_GPIO
#ifdef CONFIG_VOLUME_DOWN_BUTTON_GPIO
#define VOLUME_DOWN_BUTTON_GPIO ((gpio_num_t)CONFIG_VOLUME_DOWN_BUTTON_GPIO)
#else
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC
#endif
#endif

//Enhanced Camera Config - Multi-model support
#ifndef CAMERA_PIN_D0
#ifdef CONFIG_CAMERA_PIN_D0
#define CAMERA_PIN_D0 ((gpio_num_t)CONFIG_CAMERA_PIN_D0)
#else
#define CAMERA_PIN_D0 GPIO_NUM_11
#endif
#endif

#ifndef CAMERA_PIN_D1
#ifdef CONFIG_CAMERA_PIN_D1
#define CAMERA_PIN_D1 ((gpio_num_t)CONFIG_CAMERA_PIN_D1)
#else
#define CAMERA_PIN_D1 GPIO_NUM_9
#endif
#endif

#ifndef CAMERA_PIN_D2
#ifdef CONFIG_CAMERA_PIN_D2
#define CAMERA_PIN_D2 ((gpio_num_t)CONFIG_CAMERA_PIN_D2)
#else
#define CAMERA_PIN_D2 GPIO_NUM_8
#endif
#endif

#ifndef CAMERA_PIN_D3
#ifdef CONFIG_CAMERA_PIN_D3
#define CAMERA_PIN_D3 ((gpio_num_t)CONFIG_CAMERA_PIN_D3)
#else
#define CAMERA_PIN_D3 GPIO_NUM_10
#endif
#endif

#ifndef CAMERA_PIN_D4
#ifdef CONFIG_CAMERA_PIN_D4
#define CAMERA_PIN_D4 ((gpio_num_t)CONFIG_CAMERA_PIN_D4)
#else
#define CAMERA_PIN_D4 GPIO_NUM_12
#endif
#endif

#ifndef CAMERA_PIN_D5
#ifdef CONFIG_CAMERA_PIN_D5
#define CAMERA_PIN_D5 ((gpio_num_t)CONFIG_CAMERA_PIN_D5)
#else
#define CAMERA_PIN_D5 GPIO_NUM_18
#endif
#endif

#ifndef CAMERA_PIN_D6
#ifdef CONFIG_CAMERA_PIN_D6
#define CAMERA_PIN_D6 ((gpio_num_t)CONFIG_CAMERA_PIN_D6)
#else
#define CAMERA_PIN_D6 GPIO_NUM_17
#endif
#endif

#ifndef CAMERA_PIN_D7
#ifdef CONFIG_CAMERA_PIN_D7
#define CAMERA_PIN_D7 ((gpio_num_t)CONFIG_CAMERA_PIN_D7)
#else
#define CAMERA_PIN_D7 GPIO_NUM_16
#endif
#endif

#ifndef CAMERA_PIN_XCLK
#ifdef CONFIG_CAMERA_PIN_XCLK
#define CAMERA_PIN_XCLK ((gpio_num_t)CONFIG_CAMERA_PIN_XCLK)
#else
#define CAMERA_PIN_XCLK GPIO_NUM_15
#endif
#endif

#ifndef CAMERA_PIN_PCLK
#ifdef CONFIG_CAMERA_PIN_PCLK
#define CAMERA_PIN_PCLK ((gpio_num_t)CONFIG_CAMERA_PIN_PCLK)
#else
#define CAMERA_PIN_PCLK GPIO_NUM_13
#endif
#endif

#ifndef CAMERA_PIN_VSYNC
#ifdef CONFIG_CAMERA_PIN_VSYNC
#define CAMERA_PIN_VSYNC ((gpio_num_t)CONFIG_CAMERA_PIN_VSYNC)
#else
#define CAMERA_PIN_VSYNC GPIO_NUM_6
#endif
#endif

#ifndef CAMERA_PIN_HREF
#ifdef CONFIG_CAMERA_PIN_HREF
#define CAMERA_PIN_HREF ((gpio_num_t)CONFIG_CAMERA_PIN_HREF)
#else
#define CAMERA_PIN_HREF GPIO_NUM_7
#endif
#endif

#ifndef CAMERA_PIN_SIOC
#ifdef CONFIG_CAMERA_PIN_SIOC
#define CAMERA_PIN_SIOC ((gpio_num_t)CONFIG_CAMERA_PIN_SIOC)
#else
#define CAMERA_PIN_SIOC GPIO_NUM_5
#endif
#endif

#ifndef CAMERA_PIN_SIOD
#ifdef CONFIG_CAMERA_PIN_SIOD
#define CAMERA_PIN_SIOD ((gpio_num_t)CONFIG_CAMERA_PIN_SIOD)
#else
#define CAMERA_PIN_SIOD GPIO_NUM_4
#endif
#endif

#ifndef CAMERA_PIN_PWDN
#ifdef CONFIG_CAMERA_PIN_PWDN
#define CAMERA_PIN_PWDN ((gpio_num_t)CONFIG_CAMERA_PIN_PWDN)
#else
#define CAMERA_PIN_PWDN GPIO_NUM_NC
#endif
#endif

#ifndef CAMERA_PIN_RESET
#ifdef CONFIG_CAMERA_PIN_RESET
#define CAMERA_PIN_RESET ((gpio_num_t)CONFIG_CAMERA_PIN_RESET)
#else
#define CAMERA_PIN_RESET GPIO_NUM_NC
#endif
#endif
#define XCLK_FREQ_HZ 20000000

// Enhanced camera resource management
#define CAMERA_RESOURCE_LOCK_TIMEOUT_MS 5000
#define CAMERA_SWITCH_DEBOUNCE_MS 100

// Camera model configuration based on Kconfig
#ifdef CONFIG_CAMERA_MODEL_AUTO_DETECT
#define CAMERA_AUTO_DETECT_ENABLED 1
#else
#define CAMERA_AUTO_DETECT_ENABLED 0
#endif

#ifdef CONFIG_CAMERA_MODEL_OV2640_DEFAULT
#define CAMERA_DEFAULT_MODEL CAMERA_OV2640
#elif defined(CONFIG_CAMERA_MODEL_OV3660_DEFAULT)
#define CAMERA_DEFAULT_MODEL CAMERA_OV3660
#elif defined(CONFIG_CAMERA_MODEL_OV5640_DEFAULT)
#define CAMERA_DEFAULT_MODEL CAMERA_OV5640
#else
#define CAMERA_DEFAULT_MODEL CAMERA_OV2640
#endif

// Camera model support flags
#ifdef CONFIG_CAMERA_OV2640_SUPPORT
#define CAMERA_OV2640_SUPPORT 1
#else
#define CAMERA_OV2640_SUPPORT 0
#endif

#ifdef CONFIG_CAMERA_OV3660_SUPPORT
#define CAMERA_OV3660_SUPPORT 1
#else
#define CAMERA_OV3660_SUPPORT 0
#endif

#ifdef CONFIG_CAMERA_OV5640_SUPPORT
#define CAMERA_OV5640_SUPPORT 1
#else
#define CAMERA_OV5640_SUPPORT 0
#endif

// Camera flash configuration
#ifdef CONFIG_CAMERA_FLASH_PIN
#if CONFIG_CAMERA_FLASH_PIN >= 0
#define CAMERA_FLASH_PIN (gpio_num_t)CONFIG_CAMERA_FLASH_PIN
#else
#define CAMERA_FLASH_PIN GPIO_NUM_NC
#endif
#else
#define CAMERA_FLASH_PIN GPIO_NUM_NC
#endif

#ifdef CONFIG_CAMERA_FLASH_DEFAULT_LEVEL
#define CAMERA_FLASH_DEFAULT_LEVEL CONFIG_CAMERA_FLASH_DEFAULT_LEVEL
#else
#define CAMERA_FLASH_DEFAULT_LEVEL 50
#endif

// SPI LCD显示屏配置
#ifndef DISPLAY_BACKLIGHT_PIN
#ifdef CONFIG_DISPLAY_BACKLIGHT_PIN
#define DISPLAY_BACKLIGHT_PIN ((gpio_num_t)CONFIG_DISPLAY_BACKLIGHT_PIN)
#else
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_38
#endif
#endif

#ifndef DISPLAY_MOSI_PIN
#ifdef CONFIG_DISPLAY_MOSI_PIN
#define DISPLAY_MOSI_PIN      ((gpio_num_t)CONFIG_DISPLAY_MOSI_PIN)
#else
#define DISPLAY_MOSI_PIN      GPIO_NUM_20
#endif
#endif

#ifndef DISPLAY_CLK_PIN
#ifdef CONFIG_DISPLAY_CLK_PIN
#define DISPLAY_CLK_PIN       ((gpio_num_t)CONFIG_DISPLAY_CLK_PIN)
#else
#define DISPLAY_CLK_PIN       GPIO_NUM_19
#endif
#endif

#ifndef DISPLAY_DC_PIN
#ifdef CONFIG_DISPLAY_DC_PIN
#define DISPLAY_DC_PIN        ((gpio_num_t)CONFIG_DISPLAY_DC_PIN)
#else
#define DISPLAY_DC_PIN        GPIO_NUM_21
#endif
#endif

#ifndef DISPLAY_RST_PIN
#ifdef CONFIG_DISPLAY_RST_PIN
#define DISPLAY_RST_PIN       ((gpio_num_t)CONFIG_DISPLAY_RST_PIN)
#else
#define DISPLAY_RST_PIN       ((gpio_num_t)107)    // 使用PCF8575 P7
#endif
#endif

#ifndef DISPLAY_CS_PIN
#ifdef CONFIG_DISPLAY_CS_PIN
#define DISPLAY_CS_PIN        ((gpio_num_t)CONFIG_DISPLAY_CS_PIN)
#else
#define DISPLAY_CS_PIN        GPIO_NUM_1
#endif
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


// 引入通用板级配置头文件
#include "../common/board.h"

// A MCP Test: Control a lamp
#define LAMP_GPIO GPIO_NUM_NC

// ========== 级联多路复用器配置 ==========
// 主I2C总线配置 (用于所有I2C级联多路复用器)
// 使用Kconfig配置的引脚，支持用户自定义
#ifdef CONFIG_PCA9548A_SDA_PIN
#define I2C_MUX_SDA_PIN           ((gpio_num_t)CONFIG_PCA9548A_SDA_PIN)
#else
#define I2C_MUX_SDA_PIN           GPIO_NUM_14    // 默认I2C数据线
#endif

#ifdef CONFIG_PCA9548A_SCL_PIN
#define I2C_MUX_SCL_PIN           ((gpio_num_t)CONFIG_PCA9548A_SCL_PIN)
#else
#define I2C_MUX_SCL_PIN           GPIO_NUM_44    // 移动到 GPIO 44 以避开 ADC 冲突
#endif

#ifdef CONFIG_PCA9548A_I2C_FREQ_HZ
#define I2C_MUX_FREQ_HZ           CONFIG_PCA9548A_I2C_FREQ_HZ
#else
#define I2C_MUX_FREQ_HZ           400000         // 默认I2C频率 400KHz
#endif

#ifdef CONFIG_DISPLAY_I2C_PORT
#define I2C_MUX_PORT              ((i2c_port_t)CONFIG_DISPLAY_I2C_PORT)
#else
#define I2C_MUX_PORT              I2C_NUM_0      // 使用I2C_NUM_0以避开摄像头的I2C_NUM_1
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
// 重新定义HW178_S0_PIN，覆盖common/board.h中的默认值
#ifdef CONFIG_HW178_S0_PIN
#if CONFIG_HW178_S0_PIN >= 0
#undef HW178_S0_PIN
#define HW178_S0_PIN              ((gpio_num_t)CONFIG_HW178_S0_PIN)
#else
#undef HW178_S0_PIN
#define HW178_S0_PIN              GPIO_NUM_NC
#endif
#else
#undef HW178_S0_PIN
#define HW178_S0_PIN              ((gpio_num_t)104)    // 使用PCF8575 P4 (避免PSRAM冲突)
#endif

// 重新定义HW178_S1_PIN，覆盖common/board.h中的默认值
#ifdef CONFIG_HW178_S1_PIN
#if CONFIG_HW178_S1_PIN >= 0
#undef HW178_S1_PIN
#define HW178_S1_PIN              ((gpio_num_t)CONFIG_HW178_S1_PIN)
#else
#undef HW178_S1_PIN
#define HW178_S1_PIN              GPIO_NUM_NC
#endif
#else
#undef HW178_S1_PIN
#define HW178_S1_PIN              ((gpio_num_t)105)    // 使用PCF8575 P5 (避免PSRAM冲突)
#endif

// 重新定义HW178_S2_PIN，覆盖common/board.h中的默认值
#ifdef CONFIG_HW178_S2_PIN
#if CONFIG_HW178_S2_PIN >= 0
#undef HW178_S2_PIN
#define HW178_S2_PIN              ((gpio_num_t)CONFIG_HW178_S2_PIN)
#else
#undef HW178_S2_PIN
#define HW178_S2_PIN              GPIO_NUM_NC
#endif
#else
#undef HW178_S2_PIN
#define HW178_S2_PIN              ((gpio_num_t)106)    // 使用PCF8575 P6 (避免PSRAM冲突)
#endif

// 重新定义HW178_S3_PIN，覆盖common/board.h中的默认值
#ifdef CONFIG_HW178_S3_PIN
#undef HW178_S3_PIN
#define HW178_S3_PIN              ((gpio_num_t)CONFIG_HW178_S3_PIN)
#else
#undef HW178_S3_PIN
#define HW178_S3_PIN              ((gpio_num_t)103)    // 使用PCF8575 P3 (避免PSRAM冲突)
#endif

// 重新定义HW178_SIG_PIN，覆盖common/board.h中的默认值
#ifdef CONFIG_HW178_SIG_PIN
#undef HW178_SIG_PIN
#define HW178_SIG_PIN             ((gpio_num_t)CONFIG_HW178_SIG_PIN)
#else
#undef HW178_SIG_PIN
#define HW178_SIG_PIN             GPIO_NUM_3    // 移动到 GPIO 3 (ADC1_CH2) 以避开 PSRAM 冲突
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

#endif // _BOARD_CONFIG_H_
