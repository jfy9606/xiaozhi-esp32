#include "iot/thing.h"
#include "board.h"
#include "../boards/common/board_config.h"

#include <esp_log.h>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <driver/ledc.h>
#include <algorithm>
#include <vector>
#include "sdkconfig.h"

#define TAG "ServoThing"

// 舵机配置参数
#ifdef CONFIG_SERVO_MIN_PULSE_WIDTH
#define SERVO_MIN_PULSE_WIDTH_US CONFIG_SERVO_MIN_PULSE_WIDTH    // 从Kconfig获取最小脉冲宽度
#else
#define SERVO_MIN_PULSE_WIDTH_US 500    // 最小脉冲宽度 (微秒)
#endif

#ifdef CONFIG_SERVO_MAX_PULSE_WIDTH
#define SERVO_MAX_PULSE_WIDTH_US CONFIG_SERVO_MAX_PULSE_WIDTH    // 从Kconfig获取最大脉冲宽度
#else
#define SERVO_MAX_PULSE_WIDTH_US 2500   // 最大脉冲宽度 (微秒)
#endif

#define SERVO_FREQUENCY_HZ 50           // 舵机控制频率 (Hz)
#define SERVO_TIMER_RESOLUTION_BITS 13  // LEDC计时器分辨率，13位(0-8191)
#define SERVO_MIN_ANGLE 0               // 最小角度
#define SERVO_MAX_ANGLE 180             // 最大角度
#define SERVO_DEFAULT_ANGLE 90          // 默认角度

// 舵机移动模式
typedef enum {
    SERVO_MODE_STATIC,      // 静止模式
    SERVO_MODE_SWEEP,       // 来回扫描模式
    SERVO_MODE_CONTINUOUS   // 连续旋转模式
} servo_mode_t;

namespace iot {

/**
 * @brief 舵机控制类，用于控制伺服电机的角度和运动模式
 */
class Servo {
private:
    int pin_;                       // 舵机信号引脚
    ledc_channel_t channel_;        // LEDC通道
    ledc_timer_t timer_;            // LEDC计时器
    int current_angle_;             // 当前角度
    int min_angle_;                 // 最小角度
    int max_angle_;                 // 最大角度
    bool is_initialized_;           // 是否已初始化
    int sweep_step_;                // 扫描步长
    int sweep_delay_;               // 扫描延迟(毫秒)
    servo_mode_t mode_;             // 舵机模式
    bool continuous_clockwise_;     // 连续模式下是否顺时针
    TimerHandle_t sweep_timer_;     // 扫描定时器

    // 将角度转换为占空比
    uint32_t angle_to_duty(int angle) {
        // 限制角度范围
        angle = std::min(std::max(angle, min_angle_), max_angle_);
        
        // 计算脉冲宽度 (微秒)
        uint32_t pulse_width_us = SERVO_MIN_PULSE_WIDTH_US + 
            ((SERVO_MAX_PULSE_WIDTH_US - SERVO_MIN_PULSE_WIDTH_US) * angle) / SERVO_MAX_ANGLE;
        
        // 将脉冲宽度转换为LEDC占空比
        // LEDC最大值 = (2^SERVO_TIMER_RESOLUTION_BITS) - 1
        uint32_t max_duty = (1 << SERVO_TIMER_RESOLUTION_BITS) - 1;
        
        // 计算占空比: pulse_width / period
        // period = 1000000 / SERVO_FREQUENCY_HZ (微秒)
        uint32_t duty = (pulse_width_us * max_duty) / (1000000 / SERVO_FREQUENCY_HZ);
        
        return duty;
    }
    
    // 定时器回调函数
    static void sweep_timer_callback(TimerHandle_t xTimer) {
        Servo* servo = static_cast<Servo*>(pvTimerGetTimerID(xTimer));
        if (servo) {
            servo->update_sweep();
        }
    }
    
