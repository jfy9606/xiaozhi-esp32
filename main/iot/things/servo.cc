/**
 * @file servo.cc
 * @brief 舵机控制器实现和舵机Thing类
 */

#include "iot/things/servo.h"
#include "iot/thing.h"
#include "iot/thing_manager.h"
#include "board.h"
#include "../boards/common/board_config.h"
#include "ext/include/lu9685.h"
#include "sdkconfig.h"
#include "include/multiplexer.h"

#include <esp_log.h>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <driver/ledc.h>
#include <algorithm>
#include <vector>
#include <string.h>
#include <driver/i2c_master.h>

// C语言实现部分，舵机控制器API
extern "C" {

#define TAG_CTRL "ServoCtrl"

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

// LEDC通道配置
#define SERVO_LEDC_TIMER LEDC_TIMER_0
#define SERVO_LEDC_MODE LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_CHANNEL_LEFT LEDC_CHANNEL_0
#define SERVO_LEDC_CHANNEL_RIGHT LEDC_CHANNEL_1
#define SERVO_LEDC_CHANNEL_UP LEDC_CHANNEL_2
#define SERVO_LEDC_CHANNEL_DOWN LEDC_CHANNEL_3

// 舵机控制器结构体
typedef struct servo_controller_t {
    servo_controller_type_t type;
    bool is_initialized;
    
    // I2C端口配置
    i2c_master_bus_handle_t i2c_bus_handle;  // I2C总线句柄
    i2c_port_t i2c_port;                     // I2C端口号
    
    // 直接GPIO控制时的配置
    struct {
        int left_pin;
        int right_pin;
        int up_pin;
        int down_pin;
        bool gpio_initialized;
    } gpio;
    
    // LU9685控制时的配置
    struct {
        lu9685_handle_t handle;
        uint8_t left_channel;
        uint8_t right_channel;
        uint8_t up_channel;
        uint8_t down_channel;
        bool lu9685_initialized;
    } lu9685;
    
} servo_controller_t;

// 全局单例
static servo_controller_t *servo_ctrl = NULL;

// 将角度转换为占空比
static uint32_t angle_to_duty(int angle) {
    // 限制角度范围
    if (angle < SERVO_MIN_ANGLE) {
        angle = SERVO_MIN_ANGLE;
    } else if (angle > SERVO_MAX_ANGLE) {
        angle = SERVO_MAX_ANGLE;
    }
    
    // 计算脉冲宽度 (微秒)
    uint32_t pulse_width_us = SERVO_MIN_PULSE_WIDTH_US + 
        ((SERVO_MAX_PULSE_WIDTH_US - SERVO_MIN_PULSE_WIDTH_US) * angle) / (SERVO_MAX_ANGLE - SERVO_MIN_ANGLE);
    
    // 将脉冲宽度转换为LEDC占空比
    // LEDC最大值 = (2^SERVO_TIMER_RESOLUTION_BITS) - 1
    uint32_t max_duty = (1 << SERVO_TIMER_RESOLUTION_BITS) - 1;
    
    // 计算占空比: pulse_width / period
    // period = 1000000 / SERVO_FREQUENCY_HZ (微秒)
    uint32_t duty = (pulse_width_us * max_duty) / (1000000 / SERVO_FREQUENCY_HZ);
    
    return duty;
}

// 获取I2C总线句柄
static i2c_master_bus_handle_t get_i2c_bus_handle(i2c_port_t port) {
    // 检查PCA9548A是否初始化，如果已初始化，可以尝试获取Board的I2C总线句柄
    if (pca9548a_is_initialized()) {
        ESP_LOGI(TAG_CTRL, "PCA9548A is initialized, can use the same I2C bus");
    }
    
    // 尝试获取Board的I2C总线句柄
    i2c_master_bus_handle_t board_bus = board_get_i2c_bus_handle();
    if (board_bus != NULL) {
        ESP_LOGI(TAG_CTRL, "Using board's I2C bus handle");
        return board_bus;
    }
    
    // 如果无法获取，记录警告并返回NULL
    ESP_LOGW(TAG_CTRL, "Could not get I2C bus handle, returning NULL");
    return NULL;
}

// 初始化直接GPIO控制的PWM配置
static esp_err_t init_gpio_servo(servo_controller_t *controller) {
    if (controller == NULL || controller->gpio.gpio_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG_CTRL, "Initializing GPIO servo controller");
    
    // 配置LEDC定时器
    ledc_timer_config_t timer_conf = {
        .speed_mode = SERVO_LEDC_MODE,
        .duty_resolution = (ledc_timer_bit_t)SERVO_TIMER_RESOLUTION_BITS,
        .timer_num = SERVO_LEDC_TIMER,
        .freq_hz = SERVO_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    
    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_CTRL, "LEDC timer config failed: %s", esp_err_to_name(err));
        return err;
    }
    
    // 配置左舵机通道
    if (controller->gpio.left_pin >= 0) {
        ledc_channel_config_t ledc_conf = {
            .gpio_num = controller->gpio.left_pin,
            .speed_mode = SERVO_LEDC_MODE,
            .channel = SERVO_LEDC_CHANNEL_LEFT,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = SERVO_LEDC_TIMER,
            .duty = angle_to_duty(SERVO_DEFAULT_ANGLE),
            .hpoint = 0,
            .flags = {
                .output_invert = 0
            }
        };
        
        err = ledc_channel_config(&ledc_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_CTRL, "Left servo LEDC channel config failed: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG_CTRL, "Left servo initialized on GPIO %d", controller->gpio.left_pin);
    }
    
    // 配置右舵机通道
    if (controller->gpio.right_pin >= 0) {
        ledc_channel_config_t ledc_conf = {
            .gpio_num = controller->gpio.right_pin,
            .speed_mode = SERVO_LEDC_MODE,
            .channel = SERVO_LEDC_CHANNEL_RIGHT,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = SERVO_LEDC_TIMER,
            .duty = angle_to_duty(SERVO_DEFAULT_ANGLE),
            .hpoint = 0,
            .flags = {
                .output_invert = 0
            }
        };
        
        err = ledc_channel_config(&ledc_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_CTRL, "Right servo LEDC channel config failed: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG_CTRL, "Right servo initialized on GPIO %d", controller->gpio.right_pin);
    }
    
    // 配置上舵机通道
    if (controller->gpio.up_pin >= 0) {
        ledc_channel_config_t ledc_conf = {
            .gpio_num = controller->gpio.up_pin,
            .speed_mode = SERVO_LEDC_MODE,
            .channel = SERVO_LEDC_CHANNEL_UP,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = SERVO_LEDC_TIMER,
            .duty = angle_to_duty(SERVO_DEFAULT_ANGLE),
            .hpoint = 0,
            .flags = {
                .output_invert = 0
            }
        };
        
        err = ledc_channel_config(&ledc_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_CTRL, "Up servo LEDC channel config failed: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG_CTRL, "Up servo initialized on GPIO %d", controller->gpio.up_pin);
    }
    
    // 配置下舵机通道
    if (controller->gpio.down_pin >= 0) {
        ledc_channel_config_t ledc_conf = {
            .gpio_num = controller->gpio.down_pin,
            .speed_mode = SERVO_LEDC_MODE,
            .channel = SERVO_LEDC_CHANNEL_DOWN,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = SERVO_LEDC_TIMER,
            .duty = angle_to_duty(SERVO_DEFAULT_ANGLE),
            .hpoint = 0,
            .flags = {
                .output_invert = 0
            }
        };
        
        err = ledc_channel_config(&ledc_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_CTRL, "Down servo LEDC channel config failed: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG_CTRL, "Down servo initialized on GPIO %d", controller->gpio.down_pin);
    }
    
    controller->gpio.gpio_initialized = true;
    
    // 将所有舵机重置到中心位置
    servo_controller_reset((servo_controller_handle_t)controller);
    
    return ESP_OK;
}

// 初始化LU9685控制
static esp_err_t init_lu9685_servo(servo_controller_t *controller) {
    if (controller == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 如果已经初始化过，直接返回OK
    if (controller->lu9685.lu9685_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG_CTRL, "Initializing LU9685 servo controller");
    
    // 检查 PCA9548A 是否初始化完成
    if (!pca9548a_is_initialized()) {
        ESP_LOGI(TAG_CTRL, "PCA9548A not initialized yet, initializing multiplexer first");
        esp_err_t ret = multiplexer_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_CTRL, "Failed to initialize multiplexer: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // 确保初始化后 PCA9548A 就绪
        if (!pca9548a_is_initialized()) {
            ESP_LOGE(TAG_CTRL, "PCA9548A still not initialized after multiplexer_init()");
            return ESP_FAIL;
        }
        
        // 给 PCA9548A 更多时间稳定
        vTaskDelay(pdMS_TO_TICKS(100));
    } else {
        // 即使已初始化，也等待一下保证稳定
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // 在获取I2C总线句柄前，先明确切换到LU9685所在的通道1
    ESP_LOGI(TAG_CTRL, "Explicitly switching to PCA9548A channel %d for LU9685", CONFIG_LU9685_PCA9548A_CHANNEL);
    esp_err_t switch_ret = pca9548a_select_channel(CONFIG_LU9685_PCA9548A_CHANNEL);
    if (switch_ret != ESP_OK) {
        ESP_LOGE(TAG_CTRL, "Failed to switch PCA9548A to channel %d: %s", 
                CONFIG_LU9685_PCA9548A_CHANNEL, esp_err_to_name(switch_ret));
        // 虽然切换失败，但我们仍然尝试继续，可能总线已经在正确的通道上
        // 记录警告而不是直接返回错误
        ESP_LOGW(TAG_CTRL, "Attempting to continue despite channel switch failure");
    } else {
        ESP_LOGI(TAG_CTRL, "Successfully switched to PCA9548A channel %d", CONFIG_LU9685_PCA9548A_CHANNEL);
        // 给通道切换一点时间稳定
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    
    // 尝试获取I2C总线句柄，添加超时保护
    int retry_count = 0;
    const int max_retries = 3;
    
    while (controller->i2c_bus_handle == NULL && retry_count < max_retries) {
        controller->i2c_bus_handle = get_i2c_bus_handle(controller->i2c_port);
        if (controller->i2c_bus_handle == NULL) {
            ESP_LOGW(TAG_CTRL, "Failed to get I2C bus handle (attempt %d/%d), retrying...", 
                    retry_count + 1, max_retries);
            retry_count++;
            vTaskDelay(pdMS_TO_TICKS(50));  // 等待一段时间后重试
        } else {
            ESP_LOGI(TAG_CTRL, "Successfully obtained I2C bus handle after %d attempt(s)", retry_count + 1);
        }
    }
    
    // 如果最终无法获取I2C句柄，记录错误但继续尝试初始化（也许可以创建新的总线）
    if (controller->i2c_bus_handle == NULL) {
        ESP_LOGE(TAG_CTRL, "Failed to get I2C bus handle after %d attempts", max_retries);
        // 为了避免完全卡住，我们可以返回错误或继续尝试初始化
        // 这里我们选择继续尝试初始化，但记录警告
        ESP_LOGW(TAG_CTRL, "Attempting to continue without valid I2C bus handle");
    }
    
    // 初始化LU9685驱动
    lu9685_config_t lu9685_config = {
        .i2c_port = controller->i2c_bus_handle,  // 使用总线句柄
        .i2c_addr = CONFIG_LU9685_I2C_ADDR,      // 使用配置中的地址
        .pwm_freq = 50,                          // 50Hz
        .use_pca9548a = true,                    // 使用 PCA9548A
        .pca9548a_channel = CONFIG_LU9685_PCA9548A_CHANNEL  // 使用配置中的通道
    };
    
    ESP_LOGI(TAG_CTRL, "Configuring LU9685 with PCA9548A channel %d", CONFIG_LU9685_PCA9548A_CHANNEL);
    
    // 在初始化前多等待一段时间
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 再次确保我们在正确的通道
    ESP_LOGI(TAG_CTRL, "Rechecking PCA9548A channel before initializing LU9685");
    esp_err_t recheck_ret = pca9548a_select_channel(CONFIG_LU9685_PCA9548A_CHANNEL);
    if (recheck_ret == ESP_OK) {
        ESP_LOGI(TAG_CTRL, "PCA9548A channel %d confirmed", CONFIG_LU9685_PCA9548A_CHANNEL);
    } else {
        ESP_LOGW(TAG_CTRL, "Failed to recheck PCA9548A channel: %s", esp_err_to_name(recheck_ret));
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // 设置超时保护
    const TickType_t xTimeout = pdMS_TO_TICKS(5000); // 5秒超时
    TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
    
    // 创建一个看门狗任务来防止无限等待
    TaskHandle_t watchdogTaskHandle = NULL;
    xTaskCreate([](void* pvParameters) {
        TaskHandle_t mainTaskHandle = (TaskHandle_t)pvParameters;
        vTaskDelay(pdMS_TO_TICKS(5000)); // 等待5秒
        
        // 如果主任务仍在等待，记录警告并继续
        ESP_LOGW(TAG_CTRL, "LU9685 initialization timed out, resuming main task");
        
        // 恢复主任务
        if (eTaskGetState(mainTaskHandle) != eDeleted) {
            xTaskAbortDelay(mainTaskHandle);
        }
        
        vTaskDelete(NULL);
    }, "lu9685_watchdog", 2048, currentTask, tskIDLE_PRIORITY + 1, &watchdogTaskHandle);
    
    // 尝试初始化LU9685
    controller->lu9685.handle = lu9685_init(&lu9685_config);
    
    // 删除看门狗任务，因为已经完成或超时
    if (watchdogTaskHandle != NULL) {
        vTaskDelete(watchdogTaskHandle);
    }
    
    if (controller->lu9685.handle == NULL) {
        ESP_LOGE(TAG_CTRL, "Failed to initialize LU9685 servo controller");
        
        // 即使初始化失败，也标记为初始化完成以避免卡住启动
        controller->lu9685.lu9685_initialized = true;
        controller->is_initialized = true;
        ESP_LOGW(TAG_CTRL, "Marking servo controller as initialized despite failure to avoid boot hang");
        
        return ESP_FAIL;
    }
    
    // 初始化成功后再等待一段时间
    vTaskDelay(pdMS_TO_TICKS(50));
    
    controller->lu9685.lu9685_initialized = true;
    ESP_LOGI(TAG_CTRL, "LU9685 servo controller initialized successfully");
    
    // 将所有舵机重置到中心位置
    servo_controller_reset((servo_controller_handle_t)controller);
    
    return ESP_OK;
}

servo_controller_handle_t servo_controller_init(const servo_controller_config_t *config) {
    if (servo_ctrl != NULL) {
        ESP_LOGW(TAG_CTRL, "Servo controller already initialized");
        return servo_ctrl;
    }
    
    if (config == NULL) {
        ESP_LOGE(TAG_CTRL, "Config is NULL");
        return NULL;
    }
    
    servo_ctrl = (servo_controller_t *)calloc(1, sizeof(servo_controller_t));
    if (servo_ctrl == NULL) {
        ESP_LOGE(TAG_CTRL, "Failed to allocate memory for servo controller");
        return NULL;
    }
    
    // 配置控制器
    servo_ctrl->type = config->type;
    
    // 保存I2C总线句柄
    servo_ctrl->i2c_port = config->i2c_port;
    servo_ctrl->i2c_bus_handle = config->i2c_bus_handle;
    
    if (config->type == SERVO_CONTROLLER_TYPE_DIRECT) {
        // 配置直接GPIO控制
        servo_ctrl->gpio.left_pin = config->gpio.left_pin;
        servo_ctrl->gpio.right_pin = config->gpio.right_pin;
        servo_ctrl->gpio.up_pin = config->gpio.up_pin;
        servo_ctrl->gpio.down_pin = config->gpio.down_pin;
        
        if (init_gpio_servo(servo_ctrl) != ESP_OK) {
            ESP_LOGE(TAG_CTRL, "Failed to initialize GPIO servo controller");
            free(servo_ctrl);
            servo_ctrl = NULL;
            return NULL;
        }
    } else if (config->type == SERVO_CONTROLLER_TYPE_LU9685) {
        // 配置LU9685控制
        servo_ctrl->lu9685.left_channel = config->lu9685.left_channel;
        servo_ctrl->lu9685.right_channel = config->lu9685.right_channel;
        servo_ctrl->lu9685.up_channel = config->lu9685.up_channel;
        servo_ctrl->lu9685.down_channel = config->lu9685.down_channel;
        
        if (init_lu9685_servo(servo_ctrl) != ESP_OK) {
            ESP_LOGE(TAG_CTRL, "Failed to initialize LU9685 servo controller");
            free(servo_ctrl);
            servo_ctrl = NULL;
            return NULL;
        }
    } else {
        ESP_LOGE(TAG_CTRL, "Invalid servo controller type: %d", config->type);
        free(servo_ctrl);
        servo_ctrl = NULL;
        return NULL;
    }
    
    servo_ctrl->is_initialized = true;
    ESP_LOGI(TAG_CTRL, "Servo controller initialized successfully");
    
    return (servo_controller_handle_t)servo_ctrl;
}

esp_err_t servo_controller_deinit(servo_controller_handle_t handle) {
    servo_controller_t *controller = (servo_controller_t *)handle;
    if (controller == NULL || controller != servo_ctrl) {
        ESP_LOGE(TAG_CTRL, "Invalid servo controller handle");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!controller->is_initialized) {
        ESP_LOGW(TAG_CTRL, "Servo controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (controller->type == SERVO_CONTROLLER_TYPE_DIRECT) {
        // 停止直接GPIO控制
        if (controller->gpio.left_pin >= 0) {
            ledc_stop(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL_LEFT, 0);
        }
        if (controller->gpio.right_pin >= 0) {
            ledc_stop(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL_RIGHT, 0);
        }
        if (controller->gpio.up_pin >= 0) {
            ledc_stop(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL_UP, 0);
        }
        if (controller->gpio.down_pin >= 0) {
            ledc_stop(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL_DOWN, 0);
        }
        
        controller->gpio.gpio_initialized = false;
    } else if (controller->type == SERVO_CONTROLLER_TYPE_LU9685) {
        // 释放LU9685控制器
        if (controller->lu9685.handle != NULL) {
            lu9685_handle_t handle_ptr = controller->lu9685.handle;
            lu9685_deinit(&handle_ptr);
            controller->lu9685.handle = NULL;
        }
        
        controller->lu9685.lu9685_initialized = false;
    }
    
    controller->is_initialized = false;
    
    free(servo_ctrl);
    servo_ctrl = NULL;
    
    ESP_LOGI(TAG_CTRL, "Servo controller deinitialized");
    return ESP_OK;
}

esp_err_t servo_controller_set_left_angle(servo_controller_handle_t handle, uint8_t angle) {
    servo_controller_t *controller = (servo_controller_t *)handle;
    if (controller == NULL || controller != servo_ctrl) {
        ESP_LOGE(TAG_CTRL, "Invalid servo controller handle");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!controller->is_initialized) {
        ESP_LOGW(TAG_CTRL, "Servo controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG_CTRL, "Setting left servo to angle %d", angle);
    
    if (angle > SERVO_MAX_ANGLE) {
        angle = SERVO_MAX_ANGLE;
    }
    
    if (controller->type == SERVO_CONTROLLER_TYPE_DIRECT) {
        // 直接GPIO控制
        if (controller->gpio.left_pin >= 0) {
            uint32_t duty = angle_to_duty(angle);
            esp_err_t err = ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL_LEFT, duty);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_CTRL, "Set left servo duty failed: %s", esp_err_to_name(err));
                return err;
            }
            
            err = ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL_LEFT);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_CTRL, "Update left servo duty failed: %s", esp_err_to_name(err));
                return err;
            }
        }
    } else if (controller->type == SERVO_CONTROLLER_TYPE_LU9685) {
        // LU9685控制
        if (controller->lu9685.handle != NULL) {
            esp_err_t err = lu9685_set_channel_angle(controller->lu9685.handle, 
                                               controller->lu9685.left_channel, 
                                               angle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_CTRL, "Set left servo angle through LU9685 failed: %s", esp_err_to_name(err));
                return err;
            }
        }
    }
    
    return ESP_OK;
}

esp_err_t servo_controller_set_right_angle(servo_controller_handle_t handle, uint8_t angle) {
    servo_controller_t *controller = (servo_controller_t *)handle;
    if (controller == NULL || controller != servo_ctrl) {
        ESP_LOGE(TAG_CTRL, "Invalid servo controller handle");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!controller->is_initialized) {
        ESP_LOGW(TAG_CTRL, "Servo controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG_CTRL, "Setting right servo to angle %d", angle);
    
    if (angle > SERVO_MAX_ANGLE) {
        angle = SERVO_MAX_ANGLE;
    }
    
    if (controller->type == SERVO_CONTROLLER_TYPE_DIRECT) {
        // 直接GPIO控制
        if (controller->gpio.right_pin >= 0) {
            uint32_t duty = angle_to_duty(angle);
            esp_err_t err = ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL_RIGHT, duty);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_CTRL, "Set right servo duty failed: %s", esp_err_to_name(err));
                return err;
            }
            
            err = ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL_RIGHT);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_CTRL, "Update right servo duty failed: %s", esp_err_to_name(err));
                return err;
            }
        }
    } else if (controller->type == SERVO_CONTROLLER_TYPE_LU9685) {
        // LU9685控制
        if (controller->lu9685.handle != NULL) {
            esp_err_t err = lu9685_set_channel_angle(controller->lu9685.handle, 
                                               controller->lu9685.right_channel, 
                                               angle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_CTRL, "Set right servo angle through LU9685 failed: %s", esp_err_to_name(err));
                return err;
            }
        }
    }
    
    return ESP_OK;
}

