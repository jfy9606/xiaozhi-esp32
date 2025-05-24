#include "iot/thing.h"
#include "iot/thing_manager.h"
#include "board.h"
#include "../boards/common/board_config.h"
#include "motor.h"
#include "ext/include/pcf8575.h"
#include "ext/include/pca9548a.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

#define TAG "MotorThing"

// 定义GPIO电平
#define HIGH 1
#define LOW  0

// 电机控制引脚默认定义 - 将由板级配置提供实际引脚号
#define DEFAULT_ENA_PIN -1  // 电机A使能
#define DEFAULT_ENB_PIN -1  // 电机B使能
#define DEFAULT_IN1_PIN -1  // 电机A输入1
#define DEFAULT_IN2_PIN -1  // 电机A输入2
#define DEFAULT_IN3_PIN -1  // 电机B输入1
#define DEFAULT_IN4_PIN -1  // 电机B输入2

// PCF8575引脚定义 - 将使用menuconfig配置
#ifdef CONFIG_MOTOR_CONNECTION_PCF8575
#define MOTOR_PCF8575_IN1_PIN CONFIG_MOTOR_PCF8575_IN1_PIN
#define MOTOR_PCF8575_IN2_PIN CONFIG_MOTOR_PCF8575_IN2_PIN
#define MOTOR_PCF8575_IN3_PIN CONFIG_MOTOR_PCF8575_IN3_PIN
#define MOTOR_PCF8575_IN4_PIN CONFIG_MOTOR_PCF8575_IN4_PIN
#endif

// 速度控制参数
#define DEFAULT_SPEED 100
#define MIN_SPEED 100
#define MAX_SPEED 255

/**
 * 电机LEDC配置 - 特意选择与servo实现不冲突的定时器和通道
 * 注意: 舵机使用MCPWM实现，而电机使用LEDC实现，所以主要是避免LEDC资源冲突
 * ESP32系列有8个LEDC通道(0-7)和4个定时器(0-3)
 * - 通道0-1和定时器0分配给其他功能
 * - 电机控制使用定时器3和通道6-7
 */
#define MOTOR_LEDC_TIMER     LEDC_TIMER_3       // 改为使用定时器3 (原来是TIMER_0)
#define MOTOR_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define MOTOR_LEDC_CHANNEL_A LEDC_CHANNEL_6     // 改为使用通道6 (原来是CHANNEL_0)
#define MOTOR_LEDC_CHANNEL_B LEDC_CHANNEL_7     // 改为使用通道7 (原来是CHANNEL_1)
#define MOTOR_LEDC_DUTY_RES  LEDC_TIMER_8_BIT   // 8位分辨率，0-255
#define MOTOR_LEDC_FREQ      5000               // PWM频率5kHz

namespace iot {

class Motor : public Thing {
private:
    int ena_pin_;  // 电机A使能引脚
    int enb_pin_;  // 电机B使能引脚
    int in1_pin_;  // 电机A输入1引脚
    int in2_pin_;  // 电机A输入2引脚
    int in3_pin_;  // 电机B输入1引脚
    int in4_pin_;  // 电机B输入2引脚
    
    bool ledc_initialized_; // LEDC是否已初始化
    int init_retry_count_;  // 初始化重试次数
    
    int motor_speed_;        // 电机速度(0-255)
    float direction_x_;      // X方向(左右)
    float direction_y_;      // Y方向(前后)
    float distance_percent_; // 摇杆拖动距离百分比

    // 状态标志
    bool running_;
    bool use_pcf8575_;      // 是否使用PCF8575
    
    // 缓存值
    int last_dir_x_;  // 缓存上一次的X方向
    int last_dir_y_;  // 缓存上一次的Y方向
    float cached_angle_degrees_; // 缓存计算的角度

