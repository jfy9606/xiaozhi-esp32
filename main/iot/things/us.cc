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
#include "ext/include/pcf8575.h"

static constexpr char TAG[] = "Ultrasonic";

// Default configurations
#define DEFAULT_SAFE_DISTANCE_CM         15                  // Default safe distance in cm for obstacle detection
#define DEFAULT_MAX_DISTANCE_CM          400                 // Maximum measurable distance in cm
#define DEFAULT_MIN_DISTANCE_CM          2                   // Minimum reliable distance in cm
#define DEFAULT_MEASUREMENT_INTERVAL_MS  200                 // Interval between measurements in ms
#define SOUND_SPEED_CM_US                0.0343              // Speed of sound in cm/μs

#define US_TIMEOUT_MS                    25       // Maximum time to wait for echo
#define US_NO_OBSTACLE_DISTANCE_CM       400      // Default distance when no obstacle detected

// Ultrasonic sensor GPIO pins for direct connection
#ifdef CONFIG_US_CONNECTION_DIRECT
#define US_FRONT_TRIG_PIN                CONFIG_US_FRONT_TRIG_PIN
#define US_FRONT_ECHO_PIN                CONFIG_US_FRONT_ECHO_PIN
#define US_REAR_TRIG_PIN                 CONFIG_US_REAR_TRIG_PIN
#define US_REAR_ECHO_PIN                 CONFIG_US_REAR_ECHO_PIN
#endif

// PCF8575 pins for ultrasonic sensors
#ifdef CONFIG_US_CONNECTION_PCF8575
#define US_PCF8575_FRONT_TRIG_PIN        CONFIG_US_PCF8575_FRONT_TRIG_PIN
#define US_PCF8575_FRONT_ECHO_PIN        CONFIG_US_PCF8575_FRONT_ECHO_PIN
#define US_PCF8575_REAR_TRIG_PIN         CONFIG_US_PCF8575_REAR_TRIG_PIN
#define US_PCF8575_REAR_ECHO_PIN         CONFIG_US_PCF8575_REAR_ECHO_PIN
#endif

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
        
#ifdef CONFIG_US_CONNECTION_PCF8575
        // 检查PCF8575 GPIO扩展器是否已初始化
        if (!pcf8575_is_initialized()) {
            // 检查PCA9548A是否已初始化，因为PCF8575连接在PCA9548A上
            if (!pca9548a_is_initialized()) {
                ESP_LOGE(TAG, "PCA9548A multiplexer is not enabled, but ultrasonic sensors are configured to use PCF8575");
                ESP_LOGE(TAG, "Please enable PCA9548A and PCF8575 in menuconfig or change ultrasonic sensor connection type");
                return;
            }
            
            // 如果PCA9548A已初始化但PCF8575未初始化，尝试初始化PCF8575
            ESP_LOGI(TAG, "Initializing PCF8575 for ultrasonic sensors");
            esp_err_t ret = pcf8575_init();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to initialize PCF8575: %s", esp_err_to_name(ret));
                return;
            }
        }
        
        ESP_LOGI(TAG, "Using PCF8575 GPIO expander for ultrasonic sensors");
        
        // 获取PCF8575句柄
        pcf8575_handle_t pcf_handle = pcf8575_get_handle();
        if (pcf_handle == NULL) {
            ESP_LOGE(TAG, "Failed to get PCF8575 handle");
            return;
        }
        
        // 配置PCF8575上的超声波引脚
        ESP_LOGI(TAG, "Configuring PCF8575 pins for ultrasonic sensors");
        ESP_LOGI(TAG, "Front sensor - Trig: P%02d, Echo: P%02d", 
                US_PCF8575_FRONT_TRIG_PIN, US_PCF8575_FRONT_ECHO_PIN);
        ESP_LOGI(TAG, "Rear sensor - Trig: P%02d, Echo: P%02d", 
                US_PCF8575_REAR_TRIG_PIN, US_PCF8575_REAR_ECHO_PIN);
        
        // 配置触发引脚为输出模式，回响引脚为输入模式
        pcf8575_set_gpio_mode(pcf_handle, (pcf8575_gpio_t)US_PCF8575_FRONT_TRIG_PIN, PCF8575_GPIO_MODE_OUTPUT);
        pcf8575_set_gpio_mode(pcf_handle, (pcf8575_gpio_t)US_PCF8575_FRONT_ECHO_PIN, PCF8575_GPIO_MODE_INPUT);
        pcf8575_set_gpio_mode(pcf_handle, (pcf8575_gpio_t)US_PCF8575_REAR_TRIG_PIN, PCF8575_GPIO_MODE_OUTPUT);
        pcf8575_set_gpio_mode(pcf_handle, (pcf8575_gpio_t)US_PCF8575_REAR_ECHO_PIN, PCF8575_GPIO_MODE_INPUT);
        
        // 设置触发引脚的初始电平为低
        pcf8575_set_gpio_level(pcf_handle, (pcf8575_gpio_t)US_PCF8575_FRONT_TRIG_PIN, 0);
        pcf8575_set_gpio_level(pcf_handle, (pcf8575_gpio_t)US_PCF8575_REAR_TRIG_PIN, 0);