    // 更新舵机位置
    void update_sweep() {
        if (mode_ == SERVO_MODE_SWEEP) {
            // 如果达到边界，改变方向
            if (current_angle_ >= max_angle_) {
                sweep_step_ = -abs(sweep_step_);
            } else if (current_angle_ <= min_angle_) {
                sweep_step_ = abs(sweep_step_);
            }
            
            // 更新角度
            current_angle_ += sweep_step_;
            set_angle(current_angle_);
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
            set_angle(current_angle_);
        }
    }

public:
    Servo() :
        pin_(-1),
        channel_(LEDC_CHANNEL_0),   // 默认通道
        timer_(LEDC_TIMER_0),       // 默认定时器
        current_angle_(SERVO_DEFAULT_ANGLE),
        min_angle_(SERVO_MIN_ANGLE),
        max_angle_(SERVO_MAX_ANGLE),
        is_initialized_(false),
        sweep_step_(5),
        sweep_delay_(100),
        mode_(SERVO_MODE_STATIC),
        continuous_clockwise_(true),
        sweep_timer_(nullptr) {
    }
    
    ~Servo() {
        deinitialize();
    }
    
    bool initialize(int pin, int group = 0) {
        // 检查引脚有效性
        if (pin < 0) {
            ESP_LOGE(TAG, "Invalid servo pin: %d", pin);
            return false;
        }
        
        pin_ = pin;
        
        // 根据组号设置通道和定时器
        // 每个定时器可以控制多个通道，我们使用相同的定时器设置不同的通道
        channel_ = (ledc_channel_t)(LEDC_CHANNEL_0 + (group % LEDC_CHANNEL_MAX));
        timer_ = (ledc_timer_t)(LEDC_TIMER_0 + (group / LEDC_CHANNEL_MAX) % LEDC_TIMER_MAX);
        
        ESP_LOGI(TAG, "Initializing servo on pin %d, channel %d, timer %d", 
                pin, channel_, timer_);
        
        // 1. 配置定时器
        ledc_timer_config_t timer_conf = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = (ledc_timer_bit_t)SERVO_TIMER_RESOLUTION_BITS,
            .timer_num = timer_,
            .freq_hz = SERVO_FREQUENCY_HZ,
            .clk_cfg = LEDC_AUTO_CLK
        };
        
        esp_err_t err = ledc_timer_config(&timer_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(err));
            return false;
        }
        
        // 2. 配置通道
        ledc_channel_config_t ledc_conf = {
            .gpio_num = pin,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = channel_,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = timer_,
            .duty = 0,
            .hpoint = 0,
            .flags = {
                .output_invert = 0
            }
        };
        