esp_err_t servo_controller_set_up_angle(servo_controller_handle_t handle, uint8_t angle) {
    servo_controller_t *controller = (servo_controller_t *)handle;
    if (controller == NULL || controller != servo_ctrl) {
        ESP_LOGE(TAG_CTRL, "Invalid servo controller handle");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!controller->is_initialized) {
        ESP_LOGW(TAG_CTRL, "Servo controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG_CTRL, "Setting up servo to angle %d", angle);
    
    if (angle > SERVO_MAX_ANGLE) {
        angle = SERVO_MAX_ANGLE;
    }
    
    if (controller->type == SERVO_CONTROLLER_TYPE_DIRECT) {
        // 直接GPIO控制
        if (controller->gpio.up_pin >= 0) {
            uint32_t duty = angle_to_duty(angle);
            esp_err_t err = ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL_UP, duty);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_CTRL, "Set up servo duty failed: %s", esp_err_to_name(err));
                return err;
            }
            
            err = ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL_UP);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_CTRL, "Update up servo duty failed: %s", esp_err_to_name(err));
                return err;
            }
        }
    } else if (controller->type == SERVO_CONTROLLER_TYPE_LU9685) {
        // LU9685控制
        if (controller->lu9685.handle != NULL) {
            esp_err_t err = lu9685_set_channel_angle(controller->lu9685.handle, 
                                               controller->lu9685.up_channel, 
                                               angle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_CTRL, "Set up servo angle through LU9685 failed: %s", esp_err_to_name(err));
                return err;
            }
        }
    }
    
    return ESP_OK;
}