#else
        ESP_LOGI(TAG, "Using direct GPIO connection for ultrasonic sensors");
        
        // 检查超声波引脚配置
        ESP_LOGI(TAG, "Ultrasonic sensor pins configuration:");
        ESP_LOGI(TAG, "Front sensor - Trig: %d, Echo: %d", US_FRONT_TRIG_PIN, US_FRONT_ECHO_PIN);
        ESP_LOGI(TAG, "Rear sensor - Trig: %d, Echo: %d", US_REAR_TRIG_PIN, US_REAR_ECHO_PIN);
        
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
#endif
        
        // Apply default values if needed
        if (front_safe_distance_ <= 0) front_safe_distance_ = DEFAULT_SAFE_DISTANCE_CM;
        if (rear_safe_distance_ <= 0) rear_safe_distance_ = DEFAULT_SAFE_DISTANCE_CM;
        if (max_distance_ <= 0) max_distance_ = DEFAULT_MAX_DISTANCE_CM;
        
        ESP_LOGI(TAG, "Using front_safe_distance: %.2f, rear_safe_distance: %.2f, max_distance: %.2f", 
                 front_safe_distance_, rear_safe_distance_, max_distance_);
        
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
#ifdef CONFIG_US_CONNECTION_PCF8575
            bool front_success = us->MeasurePCF8575Distance(US_PCF8575_FRONT_TRIG_PIN, US_PCF8575_FRONT_ECHO_PIN, &front_dist);
#else
            bool front_success = us->MeasureDirectDistance(US_FRONT_TRIG_PIN, US_FRONT_ECHO_PIN, &front_dist);
#endif
            
            if (front_success) {
                us->front_distance_ = front_dist;
                us->front_obstacle_detected_ = front_dist < us->front_safe_distance_;
                ESP_LOGD(TAG, "Front Distance: %.2f cm, Obstacle: %d", 
                         us->front_distance_, us->front_obstacle_detected_);
            } else {
                ESP_LOGW(TAG, "Front measurement failed");
            }
            
            // Small delay between measurements
            vTaskDelay(pdMS_TO_TICKS(30));
            
            // Measure rear distance
            float rear_dist = 0;
#ifdef CONFIG_US_CONNECTION_PCF8575
            bool rear_success = us->MeasurePCF8575Distance(US_PCF8575_REAR_TRIG_PIN, US_PCF8575_REAR_ECHO_PIN, &rear_dist);
#else
            bool rear_success = us->MeasureDirectDistance(US_REAR_TRIG_PIN, US_REAR_ECHO_PIN, &rear_dist);
#endif
            
            if (rear_success) {
                us->rear_distance_ = rear_dist;
                us->rear_obstacle_detected_ = rear_dist < us->rear_safe_distance_;
                ESP_LOGD(TAG, "Rear Distance: %.2f cm, Obstacle: %d", 
                         us->rear_distance_, us->rear_obstacle_detected_);
            } else {
                ESP_LOGW(TAG, "Rear measurement failed");
            }
            
            // Wait for the next measurement period
            vTaskDelay(pdMS_TO_TICKS(170));  // 5Hz measurement rate per sensor (200ms total cycle)
        }
    }
    
