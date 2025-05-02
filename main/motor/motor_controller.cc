#include "motor_controller.h"
#include <esp_log.h>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../boards/bread-compact-wifi/config.h"

// 定义GPIO电平
#define HIGH 1
#define LOW  0

#define TAG "MotorController"

MotorController::MotorController(int ena_pin, int enb_pin, int in1_pin, int in2_pin, int in3_pin, int in4_pin)
    : ena_pin_(ena_pin), 
      enb_pin_(enb_pin), 
      in1_pin_(in1_pin), 
      in2_pin_(in2_pin), 
      in3_pin_(in3_pin), 
      in4_pin_(in4_pin),
      running_(false),
      direction_x_(0),
      direction_y_(0),
      motor_speed_(DEFAULT_SPEED),
      distance_percent_(0),
      last_dir_x_(0),
      last_dir_y_(0),
      cached_angle_degrees_(0) {
}

MotorController::~MotorController() {
    // 调用自身的Stop方法停止电机，指定参数以避免歧义
    if (this->IsRunning()) {
        this->Stop(true);  // 显式调用带参数的Stop方法
    }
}

bool MotorController::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Motor controller already running");
        return true;
    }

    InitGPIO();
    
    // 确保电机停止
    ControlMotor(LOW, LOW, LOW, LOW);
    
    running_ = true;
    ESP_LOGI(TAG, "Motor controller started");
    return true;
}

// Component接口实现的Stop方法
void MotorController::Stop() {
    // 实现Component::Stop虚函数，调用带参数的Stop方法
    if (running_) {
        Stop(true);
        // 确保标志被重置
        running_ = false;
        ESP_LOGI(TAG, "Motor controller stopped from Component::Stop()");
    }
}

bool MotorController::IsRunning() const {
    return running_;
}

const char* MotorController::GetName() const {
    return "MotorController";
}

void MotorController::SetControlParams(float distance, int dirX, int dirY) {
    if (!running_) {
        ESP_LOGW(TAG, "Motor controller not running");
        return;
    }

    distance_percent_ = distance;
    direction_x_ = dirX;
    direction_y_ = dirY;
    
    // 根据摇杆拖动距离计算速度
    float speedFactor = pow(distance_percent_, 2.0);
    motor_speed_ = MIN_SPEED + (int)((MAX_SPEED - MIN_SPEED) * speedFactor);
    
    // 根据方向值确定电机控制方式
    if (direction_x_ == 0 && direction_y_ == 0) {
        // 停止
        ControlMotor(LOW, LOW, LOW, LOW);
    } else {
        // 计算角度，确定前进、后退、左转、右转
        float angle = atan2(direction_y_, direction_x_);
        float angleDegrees = angle * 180.0 / M_PI;
        
        // 前进区域（大致在-135度到-45度之间）
        if (angleDegrees < -45 && angleDegrees > -135) {
            ControlMotor(HIGH, LOW, HIGH, LOW); // 前进
        }
        // 后退区域（大致在45度到135度之间）
        else if (angleDegrees > 45 && angleDegrees < 135) {
            ControlMotor(LOW, HIGH, LOW, HIGH); // 后退
        }
        // 右转区域（大致在-45度到45度之间）
        else if (angleDegrees >= -45 && angleDegrees <= 45) {
            ControlMotor(LOW, HIGH, HIGH, LOW); // 右转
        }
        // 左转区域（135度到180度或-180度到-135度之间）
        else {
            ControlMotor(HIGH, LOW, LOW, HIGH); // 左转
        }
    }
}

void MotorController::Forward(int speed) {
    if (!running_) {
        ESP_LOGW(TAG, "Motor controller not running");
        return;
    }
    
    motor_speed_ = speed;
    ControlMotor(HIGH, LOW, HIGH, LOW);
}

void MotorController::Backward(int speed) {
    if (!running_) {
        ESP_LOGW(TAG, "Motor controller not running");
        return;
    }
    
    motor_speed_ = speed;
    ControlMotor(LOW, HIGH, LOW, HIGH);
}

void MotorController::TurnLeft(int speed) {
    if (!running_) {
        ESP_LOGW(TAG, "Motor controller not running");
        return;
    }
    
    motor_speed_ = speed;
    ControlMotor(HIGH, LOW, LOW, HIGH);
}

void MotorController::TurnRight(int speed) {
    if (!running_) {
        ESP_LOGW(TAG, "Motor controller not running");
        return;
    }
    
    motor_speed_ = speed;
    ControlMotor(LOW, HIGH, HIGH, LOW);
}

