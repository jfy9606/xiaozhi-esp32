// Copyright 2023-2024 Espressif Systems
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <memory>
#include <cstring>

#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "board_config.h"
#include "../thing.h"
#include "ext/include/multiplexer.h"
#include "ext/include/pca9548a.h"

static constexpr char TAG[] = "Ultrasonic";

// Default configurations
#define DEFAULT_SAFE_DISTANCE_CM         15                  // Default safe distance in cm for obstacle detection
#define DEFAULT_MAX_DISTANCE_CM          400                 // Maximum measurable distance in cm
#define DEFAULT_MIN_DISTANCE_CM          2                   // Minimum reliable distance in cm
#define DEFAULT_MEASUREMENT_INTERVAL_MS  200                 // Interval between measurements in ms
#define SOUND_SPEED_CM_US                0.0343              // Speed of sound in cm/μs

#define US_TIMEOUT_MS                     25       // Maximum time to wait for echo
#define US_NO_OBSTACLE_DISTANCE_CM        400      // Default distance when no obstacle detected

// Ultrasonic sensor GPIO pins
#define US_FRONT_TRIG_PIN                 21  // Default trigger pin for front sensor
#define US_FRONT_ECHO_PIN                 22  // Default echo pin for front sensor
#define US_REAR_TRIG_PIN                  23  // Default trigger pin for rear sensor 
#define US_REAR_ECHO_PIN                  24  // Default echo pin for rear sensor

// Multiplexer channels
#define FRONT_CHANNEL PCA9548A_CHANNEL_0  // 前超声波传感器使用通道0
#define REAR_CHANNEL  PCA9548A_CHANNEL_1  // 后超声波传感器使用通道1

namespace iot {

class US : public Thing {
public:
    US() : Thing("US", "Ultrasonic distance sensor") {
        // Add properties
        properties_.AddNumberProperty("front_distance", "Front distance in cm", [this]() -> float {
            return front_distance_;
        });
        
        properties_.AddNumberProperty("rear_distance", "Rear distance in cm", [this]() -> float {
            return rear_distance_;
        });
        
        properties_.AddBooleanProperty("front_obstacle_detected", "Front obstacle detected", [this]() -> bool {
            return front_obstacle_detected_;
        });
        
        properties_.AddBooleanProperty("rear_obstacle_detected", "Rear obstacle detected", [this]() -> bool {
            return rear_obstacle_detected_;
        });
        
        properties_.AddNumberProperty("front_safe_distance", "Front safe distance in cm", [this]() -> float {
            return front_safe_distance_;
        });
        
        properties_.AddNumberProperty("rear_safe_distance", "Rear safe distance in cm", [this]() -> float {
            return rear_safe_distance_;
        });
            
        // Add configuration method
        ParameterList configParams;
        configParams.AddParameter(Parameter("front_safe_distance", "Front safety distance threshold in cm", kValueTypeNumber));
        configParams.AddParameter(Parameter("rear_safe_distance", "Rear safety distance threshold in cm", kValueTypeNumber));
        configParams.AddParameter(Parameter("max_distance", "Maximum measurable distance in cm", kValueTypeNumber));
        
        methods_.AddMethod("configure", "Configure the ultrasonic sensors", configParams,
            [this](const ParameterList& parameters) {
                // Update the safety distance thresholds
                try {
                    front_safe_distance_ = parameters["front_safe_distance"].number();
                } catch(...) {
                    // Keep the default if not provided
                }
                
                try {
                    rear_safe_distance_ = parameters["rear_safe_distance"].number();
                } catch(...) {
                    // Keep the default if not provided
                }
                
                try {
                    max_distance_ = parameters["max_distance"].number();
                } catch(...) {
                    // Keep the default if not provided
                    }
                    
                if (front_safe_distance_ <= 0) front_safe_distance_ = DEFAULT_SAFE_DISTANCE_CM;
                if (rear_safe_distance_ <= 0) rear_safe_distance_ = DEFAULT_SAFE_DISTANCE_CM;
                if (max_distance_ <= 0) max_distance_ = DEFAULT_MAX_DISTANCE_CM;
                
                ESP_LOGI(TAG, "Configured front_safe_distance: %.2f, rear_safe_distance: %.2f, max_distance: %.2f", 
                    front_safe_distance_, rear_safe_distance_, max_distance_);
                
                return true;
            });
    }

    ~US() {
        DeInit();
    }

