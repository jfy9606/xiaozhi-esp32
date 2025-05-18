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

// Multiplexer channels for ultrasonic sensors
#define US_FRONT_CHANNEL                  PCA9548A_CHANNEL_0  // Front ultrasonic sensor on channel 0 (SD0/SA0)
#define US_REAR_CHANNEL                   PCA9548A_CHANNEL_1  // Rear ultrasonic sensor on channel 1 (SD1/SA1)

// PCA9548A 多路复用器管理
static bool multiplexer_initialized = false;
static int current_active_channel = PCA9548A_CHANNEL_NONE;

// 互斥锁，用于保护PCA9548A通道切换操作
static SemaphoreHandle_t mux_mutex = NULL;

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
        
        // 创建PCA9548A操作的互斥锁，如果尚未创建
        if (mux_mutex == NULL) {
            mux_mutex = xSemaphoreCreateMutex();
            if (mux_mutex == NULL) {
                ESP_LOGE(TAG, "Failed to create multiplexer mutex");
                return;
            }
        }
        
#ifdef CONFIG_US_CONNECTION_PCA9548A
        if (!multiplexer_initialized) {
            // 检查PCA9548A多路复用器是否已初始化
            if (!pca9548a_is_initialized()) {
                ESP_LOGE(TAG, "PCA9548A multiplexer is not enabled, but ultrasonic sensors are configured to use it");
                ESP_LOGI(TAG, "Attempting to initialize PCA9548A on I2C bus 0 (GPIO41/42)");
                
                // 初始化PCA9548A - GPIO41/42已经连接了显示屏和PCA9548A，使用I2C总线0
                esp_err_t ret = multiplexer_init_with_i2c_port(0);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to initialize PCA9548A: %s", esp_err_to_name(ret));
                    ESP_LOGE(TAG, "Distance measurements will not work");
                    return;
                }
                
                // 等待I2C总线稳定
                vTaskDelay(pdMS_TO_TICKS(200));
                multiplexer_initialized = true;
                ESP_LOGI(TAG, "Successfully initialized PCA9548A multiplexer");
            } else {
                multiplexer_initialized = true;
                ESP_LOGI(TAG, "PCA9548A multiplexer was already initialized");
            }
        }
        
        ESP_LOGI(TAG, "Using PCA9548A multiplexer for ultrasonic sensors");
        
        // 初始化时先清除所有通道，避免通道冲突
        if (xSemaphoreTake(mux_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {  // 增加超时时间
            esp_err_t ret = pca9548a_select_channel(PCA9548A_CHANNEL_NONE);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to clear PCA9548A channels: %s", esp_err_to_name(ret));
            } else {
                ESP_LOGI(TAG, "Successfully cleared PCA9548A channels");
                current_active_channel = PCA9548A_CHANNEL_NONE;
            }
            xSemaphoreGive(mux_mutex);
            
            // 等待一段时间，确保I2C总线稳定
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            ESP_LOGW(TAG, "Failed to acquire multiplexer mutex for initialization");
            // 尝试强制重置
            esp_err_t ret = multiplexer_reset();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to reset multiplexer: %s", esp_err_to_name(ret));
            } else {
                ESP_LOGI(TAG, "Successfully reset multiplexer");
                vTaskDelay(pdMS_TO_TICKS(200));
            }
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
        
        // Create task for periodic measurements with higher stack size for safety
        xTaskCreate(
            MeasurementTask,
            "US_task",
            5120,  // 增加堆栈大小以避免溢出
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
        
        // 清除PCA9548A通道
        if (multiplexer_initialized && mux_mutex != NULL) {
            if (xSemaphoreTake(mux_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                pca9548a_select_channel(PCA9548A_CHANNEL_NONE);
                xSemaphoreGive(mux_mutex);
            }
        }
        
        initialized_ = false;
    }

private:
    static void MeasurementTask(void* pvParameters) {
        US* us = static_cast<US*>(pvParameters);
        int retry_count = 0;
        const int max_retries = 5;
        int consecutive_failures = 0;
        const int max_consecutive_failures = 10;
        
        // 延迟启动，确保其他组件已初始化完毕
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        ESP_LOGI(TAG, "Ultrasonic measurement task started");
        
        // 先尝试清除所有通道，确保开始状态正确
        if (mux_mutex != NULL && multiplexer_initialized) {
            if (xSemaphoreTake(mux_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                esp_err_t ret = pca9548a_select_channel(PCA9548A_CHANNEL_NONE);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Initial channel clear failed: %s", esp_err_to_name(ret));
                    // 尝试复位PCA9548A
                    ret = multiplexer_reset();
                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG, "PCA9548A reset successful");
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                    ret = pca9548a_select_channel(PCA9548A_CHANNEL_NONE);
                }
                current_active_channel = PCA9548A_CHANNEL_NONE;
                xSemaphoreGive(mux_mutex);
                vTaskDelay(pdMS_TO_TICKS(100));  // 给I2C总线一些稳定时间
            } else {
                ESP_LOGW(TAG, "Failed to acquire mutex for initialization");
            }
        }
        
        // Main measurement loop
        while (1) {
            // 检查连续失败次数，如果太多就重置多路复用器
            if (consecutive_failures >= max_consecutive_failures) {
                ESP_LOGE(TAG, "Too many consecutive measurement failures (%d), resetting multiplexer", 
                        consecutive_failures);
                
#ifdef CONFIG_US_CONNECTION_PCA9548A
                // 尝试重置PCA9548A
                if (multiplexer_initialized) {
                    esp_err_t ret = multiplexer_reset();
                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG, "PCA9548A reset successful");
                        // 等待重置完成
                        vTaskDelay(pdMS_TO_TICKS(300));
                        current_active_channel = PCA9548A_CHANNEL_NONE;
                    } else {
                        ESP_LOGW(TAG, "PCA9548A reset failed: %s", esp_err_to_name(ret));
                    }
                }
#endif
                consecutive_failures = 0;
                vTaskDelay(pdMS_TO_TICKS(500)); // 给系统一些恢复时间
                continue;
            }
            
            // Measure front distance
            float front_dist = 0;
            bool front_success = us->MeasureDistance(US_FRONT_CHANNEL, US_FRONT_TRIG_PIN, US_FRONT_ECHO_PIN, &front_dist);
            
            if (front_success) {
                us->front_distance_ = front_dist;
                us->front_obstacle_detected_ = front_dist < us->front_safe_distance_;
                ESP_LOGI(TAG, "Front Distance: %.2f cm, Obstacle: %d", 
                         us->front_distance_, us->front_obstacle_detected_);
                retry_count = 0; // Reset retry counter on success
                consecutive_failures = 0; // 重置连续失败计数
            } else {
                ESP_LOGW(TAG, "Front measurement failed (retry %d/%d)", retry_count + 1, max_retries);
                retry_count++;
                consecutive_failures++;
                
                if (retry_count > max_retries) {
                    ESP_LOGE(TAG, "Exceeded maximum retries, resetting multiplexer");
                    
#ifdef CONFIG_US_CONNECTION_PCA9548A
                    // 尝试重置PCA9548A
                    if (mux_mutex != NULL) {
                        if (xSemaphoreTake(mux_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                            // 清除所有通道
                            esp_err_t ret = pca9548a_select_channel(PCA9548A_CHANNEL_NONE);
                            if (ret != ESP_OK) {
                                ESP_LOGW(TAG, "Failed to clear channels during reset: %s", esp_err_to_name(ret));
                            }
                            current_active_channel = PCA9548A_CHANNEL_NONE;
                            xSemaphoreGive(mux_mutex);
                            
                            // 给其他设备一些时间访问I2C总线
                            vTaskDelay(pdMS_TO_TICKS(500));
                        } else {
                            ESP_LOGW(TAG, "Failed to acquire multiplexer mutex for reset");
                            vTaskDelay(pdMS_TO_TICKS(100));
                        }
                    }
#endif
                    retry_count = 0;
                    // 跳过这次循环的剩余部分，下次重试
                    vTaskDelay(pdMS_TO_TICKS(200));
                    continue;
                }
            }
            
            // Small delay between measurements
            vTaskDelay(pdMS_TO_TICKS(30));
            
            // Measure rear distance
            float rear_dist = 0;
            bool rear_success = us->MeasureDistance(US_REAR_CHANNEL, US_REAR_TRIG_PIN, US_REAR_ECHO_PIN, &rear_dist);
            
            if (rear_success) {
                us->rear_distance_ = rear_dist;
                us->rear_obstacle_detected_ = rear_dist < us->rear_safe_distance_;
                ESP_LOGI(TAG, "Rear Distance: %.2f cm, Obstacle: %d", 
                         us->rear_distance_, us->rear_obstacle_detected_);
                consecutive_failures = 0; // 重置连续失败计数
            } else {
                ESP_LOGW(TAG, "Rear measurement failed");
                consecutive_failures++;
            }
            
            // Wait for the next measurement period,给其他设备足够时间使用I2C
            vTaskDelay(pdMS_TO_TICKS(170));  // 约200ms总周期，每秒5次测量
        }
    }
    
    bool MeasureDistance(uint8_t mux_channel, int trig_pin, int echo_pin, float* distance) {
        if (distance == nullptr) {
            return false;
        }
        
#ifdef CONFIG_US_CONNECTION_PCA9548A
        bool channel_switched = false;
        
        // 获取互斥锁并切换通道
        if (mux_mutex != NULL && multiplexer_initialized) {
            if (xSemaphoreTake(mux_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {  // 增加超时时间
                // 只有在当前通道与所需通道不同时才切换
                if (current_active_channel != mux_channel) {
                    ESP_LOGD(TAG, "Switching multiplexer from channel %d to %d", 
                            current_active_channel, mux_channel);
                    
                    // 先切换到NONE，然后再切换到目标通道，这样更可靠
                    if (current_active_channel != PCA9548A_CHANNEL_NONE) {
                        esp_err_t clear_ret = pca9548a_select_channel(PCA9548A_CHANNEL_NONE);
                        if (clear_ret != ESP_OK) {
                            ESP_LOGW(TAG, "Failed to clear channel before switching: %s", esp_err_to_name(clear_ret));
                        }
                        // 给总线一些时间稳定
                        vTaskDelay(pdMS_TO_TICKS(5));
                    }
                    
                    // 现在切换到目标通道
                    esp_err_t ret = pca9548a_select_channel(mux_channel);
                    if (ret == ESP_OK) {
                        current_active_channel = mux_channel;
                        channel_switched = true;
                        
                        // 给足够的切换时间
                        vTaskDelay(pdMS_TO_TICKS(20));
                    } else {
                        ESP_LOGE(TAG, "Failed to select PCA9548A channel %d: %s", 
                                mux_channel, esp_err_to_name(ret));
                        
                        // 尝试恢复到安全状态
                        pca9548a_select_channel(PCA9548A_CHANNEL_NONE);
                        current_active_channel = PCA9548A_CHANNEL_NONE;
                        
                        xSemaphoreGive(mux_mutex);
                        return false;
                    }
                } else {
                    // 通道已经是正确的，不需要切换
                    ESP_LOGD(TAG, "Multiplexer already on correct channel %d", mux_channel);
                    channel_switched = true;  // 标记为已切换，以便后续清理
                }
            } else {
                ESP_LOGW(TAG, "Failed to acquire multiplexer mutex for channel %d - I2C bus may be busy", mux_channel);
                return false;
            }
        } else {
            ESP_LOGW(TAG, "Multiplexer mutex or initialization issue");
            return false;
        }
#endif
        
        // 触发超声波测量
        gpio_set_level((gpio_num_t)trig_pin, 0);
        // 短暂延迟 - 忙等待几微秒
        int64_t start_us = esp_timer_get_time();
        while ((esp_timer_get_time() - start_us) < 5) { }  // 增加延迟
        
        gpio_set_level((gpio_num_t)trig_pin, 1);
        // 短暂延迟 - 忙等待10微秒
        start_us = esp_timer_get_time();
        while ((esp_timer_get_time() - start_us) < 15) { }  // 增加触发时间
        
        gpio_set_level((gpio_num_t)trig_pin, 0);
        
        // 等待回波开始
        int64_t start_time = esp_timer_get_time();
        bool echo_detected = false;
        
        // 添加适当的超时
        while ((esp_timer_get_time() - start_time) <= US_TIMEOUT_MS * 1000) {
            if (gpio_get_level((gpio_num_t)echo_pin) == 1) {
                echo_detected = true;
                break;
            }
        }
        
        if (!echo_detected) {
            ESP_LOGD(TAG, "Echo start timeout on channel %d", mux_channel);
            
#ifdef CONFIG_US_CONNECTION_PCA9548A
            // 释放互斥锁，并切回到无通道状态
            if (channel_switched && mux_mutex != NULL) {
                pca9548a_select_channel(PCA9548A_CHANNEL_NONE);
                current_active_channel = PCA9548A_CHANNEL_NONE;
                xSemaphoreGive(mux_mutex);
            }
#endif
            *distance = max_distance_; // 设置为最大距离表示无障碍物
            return false;  // 超时等待回波
        }
        
        // 测量回波时长
        start_time = esp_timer_get_time();
        while (gpio_get_level((gpio_num_t)echo_pin) == 1) {
            if ((esp_timer_get_time() - start_time) > US_TIMEOUT_MS * 1000) {
                *distance = max_distance_;  // 超时时设置为最大距离
                ESP_LOGD(TAG, "Echo end timeout on channel %d, setting max distance", mux_channel);
                
#ifdef CONFIG_US_CONNECTION_PCA9548A
                // 释放互斥锁，并切回到无通道状态
                if (channel_switched && mux_mutex != NULL) {
                    pca9548a_select_channel(PCA9548A_CHANNEL_NONE);
                    current_active_channel = PCA9548A_CHANNEL_NONE;
                    xSemaphoreGive(mux_mutex);
                }
#endif
                return true;
            }
        }
        
        int64_t end_time = esp_timer_get_time();
        int64_t duration_us = end_time - start_time;
        
        // 计算距离 (声速 = 343m/s = 34300cm/s)
        // 距离 = (时间 * 速度) / 2 (往返)
        // 34300 / 2 = 17150cm/s
        // 距离(cm) = duration_us * 0.017150
        float calculated_distance = duration_us * SOUND_SPEED_CM_US / 2.0f;
        
        // 限制到可测量的最大距离和最小距离
        if (calculated_distance > max_distance_) {
            calculated_distance = max_distance_;
        } else if (calculated_distance < DEFAULT_MIN_DISTANCE_CM) {
            calculated_distance = DEFAULT_MIN_DISTANCE_CM;
        }
        
        *distance = calculated_distance;
        
#ifdef CONFIG_US_CONNECTION_PCA9548A
        // 每次测量后清除通道，释放I2C总线给其他设备
        if (channel_switched && mux_mutex != NULL) {
            esp_err_t ret = pca9548a_select_channel(PCA9548A_CHANNEL_NONE);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to clear channel after measurement: %s", esp_err_to_name(ret));
            }
            current_active_channel = PCA9548A_CHANNEL_NONE;
            xSemaphoreGive(mux_mutex);
        }
#endif
        
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
    us_instance->Init(); // 自动初始化实例
    return us_instance.get();
}

void RegisterUS() {
    iot::RegisterThing("US", CreateUS);
}

} // namespace iot 

DECLARE_THING(US);
