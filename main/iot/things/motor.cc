#include "iot/thing.h"
#include "board.h"
#include "../boards/common/board_config.h"

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

// 速度控制参数
#define DEFAULT_SPEED 100
#define MIN_SPEED 100
#define MAX_SPEED 255

// SG90舵机控制参数
#define SERVO_FREQ 50              // 50Hz，周期为20ms
#define SERVO_TIMER LEDC_TIMER_1   // 使用计时器1（避免与电机冲突）
#define SERVO_RESOLUTION LEDC_TIMER_16_BIT // 16位分辨率
#define SERVO_MIN_PULSEWIDTH 500   // 0.5ms - 0度
#define SERVO_MAX_PULSEWIDTH 2500  // 2.5ms - 180度
#define SERVO_MAX_DEGREE 180       // 最大角度

// 定义舵机扫描模式
typedef enum {
    SERVO_MODE_STATIC,      // 静止模式
    SERVO_MODE_SWEEP,       // 来回扫描模式
    SERVO_MODE_CONTINUOUS   // 连续旋转模式
} servo_mode_t;

namespace iot {

// 舵机控制类
class ServoMotor {
private:
    int pin_;                 // 舵机信号引脚
    ledc_channel_t channel_;  // LEDC通道
    int current_angle_;       // 当前角度
    int target_angle_;        // 目标角度
    int min_angle_;           // 最小角度
    int max_angle_;           // 最大角度
    int sweep_step_;          // 扫描步长
    int sweep_delay_;         // 扫描延迟(毫秒)
    servo_mode_t mode_;       // 舵机模式
    bool continuous_clockwise_; // 连续模式下是否顺时针
    TimerHandle_t sweep_timer_; // 扫描定时器
    bool is_initialized_;     // 是否已初始化
    
    // 将角度转换为PWM值
    uint32_t AngleToDuty(int angle) {
        // 确保角度在有效范围内
        if (angle < min_angle_) angle = min_angle_;
        if (angle > max_angle_) angle = max_angle_;
        
        // 将角度映射到脉冲宽度
        uint32_t pulse_width = SERVO_MIN_PULSEWIDTH + (SERVO_MAX_PULSEWIDTH - SERVO_MIN_PULSEWIDTH) * angle / SERVO_MAX_DEGREE;
        
        // 将脉冲宽度转换为占空比(LEDC使用)
        uint32_t duty = (1 << SERVO_RESOLUTION) * pulse_width / (1000000 / SERVO_FREQ);
        
        return duty;
    }
    
    // 扫描定时器回调函数
    static void SweepTimerCallback(TimerHandle_t xTimer) {
        ServoMotor* servo = static_cast<ServoMotor*>(pvTimerGetTimerID(xTimer));
        if (servo) {
            servo->UpdateSweep();
        }
    }
    
    // 更新扫描位置
    void UpdateSweep() {
        if (mode_ == SERVO_MODE_SWEEP) {
            // 如果达到边界，改变方向
            if (current_angle_ >= max_angle_) {
                sweep_step_ = -abs(sweep_step_);
            } else if (current_angle_ <= min_angle_) {
                sweep_step_ = abs(sweep_step_);
            }
            
            // 更新角度
            current_angle_ += sweep_step_;
            SetAngle(current_angle_);
        } else if (mode_ == SERVO_MODE_CONTINUOUS) {
            // 连续旋转模式
            int next_angle = current_angle_ + (continuous_clockwise_ ? sweep_step_ : -sweep_step_);
            
            // 处理角度回环
            if (next_angle > max_angle_) {
                next_angle = min_angle_;
            } else if (next_angle < min_angle_) {
                next_angle = max_angle_;
            }
            
            current_angle_ = next_angle;
            SetAngle(current_angle_);
        }
    }

public:
    ServoMotor() : 
        pin_(-1),
        channel_(LEDC_CHANNEL_MAX),
        current_angle_(0),
        target_angle_(0),
        min_angle_(0),
        max_angle_(SERVO_MAX_DEGREE),
        sweep_step_(5),
        sweep_delay_(100),
        mode_(SERVO_MODE_STATIC),
        continuous_clockwise_(true),
        sweep_timer_(nullptr),
        is_initialized_(false) {
    }
    
    ~ServoMotor() {
        Deinitialize();
    }
    
