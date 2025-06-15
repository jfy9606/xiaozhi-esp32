#include <memory>
#include <string>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <thread>
#include <atomic>

#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

#include "../boards/common/board.h"
#include "../thing.h"
#include "../thing_manager.h"
#include "ext/include/multiplexer.h"
#include "ext/include/pcf8575.h"

static constexpr char TAG[] = "Ultrasonic";

// Thing类型定义
#define XT_ULTRASONIC_SENSOR "ultrasonic_sensor"

// Default configurations
#define DEFAULT_SAFE_DISTANCE_CM         15                  // Default safe distance in cm for obstacle detection
#define DEFAULT_MAX_DISTANCE_CM          400                 // Maximum measurable distance in cm
#define DEFAULT_MIN_DISTANCE_CM          2                   // Minimum reliable distance in cm
#define DEFAULT_ECHO_TIMEOUT_US          25000               // Echo timeout in microseconds (for 400cm max distance)
#define DEFAULT_MEASURE_INTERVAL_MS      100                 // Measurement interval in ms

// Sensor constants
#define SC_SOUND_SPEED_M_S               343                 // Sound speed in m/s at 20°C
#define SC_MIN_DISTANCE_CM               DEFAULT_MIN_DISTANCE_CM
#define SC_MAX_DISTANCE_CM               DEFAULT_MAX_DISTANCE_CM
#define SC_ONE_ECHO_TIMEOUT_US           DEFAULT_ECHO_TIMEOUT_US
#define SC_IO_MODE_OUTPUT                1
#define SC_IO_MODE_INPUT                 0
#define SC_IO_LEVEL_HIGH                 1
#define SC_IO_LEVEL_LOW                  0

namespace iot {

enum ultrasonic_position_t {
    USP_FRONT = 0,
    USP_REAR,
    USP_LEFT,
    USP_RIGHT,
    USP_MAX
};

enum ultrasonic_connection_t {
    DIRECT_GPIO = 0,
    PCF8575_GPIO,
};

struct ultrasonic_sensor_config_t {
    ultrasonic_position_t position;
    ultrasonic_connection_t connection_type;
    union {
        struct {
            gpio_num_t trigger_pin;
            gpio_num_t echo_pin;
        } gpio;
        struct {
            pcf8575_handle_t handle;
            int trigger_pin;
            int echo_pin;
        } pcf8575;
    };
    float max_distance_cm;
    float min_distance_cm;
    uint32_t echo_timeout_us;
    bool is_initialized;
};

// 直接使用pcf8575.h中已经定义的函数来设置和读取引脚电平
static esp_err_t pcf8575_set_trigger(ultrasonic_sensor_config_t* sensor, uint32_t level) {
    if (!sensor || sensor->connection_type != PCF8575_GPIO) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return pcf8575_set_level(sensor->pcf8575.handle, sensor->pcf8575.trigger_pin, level);
}

static esp_err_t pcf8575_get_echo(ultrasonic_sensor_config_t* sensor, uint32_t* level) {
    if (!sensor || sensor->connection_type != PCF8575_GPIO || !level) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return pcf8575_get_level(sensor->pcf8575.handle, sensor->pcf8575.echo_pin, level);
}

class UltrasonicSensor : public Thing {
private:
    std::unique_ptr<ultrasonic_sensor_config_t[]> sensors_;
    size_t sensor_count_ = 0;
    float safe_distance_cm_ = DEFAULT_SAFE_DISTANCE_CM;
    bool front_obstacle_ = false;
    bool rear_obstacle_ = false;
    
    TaskHandle_t measure_task_handle_ = nullptr;
    esp_timer_handle_t measure_timer_handle_ = nullptr;
    SemaphoreHandle_t measure_mutex_ = nullptr;
    
    uint32_t measure_interval_ms_ = DEFAULT_MEASURE_INTERVAL_MS;
    
