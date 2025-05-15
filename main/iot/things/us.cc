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
static constexpr char TAG[] = "Ultrasonic";

// Default configuration
#ifdef CONFIG_US_TRIG_PIN
#define DEFAULT_TRIG_PIN                 CONFIG_US_TRIG_PIN  // Default trigger pin from Kconfig
#else
#define DEFAULT_TRIG_PIN                 21  // Default trigger pin if not configured
#endif

#ifdef CONFIG_US_ECHO_PIN
#define DEFAULT_ECHO_PIN                 CONFIG_US_ECHO_PIN  // Default echo pin from Kconfig
#else
#define DEFAULT_ECHO_PIN                 22  // Default echo pin if not configured
#endif

#define DEFAULT_SAFE_DISTANCE_CM         15                  // Default safe distance in cm for obstacle detection
#define DEFAULT_MAX_DISTANCE_CM          400                 // Maximum measurable distance in cm
#define DEFAULT_MIN_DISTANCE_CM          2                   // Minimum reliable distance in cm
#define DEFAULT_MEASUREMENT_INTERVAL_MS  200                 // Interval between measurements in ms
#define SOUND_SPEED_CM_US                0.0343              // Speed of sound in cm/μs

#define US_TIMEOUT_MS                     25       // Maximum time to wait for echo
#define US_NO_OBSTACLE_DISTANCE_CM        400      // Default distance when no obstacle detected

namespace iot {

class US : public Thing {
public:
    US() : Thing("us", "Ultrasonic distance sensor") {
        // Setup properties for the US sensor
        properties_.AddNumberProperty("distance", "Current distance in cm", 
            [this]() { return static_cast<int>(current_distance_); });
        
        properties_.AddBooleanProperty("obstacle", "Obstacle detected", 
            [this]() { return obstacle_detected_; });
        
        properties_.AddNumberProperty("safe_distance", "Safe distance threshold in cm",
            [this]() { return static_cast<int>(safe_distance_); });
            
        // Add configuration method
        ParameterList configParams;
        configParams.AddParameter(Parameter("trig_pin", "Trigger pin", kValueTypeNumber, false));
        configParams.AddParameter(Parameter("echo_pin", "Echo pin", kValueTypeNumber, false));
        configParams.AddParameter(Parameter("safe_distance", "Safe distance in cm", kValueTypeNumber, false));
        configParams.AddParameter(Parameter("max_distance", "Maximum distance in cm", kValueTypeNumber, false));
        
        methods_.AddMethod("configure", "Configure the ultrasonic sensor", configParams,
            [this](const ParameterList& params) {
                // Update configuration if parameters are provided
                try {
                    // Set optional parameters if provided
                    bool updated = false;
                    
                    if (params["trig_pin"].type() == kValueTypeNumber) {
                        trig_pin_ = params["trig_pin"].number();
                        updated = true;
                    }
                    
                    if (params["echo_pin"].type() == kValueTypeNumber) {
                        echo_pin_ = params["echo_pin"].number();
                        updated = true;
                    }
                    
                    if (params["safe_distance"].type() == kValueTypeNumber) {
                        safe_distance_ = static_cast<float>(params["safe_distance"].number());
                        updated = true;
                    }
                    
                    if (params["max_distance"].type() == kValueTypeNumber) {
                        max_distance_ = static_cast<float>(params["max_distance"].number());
                        updated = true;
                    }
                    
                    // Re-initialize hardware if any parameters were updated
                    if (updated && initialized_) {
                        DeInit();
                        Init();
                    }
                } catch (const std::exception& e) {
                    ESP_LOGE(TAG, "Error in configure method: %s", e.what());
                }
            });
    }

    ~US() {
        DeInit();
    }

    void Init() {
        ESP_LOGI(TAG, "Initializing US ultrasonic sensor");
        
        // Set default pins
        if (trig_pin_ == -1) trig_pin_ = DEFAULT_TRIG_PIN;
        if (echo_pin_ == -1) echo_pin_ = DEFAULT_ECHO_PIN;
        if (safe_distance_ <= 0) safe_distance_ = DEFAULT_SAFE_DISTANCE_CM;
        if (max_distance_ <= 0) max_distance_ = DEFAULT_MAX_DISTANCE_CM;
        
        ESP_LOGI(TAG, "Using trig_pin: %d, echo_pin: %d", trig_pin_, echo_pin_);
        ESP_LOGI(TAG, "Using safe_distance: %.2f, max_distance: %.2f", safe_distance_, max_distance_);
        
        gpio_config_t io_conf = {};
        
        // Configure trigger pin as output
        io_conf.pin_bit_mask = (1ULL << trig_pin_);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        
        // Configure echo pin as input
        io_conf.pin_bit_mask = (1ULL << echo_pin_);
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        
        // Create task for periodic measurements
        xTaskCreate(
            MeasurementTask,
            "US_task",
            2048,
            this,
            5,
            &measurement_task_handle_);
            
        initialized_ = true;
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
            // Measure distance
            float distance = 0;
            bool success = us->MeasureDistance(&distance);
            
            if (success) {
                us->current_distance_ = distance;
                us->obstacle_detected_ = distance < us->safe_distance_;
                ESP_LOGD(TAG, "Distance: %.2f cm, Obstacle: %d", 
                         us->current_distance_, us->obstacle_detected_);
            } else {
                ESP_LOGW(TAG, "Measurement failed");
            }
            
            // Wait for the next measurement period
            vTaskDelay(pdMS_TO_TICKS(100));  // 10Hz measurement rate
        }
    }
    
    bool MeasureDistance(float* distance) {
        if (distance == nullptr) {
            return false;
        }
        
        // Trigger pulse
        gpio_set_level((gpio_num_t)trig_pin_, 0);
        // Short delay - busy wait for few microseconds
        int64_t start_us = esp_timer_get_time();
        while ((esp_timer_get_time() - start_us) < 2) { }
        
        gpio_set_level((gpio_num_t)trig_pin_, 1);
        // Short delay - busy wait for 10 microseconds
        start_us = esp_timer_get_time();
        while ((esp_timer_get_time() - start_us) < 10) { }
        
        gpio_set_level((gpio_num_t)trig_pin_, 0);
        
        // Wait for echo start
        int64_t start_time = esp_timer_get_time();
        while (gpio_get_level((gpio_num_t)echo_pin_) == 0) {
            if ((esp_timer_get_time() - start_time) > US_TIMEOUT_MS * 1000) {
                return false;  // Timeout waiting for echo
            }
        }
        
        // Measure echo duration
        start_time = esp_timer_get_time();
        while (gpio_get_level((gpio_num_t)echo_pin_) == 1) {
            if ((esp_timer_get_time() - start_time) > US_TIMEOUT_MS * 1000) {
                *distance = max_distance_;  // Maximum distance if timeout
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
    int trig_pin_ = -1;
    int echo_pin_ = -1;
    float safe_distance_ = DEFAULT_SAFE_DISTANCE_CM;
    float max_distance_ = DEFAULT_MAX_DISTANCE_CM;
    
    // State
    float current_distance_ = 0;
    bool obstacle_detected_ = false;
    bool initialized_ = false;
    TaskHandle_t measurement_task_handle_ = nullptr;
};

// Register thing with factory
DECLARE_THING(US)

}  // namespace iot 