    // 初始化GPIO
    void InitGPIO() {
        // 从board配置中获取电机引脚
        board_config_t* config = board_get_config();
        if (config) {
            // 如果板级配置中定义了电机引脚，则使用板级配置
            if (config->ena_pin >= 0) ena_pin_ = config->ena_pin;
            if (config->enb_pin >= 0) enb_pin_ = config->enb_pin;
            if (config->in1_pin >= 0) in1_pin_ = config->in1_pin;
            if (config->in2_pin >= 0) in2_pin_ = config->in2_pin;
            if (config->in3_pin >= 0) in3_pin_ = config->in3_pin;
            if (config->in4_pin >= 0) in4_pin_ = config->in4_pin;
        }

#ifdef CONFIG_MOTOR_CONNECTION_PCF8575
        // 检查是否使用PCF8575连接方式
        use_pcf8575_ = true;
        
        // 检查PCF8575是否已初始化
        if (!pcf8575_is_initialized()) {
            ESP_LOGW(TAG, "PCF8575 not initialized, attempting initialization");
            // 检查PCA9548A是否已初始化，因为PCF8575连接在PCA9548A上
            if (!pca9548a_is_initialized()) {
                ESP_LOGE(TAG, "PCA9548A multiplexer is not enabled, but motors are configured to use PCF8575");
                ESP_LOGE(TAG, "Please enable PCA9548A and PCF8575 in menuconfig or change motor connection type");
                use_pcf8575_ = false;
            } else {
                // 尝试初始化PCF8575
                ESP_LOGI(TAG, "Initializing PCF8575 for motor control");
                esp_err_t ret = pcf8575_init();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to initialize PCF8575: %s", esp_err_to_name(ret));
                    use_pcf8575_ = false;
                }
            }
        }
        
        // 如果PCF8575可用，则使用PCF8575
        if (use_pcf8575_) {
            ESP_LOGI(TAG, "Using PCF8575 GPIO expander for motor control");
            // 获取PCF8575句柄
            pcf8575_handle_t pcf_handle = pcf8575_get_handle();
            if (!pcf_handle) {
                ESP_LOGE(TAG, "Failed to get PCF8575 handle");
                use_pcf8575_ = false;
            } else {
                // 配置PCF8575上的电机引脚
                ESP_LOGI(TAG, "Configuring PCF8575 pins for motor control");
                ESP_LOGI(TAG, "Motor control pins: IN1: P%02d, IN2: P%02d, IN3: P%02d, IN4: P%02d", 
                        MOTOR_PCF8575_IN1_PIN, MOTOR_PCF8575_IN2_PIN, MOTOR_PCF8575_IN3_PIN, MOTOR_PCF8575_IN4_PIN);
                
                // 设置所有引脚为输出模式
                pcf8575_set_gpio_mode(pcf_handle, (pcf8575_gpio_t)MOTOR_PCF8575_IN1_PIN, PCF8575_GPIO_MODE_OUTPUT);
                pcf8575_set_gpio_mode(pcf_handle, (pcf8575_gpio_t)MOTOR_PCF8575_IN2_PIN, PCF8575_GPIO_MODE_OUTPUT);
                pcf8575_set_gpio_mode(pcf_handle, (pcf8575_gpio_t)MOTOR_PCF8575_IN3_PIN, PCF8575_GPIO_MODE_OUTPUT);
                pcf8575_set_gpio_mode(pcf_handle, (pcf8575_gpio_t)MOTOR_PCF8575_IN4_PIN, PCF8575_GPIO_MODE_OUTPUT);
                
                // 设置初始状态为低电平
                pcf8575_set_gpio_level(pcf_handle, (pcf8575_gpio_t)MOTOR_PCF8575_IN1_PIN, 0);
                pcf8575_set_gpio_level(pcf_handle, (pcf8575_gpio_t)MOTOR_PCF8575_IN2_PIN, 0);
                pcf8575_set_gpio_level(pcf_handle, (pcf8575_gpio_t)MOTOR_PCF8575_IN3_PIN, 0);
                pcf8575_set_gpio_level(pcf_handle, (pcf8575_gpio_t)MOTOR_PCF8575_IN4_PIN, 0);
                
                // PCF8575配置成功，标记初始化完成
                ledc_initialized_ = true;
                init_retry_count_ = 0; // 重置重试计数器
                ESP_LOGI(TAG, "Motor pins initialized successfully with PCF8575");
                return; // 使用PCF8575配置成功，不需要继续进行直接GPIO配置
            }
        }
#else
        use_pcf8575_ = false;
#endif

        // 如果代码运行到这里，要么不使用PCF8575，要么PCF8575初始化失败，需要使用直接GPIO
        ESP_LOGI(TAG, "Initializing motor GPIO pins: ENA=%d, ENB=%d, IN1=%d, IN2=%d, IN3=%d, IN4=%d",
                 ena_pin_, enb_pin_, in1_pin_, in2_pin_, in3_pin_, in4_pin_);
        