    // 初始化舵机
    bool Initialize(int pin, ledc_channel_t channel) {
        // 检查引脚有效性
        if (pin < 0 || pin >= GPIO_NUM_MAX) {
            ESP_LOGE(TAG, "Invalid servo pin: %d", pin);
            return false;
        }
        
        pin_ = pin;
        channel_ = channel;
        
        // 配置LEDC计时器
        ledc_timer_config_t timer_conf = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = SERVO_RESOLUTION,
            .timer_num = SERVO_TIMER,
            .freq_hz = SERVO_FREQ,
            .clk_cfg = LEDC_AUTO_CLK
        };
        
        esp_err_t err = ledc_timer_config(&timer_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Servo timer config failed: 0x%x", err);
            return false;
        }
        
        // 配置LEDC通道
        ledc_channel_config_t ledc_conf = {
            .gpio_num = pin_,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = channel_,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = SERVO_TIMER,
            .duty = 0,
            .hpoint = 0
        };
        
        err = ledc_channel_config(&ledc_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Servo channel config failed: 0x%x", err);
            return false;
        }
        
        // 创建定时器
        sweep_timer_ = xTimerCreate(
            "servo_sweep_timer",
            pdMS_TO_TICKS(sweep_delay_),
            pdTRUE,  // 自动重载
            this,    // 定时器ID
            SweepTimerCallback
        );
        
        if (sweep_timer_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create servo sweep timer");
            return false;
        }
        
        is_initialized_ = true;
        SetAngle(current_angle_);  // 设置初始角度
        
        ESP_LOGI(TAG, "Servo initialized on pin %d, channel %d", pin_, channel_);
        return true;
    }
    
    // 反初始化舵机
    void Deinitialize() {
        if (!is_initialized_) {
            return;
        }
        
        // 停止定时器
        if (sweep_timer_ != nullptr) {
            xTimerStop(sweep_timer_, 0);
            xTimerDelete(sweep_timer_, 0);
            sweep_timer_ = nullptr;
        }
        
        // 停止PWM输出
        ledc_stop(LEDC_LOW_SPEED_MODE, channel_, 0);
        
        is_initialized_ = false;
    }
    
    // 设置舵机角度
    void SetAngle(int angle) {
        if (!is_initialized_) {
            ESP_LOGW(TAG, "Servo not initialized");
            return;
        }
        
        // 确保角度在有效范围内
        if (angle < min_angle_) angle = min_angle_;
        if (angle > max_angle_) angle = max_angle_;
        
        current_angle_ = angle;
        target_angle_ = angle;
        
        // 设置PWM占空比
        uint32_t duty = AngleToDuty(angle);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, channel_, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, channel_);
    }
    
    // 设置舵机角度范围
    void SetAngleRange(int min_angle, int max_angle) {
        if (min_angle < 0) min_angle = 0;
        if (max_angle > SERVO_MAX_DEGREE) max_angle = SERVO_MAX_DEGREE;
        if (min_angle > max_angle) {
            // 交换值
            int temp = min_angle;
            min_angle = max_angle;
            max_angle = temp;
        }
        
        min_angle_ = min_angle;
        max_angle_ = max_angle;
        
        // 确保当前角度在新范围内
        if (current_angle_ < min_angle_) {
            SetAngle(min_angle_);
        } else if (current_angle_ > max_angle_) {
            SetAngle(max_angle_);
        }
    }
    
    // 设置扫描参数
    void SetSweepParams(int step, int delay_ms) {
        if (step <= 0) step = 1;
        if (delay_ms < 10) delay_ms = 10;
        
        sweep_step_ = step;
        sweep_delay_ = delay_ms;
        
        // 更新定时器周期
        if (sweep_timer_ != nullptr) {
            xTimerChangePeriod(sweep_timer_, pdMS_TO_TICKS(sweep_delay_), 0);
        }
    }
    
    // 开始来回扫描
    void StartSweep() {
        if (!is_initialized_) {
            ESP_LOGW(TAG, "Servo not initialized");
            return;
        }
        
        mode_ = SERVO_MODE_SWEEP;
        
        // 确保定时器运行
        if (sweep_timer_ != nullptr) {
            xTimerStart(sweep_timer_, 0);
        }
    }
    
    // 开始连续旋转
    void StartContinuous(bool clockwise) {
        if (!is_initialized_) {
            ESP_LOGW(TAG, "Servo not initialized");
            return;
        }
        
        mode_ = SERVO_MODE_CONTINUOUS;
        continuous_clockwise_ = clockwise;
        
        // 确保定时器运行
        if (sweep_timer_ != nullptr) {
            xTimerStart(sweep_timer_, 0);
        }
    }
    
    // 停止运动
    void Stop() {
        if (!is_initialized_) {
            return;
        }
        
        mode_ = SERVO_MODE_STATIC;
        
        // 停止定时器
        if (sweep_timer_ != nullptr) {
            xTimerStop(sweep_timer_, 0);
        }
    }
    
    // 获取当前角度
    int GetCurrentAngle() const {
        return current_angle_;
    }
    
    // 获取当前模式
    servo_mode_t GetMode() const {
        return mode_;
    }
};