        err = ledc_channel_config(&ledc_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(err));
            return false;
        }
        
        // 创建定时器用于舵机扫描模式
        sweep_timer_ = xTimerCreate(
            "servo_sweep_timer",
            pdMS_TO_TICKS(sweep_delay_),
            pdTRUE,  // 自动重载
            this,    // 定时器ID
            sweep_timer_callback
        );
        
        if (sweep_timer_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create servo sweep timer");
            return false;
        }
        
        is_initialized_ = true;
        
        // 设置舵机到默认位置
        set_angle(SERVO_DEFAULT_ANGLE);
        
        ESP_LOGI(TAG, "Servo initialized on pin %d", pin_);
        return true;
    }
    
    void deinitialize() {
        if (!is_initialized_) {
            return;
        }
        
        // 停止定时器
        if (sweep_timer_ != nullptr) {
            xTimerStop(sweep_timer_, 0);
            xTimerDelete(sweep_timer_, 0);
            sweep_timer_ = nullptr;
        }
        
        // 停止LEDC输出
        ledc_stop(LEDC_LOW_SPEED_MODE, channel_, 0);
        
        is_initialized_ = false;
        ESP_LOGI(TAG, "Servo deinitialized");
    }
    
    bool set_angle(int angle) {
        if (!is_initialized_) {
            ESP_LOGW(TAG, "Servo not initialized");
            return false;
        }
        
        // 确保角度在有效范围内
        angle = std::min(std::max(angle, min_angle_), max_angle_);
        
        current_angle_ = angle;
        
        // 计算占空比
        uint32_t duty = angle_to_duty(angle);
        
        // 设置PWM占空比
        esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel_, duty);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Set duty failed: %s", esp_err_to_name(err));
            return false;
        }
        
        // 更新占空比
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, channel_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Update duty failed: %s", esp_err_to_name(err));
            return false;
        }
        
        ESP_LOGD(TAG, "Servo set to %d degrees (duty: %" PRIu32 ")", angle, duty);
        return true;
    }
    
    int get_angle() const {
        return current_angle_;
    }
    
    void set_angle_range(int min_angle, int max_angle) {
        // 确保范围有效
        if (min_angle < SERVO_MIN_ANGLE) {
            min_angle = SERVO_MIN_ANGLE;
        }
        
        if (max_angle > SERVO_MAX_ANGLE) {
            max_angle = SERVO_MAX_ANGLE;
        }
        
        if (min_angle > max_angle) {
            std::swap(min_angle, max_angle);
        }
        
        min_angle_ = min_angle;
        max_angle_ = max_angle;
        
        // 确保当前角度在新范围内
        if (current_angle_ < min_angle_) {
            set_angle(min_angle_);
        } else if (current_angle_ > max_angle_) {
            set_angle(max_angle_);
        }
    }
    
    bool smooth_move(int target_angle, int steps = 10, int delay_ms = 20) {
        // 确保舵机已初始化
        if (!is_initialized_) {
            ESP_LOGW(TAG, "Servo not initialized");
            return false;
        }
        
        // 限制范围
        target_angle = std::min(std::max(target_angle, min_angle_), max_angle_);
        
        // 如果步数无效，直接设置角度
        if (steps <= 1) {
            return set_angle(target_angle);
        }
        
        int current = current_angle_;
        
        // 如果角度相同，不需要移动
        if (current == target_angle) {
            return true;
        }
        
        // 计算每步的增量
        float step_increment = (float)(target_angle - current) / steps;
        
        // 逐步移动舵机
        for (int i = 1; i <= steps; i++) {
            int next_angle = current + (int)(step_increment * i);
            if (!set_angle(next_angle)) {
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
        
        // 确保最终到达目标角度
        return set_angle(target_angle);
    }
    
    void set_sweep_params(int step, int delay_ms) {
        if (step <= 0) {
            step = 1;
        }
        
        if (delay_ms < 10) {
            delay_ms = 10;
        }
        
        sweep_step_ = step;
        sweep_delay_ = delay_ms;
        
        // 更新定时器周期
        if (sweep_timer_ != nullptr) {
            xTimerChangePeriod(sweep_timer_, pdMS_TO_TICKS(sweep_delay_), 0);
        }
    }
    
    void start_sweep() {
        if (!is_initialized_) {
            ESP_LOGW(TAG, "Servo not initialized");
            return;
        }
        
        mode_ = SERVO_MODE_SWEEP;
        
        // 确保定时器运行
        if (sweep_timer_ != nullptr) {
            xTimerStart(sweep_timer_, 0);
        }
        
        ESP_LOGI(TAG, "Servo sweep started: min=%d, max=%d, step=%d, delay=%d ms", 
                min_angle_, max_angle_, sweep_step_, sweep_delay_);
    }
    
    void start_continuous(bool clockwise) {
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
        
        ESP_LOGI(TAG, "Servo continuous rotation started: %s, step=%d, delay=%d ms", 
                clockwise ? "clockwise" : "counter-clockwise", sweep_step_, sweep_delay_);
    }
    
    void stop() {
        if (!is_initialized_) {
            return;
        }
        
        mode_ = SERVO_MODE_STATIC;
        
        // 停止定时器
        if (sweep_timer_ != nullptr) {
            xTimerStop(sweep_timer_, 0);
        }
        
        ESP_LOGI(TAG, "Servo stopped at angle %d", current_angle_);
    }
};

class ServoThing : public Thing {
private:
    std::vector<Servo> servos_;      // 舵机实例列表
    std::vector<int> servo_pins_;    // 舵机引脚列表
    
    // 初始化舵机
    void InitServos() {
        // 从board配置中获取舵机引脚
        board_config_t* config = board_get_config();
        if (config && config->servo_count > 0) {
            ESP_LOGI(TAG, "Found %d servo pins in board config", config->servo_count);
            
            // 初始化舵机
            for (int i = 0; i < config->servo_count; i++) {
                int pin = config->servo_pins[i];
                if (pin >= 0) {
                    servo_pins_.push_back(pin);
                    servos_.push_back(Servo());
                    
                    // 初始化舵机
                    if (servos_[i].initialize(pin, i)) {
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
    }

public:
    ServoThing() : Thing("Servo", "舵机控制器") {
        // 初始化舵机
        InitServos();
        
        // 添加舵机数量属性
        properties_.AddNumberProperty("servoCount", "舵机数量", [this]() -> int {
            return servos_.size();
        });
        
        // 为每个舵机添加当前角度属性
        for (size_t i = 0; i < servos_.size(); i++) {
            std::string propName = "servo" + std::to_string(i) + "Angle";
            std::string propDesc = "舵机" + std::to_string(i) + "当前角度";
            
            properties_.AddNumberProperty(propName.c_str(), propDesc.c_str(), 
                [this, i]() -> int {
                    if (i < servos_.size()) {
                        return servos_[i].get_angle();
                    }
                    return 0;
                });
        }
        
        // 添加设置舵机角度方法
        ParameterList servoAngleParams;
        servoAngleParams.AddParameter(Parameter("servoId", "舵机ID (0-based索引)", kValueTypeNumber));
        servoAngleParams.AddParameter(Parameter("angle", "角度 (0-180度)", kValueTypeNumber));
        
        methods_.AddMethod("SetAngle", "设置舵机角度", servoAngleParams, [this](const ParameterList& parameters) {
            int servo_id = parameters["servoId"].number();
            int angle = parameters["angle"].number();
            
            if (servo_id < 0 || servo_id >= servos_.size()) {
                ESP_LOGW(TAG, "Invalid servo ID: %d", servo_id);
                return;
            }
            
            servos_[servo_id].set_angle(angle);
            ESP_LOGI(TAG, "Set servo %d to angle %d", servo_id, angle);
        });
        
        // 添加平滑移动舵机到指定角度方法
        ParameterList smoothMoveParams;
        smoothMoveParams.AddParameter(Parameter("servoId", "舵机ID (0-based索引)", kValueTypeNumber));
        smoothMoveParams.AddParameter(Parameter("angle", "目标角度 (0-180度)", kValueTypeNumber));
        smoothMoveParams.AddParameter(Parameter("steps", "移动步数", kValueTypeNumber));
        smoothMoveParams.AddParameter(Parameter("delayMs", "每步延迟(毫秒)", kValueTypeNumber));
        
        methods_.AddMethod("SmoothMove", "平滑移动舵机", smoothMoveParams, [this](const ParameterList& parameters) {
            int servo_id = parameters["servoId"].number();
            int angle = parameters["angle"].number();
            int steps = parameters["steps"].number();
            int delay_ms = parameters["delayMs"].number();
            
            if (servo_id < 0 || servo_id >= servos_.size()) {
                ESP_LOGW(TAG, "Invalid servo ID: %d", servo_id);
                return;
            }
            
            ESP_LOGI(TAG, "Smooth moving servo %d to angle %d with %d steps and %d ms delay", 
                     servo_id, angle, steps, delay_ms);
            servos_[servo_id].smooth_move(angle, steps, delay_ms);
        });
        
        // 添加设置舵机角度范围方法
        ParameterList rangeParams;
        rangeParams.AddParameter(Parameter("servoId", "舵机ID (0-based索引)", kValueTypeNumber));
        rangeParams.AddParameter(Parameter("minAngle", "最小角度", kValueTypeNumber));
        rangeParams.AddParameter(Parameter("maxAngle", "最大角度", kValueTypeNumber));
        
        methods_.AddMethod("SetAngleRange", "设置舵机角度范围", rangeParams, [this](const ParameterList& parameters) {
            int servo_id = parameters["servoId"].number();
            int min_angle = parameters["minAngle"].number();
            int max_angle = parameters["maxAngle"].number();
            
            if (servo_id < 0 || servo_id >= servos_.size()) {
                ESP_LOGW(TAG, "Invalid servo ID: %d", servo_id);
                return;
            }
            
            servos_[servo_id].set_angle_range(min_angle, max_angle);
            ESP_LOGI(TAG, "Set servo %d angle range to [%d, %d]", servo_id, min_angle, max_angle);
        });
        
        // 添加开始舵机扫描方法
        ParameterList sweepParams;
        sweepParams.AddParameter(Parameter("servoId", "舵机ID (0-based索引)", kValueTypeNumber));
        sweepParams.AddParameter(Parameter("step", "扫描步长", kValueTypeNumber));
        sweepParams.AddParameter(Parameter("delayMs", "步进延迟(毫秒)", kValueTypeNumber));
        
        methods_.AddMethod("StartSweep", "开始舵机来回扫描", sweepParams, [this](const ParameterList& parameters) {
            int servo_id = parameters["servoId"].number();
            int step = parameters["step"].number();
            int delay_ms = parameters["delayMs"].number();
            
            if (servo_id < 0 || servo_id >= servos_.size()) {
                ESP_LOGW(TAG, "Invalid servo ID: %d", servo_id);
                return;
            }
            
            servos_[servo_id].set_sweep_params(step, delay_ms);
            servos_[servo_id].start_sweep();
            ESP_LOGI(TAG, "Started servo %d sweep with step %d and delay %d ms", 
                     servo_id, step, delay_ms);
        });
        
        // 添加开始舵机连续旋转方法
        ParameterList continuousParams;
        continuousParams.AddParameter(Parameter("servoId", "舵机ID (0-based索引)", kValueTypeNumber));
        continuousParams.AddParameter(Parameter("clockwise", "是否顺时针", kValueTypeBoolean));
        continuousParams.AddParameter(Parameter("step", "步长", kValueTypeNumber));
        continuousParams.AddParameter(Parameter("delayMs", "步进延迟(毫秒)", kValueTypeNumber));
        
        methods_.AddMethod("StartContinuous", "开始舵机连续旋转", continuousParams, [this](const ParameterList& parameters) {
            int servo_id = parameters["servoId"].number();
            bool clockwise = parameters["clockwise"].boolean();
            int step = parameters["step"].number();
            int delay_ms = parameters["delayMs"].number();
            
            if (servo_id < 0 || servo_id >= servos_.size()) {
                ESP_LOGW(TAG, "Invalid servo ID: %d", servo_id);
                return;
            }
            
            servos_[servo_id].set_sweep_params(step, delay_ms);
            servos_[servo_id].start_continuous(clockwise);
            ESP_LOGI(TAG, "Started servo %d continuous rotation: %s with step %d and delay %d ms", 
                     servo_id, clockwise ? "clockwise" : "counter-clockwise", step, delay_ms);
        });
        
        // 添加停止舵机方法
        ParameterList stopParams;
        stopParams.AddParameter(Parameter("servoId", "舵机ID (0-based索引)", kValueTypeNumber));
        
        methods_.AddMethod("Stop", "停止舵机运动", stopParams, [this](const ParameterList& parameters) {
            int servo_id = parameters["servoId"].number();
            
            if (servo_id < 0 || servo_id >= servos_.size()) {
                ESP_LOGW(TAG, "Invalid servo ID: %d", servo_id);
                return;
            }
            
            servos_[servo_id].stop();
            ESP_LOGI(TAG, "Stopped servo %d", servo_id);
        });
    }
    
    ~ServoThing() {
        // 停止所有舵机
        for (auto& servo : servos_) {
            servo.deinitialize();
        }
    }
};

} // namespace iot

// 根据配置决定是否注册舵机Thing
#ifdef CONFIG_ENABLE_SERVO_CONTROLLER
DECLARE_THING(ServoThing);
#endif 