        // 检查引脚是否在有效范围内
        const int MAX_VALID_GPIO = GPIO_NUM_MAX - 1;
        bool pins_valid = true;
        
        if (ena_pin_ < 0 || ena_pin_ > MAX_VALID_GPIO) {
            ESP_LOGW(TAG, "Invalid ENA pin: %d", ena_pin_);
            pins_valid = false;
        }
        if (enb_pin_ < 0 || enb_pin_ > MAX_VALID_GPIO) {
            ESP_LOGW(TAG, "Invalid ENB pin: %d", enb_pin_);
            pins_valid = false;
        }
        if (in1_pin_ < 0 || in1_pin_ > MAX_VALID_GPIO) {
            ESP_LOGW(TAG, "Invalid IN1 pin: %d", in1_pin_);
            pins_valid = false;
        }
        if (in2_pin_ < 0 || in2_pin_ > MAX_VALID_GPIO) {
            ESP_LOGW(TAG, "Invalid IN2 pin: %d", in2_pin_);
            pins_valid = false;
        }
        if (in3_pin_ < 0 || in3_pin_ > MAX_VALID_GPIO) {
            ESP_LOGW(TAG, "Invalid IN3 pin: %d", in3_pin_);
            pins_valid = false;
        }
        if (in4_pin_ < 0 || in4_pin_ > MAX_VALID_GPIO) {
            ESP_LOGW(TAG, "Invalid IN4 pin: %d", in4_pin_);
            pins_valid = false;
        }
        
        if (!pins_valid) {
            ESP_LOGE(TAG, "Invalid motor pin configuration detected! Motors will not function properly.");
            ESP_LOGE(TAG, "Valid GPIO pin range: 0-%d", MAX_VALID_GPIO);
            ledc_initialized_ = false;
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
            ledc_initialized_ = false;
            return;
        }
        
        // 初始化LEDC用于PWM控制电机速度
        ESP_LOGI(TAG, "Initializing LEDC for motor control - Timer:%d, Channels:%d,%d",
                 MOTOR_LEDC_TIMER, MOTOR_LEDC_CHANNEL_A, MOTOR_LEDC_CHANNEL_B);
        
        // 直接进行LEDC配置，不预先调用ledc_stop
        // 配置LEDC定时器
        ledc_timer_config_t ledc_timer = {
            .speed_mode = MOTOR_LEDC_MODE,
            .duty_resolution = MOTOR_LEDC_DUTY_RES,
            .timer_num = MOTOR_LEDC_TIMER,
            .freq_hz = MOTOR_LEDC_FREQ,
            .clk_cfg = LEDC_AUTO_CLK
        };
        
        err = ledc_timer_config(&ledc_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC timer config failed: 0x%x (%s)", err, esp_err_to_name(err));
            
            // 如果是定时器冲突错误，提供更详细的诊断信息
            if (err == ESP_ERR_INVALID_STATE) {
                ESP_LOGE(TAG, "Timer conflict detected! This may indicate that another component is using TIMER_%d", 
                         MOTOR_LEDC_TIMER);
                ESP_LOGE(TAG, "Try modifying MOTOR_LEDC_TIMER to use a different timer (current: %d)", 
                         MOTOR_LEDC_TIMER);
            }
            
            ledc_initialized_ = false;
            return;
        }
        
        // 配置ENA通道
        ledc_channel_config_t ledc_channel_ena = {
            .gpio_num = (gpio_num_t)ena_pin_,
            .speed_mode = MOTOR_LEDC_MODE,
            .channel = MOTOR_LEDC_CHANNEL_A,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = MOTOR_LEDC_TIMER,
            .duty = 0,
            .hpoint = 0
        };
        
        err = ledc_channel_config(&ledc_channel_ena);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC ENA channel config failed: 0x%x (%s)", err, esp_err_to_name(err));
            
            // 如果是通道冲突错误，提供详细诊断
            if (err == ESP_ERR_INVALID_STATE) {
                ESP_LOGE(TAG, "Channel conflict detected! Another component may be using CHANNEL_%d", 
                         MOTOR_LEDC_CHANNEL_A);
                ESP_LOGE(TAG, "Try modifying MOTOR_LEDC_CHANNEL_A to use a different channel (current: %d)",
                         MOTOR_LEDC_CHANNEL_A);
            }
            
            ledc_initialized_ = false;
            return;
        }
        
