#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

// 统一的板级配置结构体
typedef struct {
    // 电机控制引脚
    int ena_pin;
    int enb_pin;
    int in1_pin;
    int in2_pin;
    int in3_pin;
    int in4_pin;
    
    // 舵机引脚
    int* servo_pins;
    int servo_count;
    
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

// 获取当前板级配置
board_config_t* board_get_config(void);

#ifdef __cplusplus
}
#endif

#endif // _BOARD_CONFIG_H_ 