#ifdef CONFIG_US_CONNECTION_PCF8575
    bool MeasurePCF8575Distance(int trig_pin, int echo_pin, float* distance) {
        if (distance == nullptr) {
            return false;
        }
        
        pcf8575_handle_t pcf_handle = pcf8575_get_handle();
        if (pcf_handle == NULL) {
            ESP_LOGE(TAG, "PCF8575 handle is NULL");
            return false;
        }
        
        // 触发超声波
        pcf8575_set_gpio_level(pcf_handle, (pcf8575_gpio_t)trig_pin, 0);
        // 短暂延时
        int64_t start_us = esp_timer_get_time();
        while ((esp_timer_get_time() - start_us) < 5) { }  // 5us稳定延时
        
        pcf8575_set_gpio_level(pcf_handle, (pcf8575_gpio_t)trig_pin, 1);
        // 短暂延时
        start_us = esp_timer_get_time();
        while ((esp_timer_get_time() - start_us) < 15) { }  // 15us触发脉冲
        
        pcf8575_set_gpio_level(pcf_handle, (pcf8575_gpio_t)trig_pin, 0);
        
        // 等待回响开始
        uint8_t echo_level = 0;
        int64_t start_time = esp_timer_get_time();
        do {
            if ((esp_timer_get_time() - start_time) > US_TIMEOUT_MS * 1000) {
                ESP_LOGW(TAG, "Echo start timeout on pin P%d", echo_pin);
                return false;  // 等待回响超时
            }
            pcf8575_get_gpio_level(pcf_handle, (pcf8575_gpio_t)echo_pin, &echo_level);
        } while (echo_level == 0);
        
        // 测量回响持续时间
        start_time = esp_timer_get_time();
        do {
            if ((esp_timer_get_time() - start_time) > US_TIMEOUT_MS * 1000) {
                *distance = max_distance_;  // 如果超时则设为最大距离
                ESP_LOGD(TAG, "Echo end timeout on pin P%d, setting max distance", echo_pin);
                return true;
            }
            pcf8575_get_gpio_level(pcf_handle, (pcf8575_gpio_t)echo_pin, &echo_level);
        } while (echo_level == 1);
        
        int64_t end_time = esp_timer_get_time();
        int64_t duration_us = end_time - start_time;
        
        // 计算距离
        *distance = duration_us * 0.017150f;
        
        // 限制最大可测量距离
        if (*distance > max_distance_) {
            *distance = max_distance_;
        }
        
        ESP_LOGI(TAG, "PCF8575 pin P%d measured distance: %.2f cm", echo_pin, *distance);
        return true;
    }
#endif

#ifdef CONFIG_US_CONNECTION_DIRECT
    bool MeasureDirectDistance(int trig_pin, int echo_pin, float* distance) {
        if (distance == nullptr) {
            return false;
        }
        
        // 触发超声波
        gpio_set_level((gpio_num_t)trig_pin, 0);
        // 短暂延时
        int64_t start_us = esp_timer_get_time();
        while ((esp_timer_get_time() - start_us) < 5) { }  // 5us稳定延时
        
        gpio_set_level((gpio_num_t)trig_pin, 1);
        // 短暂延时
        start_us = esp_timer_get_time();
        while ((esp_timer_get_time() - start_us) < 15) { }  // 15us触发脉冲
        
        gpio_set_level((gpio_num_t)trig_pin, 0);
        
        // 等待回响开始
        int64_t start_time = esp_timer_get_time();
        while (gpio_get_level((gpio_num_t)echo_pin) == 0) {
            if ((esp_timer_get_time() - start_time) > US_TIMEOUT_MS * 1000) {
                ESP_LOGW(TAG, "Echo start timeout on pin %d", echo_pin);
                return false;  // 等待回响超时
            }
        }
        
        // 测量回响持续时间
        start_time = esp_timer_get_time();
        while (gpio_get_level((gpio_num_t)echo_pin) == 1) {
            if ((esp_timer_get_time() - start_time) > US_TIMEOUT_MS * 1000) {
                *distance = max_distance_;  // 如果超时则设为最大距离
                ESP_LOGD(TAG, "Echo end timeout on pin %d, setting max distance", echo_pin);
                return true;
            }
        }
        
        int64_t end_time = esp_timer_get_time();
        int64_t duration_us = end_time - start_time;
        
        // 计算距离
        *distance = duration_us * 0.017150f;
        
        // 限制最大可测量距离
        if (*distance > max_distance_) {
            *distance = max_distance_;
        }
        
        ESP_LOGI(TAG, "Direct GPIO pin %d measured distance: %.2f cm", echo_pin, *distance);
        return true;
    }
#endif
    
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