        // 配置ENB通道
        ledc_channel_config_t ledc_channel_enb = {
            .gpio_num = (gpio_num_t)enb_pin_,
            .speed_mode = MOTOR_LEDC_MODE,
            .channel = MOTOR_LEDC_CHANNEL_B,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = MOTOR_LEDC_TIMER,
            .duty = 0,
            .hpoint = 0
        };
        
        err = ledc_channel_config(&ledc_channel_enb);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC ENB channel config failed: 0x%x (%s)", err, esp_err_to_name(err));
            
            // 如果是通道冲突错误，提供详细诊断
            if (err == ESP_ERR_INVALID_STATE) {
                ESP_LOGE(TAG, "Channel conflict detected! Another component may be using CHANNEL_%d", 
                         MOTOR_LEDC_CHANNEL_B);
                ESP_LOGE(TAG, "Try modifying MOTOR_LEDC_CHANNEL_B to use a different channel (current: %d)",
                         MOTOR_LEDC_CHANNEL_B);
            }
            
            ledc_initialized_ = false;
            return;
        }
        
        // 初始时设置为0
        err = ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL_A, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set initial ENA duty: %s", esp_err_to_name(err));
        }
        
        err = ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL_A);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to update initial ENA duty: %s", esp_err_to_name(err));
        }
        
        err = ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL_B, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set initial ENB duty: %s", esp_err_to_name(err));
        }
        
        // 等待ESP32处理完成
        err = ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL_B);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to update initial ENB duty: %s", esp_err_to_name(err));
        }
        
        // 标记初始化完成
        ledc_initialized_ = true;
        init_retry_count_ = 0; // 重置重试计数器
        
        ESP_LOGI(TAG, "Motor GPIO pins and LEDC initialized successfully");
    }
    
    // 控制电机函数
    void ControlMotor(int in1, int in2, int in3, int in4) {
        // 检查是否使用PCF8575连接
#ifdef CONFIG_MOTOR_CONNECTION_PCF8575
        if (use_pcf8575_) {
            pcf8575_handle_t pcf_handle = pcf8575_get_handle();
            if (!pcf_handle) {
                ESP_LOGE(TAG, "PCF8575 handle is NULL");
                return;
            }
            
            // 设置电机控制引脚电平
            pcf8575_set_gpio_level(pcf_handle, (pcf8575_gpio_t)MOTOR_PCF8575_IN1_PIN, in1);
            pcf8575_set_gpio_level(pcf_handle, (pcf8575_gpio_t)MOTOR_PCF8575_IN2_PIN, in2);
            pcf8575_set_gpio_level(pcf_handle, (pcf8575_gpio_t)MOTOR_PCF8575_IN3_PIN, in3);
            pcf8575_set_gpio_level(pcf_handle, (pcf8575_gpio_t)MOTOR_PCF8575_IN4_PIN, in4);
            
            // 如果是停止状态，两个电机都停止
            if (in1 == LOW && in2 == LOW && in3 == LOW && in4 == LOW) {
                // 停止，不需要额外处理
                return;
            }
            
            // 计算PWM控制
            // ... existing PWM calculation code
            
            // 对于PCF8575控制，仅使用方向控制，不能控制PWM
            ESP_LOGD(TAG, "PCF8575 motor control: IN1=%d, IN2=%d, IN3=%d, IN4=%d", in1, in2, in3, in4);
            return;
        }
#endif
        
        // 使用直接GPIO控制（原有代码）
        // 检查引脚是否有效
        if (in1_pin_ < 0 || in2_pin_ < 0 || in3_pin_ < 0 || in4_pin_ < 0) {
            ESP_LOGW(TAG, "Invalid motor pins, cannot control motors");
            return;
        }
        
        // 检查LEDC是否已初始化
        if (!ledc_initialized_) {
            ESP_LOGW(TAG, "LEDC not initialized, attempting reinitialization (retry #%d)", init_retry_count_ + 1);
            
            // 限制重试次数，避免无限循环
            if (init_retry_count_ < 3) {
                init_retry_count_++;
                
                // 短暂延迟后重试，给其他可能使用相同资源的组件一些时间释放资源
                vTaskDelay(pdMS_TO_TICKS(100 * init_retry_count_)); // 递增延迟时间
                
                // 尝试重新初始化GPIO
                InitGPIO();
                
                // 再次检查
                if (!ledc_initialized_) {
                    ESP_LOGE(TAG, "Failed to initialize LEDC (retry #%d), cannot control motors", init_retry_count_);
                    return;
                }
                
                ESP_LOGI(TAG, "LEDC reinitialization successful on retry #%d", init_retry_count_);
            } else {
                ESP_LOGE(TAG, "Exceeded maximum LEDC initialization retries. Motor control disabled.");
                return;
            }
        }
        
        // 设置电机方向控制引脚，添加错误处理
        esp_err_t err;
        
        // 先设置好所有方向引脚，再一次性使能两个电机，避免左右轮不同步问题
        
        // 1. 准备所有方向引脚
        err = gpio_set_level((gpio_num_t)in1_pin_, in1);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set IN1 pin %d: %s", in1_pin_, esp_err_to_name(err));
        }
        
        err = gpio_set_level((gpio_num_t)in2_pin_, in2);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set IN2 pin %d: %s", in2_pin_, esp_err_to_name(err));
        }
        
        err = gpio_set_level((gpio_num_t)in3_pin_, in3);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set IN3 pin %d: %s", in3_pin_, esp_err_to_name(err));
        }
        
        err = gpio_set_level((gpio_num_t)in4_pin_, in4);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set IN4 pin %d: %s", in4_pin_, esp_err_to_name(err));
        }
        
        // 如果是停止状态，两个电机都停止
        if (in1 == LOW && in2 == LOW && in3 == LOW && in4 == LOW) {
            // 2a. 对于停止状态，同时关闭两个电机使能
            err = ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL_A, 0);
            err |= ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL_B, 0);
            
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to set motor duty to 0: %s", esp_err_to_name(err));
            }
            
            // 同时更新两个通道
            err = ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL_A);
            err |= ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL_B);
            
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to update motor duty: %s", esp_err_to_name(err));
            }
            
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
        
        // 2b. 对于运动状态，同时设置好两个电机的占空比
        err = ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL_A, leftSpeed);
        err |= ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL_B, rightSpeed);
        
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set motor duty: %s", esp_err_to_name(err));
        }
        
        // 最后一步，同时更新两个通道，确保左右轮同步启动
        err = ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL_A);
        err |= ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL_B);
        
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to update motor duty: %s", esp_err_to_name(err));
        }
    }