esp_err_t servo_controller_set_down_angle(servo_controller_handle_t handle, uint8_t angle) {
    servo_controller_t *controller = (servo_controller_t *)handle;
    if (controller == NULL || controller != servo_ctrl) {
        ESP_LOGE(TAG_CTRL, "Invalid servo controller handle");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!controller->is_initialized) {
        ESP_LOGW(TAG_CTRL, "Servo controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG_CTRL, "Setting down servo to angle %d", angle);
    
    if (angle > SERVO_MAX_ANGLE) {
        angle = SERVO_MAX_ANGLE;
    }
    
    if (controller->type == SERVO_CONTROLLER_TYPE_DIRECT) {
        // 直接GPIO控制
        if (controller->gpio.down_pin >= 0) {
            uint32_t duty = angle_to_duty(angle);
            esp_err_t err = ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL_DOWN, duty);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_CTRL, "Set down servo duty failed: %s", esp_err_to_name(err));
                return err;
            }
            
            err = ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL_DOWN);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_CTRL, "Update down servo duty failed: %s", esp_err_to_name(err));
                return err;
            }
        }
    } else if (controller->type == SERVO_CONTROLLER_TYPE_LU9685) {
        // LU9685控制
        if (controller->lu9685.handle != NULL) {
            esp_err_t err = lu9685_set_channel_angle(controller->lu9685.handle, 
                                               controller->lu9685.down_channel, 
                                               angle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_CTRL, "Set down servo angle through LU9685 failed: %s", esp_err_to_name(err));
                return err;
            }
        }
    }
    
    return ESP_OK;
}

