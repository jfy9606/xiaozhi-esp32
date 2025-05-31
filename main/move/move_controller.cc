#include "move_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_err.h"

#include "board_config.h"
#include "ext/include/pcf8575.h"
#include "ext/include/pca9548a.h"
#include "ext/include/multiplexer.h"
#include "../iot/thing_manager.h"
#include "../iot/things/motor.h"
#include "../iot/things/servo.h"
#include <cJSON.h>
#include "../ext/include/lu9685.h"

// 添加外部声明
extern "C" {
    bool lu9685_is_initialized(void);
    lu9685_handle_t lu9685_get_handle(void);
}

// 定义GPIO电平
#define HIGH 1
#define LOW  0

// 舵机LEDC配置
#define SERVO_LEDC_TIMER      LEDC_TIMER_1
#define SERVO_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define STEERING_LEDC_CHANNEL LEDC_CHANNEL_0
#define THROTTLE_LEDC_CHANNEL LEDC_CHANNEL_1
#define SERVO_LEDC_DUTY_RES   LEDC_TIMER_13_BIT  // 使用13位分辨率
#define SERVO_FREQ_HZ         50                 // 50Hz PWM频率（20ms周期）

#define MIN_PULSE_WIDTH_US    500                // 0.5ms - 对应0度
#define MAX_PULSE_WIDTH_US    2500               // 2.5ms - 对应180度

#define TAG "MoveController"

// 电机控制构造函数
MoveController::MoveController(int ena_pin, int enb_pin, int in1_pin, int in2_pin, int in3_pin, int in4_pin)
    : controller_type_(MOVE_TYPE_MOTOR), 
      ena_pin_(ena_pin), 
      enb_pin_(enb_pin), 
      in1_pin_(in1_pin), 
      in2_pin_(in2_pin), 
      in3_pin_(in3_pin), 
      in4_pin_(in4_pin),
      steering_servo_pin_(-1),
      throttle_servo_pin_(-1),
      running_(false),
      direction_x_(0),
      direction_y_(0),
      motor_speed_(DEFAULT_SPEED),
      distance_percent_(0),
      last_dir_x_(0),
      last_dir_y_(0),
      cached_angle_degrees_(0),
      steering_angle_(DEFAULT_SERVO_ANGLE),
      throttle_position_(DEFAULT_SERVO_ANGLE) {
}

// 舵机控制构造函数
MoveController::MoveController(int steering_servo_pin, int throttle_servo_pin)
    : controller_type_(MOVE_TYPE_SERVO), 
      ena_pin_(-1), 
      enb_pin_(-1), 
      in1_pin_(-1), 
      in2_pin_(-1), 
      in3_pin_(-1), 
      in4_pin_(-1),
      steering_servo_pin_(steering_servo_pin),
      throttle_servo_pin_(throttle_servo_pin),
      running_(false),
      direction_x_(0),
      direction_y_(0),
      motor_speed_(DEFAULT_SPEED),
      distance_percent_(0),
      last_dir_x_(0),
      last_dir_y_(0),
      cached_angle_degrees_(0),
      steering_angle_(DEFAULT_SERVO_ANGLE),
      throttle_position_(DEFAULT_SERVO_ANGLE) {
}

// 混合控制构造函数
MoveController::MoveController(int ena_pin, int enb_pin, int in1_pin, int in2_pin, int in3_pin, int in4_pin,
                             int steering_servo_pin, int throttle_servo_pin)
    : controller_type_(MOVE_TYPE_HYBRID), 
      ena_pin_(ena_pin), 
      enb_pin_(enb_pin), 
      in1_pin_(in1_pin), 
      in2_pin_(in2_pin), 
      in3_pin_(in3_pin), 
      in4_pin_(in4_pin),
      steering_servo_pin_(steering_servo_pin),
      throttle_servo_pin_(throttle_servo_pin),
      running_(false),
      direction_x_(0),
      direction_y_(0),
      motor_speed_(DEFAULT_SPEED),
      distance_percent_(0),
      last_dir_x_(0),
      last_dir_y_(0),
      cached_angle_degrees_(0),
      steering_angle_(DEFAULT_SERVO_ANGLE),
      throttle_position_(DEFAULT_SERVO_ANGLE) {
}

MoveController::~MoveController() {
    // 关闭电机和舵机
    if (this->IsRunning()) {
        this->Stop(true);
    }
}

