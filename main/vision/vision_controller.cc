#include "vision_controller.h"
#include <esp_log.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <driver/ledc.h>
#include "iot/thing_manager.h"
#include <cJSON.h>
#include "sdkconfig.h"

#define TAG "VisionController"

// LEDC PWM配置 - 用于LED控制
#define LED_LEDC_TIMER     LEDC_TIMER_0
#define LED_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define LED_LEDC_CHANNEL   LEDC_CHANNEL_2
#define LED_LEDC_DUTY_RES  LEDC_TIMER_8_BIT  // 8位分辨率，0-255
#define LED_LEDC_FREQ      5000              // PWM频率5kHz

VisionController::VisionController() 
    : running_(false), streaming_(false), led_intensity_(0) {
}

VisionController::~VisionController() {
    Stop();
}

bool VisionController::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Vision controller already running");
        return true;
    }

    ESP_LOGI(TAG, "Starting VisionController with integrated camera functionality");

    // 摄像头初始化已经在iot/things/cam.cc中完成
    // 这里直接标记为已启动状态
    running_ = true;
    ESP_LOGI(TAG, "VisionController started successfully, camera ready for use");
    return true;
}

void VisionController::Stop() {
    if (!running_) {
        return;
    }

    // 使用Thing Manager停止直播
    auto& thing_manager = iot::ThingManager::GetInstance();
    // 构造JSON命令
    char command[128];
    sprintf(command, "{\"name\":\"Camera\",\"method\":\"StopStreaming\",\"parameters\":{}}");
    
    // 解析并调用方法
    cJSON* json = cJSON_Parse(command);
    if (json) {
        thing_manager.Invoke(json);
        cJSON_Delete(json);
    } else {
        ESP_LOGE(TAG, "Failed to create JSON command");
    }
    
    running_ = false;
    ESP_LOGI(TAG, "Vision controller stopped");
}

bool VisionController::IsRunning() const {
    return running_;
}

const char* VisionController::GetName() const {
    return "VisionController";
}

camera_fb_t* VisionController::GetFrame() {
    if (!running_) {
        ESP_LOGW(TAG, "Vision controller not running");
        return nullptr;
    }
    
    // 查找Camera Thing并获取帧
    // 这里我们只是简单地调用esp_camera_fb_get()
    // 在实际应用中您可能需要适当的线程安全处理
    return esp_camera_fb_get();
}

void VisionController::ReturnFrame(camera_fb_t* fb) {
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

void VisionController::SetLedIntensity(int intensity) {
    if (intensity < 0) intensity = 0;
    if (intensity > 255) intensity = 255;
    
    led_intensity_ = intensity;
    
    // 使用Thing Manager进行调用
    auto& thing_manager = iot::ThingManager::GetInstance();
    // 构造JSON命令
    char command[128];
    sprintf(command, "{\"name\":\"Camera\",\"method\":\"SetLedIntensity\",\"parameters\":{\"intensity\":%d}}", intensity);
    
    // 解析并调用方法
    cJSON* json = cJSON_Parse(command);
    if (json) {
        thing_manager.Invoke(json);
        cJSON_Delete(json);
    } else {
        ESP_LOGE(TAG, "Failed to create JSON command");
    }
}

int VisionController::GetLedIntensity() const {
    return led_intensity_;
}

bool VisionController::IsStreaming() const {
    return streaming_;
}

void VisionController::SetStreaming(bool streaming) {
    streaming_ = streaming;
    
    // 使用Thing Manager进行调用
    auto& thing_manager = iot::ThingManager::GetInstance();
    // 构造JSON命令
    char command[128];
    if (streaming) {
        sprintf(command, "{\"name\":\"Camera\",\"method\":\"StartStreaming\",\"parameters\":{}}");
        } else {
        sprintf(command, "{\"name\":\"Camera\",\"method\":\"StopStreaming\",\"parameters\":{}}");
    }
    
    // 解析并调用方法
    cJSON* json = cJSON_Parse(command);
    if (json) {
        thing_manager.Invoke(json);
        cJSON_Delete(json);
    } else {
        ESP_LOGE(TAG, "Failed to create JSON command");
    }
} 