// 重载方法，带参数的实现
void MotorController::Stop(bool brake) {
    if (!running_) {
        ESP_LOGW(TAG, "Motor controller not running");
        return;
    }
    
    if (brake) {
        // 刹车模式 - 向电机施加反向电流以迅速停止
        ControlMotor(HIGH, HIGH, HIGH, HIGH);
        // 短暂延时让电机有时间停止
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 自由停止模式 - 切断电源让电机自然停止
    ControlMotor(LOW, LOW, LOW, LOW);
    
    running_ = false;
    ESP_LOGI(TAG, "Motor controller stopped");
}

void MotorController::SetSpeed(int speed) {
    motor_speed_ = speed < MIN_SPEED ? MIN_SPEED : (speed > MAX_SPEED ? MAX_SPEED : speed);
}

void MotorController::InitGPIO() {
    ESP_LOGI(TAG, "Initializing motor GPIO pins: ENA=%d, ENB=%d, IN1=%d, IN2=%d, IN3=%d, IN4=%d",
             ena_pin_, enb_pin_, in1_pin_, in2_pin_, in3_pin_, in4_pin_);
    
    // 验证所有引脚是否在有效范围内
    if (ena_pin_ < 0 || ena_pin_ >= GPIO_NUM_MAX ||
        enb_pin_ < 0 || enb_pin_ >= GPIO_NUM_MAX ||
        in1_pin_ < 0 || in1_pin_ >= GPIO_NUM_MAX ||
        in2_pin_ < 0 || in2_pin_ >= GPIO_NUM_MAX ||
        in3_pin_ < 0 || in3_pin_ >= GPIO_NUM_MAX ||
        in4_pin_ < 0 || in4_pin_ >= GPIO_NUM_MAX) {
        ESP_LOGE(TAG, "Invalid motor pin configuration detected!");
        ESP_LOGE(TAG, "Valid pin range: 0-%d, ENA=%d, ENB=%d, IN1=%d, IN2=%d, IN3=%d, IN4=%d",
                 GPIO_NUM_MAX-1, ena_pin_, enb_pin_, in1_pin_, in2_pin_, in3_pin_, in4_pin_);
        // 安全处理：使用默认pin值
        return;
    }
    
    // 配置电机控制引脚
    gpio_config_t io_conf = {};
    
    // 设置为输出模式
    io_conf.mode = GPIO_MODE_OUTPUT;
    
    // 设置引脚掩码
    io_conf.pin_bit_mask = (1ULL << in1_pin_) | (1ULL << in2_pin_) |
                           (1ULL << in3_pin_) | (1ULL << in4_pin_);
    
    // 禁用中断和上拉/下拉
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    
    // 配置GPIO
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Motor GPIO config failed with error 0x%x", err);
        return;
    }
    
    // 配置PWM控制引脚 (ENA, ENB)
    // 使用LEDC通道0和通道1
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);
    
    // 配置ENA
    ledc_channel_config_t ledc_channel_ena = {
        .gpio_num = ena_pin_,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel_ena);
    
    // 配置ENB
    ledc_channel_config_t ledc_channel_enb = {
        .gpio_num = enb_pin_,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel_enb);
    
    // 初始时设置为0
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

void MotorController::ControlMotor(int in1, int in2, int in3, int in4) {
    // 设置电机方向控制引脚
    gpio_set_level((gpio_num_t)in1_pin_, in1);
    gpio_set_level((gpio_num_t)in2_pin_, in2);
    gpio_set_level((gpio_num_t)in3_pin_, in3);
    gpio_set_level((gpio_num_t)in4_pin_, in4);
    
    // 如果是停止状态，两个电机都停止
    if (in1 == LOW && in2 == LOW && in3 == LOW && in4 == LOW) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
        return;
    }
    
    // 计算角度（弧度）
    if (last_dir_x_ != direction_x_ || last_dir_y_ != direction_y_) {
        float angle = atan2(direction_y_, direction_x_);
        cached_angle_degrees_ = angle * 180.0 / M_PI;
        last_dir_x_ = direction_x_;
        last_dir_y_ = direction_y_;
    }
    
    float angleDegrees = cached_angle_degrees_;
    
    // 计算左右电机的速度
    int leftSpeed = motor_speed_;
    int rightSpeed = motor_speed_;
    
    // 根据角度调整左右电机速度
    // 前进时
    if (angleDegrees < -45 && angleDegrees > -135) {
        float deviation = abs(angleDegrees + 90);
        float ratio = 1.0 - pow(deviation / 45.0, 1.5) * 0.7;
        
        if (angleDegrees > -90) {
            rightSpeed = (int)(motor_speed_ * ratio);
        } else if (angleDegrees < -90) {
            leftSpeed = (int)(motor_speed_ * ratio);
        }
    }
    // 后退时
    else if (angleDegrees > 45 && angleDegrees < 135) {
        float deviation = abs(angleDegrees - 90);
        float ratio = 1.0 - pow(deviation / 45.0, 1.5) * 0.7;
        
        if (angleDegrees < 90) {
            leftSpeed = (int)(motor_speed_ * ratio);
        } else if (angleDegrees > 90) {
            rightSpeed = (int)(motor_speed_ * ratio);
        }
    }
    // 左转或右转
    else {
        // 右转
        if (angleDegrees >= -45 && angleDegrees <= 45) {
            float turnIntensity = 1.0 - pow(abs(angleDegrees) / 45.0, 1.2) * 0.3;
            leftSpeed = motor_speed_;
            rightSpeed = (int)(motor_speed_ * (0.3 + (0.7 * (1.0 - turnIntensity))));
        }
        // 左转
        else {
            float normalizedAngle = angleDegrees > 0 ? 180 - angleDegrees : -180 - angleDegrees;
            float turnIntensity = 1.0 - pow(abs(normalizedAngle) / 45.0, 1.2) * 0.3;
            leftSpeed = (int)(motor_speed_ * (0.3 + (0.7 * (1.0 - turnIntensity))));
            rightSpeed = motor_speed_;
        }
    }
    
    // 确保速度在有效范围内
    leftSpeed = leftSpeed < MIN_SPEED ? MIN_SPEED : (leftSpeed > MAX_SPEED ? MAX_SPEED : leftSpeed);
    rightSpeed = rightSpeed < MIN_SPEED ? MIN_SPEED : (rightSpeed > MAX_SPEED ? MAX_SPEED : rightSpeed);
    
    // 应用速度到电机
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, leftSpeed);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, rightSpeed);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
} 