bool MoveController::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Move controller already running");
        return true;
    }
    
    // 输出控制器配置信息
    if (controller_type_ == MOVE_TYPE_MOTOR || controller_type_ == MOVE_TYPE_HYBRID) {
        ESP_LOGI(TAG, "Motor pin configuration: ENA=%d, ENB=%d, IN1=%d, IN2=%d, IN3=%d, IN4=%d",
                 ena_pin_, enb_pin_, in1_pin_, in2_pin_, in3_pin_, in4_pin_);
    }
    
    if (controller_type_ == MOVE_TYPE_SERVO || controller_type_ == MOVE_TYPE_HYBRID) {
        ESP_LOGI(TAG, "Servo pin configuration: Steering=%d, Throttle=%d",
                 steering_servo_pin_, throttle_servo_pin_);
    }
    
    // 确保Thing已经注册和初始化
    auto& thing_manager = iot::ThingManager::GetInstance();
    
    // 尝试初始化电机Thing
    if (controller_type_ == MOVE_TYPE_MOTOR || controller_type_ == MOVE_TYPE_HYBRID) {
        iot::Thing* motor_thing = thing_manager.FindThingByName("Motor");
        if (!motor_thing) {
            ESP_LOGW(TAG, "Motor Thing not found, trying to register");
            
            // 尝试安全创建Motor Thing
            try {
                ESP_LOGI(TAG, "Registering Motor Thing");
                iot::RegisterThing("Motor", nullptr);
                vTaskDelay(pdMS_TO_TICKS(100)); // 给Thing一点时间创建
                
                motor_thing = thing_manager.FindThingByName("Motor");
                if (motor_thing) {
                    ESP_LOGI(TAG, "Motor Thing registered successfully");
                } else {
                    ESP_LOGW(TAG, "Failed to register Motor Thing");
                }
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Exception registering Motor Thing: %s", e.what());
            } catch (...) {
                ESP_LOGE(TAG, "Unknown exception registering Motor Thing");
            }
        }
        
        // 初始化电机GPIO
        InitGPIO();
    }
    
    // 尝试初始化舵机Thing
    if (controller_type_ == MOVE_TYPE_SERVO || controller_type_ == MOVE_TYPE_HYBRID) {
        iot::Thing* servo_thing = thing_manager.FindThingByName("Servo");
        if (!servo_thing) {
            ESP_LOGW(TAG, "Servo Thing not found, trying to register");
            
            // 尝试安全创建Servo Thing
            try {
                ESP_LOGI(TAG, "Registering Servo Thing");
                iot::RegisterThing("Servo", nullptr);
                vTaskDelay(pdMS_TO_TICKS(100)); // 给Thing一点时间创建
                
                servo_thing = thing_manager.FindThingByName("Servo");
                if (servo_thing) {
                    ESP_LOGI(TAG, "Servo Thing registered successfully");
                } else {
                    ESP_LOGW(TAG, "Failed to register Servo Thing");
                }
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Exception registering Servo Thing: %s", e.what());
            } catch (...) {
                ESP_LOGE(TAG, "Unknown exception registering Servo Thing");
            }
        }
        
        // 初始化舵机
        InitServos();
    }
    
    running_ = true;
    ESP_LOGI(TAG, "Move controller started");
    return true;
}

// Component接口实现的Stop方法
void MoveController::Stop() {
    if (running_) {
        Stop(true);
        running_ = false;
        ESP_LOGI(TAG, "Move controller stopped");
    }
}

bool MoveController::IsRunning() const {
    return running_;
}

const char* MoveController::GetName() const {
    return "MoveController";
}