    void Init() {
        ESP_LOGI(TAG, "Initializing US ultrasonic sensors");
        
#ifdef CONFIG_US_CONNECTION_PCA9548A
        // 检查PCA9548A多路复用器是否已初始化
        if (!pca9548a_is_initialized()) {
            ESP_LOGE(TAG, "PCA9548A multiplexer is not enabled, but ultrasonic sensors are configured to use it");
            ESP_LOGE(TAG, "Please enable PCA9548A in menuconfig or change ultrasonic sensor connection type");
            return;
        }
        ESP_LOGI(TAG, "Using PCA9548A multiplexer for ultrasonic sensors");
        
        // 初始化时先选择一个默认的通道，避免通道冲突
        esp_err_t ret = pca9548a_select_channel(PCA9548A_CHANNEL_NONE);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to clear PCA9548A channels: %s", esp_err_to_name(ret));
            // 继续执行，不阻断程序
        }
#else
        ESP_LOGI(TAG, "Using direct GPIO connection for ultrasonic sensors");
#endif
        
        // Apply default values if needed
        if (front_safe_distance_ <= 0) front_safe_distance_ = DEFAULT_SAFE_DISTANCE_CM;
        if (rear_safe_distance_ <= 0) rear_safe_distance_ = DEFAULT_SAFE_DISTANCE_CM;
        if (max_distance_ <= 0) max_distance_ = DEFAULT_MAX_DISTANCE_CM;
        
        ESP_LOGI(TAG, "Using front_safe_distance: %.2f, rear_safe_distance: %.2f, max_distance: %.2f", 
                 front_safe_distance_, rear_safe_distance_, max_distance_);
        
        // Configure GPIO pins for front sensor
        gpio_config_t io_conf = {};
        
        // Configure front sensor trigger pin as output
        io_conf.pin_bit_mask = (1ULL << US_FRONT_TRIG_PIN);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        
        // Configure front sensor echo pin as input
        io_conf.pin_bit_mask = (1ULL << US_FRONT_ECHO_PIN);
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        
        // Configure rear sensor trigger pin as output
        io_conf.pin_bit_mask = (1ULL << US_REAR_TRIG_PIN);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        
        // Configure rear sensor echo pin as input
        io_conf.pin_bit_mask = (1ULL << US_REAR_ECHO_PIN);
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        
        // Set initial pin states
        gpio_set_level((gpio_num_t)US_FRONT_TRIG_PIN, 0);
        gpio_set_level((gpio_num_t)US_REAR_TRIG_PIN, 0);
        
        // Create task for periodic measurements
        xTaskCreate(
            MeasurementTask,
            "US_task",
            4096,  // Increased stack size for handling two sensors
            this,
            5,
            &measurement_task_handle_);
            
        initialized_ = true;
        ESP_LOGI(TAG, "Ultrasonic sensors initialized successfully");
    }
    
    void DeInit() {
        // Stop measurement task if running
        if (measurement_task_handle_ != nullptr) {
            vTaskDelete(measurement_task_handle_);
            measurement_task_handle_ = nullptr;
        }
        initialized_ = false;
    }

private:
    static void MeasurementTask(void* pvParameters) {
        US* us = static_cast<US*>(pvParameters);
        
        // Main measurement loop
        while (1) {
            // Measure front distance
            float front_dist = 0;
            bool front_success = us->MeasureDistance(FRONT_CHANNEL, US_FRONT_TRIG_PIN, US_FRONT_ECHO_PIN, &front_dist);
            
            if (front_success) {
                us->front_distance_ = front_dist;
                us->front_obstacle_detected_ = front_dist < us->front_safe_distance_;
                ESP_LOGD(TAG, "Front Distance: %.2f cm, Obstacle: %d", 
                         us->front_distance_, us->front_obstacle_detected_);
            } else {
                ESP_LOGW(TAG, "Front measurement failed");
            }
            
            // Small delay between measurements
            vTaskDelay(pdMS_TO_TICKS(10));
            
            // Measure rear distance
            float rear_dist = 0;
            bool rear_success = us->MeasureDistance(REAR_CHANNEL, US_REAR_TRIG_PIN, US_REAR_ECHO_PIN, &rear_dist);
            
            if (rear_success) {
                us->rear_distance_ = rear_dist;
                us->rear_obstacle_detected_ = rear_dist < us->rear_safe_distance_;
                ESP_LOGD(TAG, "Rear Distance: %.2f cm, Obstacle: %d", 
                         us->rear_distance_, us->rear_obstacle_detected_);
            } else {
                ESP_LOGW(TAG, "Rear measurement failed");
            }
            
            // Wait for the next measurement period
            vTaskDelay(pdMS_TO_TICKS(180));  // 5Hz measurement rate per sensor (200ms total cycle)
        }
    }
    
