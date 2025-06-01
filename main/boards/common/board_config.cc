#include "board_config.h"
#include <esp_log.h>

static const char* TAG = "BoardConfig";

// 实现板级配置获取函数 - 内部实现
board_config_t* board_config_get(void) {
    // 使用静态变量以确保只初始化一次
    static board_config_t config = {0};
    static bool initialized = false;
    static int servo_pins_array[8] = {-1, -1, -1, -1, -1, -1, -1, -1}; // 最多支持8个舵机
    
    if (!initialized) {
        ESP_LOGI(TAG, "Initializing board configuration");
        
        // 初始化舵机引脚数组
        config.servo_pins = servo_pins_array;
        config.servo_count = 0;  // 默认没有舵机
        
        // 尝试根据板子的配置头文件和Kconfig初始化引脚
        // 电机引脚配置 - 从Kconfig中读取
#ifdef CONFIG_ENABLE_MOTOR_CONTROLLER
        // 使用定义的电机引脚常量，确保从Kconfig中正确读取
        #ifdef CONFIG_MOTOR_ENA_PIN
        config.ena_pin = CONFIG_MOTOR_ENA_PIN;
        ESP_LOGI(TAG, "Motor ENA pin from Kconfig: %d", CONFIG_MOTOR_ENA_PIN);
        #else
        config.ena_pin = MOTOR_ENA_PIN; // 使用Kconfig定义的默认值
        ESP_LOGI(TAG, "Motor ENA pin using default from board_config.h: %d", MOTOR_ENA_PIN);
        #endif
        
        #ifdef CONFIG_MOTOR_ENB_PIN
        config.enb_pin = CONFIG_MOTOR_ENB_PIN;
        ESP_LOGI(TAG, "Motor ENB pin from Kconfig: %d", CONFIG_MOTOR_ENB_PIN);
        #else
        config.enb_pin = MOTOR_ENB_PIN; // 使用Kconfig定义的默认值
        ESP_LOGI(TAG, "Motor ENB pin using default from board_config.h: %d", MOTOR_ENB_PIN);
        #endif
        
        // 根据电机连接方式选择IN1-IN4引脚
        #ifdef CONFIG_MOTOR_CONNECTION_DIRECT
            // 直接连接方式：从CONFIG_MOTOR_INx_PIN获取引脚
            #ifdef CONFIG_MOTOR_IN1_PIN
            config.in1_pin = CONFIG_MOTOR_IN1_PIN;
            ESP_LOGI(TAG, "Motor IN1 pin from Kconfig (direct): %d", CONFIG_MOTOR_IN1_PIN);
            #else
            config.in1_pin = MOTOR_IN1_PIN; // 使用board_config.h默认值
            ESP_LOGI(TAG, "Motor IN1 pin using default from board_config.h: %d", MOTOR_IN1_PIN);
            #endif
            
            #ifdef CONFIG_MOTOR_IN2_PIN
            config.in2_pin = CONFIG_MOTOR_IN2_PIN;
            ESP_LOGI(TAG, "Motor IN2 pin from Kconfig (direct): %d", CONFIG_MOTOR_IN2_PIN);
            #else
            config.in2_pin = MOTOR_IN2_PIN; // 使用board_config.h默认值
            ESP_LOGI(TAG, "Motor IN2 pin using default from board_config.h: %d", MOTOR_IN2_PIN);
            #endif
            
            #ifdef CONFIG_MOTOR_IN3_PIN
            config.in3_pin = CONFIG_MOTOR_IN3_PIN;
            ESP_LOGI(TAG, "Motor IN3 pin from Kconfig (direct): %d", CONFIG_MOTOR_IN3_PIN);
            #else
            config.in3_pin = MOTOR_IN3_PIN; // 使用board_config.h默认值
            ESP_LOGI(TAG, "Motor IN3 pin using default from board_config.h: %d", MOTOR_IN3_PIN);
            #endif
            
            #ifdef CONFIG_MOTOR_IN4_PIN
            config.in4_pin = CONFIG_MOTOR_IN4_PIN;
            ESP_LOGI(TAG, "Motor IN4 pin from Kconfig (direct): %d", CONFIG_MOTOR_IN4_PIN);
            #else
            config.in4_pin = MOTOR_IN4_PIN; // 使用board_config.h默认值
            ESP_LOGI(TAG, "Motor IN4 pin using default from board_config.h: %d", MOTOR_IN4_PIN);
            #endif
        #elif defined(CONFIG_MOTOR_CONNECTION_PCF8575)
            // PCF8575连接方式：电机控制引脚通过PCF8575扩展器连接
            // 这种情况下，实际引脚编号由PCF8575控制器确定，我们在这里使用-1表示通过扩展器控制
            config.in1_pin = -1;
            config.in2_pin = -1;
            config.in3_pin = -1;
            config.in4_pin = -1;
            ESP_LOGI(TAG, "Motor control pins using PCF8575 expander");
            #ifdef CONFIG_MOTOR_PCF8575_IN1_PIN
            ESP_LOGI(TAG, "PCF8575 Motor IN1 pin: %d", CONFIG_MOTOR_PCF8575_IN1_PIN);
            #endif
            #ifdef CONFIG_MOTOR_PCF8575_IN2_PIN
            ESP_LOGI(TAG, "PCF8575 Motor IN2 pin: %d", CONFIG_MOTOR_PCF8575_IN2_PIN);
            #endif
            #ifdef CONFIG_MOTOR_PCF8575_IN3_PIN
            ESP_LOGI(TAG, "PCF8575 Motor IN3 pin: %d", CONFIG_MOTOR_PCF8575_IN3_PIN);
            #endif
            #ifdef CONFIG_MOTOR_PCF8575_IN4_PIN
            ESP_LOGI(TAG, "PCF8575 Motor IN4 pin: %d", CONFIG_MOTOR_PCF8575_IN4_PIN);
            #endif
        #else
            // 默认情况
            config.in1_pin = MOTOR_IN1_PIN;
            config.in2_pin = MOTOR_IN2_PIN;
            config.in3_pin = MOTOR_IN3_PIN;
            config.in4_pin = MOTOR_IN4_PIN;
            ESP_LOGI(TAG, "Motor pins using defaults: IN1=%d, IN2=%d, IN3=%d, IN4=%d", 
                     config.in1_pin, config.in2_pin, config.in3_pin, config.in4_pin);
        #endif
        
        // 检查是否有引脚冲突
        if (config.ena_pin == config.in1_pin || 
            config.ena_pin == config.in2_pin || 
            config.ena_pin == config.in3_pin || 
            config.ena_pin == config.in4_pin ||
            config.enb_pin == config.in1_pin || 
            config.enb_pin == config.in2_pin || 
            config.enb_pin == config.in3_pin || 
            config.enb_pin == config.in4_pin) {
            ESP_LOGW(TAG, "Warning: Motor pin conflict detected! Check your configuration");
        }
#else
        // 如果未启用电机控制器，则设置为无效值
        config.ena_pin = -1;
        config.enb_pin = -1;
        config.in1_pin = -1;
        config.in2_pin = -1;
        config.in3_pin = -1;
        config.in4_pin = -1;
        ESP_LOGI(TAG, "Motor controller disabled");
        #endif
        
        // 舵机引脚配置 - 从Kconfig中读取
#ifdef CONFIG_ENABLE_SERVO_CONTROLLER
        #ifdef CONFIG_SERVO_CONNECTION_DIRECT
            // 直接连接方式：使用GPIO引脚
            config.servo_count = SERVO_COUNT > 8 ? 8 : SERVO_COUNT; // 限制最多8个舵机
            
            ESP_LOGI(TAG, "Setting up %d servos from Kconfig (direct GPIO connection)", config.servo_count);
            
            // 默认初始化所有引脚为-1（无效值）
            for (int i = 0; i < 8; i++) {
                servo_pins_array[i] = -1;
            }
            
            // 直接使用Kconfig定义的舵机引脚
            #ifdef CONFIG_SERVO_PIN_1
            servo_pins_array[0] = CONFIG_SERVO_PIN_1;
            ESP_LOGI(TAG, "Servo 1 pin from Kconfig: %d", CONFIG_SERVO_PIN_1);
            #elif defined(SERVO_PIN_1)
            servo_pins_array[0] = SERVO_PIN_1;
            ESP_LOGI(TAG, "Servo 1 pin using default from board_config.h: %d", SERVO_PIN_1);
            #else
            ESP_LOGI(TAG, "Servo 1 pin not defined");
            #endif
            
            #ifdef CONFIG_SERVO_PIN_2
            if (config.servo_count >= 2) {
                servo_pins_array[1] = CONFIG_SERVO_PIN_2;
                ESP_LOGI(TAG, "Servo 2 pin from Kconfig: %d", CONFIG_SERVO_PIN_2);
            }
            #elif defined(SERVO_PIN_2)
            if (config.servo_count >= 2) {
                servo_pins_array[1] = SERVO_PIN_2;
                ESP_LOGI(TAG, "Servo 2 pin using default from board_config.h: %d", SERVO_PIN_2);
            }
            #else
            ESP_LOGI(TAG, "Servo 2 pin not defined");
            #endif
            
            #ifdef CONFIG_SERVO_PIN_3
            if (config.servo_count >= 3) {
                servo_pins_array[2] = CONFIG_SERVO_PIN_3;
                ESP_LOGI(TAG, "Servo 3 pin from Kconfig: %d", CONFIG_SERVO_PIN_3);
            }
            #elif defined(SERVO_PIN_3)
            if (config.servo_count >= 3) {
                servo_pins_array[2] = SERVO_PIN_3;
                ESP_LOGI(TAG, "Servo 3 pin using default from board_config.h: %d", SERVO_PIN_3);
            }
            #else
            ESP_LOGI(TAG, "Servo 3 pin not defined");
            #endif
            
            #ifdef CONFIG_SERVO_PIN_4
            if (config.servo_count >= 4) {
                servo_pins_array[3] = CONFIG_SERVO_PIN_4;
                ESP_LOGI(TAG, "Servo 4 pin from Kconfig: %d", CONFIG_SERVO_PIN_4);
            }
            #elif defined(SERVO_PIN_4)
            if (config.servo_count >= 4) {
                servo_pins_array[3] = SERVO_PIN_4;
                ESP_LOGI(TAG, "Servo 4 pin using default from board_config.h: %d", SERVO_PIN_4);
            }
            #else
            ESP_LOGI(TAG, "Servo 4 pin not defined");
            #endif
            
            #ifdef CONFIG_SERVO_PIN_5
            if (config.servo_count >= 5) {
                servo_pins_array[4] = CONFIG_SERVO_PIN_5;
                ESP_LOGI(TAG, "Servo 5 pin from Kconfig: %d", CONFIG_SERVO_PIN_5);
            }
            #elif defined(SERVO_PIN_5)
            if (config.servo_count >= 5) {
                servo_pins_array[4] = SERVO_PIN_5;
                ESP_LOGI(TAG, "Servo 5 pin using default from board_config.h: %d", SERVO_PIN_5);
            }
            #else
            ESP_LOGI(TAG, "Servo 5 pin not defined");
            #endif
            
            #ifdef CONFIG_SERVO_PIN_6
            if (config.servo_count >= 6) {
                servo_pins_array[5] = CONFIG_SERVO_PIN_6;
                ESP_LOGI(TAG, "Servo 6 pin from Kconfig: %d", CONFIG_SERVO_PIN_6);
            }
            #elif defined(SERVO_PIN_6)
            if (config.servo_count >= 6) {
                servo_pins_array[5] = SERVO_PIN_6;
                ESP_LOGI(TAG, "Servo 6 pin using default from board_config.h: %d", SERVO_PIN_6);
            }
            #else
            ESP_LOGI(TAG, "Servo 6 pin not defined");
            #endif
            
            #ifdef CONFIG_SERVO_PIN_7
            if (config.servo_count >= 7) {
                servo_pins_array[6] = CONFIG_SERVO_PIN_7;
                ESP_LOGI(TAG, "Servo 7 pin from Kconfig: %d", CONFIG_SERVO_PIN_7);
            }
            #elif defined(SERVO_PIN_7)
            if (config.servo_count >= 7) {
                servo_pins_array[6] = SERVO_PIN_7;
                ESP_LOGI(TAG, "Servo 7 pin using default from board_config.h: %d", SERVO_PIN_7);
            }
            #else
            ESP_LOGI(TAG, "Servo 7 pin not defined");
            #endif
            
            #ifdef CONFIG_SERVO_PIN_8
            if (config.servo_count >= 8) {
                servo_pins_array[7] = CONFIG_SERVO_PIN_8;
                ESP_LOGI(TAG, "Servo 8 pin from Kconfig: %d", CONFIG_SERVO_PIN_8);
            }
            #elif defined(SERVO_PIN_8)
            if (config.servo_count >= 8) {
                servo_pins_array[7] = SERVO_PIN_8;
                ESP_LOGI(TAG, "Servo 8 pin using default from board_config.h: %d", SERVO_PIN_8);
            }
            #else
            ESP_LOGI(TAG, "Servo 8 pin not defined");
            #endif
            
            ESP_LOGI(TAG, "Servo count: %d", config.servo_count);
            for (int i = 0; i < config.servo_count; i++) {
                ESP_LOGI(TAG, "Servo %d pin: %d", i+1, servo_pins_array[i]);
            }
        #elif defined(CONFIG_SERVO_CONNECTION_LU9685)
            // LU9685连接方式：使用LU9685-20CU舵机控制器
            // 这种情况下，舵机通过I2C控制器连接，不使用GPIO引脚
            // 设置舵机数量为4（标准配置为左、右、上、下四个舵机）
            config.servo_count = 4;
            ESP_LOGI(TAG, "Using LU9685 servo controller with %d servos", config.servo_count);
            
            // 标记所有GPIO引脚为无效，因为使用LU9685
            for (int i = 0; i < 8; i++) {
                servo_pins_array[i] = -1;
            }
            
            // 输出LU9685通道配置
            #ifdef CONFIG_SERVO_LU9685_LEFT_CHANNEL
            ESP_LOGI(TAG, "LU9685 Left servo channel: %d", CONFIG_SERVO_LU9685_LEFT_CHANNEL);
            #endif
            
            #ifdef CONFIG_SERVO_LU9685_RIGHT_CHANNEL
            ESP_LOGI(TAG, "LU9685 Right servo channel: %d", CONFIG_SERVO_LU9685_RIGHT_CHANNEL);
            #endif
            
            #ifdef CONFIG_SERVO_LU9685_UP_CHANNEL
            ESP_LOGI(TAG, "LU9685 Up servo channel: %d", CONFIG_SERVO_LU9685_UP_CHANNEL);
            #endif
            
            #ifdef CONFIG_SERVO_LU9685_DOWN_CHANNEL
            ESP_LOGI(TAG, "LU9685 Down servo channel: %d", CONFIG_SERVO_LU9685_DOWN_CHANNEL);
            #endif
        #else
            // 未知连接方式，设置为0舵机
            config.servo_count = 0;
            ESP_LOGW(TAG, "Unknown servo connection type, disabling servos");
        #endif
        #else
        config.servo_count = 0;
        ESP_LOGI(TAG, "Servo controller disabled");
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
        
        ESP_LOGI(TAG, "--------- Board configuration summary ---------");
        ESP_LOGI(TAG, "Motor pins (from Kconfig):");
        ESP_LOGI(TAG, "  ENA: %d, ENB: %d", config.ena_pin, config.enb_pin);
        #ifdef CONFIG_MOTOR_CONNECTION_PCF8575
        // PCF8575连接方式显示扩展器引脚
        ESP_LOGI(TAG, "  IN1: -1, IN2: -1, IN3: -1, IN4: -1 (Using PCF8575 expander)");
        ESP_LOGI(TAG, "  PCF8575 pins: IN1: %d, IN2: %d, IN3: %d, IN4: %d", 
                 CONFIG_MOTOR_PCF8575_IN1_PIN, CONFIG_MOTOR_PCF8575_IN2_PIN,
                 CONFIG_MOTOR_PCF8575_IN3_PIN, CONFIG_MOTOR_PCF8575_IN4_PIN);
        #else
        ESP_LOGI(TAG, "  IN1: %d, IN2: %d, IN3: %d, IN4: %d", 
                 config.in1_pin, config.in2_pin, config.in3_pin, config.in4_pin);
        #endif
        
        ESP_LOGI(TAG, "Servo pins (from Kconfig):");
        #ifdef CONFIG_SERVO_CONNECTION_LU9685
        // LU9685连接方式显示通道信息
        ESP_LOGI(TAG, "  Using LU9685 servo controller on PCA9548A channel %d", CONFIG_LU9685_PCA9548A_CHANNEL);
        ESP_LOGI(TAG, "  LU9685 channels: Left: %d, Right: %d, Up: %d, Down: %d",
                 CONFIG_SERVO_LU9685_LEFT_CHANNEL, CONFIG_SERVO_LU9685_RIGHT_CHANNEL,
                 CONFIG_SERVO_LU9685_UP_CHANNEL, CONFIG_SERVO_LU9685_DOWN_CHANNEL);
        #endif
        for (int i = 0; i < config.servo_count; i++) {
            ESP_LOGI(TAG, "  Servo %d: %d", i+1, servo_pins_array[i]);
        }
        
        ESP_LOGI(TAG, "Camera pins (from board-specific defines):");
        ESP_LOGI(TAG, "  XCLK: %d, SIOD: %d, SIOC: %d", 
                 config.xclk_pin, config.siod_pin, config.sioc_pin);
        ESP_LOGI(TAG, "  VSYNC: %d, HREF: %d, PCLK: %d, LED: %d",
                 config.vsync_pin, config.href_pin, config.pclk_pin, config.cam_led_pin);
        ESP_LOGI(TAG, "--------------------------------------------");
    }
    
    return &config;
}

// 为兼容旧代码提供的接口
board_config_t* board_get_config(void) {
    return board_config_get();
} 