esp_err_t servo_controller_set_horizontal_angle(servo_controller_handle_t handle, uint8_t angle) {
    esp_err_t err;
    
    // 同时设置左右舵机角度
    err = servo_controller_set_left_angle(handle, angle);
    if (err != ESP_OK) {
        return err;
    }
    
    err = servo_controller_set_right_angle(handle, angle);
    if (err != ESP_OK) {
        return err;
    }
    
    return ESP_OK;
}

esp_err_t servo_controller_set_vertical_angle(servo_controller_handle_t handle, uint8_t angle) {
    esp_err_t err;
    
    // 同时设置上下舵机角度
    err = servo_controller_set_up_angle(handle, angle);
    if (err != ESP_OK) {
        return err;
    }
    
    err = servo_controller_set_down_angle(handle, angle);
    if (err != ESP_OK) {
        return err;
    }
    
    return ESP_OK;
}

esp_err_t servo_controller_reset(servo_controller_handle_t handle) {
    servo_controller_t *controller = (servo_controller_t *)handle;
    if (controller == NULL || controller != servo_ctrl) {
        ESP_LOGE(TAG_CTRL, "Invalid servo controller handle");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!controller->is_initialized) {
        ESP_LOGW(TAG_CTRL, "Servo controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG_CTRL, "Resetting all servos to center position");
    
    // 将所有舵机设置为90度（中间位置）
    esp_err_t err;
    
    err = servo_controller_set_left_angle(handle, SERVO_DEFAULT_ANGLE);
    if (err != ESP_OK) {
        return err;
    }
    
    err = servo_controller_set_right_angle(handle, SERVO_DEFAULT_ANGLE);
    if (err != ESP_OK) {
        return err;
    }
    
    err = servo_controller_set_up_angle(handle, SERVO_DEFAULT_ANGLE);
    if (err != ESP_OK) {
        return err;
    }
    
    err = servo_controller_set_down_angle(handle, SERVO_DEFAULT_ANGLE);
    if (err != ESP_OK) {
        return err;
    }
    
    return ESP_OK;
}

bool servo_controller_is_initialized(void) {
    return (servo_ctrl != NULL && servo_ctrl->is_initialized);
}

servo_controller_handle_t servo_controller_get_handle(void) {
    return (servo_controller_handle_t)servo_ctrl;
} 

} // extern "C"

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
    servo_controller_handle_t servo_ctrl_;
    
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

    // 初始化舵机控制器
    void InitServoController() {
        ESP_LOGI(TAG, "Initializing servo controller");
        
        servo_controller_config_t config = {};
        
        // 设置I2C配置
        board_config_t* board_config = board_get_config();
        if (board_config) {
            // 从Board配置获取I2C端口号
            config.i2c_port = I2C_NUM_0;  // 默认使用I2C0
            
            // 获取I2C总线句柄
            config.i2c_bus_handle = board_get_i2c_bus_handle();
            if (config.i2c_bus_handle == NULL) {
                ESP_LOGW(TAG, "Failed to get I2C bus handle directly, trying through helper function");
                config.i2c_bus_handle = get_i2c_bus_handle(config.i2c_port);
            }
            
            if (config.i2c_bus_handle == NULL) {
                ESP_LOGE(TAG, "Could not get I2C bus handle, servo controller may not work correctly");
                // 继续初始化，尽管I2C总线句柄可能无效
            } else {
                ESP_LOGI(TAG, "Successfully obtained I2C bus handle for servo controller");
            }
        }
        
#ifdef CONFIG_SERVO_CONNECTION_DIRECT
        // 直接GPIO控制方式
        config.type = SERVO_CONTROLLER_TYPE_DIRECT;
        config.gpio.left_pin = CONFIG_SERVO_LEFT_PIN;
        config.gpio.right_pin = CONFIG_SERVO_RIGHT_PIN;
        config.gpio.up_pin = CONFIG_SERVO_UP_PIN;
        config.gpio.down_pin = CONFIG_SERVO_DOWN_PIN;
        ESP_LOGI(TAG, "Using direct GPIO servo control: left=%d, right=%d, up=%d, down=%d",
                 config.gpio.left_pin, config.gpio.right_pin, 
                 config.gpio.up_pin, config.gpio.down_pin);
#elif defined(CONFIG_SERVO_CONNECTION_LU9685)
        // LU9685控制方式
        config.type = SERVO_CONTROLLER_TYPE_LU9685;
        config.lu9685.left_channel = CONFIG_SERVO_LU9685_LEFT_CHANNEL;
        config.lu9685.right_channel = CONFIG_SERVO_LU9685_RIGHT_CHANNEL;
        config.lu9685.up_channel = CONFIG_SERVO_LU9685_UP_CHANNEL;
        config.lu9685.down_channel = CONFIG_SERVO_LU9685_DOWN_CHANNEL;
        ESP_LOGI(TAG, "Using LU9685 servo control: left=%d, right=%d, up=%d, down=%d", 
                 config.lu9685.left_channel, config.lu9685.right_channel,
                 config.lu9685.up_channel, config.lu9685.down_channel);
#else
        ESP_LOGE(TAG, "No servo connection type defined");
        return;
#endif

        // 设置超时保护
        TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
        TaskHandle_t watchdogTaskHandle = NULL;
        
        // 创建一个看门狗任务，确保初始化过程不会卡住
        xTaskCreate([](void* pvParameters) {
            TaskHandle_t mainTaskHandle = (TaskHandle_t)pvParameters;
            vTaskDelay(pdMS_TO_TICKS(8000)); // 给予8秒时间初始化
            
            // 检查任务是否还存在
            if (eTaskGetState(mainTaskHandle) != eDeleted) {
                ESP_LOGW(TAG, "Servo controller initialization took too long, forcing continuation");
                xTaskAbortDelay(mainTaskHandle);
            }
            
            vTaskDelete(NULL);
        }, "servo_init_watchdog", 2048, currentTask, tskIDLE_PRIORITY + 1, &watchdogTaskHandle);
        
        // 初始化舵机控制器
        servo_ctrl_ = servo_controller_init(&config);
        
        // 清理看门狗任务
        if (watchdogTaskHandle != NULL) {
            vTaskDelete(watchdogTaskHandle);
        }
        
        if (servo_ctrl_ == nullptr) {
            ESP_LOGE(TAG, "Failed to initialize servo controller");
        } else {
            ESP_LOGI(TAG, "Servo controller initialized successfully");
        }
    }

public:
    ServoThing() : Thing("Servo", "舵机控制器"), servo_ctrl_(nullptr) {
        // 首先确保多路复用器已初始化
        if (!pca9548a_is_initialized()) {
            ESP_LOGI(TAG, "Initializing multiplexer before servo controller");
            esp_err_t ret = multiplexer_init();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to initialize multiplexer: %s", esp_err_to_name(ret));
            } else {
                ESP_LOGI(TAG, "Multiplexer initialized successfully");
                
                // 给 PCA9548A 一些时间稳定
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        } else {
            ESP_LOGI(TAG, "Multiplexer already initialized");
        }
        
        // 明确切换到PCF8575所在通道0，确保PCF8575可以正常工作
        if (pca9548a_is_initialized()) {
            ESP_LOGI(TAG, "Switching to PCF8575 channel 0 first");
            esp_err_t pcf_ch_ret = pca9548a_select_channel(0);
            if (pcf_ch_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to switch to PCF8575 channel: %s", esp_err_to_name(pcf_ch_ret));
            } else {
                ESP_LOGI(TAG, "Successfully switched to PCF8575 channel");
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
        
        // 然后初始化舵机
        InitServos();
        
        // 再切换到LU9685所在通道1，准备初始化舵机控制器
        if (pca9548a_is_initialized()) {
            ESP_LOGI(TAG, "Switching to LU9685 channel %d", CONFIG_LU9685_PCA9548A_CHANNEL);
            esp_err_t lu_ch_ret = pca9548a_select_channel(CONFIG_LU9685_PCA9548A_CHANNEL);
            if (lu_ch_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to switch to LU9685 channel: %s", esp_err_to_name(lu_ch_ret));
            } else {
                ESP_LOGI(TAG, "Successfully switched to LU9685 channel");
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
        
        // 初始化舵机控制器
        InitServoController();
        
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
        
        // 添加设置水平角度方法
        ParameterList horizontalParams;
        horizontalParams.AddParameter(Parameter("angle", "水平角度 (0-180度)", kValueTypeNumber));
        
        methods_.AddMethod("SetHorizontalAngle", "设置水平方向(左右)舵机角度", horizontalParams, [this](const ParameterList& parameters) {
            if (servo_ctrl_ == nullptr) {
                ESP_LOGW(TAG, "Servo controller not initialized");
                return;
            }
            
            int angle = parameters["angle"].number();
            ESP_LOGI(TAG, "Setting horizontal servo angle to %d", angle);
            
            esp_err_t err = servo_controller_set_horizontal_angle(servo_ctrl_, angle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set horizontal angle: %s", esp_err_to_name(err));
            }
        });
        
        // 添加设置垂直角度方法
        ParameterList verticalParams;
        verticalParams.AddParameter(Parameter("angle", "垂直角度 (0-180度)", kValueTypeNumber));
        
        methods_.AddMethod("SetVerticalAngle", "设置垂直方向(上下)舵机角度", verticalParams, [this](const ParameterList& parameters) {
            if (servo_ctrl_ == nullptr) {
                ESP_LOGW(TAG, "Servo controller not initialized");
                return;
            }
            
            int angle = parameters["angle"].number();
            ESP_LOGI(TAG, "Setting vertical servo angle to %d", angle);
            
            esp_err_t err = servo_controller_set_vertical_angle(servo_ctrl_, angle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set vertical angle: %s", esp_err_to_name(err));
            }
        });
        
        // 添加设置左舵机角度方法
        ParameterList leftParams;
        leftParams.AddParameter(Parameter("angle", "左舵机角度 (0-180度)", kValueTypeNumber));
        
        methods_.AddMethod("SetLeftAngle", "设置左舵机角度", leftParams, [this](const ParameterList& parameters) {
            if (servo_ctrl_ == nullptr) {
                ESP_LOGW(TAG, "Servo controller not initialized");
                return;
            }
            
            int angle = parameters["angle"].number();
            ESP_LOGI(TAG, "Setting left servo angle to %d", angle);
            
            esp_err_t err = servo_controller_set_left_angle(servo_ctrl_, angle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set left angle: %s", esp_err_to_name(err));
            }
        });
        
        // 添加设置右舵机角度方法
        ParameterList rightParams;
        rightParams.AddParameter(Parameter("angle", "右舵机角度 (0-180度)", kValueTypeNumber));
        
        methods_.AddMethod("SetRightAngle", "设置右舵机角度", rightParams, [this](const ParameterList& parameters) {
            if (servo_ctrl_ == nullptr) {
                ESP_LOGW(TAG, "Servo controller not initialized");
                return;
            }
            
            int angle = parameters["angle"].number();
            ESP_LOGI(TAG, "Setting right servo angle to %d", angle);
            
            esp_err_t err = servo_controller_set_right_angle(servo_ctrl_, angle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set right angle: %s", esp_err_to_name(err));
            }
        });
        
        // 添加设置上舵机角度方法
        ParameterList upParams;
        upParams.AddParameter(Parameter("angle", "上舵机角度 (0-180度)", kValueTypeNumber));
        
        methods_.AddMethod("SetUpAngle", "设置上舵机角度", upParams, [this](const ParameterList& parameters) {
            if (servo_ctrl_ == nullptr) {
                ESP_LOGW(TAG, "Servo controller not initialized");
                return;
            }
            
            int angle = parameters["angle"].number();
            ESP_LOGI(TAG, "Setting up servo angle to %d", angle);
            
            esp_err_t err = servo_controller_set_up_angle(servo_ctrl_, angle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set up angle: %s", esp_err_to_name(err));
            }
        });
        
        // 添加设置下舵机角度方法
        ParameterList downParams;
        downParams.AddParameter(Parameter("angle", "下舵机角度 (0-180度)", kValueTypeNumber));
        
        methods_.AddMethod("SetDownAngle", "设置下舵机角度", downParams, [this](const ParameterList& parameters) {
            if (servo_ctrl_ == nullptr) {
                ESP_LOGW(TAG, "Servo controller not initialized");
                return;
            }
            
            int angle = parameters["angle"].number();
            ESP_LOGI(TAG, "Setting down servo angle to %d", angle);
            
            esp_err_t err = servo_controller_set_down_angle(servo_ctrl_, angle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set down angle: %s", esp_err_to_name(err));
            }
        });
        
        // 添加重置舵机位置方法
        ParameterList resetParams;
        methods_.AddMethod("Reset", "重置所有舵机到中间位置", resetParams, [this](const ParameterList& parameters) {
            if (servo_ctrl_ == nullptr) {
                ESP_LOGW(TAG, "Servo controller not initialized");
                return;
            }
            
            ESP_LOGI(TAG, "Resetting all servos to center position");
            
            esp_err_t err = servo_controller_reset(servo_ctrl_);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to reset servos: %s", esp_err_to_name(err));
            }
        });
    }
    
    ~ServoThing() {
        // 停止所有舵机
        for (auto& servo : servos_) {
            servo.deinitialize();
        }
        
        if (servo_ctrl_ != nullptr) {
            servo_controller_deinit(servo_ctrl_);
            servo_ctrl_ = nullptr;
            ESP_LOGI(TAG, "Servo controller deinitialized");
        }
    }
};

} // namespace iot

// 根据配置决定是否注册舵机Thing
#ifdef CONFIG_ENABLE_SERVO_CONTROLLER
DECLARE_THING(ServoThing);
#endif 