    bool MeasureDistance(uint8_t mux_channel, int trig_pin, int echo_pin, float* distance) {
        if (distance == nullptr) {
            return false;
        }
        
#ifdef CONFIG_US_CONNECTION_PCA9548A
        // 切换到超声波传感器对应的通道
        ESP_LOGD(TAG, "Switching to PCA9548A channel: 0x%02X for echo/trig pins: %d/%d", 
                mux_channel, echo_pin, trig_pin);
                
        // 获取PCA9548A句柄进行直接通道读取
        pca9548a_handle_t pca_handle = pca9548a_get_handle();
        if (pca_handle == NULL) {
            ESP_LOGE(TAG, "PCA9548A handle is NULL, cannot switch channel");
            return false;
        }
        
        // 先关闭所有通道，避免冲突
        esp_err_t ret_clear = pca9548a_select_channel(PCA9548A_CHANNEL_NONE);
        if (ret_clear != ESP_OK) {
            ESP_LOGW(TAG, "Failed to clear PCA9548A channels before selection: %s", 
                    esp_err_to_name(ret_clear));
            // 继续尝试，不阻断
        }
        
        // 短暂延时以确保通道清除
        vTaskDelay(pdMS_TO_TICKS(5));
        
        // 现在选择所需通道
        esp_err_t ret = pca9548a_select_channel(mux_channel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to select PCA9548A channel %d: %s", 
                    mux_channel, esp_err_to_name(ret));
            
            // 重试一次
            vTaskDelay(pdMS_TO_TICKS(10)); // 等待更长时间
            ret = pca9548a_select_channel(mux_channel);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed again to select PCA9548A channel %d: %s", 
                        mux_channel, esp_err_to_name(ret));
                return false;
            }
        }
        
        // 等待通道切换生效 - 增加延时
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // 读取当前通道确认已选择
        uint8_t current_channel = 0;
        esp_err_t ret_read = pca9548a_get_selected_channels(pca_handle, &current_channel);
        if (ret_read == ESP_OK) {
            if (current_channel != mux_channel) {
                ESP_LOGW(TAG, "Channel mismatch: requested=0x%02X, actual=0x%02X", 
                        mux_channel, current_channel);
                // 尝试再次切换
                pca9548a_select_channel(mux_channel);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
#endif
        
        // Trigger pulse
        gpio_set_level((gpio_num_t)trig_pin, 0);
        // Short delay - busy wait for few microseconds
        int64_t start_us = esp_timer_get_time();
        while ((esp_timer_get_time() - start_us) < 2) { }
        
        gpio_set_level((gpio_num_t)trig_pin, 1);
        // Short delay - busy wait for 10 microseconds
        start_us = esp_timer_get_time();
        while ((esp_timer_get_time() - start_us) < 10) { }
        
        gpio_set_level((gpio_num_t)trig_pin, 0);
        
        // Wait for echo start
        int64_t start_time = esp_timer_get_time();
        while (gpio_get_level((gpio_num_t)echo_pin) == 0) {
            if ((esp_timer_get_time() - start_time) > US_TIMEOUT_MS * 1000) {
                ESP_LOGD(TAG, "Echo start timeout on channel %d", mux_channel);
                return false;  // Timeout waiting for echo
            }
        }
        
        // Measure echo duration
        start_time = esp_timer_get_time();
        while (gpio_get_level((gpio_num_t)echo_pin) == 1) {
            if ((esp_timer_get_time() - start_time) > US_TIMEOUT_MS * 1000) {
                *distance = max_distance_;  // Maximum distance if timeout
                ESP_LOGD(TAG, "Echo end timeout on channel %d, setting max distance", mux_channel);
                return true;
            }
        }
        
        int64_t end_time = esp_timer_get_time();
        int64_t duration_us = end_time - start_time;
        
        // Calculate distance (speed of sound = 343m/s = 34300cm/s)
        // distance = (time * speed) / 2 (round trip)
        // 34300 / 2 = 17150cm/s
        // distance(cm) = duration_us * 0.017150
        *distance = duration_us * 0.017150f;
        
        // Limit to maximum measurable distance
        if (*distance > max_distance_) {
            *distance = max_distance_;
        }
        
        return true;
    }
    
    // Configuration
    float front_safe_distance_ = DEFAULT_SAFE_DISTANCE_CM;
    float rear_safe_distance_ = DEFAULT_SAFE_DISTANCE_CM;
    float max_distance_ = DEFAULT_MAX_DISTANCE_CM;
    
    // State
    float front_distance_ = 0;
    float rear_distance_ = 0;
    bool front_obstacle_detected_ = false;
    bool rear_obstacle_detected_ = false;
    bool initialized_ = false;
    
    // Task handle
    TaskHandle_t measurement_task_handle_ = nullptr;
};

// Singleton instance
static std::unique_ptr<US> us_instance;

// Function to create the ultrasonic sensor Thing instance
static iot::Thing* CreateUS() {
    us_instance = std::make_unique<US>();
    return us_instance.get();
}

void RegisterUS() {
    iot::RegisterThing("US", CreateUS);
}

} // namespace iot 

DECLARE_THING(US);
