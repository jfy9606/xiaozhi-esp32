#include "board_config.h"
#include <esp_log.h>

static const char* TAG = "BoardConfig";

// 实现板级配置获取函数
board_config_t* board_get_config(void) {
    // 使用静态变量以确保只初始化一次
    static board_config_t config = {0};
    static bool initialized = false;
    static int servo_pins_array[4] = {-1, -1, -1, -1}; // 最多支持4个舵机
    
    if (!initialized) {
        ESP_LOGI(TAG, "Initializing board configuration");
        
        // 初始化舵机引脚数组
        config.servo_pins = servo_pins_array;
        config.servo_count = 0;  // 默认没有舵机
        
        // 尝试根据板子的配置头文件初始化引脚
        #if defined(MOTOR_ENA_PIN)
        config.ena_pin = MOTOR_ENA_PIN;
        #else
        config.ena_pin = -1;
        #endif
        
        #if defined(MOTOR_ENB_PIN)
        config.enb_pin = MOTOR_ENB_PIN;
        #else
        config.enb_pin = -1;
        #endif
        
        #if defined(MOTOR_IN1_PIN)
        config.in1_pin = MOTOR_IN1_PIN;
        #else
        config.in1_pin = -1;
        #endif
        
        #if defined(MOTOR_IN2_PIN)
        config.in2_pin = MOTOR_IN2_PIN;
        #else
        config.in2_pin = -1;
        #endif
        
        #if defined(MOTOR_IN3_PIN)
        config.in3_pin = MOTOR_IN3_PIN;
        #else
        config.in3_pin = -1;
        #endif
        
        #if defined(MOTOR_IN4_PIN)
        config.in4_pin = MOTOR_IN4_PIN;
        #else
        config.in4_pin = -1;
        #endif
        
        // 摄像头引脚配置
        #if defined(CAM_PWDN_PIN)
        config.pwdn_pin = CAM_PWDN_PIN;
        #else
        config.pwdn_pin = -1;
        #endif
        
        #if defined(CAM_RESET_PIN)
        config.reset_pin = CAM_RESET_PIN;
        #else
        config.reset_pin = -1;
        #endif
        
        #if defined(CAM_XCLK_PIN)
        config.xclk_pin = CAM_XCLK_PIN;
        #else
        config.xclk_pin = -1;
        #endif
        
        #if defined(CAM_SIOD_PIN)
        config.siod_pin = CAM_SIOD_PIN;
        #else
        config.siod_pin = -1;
        #endif
        
        #if defined(CAM_SIOC_PIN)
        config.sioc_pin = CAM_SIOC_PIN;
        #else
        config.sioc_pin = -1;
        #endif
        
        #if defined(CAM_Y2_PIN)
        config.y2_pin = CAM_Y2_PIN;
        #else
        config.y2_pin = -1;
        #endif
        
        #if defined(CAM_Y3_PIN)
        config.y3_pin = CAM_Y3_PIN;
        #else
        config.y3_pin = -1;
        #endif
        
        #if defined(CAM_Y4_PIN)
        config.y4_pin = CAM_Y4_PIN;
        #else
        config.y4_pin = -1;
        #endif
        
        #if defined(CAM_Y5_PIN)
        config.y5_pin = CAM_Y5_PIN;
        #else
        config.y5_pin = -1;
        #endif
        
        #if defined(CAM_Y6_PIN)
        config.y6_pin = CAM_Y6_PIN;
        #else
        config.y6_pin = -1;
        #endif
        
        #if defined(CAM_Y7_PIN)
        config.y7_pin = CAM_Y7_PIN;
        #else
        config.y7_pin = -1;
        #endif
        
        #if defined(CAM_Y8_PIN)
        config.y8_pin = CAM_Y8_PIN;
        #else
        config.y8_pin = -1;
        #endif
        
        #if defined(CAM_Y9_PIN)
        config.y9_pin = CAM_Y9_PIN;
        #else
        config.y9_pin = -1;
        #endif
        
        #if defined(CAM_VSYNC_PIN)
        config.vsync_pin = CAM_VSYNC_PIN;
        #else
        config.vsync_pin = -1;
        #endif
        
        #if defined(CAM_HREF_PIN)
        config.href_pin = CAM_HREF_PIN;
        #else
        config.href_pin = -1;
        #endif
        
        #if defined(CAM_PCLK_PIN)
        config.pclk_pin = CAM_PCLK_PIN;
        #else
        config.pclk_pin = -1;
        #endif
        
        #if defined(CAM_LED_PIN)
        config.cam_led_pin = CAM_LED_PIN;
        #else
        config.cam_led_pin = -1;
        #endif
        
        // 标记为已初始化
        initialized = true;
        
        ESP_LOGI(TAG, "Board configuration initialized:");
        ESP_LOGI(TAG, "Motor pins: ENA=%d, ENB=%d, IN1=%d, IN2=%d, IN3=%d, IN4=%d",
                 config.ena_pin, config.enb_pin, config.in1_pin, config.in2_pin, config.in3_pin, config.in4_pin);
        ESP_LOGI(TAG, "Camera pins: XCLK=%d, SIOD=%d, SIOC=%d, VSYNC=%d, HREF=%d, PCLK=%d, LED=%d",
                 config.xclk_pin, config.siod_pin, config.sioc_pin, config.vsync_pin, config.href_pin, config.pclk_pin, config.cam_led_pin);
    }
    
    return &config;
} 