class Motor : public Thing {
private:
    // 电机控制引脚
    int ena_pin_;
    int enb_pin_;
    int in1_pin_;
    int in2_pin_;
    int in3_pin_;
    int in4_pin_;

    // 状态标志
    bool running_;
    
    // 控制参数
    int direction_x_; // X轴方向值
    int direction_y_; // Y轴方向值
    int motor_speed_; // 电机速度
    float distance_percent_; // 摇杆拖动距离百分比
    
    // 缓存值
    int last_dir_x_;  // 缓存上一次的X方向
    int last_dir_y_;  // 缓存上一次的Y方向
    float cached_angle_degrees_; // 缓存计算的角度

    // 舵机实例和参数
    std::vector<ServoMotor> servos_;  // 舵机实例列表
    std::vector<int> servo_pins_;     // 舵机引脚列表

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
            // 安全处理：维持默认pin值
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
    
    // 控制电机函数
    void ControlMotor(int in1, int in2, int in3, int in4) {
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

public:
    Motor() : Thing("Motor", "小车电机控制"),
              ena_pin_(DEFAULT_ENA_PIN), 
              enb_pin_(DEFAULT_ENB_PIN), 
              in1_pin_(DEFAULT_IN1_PIN), 
              in2_pin_(DEFAULT_IN2_PIN), 
              in3_pin_(DEFAULT_IN3_PIN), 
              in4_pin_(DEFAULT_IN4_PIN),
              running_(false),
              direction_x_(0),
              direction_y_(0),
              motor_speed_(DEFAULT_SPEED),
              distance_percent_(0),
              last_dir_x_(0),
              last_dir_y_(0),
              cached_angle_degrees_(0) {
        
        InitGPIO();
        // 确保电机停止
        ControlMotor(LOW, LOW, LOW, LOW);
        running_ = true;
        
        // 从board配置中获取舵机引脚
        board_config_t* config = board_get_config();
        if (config && config->servo_pins && config->servo_count > 0) {
            // 初始化舵机
            for (int i = 0; i < config->servo_count; i++) {
                int pin = config->servo_pins[i];
                if (pin >= 0) {
                    servo_pins_.push_back(pin);
                    servos_.push_back(ServoMotor());
                    
                    // 使用LEDC通道4开始(通道0-3保留给电机)
                    ledc_channel_t channel = static_cast<ledc_channel_t>(LEDC_CHANNEL_4 + i);
                    
                    // 初始化舵机
                    if (servos_[i].Initialize(pin, channel)) {
                        ESP_LOGI(TAG, "Initialized servo %d on pin %d", i, pin);
                    } else {
                        ESP_LOGE(TAG, "Failed to initialize servo %d on pin %d", i, pin);
                    }
                }
            }
            ESP_LOGI(TAG, "Initialized %zu servos", servos_.size());
        } else {
            ESP_LOGI(TAG, "No servo pins configured in board config");
        }
        
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
        
        // 添加舵机相关属性
        properties_.AddNumberProperty("servoCount", "舵机数量", [this]() -> int {
            return servos_.size();
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
        
        // 添加舵机控制方法
        ParameterList servoAngleParams;
        servoAngleParams.AddParameter(Parameter("servoId", "舵机ID (0-based索引)", kValueTypeNumber));
        servoAngleParams.AddParameter(Parameter("angle", "角度 (0-180度)", kValueTypeNumber));
        
        methods_.AddMethod("SetServoAngle", "设置舵机角度", servoAngleParams, [this](const ParameterList& parameters) {
            int servo_id = parameters["servoId"].number();
            int angle = parameters["angle"].number();
            
            if (servo_id < 0 || servo_id >= servos_.size()) {
                ESP_LOGW(TAG, "Invalid servo ID: %d", servo_id);
                return;
            }
            
            servos_[servo_id].SetAngle(angle);
            ESP_LOGI(TAG, "Set servo %d to angle %d", servo_id, angle);
        });
        
        ParameterList servoSweepParams;
        servoSweepParams.AddParameter(Parameter("servoId", "舵机ID (0-based索引)", kValueTypeNumber));
        servoSweepParams.AddParameter(Parameter("minAngle", "最小角度", kValueTypeNumber));
        servoSweepParams.AddParameter(Parameter("maxAngle", "最大角度", kValueTypeNumber));
        servoSweepParams.AddParameter(Parameter("step", "步长", kValueTypeNumber));
        servoSweepParams.AddParameter(Parameter("delayMs", "步进延迟(毫秒)", kValueTypeNumber));
        
        methods_.AddMethod("StartServoSweep", "开始舵机来回扫描", servoSweepParams, [this](const ParameterList& parameters) {
            int servo_id = parameters["servoId"].number();
            int min_angle = parameters["minAngle"].number();
            int max_angle = parameters["maxAngle"].number();
            int step = parameters["step"].number();
            int delay_ms = parameters["delayMs"].number();
            
            if (servo_id < 0 || servo_id >= servos_.size()) {
                ESP_LOGW(TAG, "Invalid servo ID: %d", servo_id);
                return;
            }
            
            servos_[servo_id].SetAngleRange(min_angle, max_angle);
            servos_[servo_id].SetSweepParams(step, delay_ms);
            servos_[servo_id].StartSweep();
            ESP_LOGI(TAG, "Started servo %d sweep from %d to %d with step %d and delay %d ms", 
                     servo_id, min_angle, max_angle, step, delay_ms);
        });
        
        ParameterList servoContinuousParams;
        servoContinuousParams.AddParameter(Parameter("servoId", "舵机ID (0-based索引)", kValueTypeNumber));
        servoContinuousParams.AddParameter(Parameter("clockwise", "是否顺时针旋转", kValueTypeBoolean));
        servoContinuousParams.AddParameter(Parameter("speed", "速度(1-10)", kValueTypeNumber));
        
        methods_.AddMethod("StartServoContinuous", "开始舵机连续旋转", servoContinuousParams, [this](const ParameterList& parameters) {
            int servo_id = parameters["servoId"].number();
            bool clockwise = parameters["clockwise"].boolean();
            int speed = parameters["speed"].number();
            
            if (servo_id < 0 || servo_id >= servos_.size()) {
                ESP_LOGW(TAG, "Invalid servo ID: %d", servo_id);
                return;
            }
            
            // 将速度(1-10)转换为步长(1-10)和延迟(200-50ms)
            if (speed < 1) speed = 1;
            if (speed > 10) speed = 10;
            
            int step = speed;
            int delay_ms = 250 - (speed * 20); // 速度1->延迟230ms, 速度10->延迟50ms
            
            servos_[servo_id].SetSweepParams(step, delay_ms);
            servos_[servo_id].StartContinuous(clockwise);
            ESP_LOGI(TAG, "Started servo %d continuous rotation: %s, speed %d", 
                     servo_id, clockwise ? "clockwise" : "counter-clockwise", speed);
        });
        
        ParameterList servoStopParams;
        servoStopParams.AddParameter(Parameter("servoId", "舵机ID (0-based索引)", kValueTypeNumber));
        
        methods_.AddMethod("StopServo", "停止舵机运动", servoStopParams, [this](const ParameterList& parameters) {
            int servo_id = parameters["servoId"].number();
            
            if (servo_id < 0 || servo_id >= servos_.size()) {
                ESP_LOGW(TAG, "Invalid servo ID: %d", servo_id);
                return;
            }
            
            servos_[servo_id].Stop();
            ESP_LOGI(TAG, "Stopped servo %d", servo_id);
        });
    }
    
    ~Motor() {
        // 确保电机停止
        ControlMotor(LOW, LOW, LOW, LOW);
        
        // 停止所有舵机
        for (auto& servo : servos_) {
            servo.Deinitialize();
        }
    }
};

} // namespace iot

DECLARE_THING(Motor); 