    static void measure_task(void* arg) {
        UltrasonicSensor* us = static_cast<UltrasonicSensor*>(arg);
        while (true) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            us->measure_all();
        }
    }
    
    static void measure_timer_callback(void* arg) {
        UltrasonicSensor* us = static_cast<UltrasonicSensor*>(arg);
        if (us->measure_task_handle_) {
            xTaskNotifyGive(us->measure_task_handle_);
        }
    }
    
    bool init_gpio_sensor(ultrasonic_sensor_config_t* sensor) {
        if (!sensor || sensor->connection_type != DIRECT_GPIO) {
            return false;
        }
        
        // Configure trigger pin as output
        gpio_config_t io_conf = {};
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << sensor->gpio.trigger_pin);
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        if (gpio_config(&io_conf) != ESP_OK) {
            return false;
        }
        
        // Configure echo pin as input
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << sensor->gpio.echo_pin);
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        if (gpio_config(&io_conf) != ESP_OK) {
            return false;
        }
        
        // Set initial level of trigger pin to LOW
        gpio_set_level(sensor->gpio.trigger_pin, 0);
        
        sensor->is_initialized = true;
        return true;
    }
    
    bool init_pcf8575_sensor(ultrasonic_sensor_config_t* sensor) {
        if (!sensor || sensor->connection_type != PCF8575_GPIO || !sensor->pcf8575.handle) {
            return false;
        }
        
        // Set trigger pin as output (LOW)
        esp_err_t err = pcf8575_set_level(sensor->pcf8575.handle, sensor->pcf8575.trigger_pin, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set trigger pin as output: %s", esp_err_to_name(err));
            return false;
        }
        
        sensor->is_initialized = true;
        return true;
    }
    
    float measure_distance_cm(ultrasonic_sensor_config_t* sensor) {
        if (!sensor || !sensor->is_initialized) {
            return -1;
        }
        
        esp_err_t err;
        uint32_t echo_level = 0;
        
        // 触发超声波测距过程
        if (sensor->connection_type == DIRECT_GPIO) {
            // 设置触发引脚为高电平
            gpio_set_level(sensor->gpio.trigger_pin, 1);
            // 保持高电平至少10us
            esp_rom_delay_us(10);
            // 设置触发引脚为低电平
            gpio_set_level(sensor->gpio.trigger_pin, 0);
        } else {
            // 使用PCF8575设置触发引脚
            err = pcf8575_set_trigger(sensor, 1);
            if (err != ESP_OK) {
                return -1;
            }
            // 保持高电平至少10us
            esp_rom_delay_us(10);
            // 设置触发引脚为低电平
            err = pcf8575_set_trigger(sensor, 0);
            if (err != ESP_OK) {
                return -1;
            }
        }
        
        // 等待回波引脚变为高电平
        int64_t start_time = esp_timer_get_time();
        while (true) {
            if (sensor->connection_type == DIRECT_GPIO) {
                echo_level = gpio_get_level(sensor->gpio.echo_pin);
            } else {
                err = pcf8575_get_echo(sensor, &echo_level);
                if (err != ESP_OK) {
                    return -1;
                }
            }
            
            if (echo_level == 1) break;
            
            // 检查是否超时（没有回波）
            if (esp_timer_get_time() - start_time > sensor->echo_timeout_us) {
                return -1;
            }
            
            esp_rom_delay_us(1);  // 小延迟防止占用过多CPU
        }
        
        // 记录回波开始时间
        int64_t echo_start = esp_timer_get_time();
        
        // 等待回波引脚变为低电平
        while (true) {
            if (sensor->connection_type == DIRECT_GPIO) {
                echo_level = gpio_get_level(sensor->gpio.echo_pin);
            } else {
                err = pcf8575_get_echo(sensor, &echo_level);
                if (err != ESP_OK) {
                    return -1;
                }
            }
            
            if (echo_level == 0) break;
            
            // 检查是否超时（回波时间过长）
            if (esp_timer_get_time() - echo_start > sensor->echo_timeout_us) {
                return -1;
            }
            
            esp_rom_delay_us(1);  // 小延迟防止占用过多CPU
        }
        
        // 计算回波持续时间（微秒）
        int64_t echo_duration = esp_timer_get_time() - echo_start;
        
        // 计算距离：距离 = (声速 × 时间) ÷ 2
        // 声速约为343米/秒，转换为厘米/微秒为0.0343厘米/微秒
        float distance_cm = static_cast<float>(echo_duration) * 0.0343 / 2.0;
        
        // 距离范围检查
        if (distance_cm < sensor->min_distance_cm || distance_cm > sensor->max_distance_cm) {
            return -1;
        }
        
        return distance_cm;
    }
    
    void measure_all() {
        if (!measure_mutex_) return;
        
        // 获取互斥锁
        if (xSemaphoreTake(measure_mutex_, pdMS_TO_TICKS(10)) == pdFALSE) {
            return;
        }
        
        bool new_front_obstacle = false;
        bool new_rear_obstacle = false;
        
        // 测量每个传感器的距离
        for (size_t i = 0; i < sensor_count_; i++) {
            float distance = measure_distance_cm(&sensors_[i]);
            
            // 记录障碍物状态
            if (distance > 0 && distance < safe_distance_cm_) {
                switch (sensors_[i].position) {
                    case USP_FRONT:
                        new_front_obstacle = true;
                        break;
                    case USP_REAR:
                        new_rear_obstacle = true;
                        break;
                    default:
                        break;
                }
            }
            
            // 记录日志（仅在值有效或状态改变时）
            if (distance > 0 || 
                (sensors_[i].position == USP_FRONT && front_obstacle_ != new_front_obstacle) || 
                (sensors_[i].position == USP_REAR && rear_obstacle_ != new_rear_obstacle)) {
                
                const char* pos_str = "unknown";
                switch (sensors_[i].position) {
                    case USP_FRONT: pos_str = "front"; break;
                    case USP_REAR:  pos_str = "rear";  break;
                    case USP_LEFT:  pos_str = "left";  break;
                    case USP_RIGHT: pos_str = "right"; break;
                    default: break;
                }
                
                if (distance > 0) {
                    ESP_LOGD(TAG, "%s distance: %.1f cm", pos_str, distance);
                } else {
                    ESP_LOGD(TAG, "%s sensor: no valid reading", pos_str);
                }
            }
        }
        
        // 更新障碍物状态
        front_obstacle_ = new_front_obstacle;
        rear_obstacle_ = new_rear_obstacle;
        
        // 释放互斥锁
        xSemaphoreGive(measure_mutex_);
    }
    