void MoveController::SetControlParams(float distance, int dirX, int dirY) {
    if (!running_) {
        ESP_LOGW(TAG, "Move controller not running");
        return;
    }

    distance_percent_ = distance;
    direction_x_ = dirX;
    direction_y_ = dirY;
    
    // 根据控制器类型调用不同的方法
    if (controller_type_ == MOVE_TYPE_MOTOR || controller_type_ == MOVE_TYPE_HYBRID) {
        // 通过Motor Thing发送移动指令
        auto& thing_manager = iot::ThingManager::GetInstance();
        // 构造JSON命令
        char command[256];
        sprintf(command, "{\"name\":\"Motor\",\"method\":\"Move\",\"parameters\":{\"dirX\":%d,\"dirY\":%d,\"distance\":%.2f}}", 
                dirX, dirY, distance * 100.0f);
        
        // 解析并调用方法
        cJSON* json = cJSON_Parse(command);
        if (json) {
            thing_manager.Invoke(json);
            cJSON_Delete(json);
        } else {
            ESP_LOGE(TAG, "Failed to create Motor JSON command");
        }
    }
    
    if (controller_type_ == MOVE_TYPE_SERVO || controller_type_ == MOVE_TYPE_HYBRID) {
        // 舵机控制逻辑 - 根据dirX控制转向舵机，根据dirY控制油门舵机
        
        // 将X方向映射到转向舵机角度范围 (-100到100) -> (0到180)
        int steering_angle = map(dirX, -100, 100, MIN_SERVO_ANGLE, MAX_SERVO_ANGLE);
        SetSteeringAngle(steering_angle);
        
        // 将Y方向映射到油门舵机角度范围，考虑distance参数 (-100到100) -> (0到180)
        // distance作为速度因子 (0.0-1.0)
        if (throttle_servo_pin_ >= 0) { // 只有当油门舵机引脚有效时才设置
            int center = (MAX_SERVO_ANGLE - MIN_SERVO_ANGLE) / 2 + MIN_SERVO_ANGLE;
            int range = (int)((MAX_SERVO_ANGLE - MIN_SERVO_ANGLE) / 2 * distance);
            int throttle_position = center;
            
            if (dirY < -10) {  // 前进
                throttle_position = center - map(abs(dirY), 0, 100, 0, range);
            } else if (dirY > 10) {  // 后退
                throttle_position = center + map(abs(dirY), 0, 100, 0, range);
            }
            
            SetThrottlePosition(throttle_position);
        }
    }
}

// 电机控制方法
void MoveController::Forward(int speed) {
    if (!running_ || (controller_type_ != MOVE_TYPE_MOTOR && controller_type_ != MOVE_TYPE_HYBRID)) {
        ESP_LOGW(TAG, "Cannot use Forward method with current configuration");
        return;
    }
    
    motor_speed_ = speed;
    
    // 使用Thing Manager进行调用
    auto& thing_manager = iot::ThingManager::GetInstance();
    // 构造JSON命令
    char command[128];
    sprintf(command, "{\"name\":\"Motor\",\"method\":\"Forward\",\"parameters\":{\"speed\":%d}}", speed);
    
    // 解析并调用方法
    cJSON* json = cJSON_Parse(command);
    if (json) {
        thing_manager.Invoke(json);
        cJSON_Delete(json);
    } else {
        ESP_LOGE(TAG, "Failed to create JSON command");
    }
}

void MoveController::Backward(int speed) {
    if (!running_ || (controller_type_ != MOVE_TYPE_MOTOR && controller_type_ != MOVE_TYPE_HYBRID)) {
        ESP_LOGW(TAG, "Cannot use Backward method with current configuration");
        return;
    }
    
    motor_speed_ = speed;
    
    // 使用Thing Manager进行调用
    auto& thing_manager = iot::ThingManager::GetInstance();
    // 构造JSON命令
    char command[128];
    sprintf(command, "{\"name\":\"Motor\",\"method\":\"Backward\",\"parameters\":{\"speed\":%d}}", speed);
    
    // 解析并调用方法
    cJSON* json = cJSON_Parse(command);
    if (json) {
        thing_manager.Invoke(json);
        cJSON_Delete(json);
    } else {
        ESP_LOGE(TAG, "Failed to create JSON command");
    }
}

void MoveController::TurnLeft(int speed) {
    if (!running_ || (controller_type_ != MOVE_TYPE_MOTOR && controller_type_ != MOVE_TYPE_HYBRID)) {
        ESP_LOGW(TAG, "Cannot use TurnLeft method with current configuration");
        return;
    }
    
    motor_speed_ = speed;
    
    // 使用Thing Manager进行调用
    auto& thing_manager = iot::ThingManager::GetInstance();
    // 构造JSON命令
    char command[128];
    sprintf(command, "{\"name\":\"Motor\",\"method\":\"TurnLeft\",\"parameters\":{\"speed\":%d}}", speed);
    
    // 解析并调用方法
    cJSON* json = cJSON_Parse(command);
    if (json) {
        thing_manager.Invoke(json);
        cJSON_Delete(json);
    } else {
        ESP_LOGE(TAG, "Failed to create JSON command");
    }
}

