#include "motor_controller.h"
#include <esp_log.h>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../boards/bread-compact-wifi/config.h"
#include "iot/thing_manager.h"
#include <cJSON.h>

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
    
    // 使用Thing Manager进行调用
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
        ESP_LOGE(TAG, "Failed to create JSON command");
    }
}

void MotorController::Forward(int speed) {
    if (!running_) {
        ESP_LOGW(TAG, "Motor controller not running");
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

void MotorController::Backward(int speed) {
    if (!running_) {
        ESP_LOGW(TAG, "Motor controller not running");
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

void MotorController::TurnLeft(int speed) {
    if (!running_) {
        ESP_LOGW(TAG, "Motor controller not running");
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

void MotorController::TurnRight(int speed) {
    if (!running_) {
        ESP_LOGW(TAG, "Motor controller not running");
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
void MotorController::Stop(bool brake) {
    if (!running_) {
        ESP_LOGW(TAG, "Motor controller not running");
        return;
    }
    
    // 使用Thing Manager进行调用
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
    
    running_ = false;
    ESP_LOGI(TAG, "Motor controller stopped");
}

void MotorController::SetSpeed(int speed) {
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

void MotorController::InitGPIO() {
    // 初始化逻辑已移至iot/things/motor.cc
    ESP_LOGI(TAG, "GPIO initialization delegated to Motor Thing");
}

void MotorController::ControlMotor(int in1, int in2, int in3, int in4) {
    // 控制逻辑已移至iot/things/motor.cc
    ESP_LOGI(TAG, "Motor control delegated to Motor Thing");
} 