public:
    Motor() : Thing("Motor", "小车电机控制"),
              ena_pin_(DEFAULT_ENA_PIN), 
              enb_pin_(DEFAULT_ENB_PIN), 
              in1_pin_(DEFAULT_IN1_PIN), 
              in2_pin_(DEFAULT_IN2_PIN), 
              in3_pin_(DEFAULT_IN3_PIN), 
              in4_pin_(DEFAULT_IN4_PIN),
        ledc_initialized_(false),
        init_retry_count_(0),
        motor_speed_(DEFAULT_SPEED),
        direction_x_(0),
        direction_y_(0),
        distance_percent_(0),
        running_(false),
        use_pcf8575_(false),
        last_dir_x_(0),
        last_dir_y_(0),
        cached_angle_degrees_(0) {
        
        // 初始化GPIO
        InitGPIO();
        
        // 确保电机停止
        ControlMotor(LOW, LOW, LOW, LOW);
        running_ = true;
        
        // 定义设备的属性
        properties_.AddNumberProperty("speed", "电机速度 (100-255)", [this]() -> int {
            return motor_speed_;
        });
        
        properties_.AddNumberProperty("directionX", "X轴方向 (-100 to 100)", [this]() -> int {
            return direction_x_;
        });
        
        properties_.AddNumberProperty("directionY", "Y轴方向 (-100 to 100)", [this]() -> int {
            return direction_y_;
        });
        
        properties_.AddBooleanProperty("running", "电机是否运行中", [this]() -> bool {
            return running_;
        });
        
        // 定义设备可以被远程执行的指令
        ParameterList moveParams;
        moveParams.AddParameter(Parameter("dirX", "X轴方向 (-100 to 100)", kValueTypeNumber));
        moveParams.AddParameter(Parameter("dirY", "Y轴方向 (-100 to 100)", kValueTypeNumber));
        moveParams.AddParameter(Parameter("distance", "拖动距离百分比 (0.0-1.0)", kValueTypeNumber));
        
        methods_.AddMethod("Move", "移动电机", moveParams, [this](const ParameterList& parameters) {
            int dirX = parameters["dirX"].number();
            int dirY = parameters["dirY"].number();
            float distance = parameters["distance"].number() / 100.0f; // 转换为0-1范围
            
            // 限制范围
            if (dirX < -100) dirX = -100;
            if (dirX > 100) dirX = 100;
            if (dirY < -100) dirY = -100;
            if (dirY > 100) dirY = 100;
            if (distance < 0.0f) distance = 0.0f;
            if (distance > 1.0f) distance = 1.0f;
            
            // 设置控制参数
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
        });
        
        ParameterList speedParams;
        speedParams.AddParameter(Parameter("speed", "速度 (100-255)", kValueTypeNumber));
        
        methods_.AddMethod("SetSpeed", "设置电机速度", speedParams, [this](const ParameterList& parameters) {
            int speed = parameters["speed"].number();
            motor_speed_ = speed < MIN_SPEED ? MIN_SPEED : (speed > MAX_SPEED ? MAX_SPEED : speed);
        });
        
        methods_.AddMethod("Forward", "向前移动", speedParams, [this](const ParameterList& parameters) {
            if (!running_) {
                ESP_LOGW(TAG, "Motor not running");
                return;
            }
            
            int speed = DEFAULT_SPEED;
            try {
                speed = parameters["speed"].number();
            } catch(...) {
                // 使用默认速度
            }
            
            motor_speed_ = speed < MIN_SPEED ? MIN_SPEED : (speed > MAX_SPEED ? MAX_SPEED : speed);
            ControlMotor(HIGH, LOW, HIGH, LOW);
        });
        
        methods_.AddMethod("Backward", "向后移动", speedParams, [this](const ParameterList& parameters) {
            if (!running_) {
                ESP_LOGW(TAG, "Motor not running");
                return;
            }
            
            int speed = DEFAULT_SPEED;
            try {
                speed = parameters["speed"].number();
            } catch(...) {
                // 使用默认速度
            }
            
            motor_speed_ = speed < MIN_SPEED ? MIN_SPEED : (speed > MAX_SPEED ? MAX_SPEED : speed);
            ControlMotor(LOW, HIGH, LOW, HIGH);
        });
        
        methods_.AddMethod("TurnLeft", "向左转", speedParams, [this](const ParameterList& parameters) {
            if (!running_) {
                ESP_LOGW(TAG, "Motor not running");
                return;
            }
            
            int speed = DEFAULT_SPEED;
            try {
                speed = parameters["speed"].number();
            } catch(...) {
                // 使用默认速度
            }
            
            motor_speed_ = speed < MIN_SPEED ? MIN_SPEED : (speed > MAX_SPEED ? MAX_SPEED : speed);
            ControlMotor(HIGH, LOW, LOW, HIGH);
        });
        
        methods_.AddMethod("TurnRight", "向右转", speedParams, [this](const ParameterList& parameters) {
            if (!running_) {
                ESP_LOGW(TAG, "Motor not running");
                return;
            }
            
            int speed = DEFAULT_SPEED;
            try {
                speed = parameters["speed"].number();
            } catch(...) {
                // 使用默认速度
            }
            
            motor_speed_ = speed < MIN_SPEED ? MIN_SPEED : (speed > MAX_SPEED ? MAX_SPEED : speed);
            ControlMotor(LOW, HIGH, HIGH, LOW);
        });
        
        ParameterList stopParams;
        stopParams.AddParameter(Parameter("brake", "是否制动", kValueTypeBoolean));
        
        methods_.AddMethod("Stop", "停止电机", stopParams, [this](const ParameterList& parameters) {
            if (!running_) {
                ESP_LOGW(TAG, "Motor not running");
                return;
            }
            
            bool brake = false;
            try {
                brake = parameters["brake"].boolean();
            } catch(...) {
                // 使用默认值
            }
            
            if (brake) {
                // 刹车模式 - 向电机施加反向电流以迅速停止
                ControlMotor(HIGH, HIGH, HIGH, HIGH);
                // 短暂延时让电机有时间停止
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            
            // 自由停止模式 - 切断电源让电机自然停止
            ControlMotor(LOW, LOW, LOW, LOW);
        });
    }
    
    ~Motor() {
        // 确保电机停止
        ControlMotor(LOW, LOW, LOW, LOW);
    }
};

void RegisterMotor() {
    static Motor* motor = nullptr;
    if (motor == nullptr) {
        motor = new Motor();
        ThingManager::GetInstance().AddThing(motor);
    }
}

} // namespace iot

DECLARE_THING(Motor); 