void MoveController::TurnRight(int speed) {
    if (!running_ || (controller_type_ != MOVE_TYPE_MOTOR && controller_type_ != MOVE_TYPE_HYBRID)) {
        ESP_LOGW(TAG, "Cannot use TurnRight method with current configuration");
        return;
    }
    
    motor_speed_ = speed;
    
    // 使用Thing Manager进行调用
    auto& thing_manager = iot::ThingManager::GetInstance();
    // 构造JSON命令
    char command[128];
    sprintf(command, "{\"name\":\"Motor\",\"method\":\"TurnRight\",\"parameters\":{\"speed\":%d}}", speed);
    
    // 解析并调用方法
    cJSON* json = cJSON_Parse(command);
    if (json) {
        thing_manager.Invoke(json);
        cJSON_Delete(json);
    } else {
        ESP_LOGE(TAG, "Failed to create JSON command");
    }
}

// 重载方法，带参数的实现
void MoveController::Stop(bool brake) {
    if (!running_) {
        ESP_LOGW(TAG, "Move controller not running");
        return;
    }
    
    // 停止电机
    if (controller_type_ == MOVE_TYPE_MOTOR || controller_type_ == MOVE_TYPE_HYBRID) {
        auto& thing_manager = iot::ThingManager::GetInstance();
        // 构造JSON命令
        char command[128];
        sprintf(command, "{\"name\":\"Motor\",\"method\":\"Stop\",\"parameters\":{\"brake\":%s}}", 
                brake ? "true" : "false");
        
        // 解析并调用方法
        cJSON* json = cJSON_Parse(command);
        if (json) {
            thing_manager.Invoke(json);
            cJSON_Delete(json);
        } else {
            ESP_LOGE(TAG, "Failed to create JSON command");
        }
    }
    
    // 舵机回中
    if (controller_type_ == MOVE_TYPE_SERVO || controller_type_ == MOVE_TYPE_HYBRID) {
        if (steering_servo_pin_ >= 0) {
            SetSteeringAngle(DEFAULT_SERVO_ANGLE);
        }
        
        if (throttle_servo_pin_ >= 0) {
            SetThrottlePosition(DEFAULT_SERVO_ANGLE);
        }
    }
    
    running_ = false;
    ESP_LOGI(TAG, "Move controller stopped");
}

void MoveController::SetSpeed(int speed) {
    if (!running_ || (controller_type_ != MOVE_TYPE_MOTOR && controller_type_ != MOVE_TYPE_HYBRID)) {
        ESP_LOGW(TAG, "Cannot set motor speed with current configuration");
        return;
    }
    
    motor_speed_ = speed < MIN_SPEED ? MIN_SPEED : (speed > MAX_SPEED ? MAX_SPEED : speed);
    
    // 使用Thing Manager进行调用
    auto& thing_manager = iot::ThingManager::GetInstance();
    // 构造JSON命令
    char command[128];
    sprintf(command, "{\"name\":\"Motor\",\"method\":\"SetSpeed\",\"parameters\":{\"speed\":%d}}", motor_speed_);
    
    // 解析并调用方法
    cJSON* json = cJSON_Parse(command);
    if (json) {
        thing_manager.Invoke(json);
        cJSON_Delete(json);
    } else {
        ESP_LOGE(TAG, "Failed to create JSON command");
    }
}

// 舵机控制方法
void MoveController::SetSteeringAngle(int angle) {
    if (!running_ || (controller_type_ != MOVE_TYPE_SERVO && controller_type_ != MOVE_TYPE_HYBRID) || 
        steering_servo_pin_ < 0) {
        ESP_LOGW(TAG, "Cannot set steering angle with current configuration");
        return;
    }
    
    // 限制角度范围
    if (angle < MIN_SERVO_ANGLE) angle = MIN_SERVO_ANGLE;
    if (angle > MAX_SERVO_ANGLE) angle = MAX_SERVO_ANGLE;
    
    steering_angle_ = angle;
    ControlSteeringServo(angle);
    
    // 同时使用Servo Thing更新状态
    auto& thing_manager = iot::ThingManager::GetInstance();
    // 构造JSON命令
    char command[128];
    sprintf(command, "{\"name\":\"Servo\",\"method\":\"SetAngle\",\"parameters\":{\"index\":0,\"angle\":%d}}", angle);
    
    // 解析并调用方法
    cJSON* json = cJSON_Parse(command);
    if (json) {
        thing_manager.Invoke(json);
        cJSON_Delete(json);
    } else {
        ESP_LOGE(TAG, "Failed to create Servo JSON command");
    }
}

