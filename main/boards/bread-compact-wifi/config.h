#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2c.h>
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

// 电机控制引脚定义
#define MOTOR_ENA_PIN 2   // 电机A使能
#define MOTOR_ENB_PIN 1   // 电机B使能
#define MOTOR_IN1_PIN 47  // 电机A输入1
#define MOTOR_IN2_PIN 21  // 电机A输入2
#define MOTOR_IN3_PIN 20  // 电机B输入1
#define MOTOR_IN4_PIN 19  // 电机B输入2

// 舵机引脚定义
#define SERVO_PAN_PIN  46  // 水平舵机引脚
#define SERVO_TILT_PIN 14  // 垂直舵机引脚
#define SERVO_COUNT    2   // 舵机数量

// 摄像头引脚定义
#define CAM_PWDN_PIN  -1  // 掉电引脚
#define CAM_RESET_PIN -1  // 复位引脚
#define CAM_XCLK_PIN  15  // XCLK时钟引脚
#define CAM_SIOD_PIN  4   // SCCB/I2C数据引脚
#define CAM_SIOC_PIN  5   // SCCB/I2C时钟引脚
#define CAM_Y2_PIN    11  // Y2数据引脚
#define CAM_Y3_PIN    9   // Y3数据引脚
#define CAM_Y4_PIN    8   // Y4数据引脚
#define CAM_Y5_PIN    10  // Y5数据引脚
#define CAM_Y6_PIN    12  // Y6数据引脚
#define CAM_Y7_PIN    18  // Y7数据引脚
#define CAM_Y8_PIN    17  // Y8数据引脚
#define CAM_Y9_PIN    16  // Y9数据引脚
#define CAM_VSYNC_PIN 6   // 垂直同步引脚
#define CAM_HREF_PIN  7   // 水平参考引脚
#define CAM_PCLK_PIN  13  // 像素时钟引脚
#define CAM_LED_PIN   45  // 摄像头LED闪光灯引脚

// 多路复用器配置已移至Kconfig配置和common/board_config.h实现
// 可以通过menuconfig进行配置

// 引入通用板级配置头文件
#include "../common/board_config.h"

#endif // _BOARD_CONFIG_H_