public:
    UltrasonicSensor() : Thing("UltrasonicSensor", "Ultrasonic distance sensor") {}
    
    bool init() {
        ESP_LOGI(TAG, "Initializing UltrasonicSensor");
        int count = 0;
        // 检查是否有有效的传感器配置
#ifdef CONFIG_US_FRONT_TRIG_PIN
        if (CONFIG_US_FRONT_TRIG_PIN >= 0 && CONFIG_US_FRONT_ECHO_PIN >= 0) count++;
#endif
#ifdef CONFIG_US_REAR_TRIG_PIN
        if (CONFIG_US_REAR_TRIG_PIN >= 0 && CONFIG_US_REAR_ECHO_PIN >= 0) count++;
#endif

#ifdef CONFIG_US_CONNECTION_PCF8575
        // 检查是否有PCF8575连接的传感器配置
        if (pcf8575_is_initialized()) {
            pcf8575_handle_t pcf_handle = pcf8575_get_handle();
            if (pcf_handle) {
                count++;  // 前方传感器
                count++;  // 后方传感器
            }
    }
#endif

        if (count == 0) {
            ESP_LOGW(TAG, "No ultrasonic sensors configured");
            return false;
        }
        
        // 分配传感器配置空间
        sensors_ = std::make_unique<ultrasonic_sensor_config_t[]>(count);
        if (!sensors_) {
            ESP_LOGE(TAG, "Failed to allocate memory for sensor configurations");
            return false;
        }
        
        // 初始化配置
        size_t index = 0;
        
#ifdef CONFIG_US_CONNECTION_DIRECT
        // 直接GPIO连接的传感器配置
        if (CONFIG_US_FRONT_TRIG_PIN >= 0 && CONFIG_US_FRONT_ECHO_PIN >= 0) {
            sensors_[index].position = USP_FRONT;
            sensors_[index].connection_type = DIRECT_GPIO;
            sensors_[index].gpio.trigger_pin = static_cast<gpio_num_t>(CONFIG_US_FRONT_TRIG_PIN);
            sensors_[index].gpio.echo_pin = static_cast<gpio_num_t>(CONFIG_US_FRONT_ECHO_PIN);
            sensors_[index].max_distance_cm = DEFAULT_MAX_DISTANCE_CM;
            sensors_[index].min_distance_cm = DEFAULT_MIN_DISTANCE_CM;
            sensors_[index].echo_timeout_us = DEFAULT_ECHO_TIMEOUT_US;
            sensors_[index].is_initialized = false;
            
            if (!init_gpio_sensor(&sensors_[index])) {
                ESP_LOGE(TAG, "Failed to initialize front GPIO sensor");
            } else {
                index++;
            }
        }
        
        if (CONFIG_US_REAR_TRIG_PIN >= 0 && CONFIG_US_REAR_ECHO_PIN >= 0) {
            sensors_[index].position = USP_REAR;
            sensors_[index].connection_type = DIRECT_GPIO;
            sensors_[index].gpio.trigger_pin = static_cast<gpio_num_t>(CONFIG_US_REAR_TRIG_PIN);
            sensors_[index].gpio.echo_pin = static_cast<gpio_num_t>(CONFIG_US_REAR_ECHO_PIN);
            sensors_[index].max_distance_cm = DEFAULT_MAX_DISTANCE_CM;
            sensors_[index].min_distance_cm = DEFAULT_MIN_DISTANCE_CM;
            sensors_[index].echo_timeout_us = DEFAULT_ECHO_TIMEOUT_US;
            sensors_[index].is_initialized = false;
            
            if (!init_gpio_sensor(&sensors_[index])) {
                ESP_LOGE(TAG, "Failed to initialize rear GPIO sensor");
            } else {
                index++;
            }
        }
#endif
        
#ifdef CONFIG_US_CONNECTION_PCF8575
        // PCF8575连接的传感器配置
        if (pcf8575_is_initialized()) {
            pcf8575_handle_t pcf_handle = pcf8575_get_handle();
            if (pcf_handle) {
                // 前方传感器
                sensors_[index].position = USP_FRONT;
                sensors_[index].connection_type = PCF8575_GPIO;
                sensors_[index].pcf8575.handle = pcf_handle;
                sensors_[index].pcf8575.trigger_pin = CONFIG_US_PCF8575_FRONT_TRIG_PIN;
                sensors_[index].pcf8575.echo_pin = CONFIG_US_PCF8575_FRONT_ECHO_PIN;
                sensors_[index].max_distance_cm = DEFAULT_MAX_DISTANCE_CM;
                sensors_[index].min_distance_cm = DEFAULT_MIN_DISTANCE_CM;
                sensors_[index].echo_timeout_us = DEFAULT_ECHO_TIMEOUT_US;
                sensors_[index].is_initialized = false;
                
                if (!init_pcf8575_sensor(&sensors_[index])) {
                    ESP_LOGE(TAG, "Failed to initialize front PCF8575 sensor");
                } else {
                    index++;
                }
                
                // 后方传感器
                sensors_[index].position = USP_REAR;
                sensors_[index].connection_type = PCF8575_GPIO;
                sensors_[index].pcf8575.handle = pcf_handle;
                sensors_[index].pcf8575.trigger_pin = CONFIG_US_PCF8575_REAR_TRIG_PIN;
                sensors_[index].pcf8575.echo_pin = CONFIG_US_PCF8575_REAR_ECHO_PIN;
                sensors_[index].max_distance_cm = DEFAULT_MAX_DISTANCE_CM;
                sensors_[index].min_distance_cm = DEFAULT_MIN_DISTANCE_CM;
                sensors_[index].echo_timeout_us = DEFAULT_ECHO_TIMEOUT_US;
                sensors_[index].is_initialized = false;
                
                if (!init_pcf8575_sensor(&sensors_[index])) {
                    ESP_LOGE(TAG, "Failed to initialize rear PCF8575 sensor");
                } else {
                    index++;
                }
            }
        }
#endif
        
        // 更新实际传感器数量
        sensor_count_ = index;
        
        if (sensor_count_ == 0) {
            ESP_LOGE(TAG, "No ultrasonic sensors were successfully initialized");
            return false;
        }
        
        // 创建测量任务
        xTaskCreate(measure_task, "us_measure", 4096, this, 5, &measure_task_handle_);
        if (!measure_task_handle_) {
            ESP_LOGE(TAG, "Failed to create measure task");
            return false;
        }
        
        // 创建定时器
        esp_timer_create_args_t timer_args = {
            .callback = measure_timer_callback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "us_timer",
            .skip_unhandled_events = true,
        };
        
        if (esp_timer_create(&timer_args, &measure_timer_handle_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create measure timer");
            return false;
        }
        
        // 启动定时器
        if (esp_timer_start_periodic(measure_timer_handle_, measure_interval_ms_ * 1000) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start measure timer");
            return false;
        }
        
        ESP_LOGI(TAG, "Initialized %d ultrasonic sensors", sensor_count_);
        return true;
    }
    
    void deinit() {
        // 停止定时器
        if (measure_timer_handle_) {
            esp_timer_stop(measure_timer_handle_);
            esp_timer_delete(measure_timer_handle_);
            measure_timer_handle_ = nullptr;
        }
        
        // 删除任务
        if (measure_task_handle_) {
            vTaskDelete(measure_task_handle_);
            measure_task_handle_ = nullptr;
        }
        
        // 释放互斥锁
        if (measure_mutex_) {
            vSemaphoreDelete(measure_mutex_);
            measure_mutex_ = nullptr;
        }
        
        // 清除传感器配置
        sensor_count_ = 0;
        sensors_.reset();
    }
    
    bool has_front_obstacle() const {
        return front_obstacle_;
    }
    
    bool has_rear_obstacle() const {
        return rear_obstacle_;
    }
    
    void set_safe_distance(float cm) {
        if (cm > 0) {
            safe_distance_cm_ = cm;
        }
    }
    
    float get_safe_distance() const {
        return safe_distance_cm_;
    }
    
    void set_measure_interval(uint32_t ms) {
        if (ms >= 10 && ms <= 1000) {
            measure_interval_ms_ = ms;
            
            // 更新定时器间隔
            if (measure_timer_handle_) {
                esp_timer_stop(measure_timer_handle_);
                esp_timer_start_periodic(measure_timer_handle_, measure_interval_ms_ * 1000);
            }
        }
    }
    
    uint32_t get_measure_interval() const {
        return measure_interval_ms_;
    }
};

} // namespace iot 

DECLARE_THING(UltrasonicSensor);

// 添加 RegisterUS 函数的实现
namespace iot {
void RegisterUS() {
    static UltrasonicSensor* instance = nullptr;
    if (instance == nullptr) {
        instance = new UltrasonicSensor();
        ThingManager::GetInstance().AddThing(instance);
        ESP_LOGI(TAG, "Ultrasonic Sensor Thing registered to ThingManager");
    }
}
} // namespace iot