void MoveController::SetThrottlePosition(int position) {
    if (!running_ || (controller_type_ != MOVE_TYPE_SERVO && controller_type_ != MOVE_TYPE_HYBRID) || 
        throttle_servo_pin_ < 0) {
        ESP_LOGW(TAG, "Cannot set throttle position with current configuration");
        return;
    }
    
    // 限制角度范围
    if (position < MIN_SERVO_ANGLE) position = MIN_SERVO_ANGLE;
    if (position > MAX_SERVO_ANGLE) position = MAX_SERVO_ANGLE;
    
    throttle_position_ = position;
    ControlThrottleServo(position);
    
    // 同时使用Servo Thing更新状态
    auto& thing_manager = iot::ThingManager::GetInstance();
    // 构造JSON命令
    char command[128];
    sprintf(command, "{\"name\":\"Servo\",\"method\":\"SetAngle\",\"parameters\":{\"index\":1,\"angle\":%d}}", position);
    
    // 解析并调用方法
    cJSON* json = cJSON_Parse(command);
    if (json) {
        thing_manager.Invoke(json);
        cJSON_Delete(json);
    } else {
        ESP_LOGE(TAG, "Failed to create Servo JSON command");
    }
}

void MoveController::InitGPIO() {
    if (controller_type_ != MOVE_TYPE_MOTOR && controller_type_ != MOVE_TYPE_HYBRID) {
        return;
    }
    
    ESP_LOGI(TAG, "Initializing GPIO pins for motor control");
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << in1_pin_) | (1ULL << in2_pin_) | 
                         (1ULL << in3_pin_) | (1ULL << in4_pin_) |
                         (1ULL << ena_pin_) | (1ULL << enb_pin_),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    gpio_config(&io_conf);
    
    // 初始状态：停止
    gpio_set_level((gpio_num_t)in1_pin_, LOW);
    gpio_set_level((gpio_num_t)in2_pin_, LOW);
    gpio_set_level((gpio_num_t)in3_pin_, LOW);
    gpio_set_level((gpio_num_t)in4_pin_, LOW);
    gpio_set_level((gpio_num_t)ena_pin_, HIGH);
    gpio_set_level((gpio_num_t)enb_pin_, HIGH);
}

void MoveController::InitServos() {
    if (controller_type_ != MOVE_TYPE_SERVO && controller_type_ != MOVE_TYPE_HYBRID) {
        return;
    }
    
    ESP_LOGI(TAG, "Initializing servo motors");
    
#ifdef CONFIG_ENABLE_LU9685
    // 检查是否需要使用LU9685舵机控制器
    if (lu9685_is_initialized()) {
        ESP_LOGI(TAG, "Using LU9685 servo controller");
        
        // 初始化成功后，将舵机归中位
        if (steering_servo_pin_ >= 0) {
            SetSteeringAngle(DEFAULT_SERVO_ANGLE);
        }
        
        if (throttle_servo_pin_ >= 0) {
            SetThrottlePosition(DEFAULT_SERVO_ANGLE);
        }
        
        return;  // 使用LU9685控制器，不需要继续LEDC初始化
    }
#endif
    
    // 配置LEDC定时器
    ledc_timer_config_t ledc_timer = {
        .speed_mode = SERVO_LEDC_MODE,
        .duty_resolution = SERVO_LEDC_DUTY_RES,
        .timer_num = SERVO_LEDC_TIMER,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    
    esp_err_t err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Servo LEDC timer config failed: 0x%x", err);
        return;
    }
    
    // 配置转向舵机通道
    if (steering_servo_pin_ >= 0) {
        ledc_channel_config_t steering_channel = {
            .gpio_num = steering_servo_pin_,
            .speed_mode = SERVO_LEDC_MODE,
            .channel = STEERING_LEDC_CHANNEL,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = SERVO_LEDC_TIMER,
            .duty = 0, // 将在后面设置
            .hpoint = 0,
            .flags = {
                .output_invert = 0
            }
        };
        
        err = ledc_channel_config(&steering_channel);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Steering servo channel config failed: 0x%x", err);
        }
        
        // 舵机归中位
        SetSteeringAngle(DEFAULT_SERVO_ANGLE);
    }
    
    // 配置油门舵机通道
    if (throttle_servo_pin_ >= 0) {
        ledc_channel_config_t throttle_channel = {
            .gpio_num = throttle_servo_pin_,
            .speed_mode = SERVO_LEDC_MODE,
            .channel = THROTTLE_LEDC_CHANNEL,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = SERVO_LEDC_TIMER,
            .duty = 0, // 将在后面设置
            .hpoint = 0,
            .flags = {
                .output_invert = 0
            }
        };
        
        err = ledc_channel_config(&throttle_channel);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Throttle servo channel config failed: 0x%x", err);
        }
        
        // 舵机归中位
        SetThrottlePosition(DEFAULT_SERVO_ANGLE);
    }
}

void MoveController::ControlMotor(int in1, int in2, int in3, int in4) {
    if (controller_type_ != MOVE_TYPE_MOTOR && controller_type_ != MOVE_TYPE_HYBRID) {
        return;
    }
    
    gpio_set_level((gpio_num_t)in1_pin_, in1);
    gpio_set_level((gpio_num_t)in2_pin_, in2);
    gpio_set_level((gpio_num_t)in3_pin_, in3);
    gpio_set_level((gpio_num_t)in4_pin_, in4);
}

// 添加一个私有的通用舵机控制方法
bool MoveController::ControlServoWithLU9685(int channel, int angle) {
#ifdef CONFIG_ENABLE_LU9685
    // 优先使用LU9685控制器
    if (lu9685_is_initialized()) {
        lu9685_handle_t handle = lu9685_get_handle();
        if (handle) {
            // 使用实际通道号控制舵机
            esp_err_t ret = lu9685_set_channel_angle(handle, channel, angle);
            if (ret == ESP_OK) {
                ESP_LOGD(TAG, "LU9685: Set servo channel %d to angle %d",
                        channel, angle);
                return true;
            } else {
                ESP_LOGW(TAG, "LU9685: Failed to set servo angle for channel %d: %s",
                        channel, esp_err_to_name(ret));
            }
        }
    }
#endif
    return false;
}

void MoveController::ControlSteeringServo(int angle) {
    if (steering_servo_pin_ < 0) {
        return;
    }
    
    // 尝试使用LU9685控制器
    if (ControlServoWithLU9685(steering_servo_pin_, angle)) {
        return;
    }
    
    // 计算PWM占空比
    // 角度(0-180)映射到脉冲宽度(500-2500微秒)
    int pulse_width_us = MIN_PULSE_WIDTH_US + (angle * (MAX_PULSE_WIDTH_US - MIN_PULSE_WIDTH_US) / MAX_SERVO_ANGLE);
    
    // 将脉冲宽度转换为占空比值
    // 例如: 对于50Hz(20ms周期)和16位分辨率(0-65535)
    // duty = (pulse_width_us / 20000) * 65536
    uint32_t duty = (pulse_width_us * 65536) / 20000;
    
    ESP_LOGD(TAG, "Setting steering servo angle to %d deg (pulse width: %d us, duty: %lu)",
             angle, pulse_width_us, duty);
    
    esp_err_t err = ledc_set_duty(SERVO_LEDC_MODE, STEERING_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set steering duty failed: 0x%x", err);
        return;
    }
    
    err = ledc_update_duty(SERVO_LEDC_MODE, STEERING_LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Update steering duty failed: 0x%x", err);
    }
}

void MoveController::ControlThrottleServo(int position) {
    if (throttle_servo_pin_ < 0) {
        return;
    }
    
    // 尝试使用LU9685控制器
    if (ControlServoWithLU9685(throttle_servo_pin_, position)) {
        return;
    }
    
    // 计算PWM占空比
    int pulse_width_us = MIN_PULSE_WIDTH_US + (position * (MAX_PULSE_WIDTH_US - MIN_PULSE_WIDTH_US) / MAX_SERVO_ANGLE);
    uint32_t duty = (pulse_width_us * 65536) / 20000;
    
    ESP_LOGD(TAG, "Setting throttle servo position to %d (pulse width: %d us, duty: %lu)",
             position, pulse_width_us, duty);
    
    esp_err_t err = ledc_set_duty(SERVO_LEDC_MODE, THROTTLE_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set throttle duty failed: 0x%x", err);
        return;
    }
    
    err = ledc_update_duty(SERVO_LEDC_MODE, THROTTLE_LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Update throttle duty failed: 0x%x", err);
    }
} 