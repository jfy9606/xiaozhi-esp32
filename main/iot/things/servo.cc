/**
 * @file servo.cc
 * @brief 舵机控制器实现和舵机Thing类
 */

#include "iot/things/servo.h"
#include "iot/thing.h"
#include "iot/thing_manager.h"
#include "board.h"
#include "../boards/common/board.h"
#include "ext/include/lu9685.h"
#include "ext/include/i2c_utils.h"  // 新添加的I2C工具头文件
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
#include <driver/gpio.h>
#include <inttypes.h>

// 避免TAG重定义的问题
#ifndef SERVO_THING_TAG
#define SERVO_THING_TAG "ServoThing"
#endif

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

// 如果此函数将来可能需要使用，保留它但添加unused属性
static i2c_master_bus_handle_t __attribute__((unused)) get_i2c_bus_handle(i2c_port_t port) {
    static i2c_master_bus_handle_t bus_handle = NULL;
    
    if (bus_handle == NULL) {
        // 创建I2C总线配置 - 确保字段顺序与声明顺序一致，并添加必要的类型转换
        i2c_master_bus_config_t bus_config = {
            .i2c_port = port,              // 首先是port，确保与声明顺序一致
            .sda_io_num = (gpio_num_t)I2C_MUX_SDA_PIN,  // 添加到gpio_num_t的类型转换
            .scl_io_num = (gpio_num_t)I2C_MUX_SCL_PIN,  // 添加到gpio_num_t的类型转换
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags = {
                .enable_internal_pullup = true
            }
        };
        
        // 创建I2C总线
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));
        
        // 测试总线是否工作
        // 使用__attribute__((unused))注解而非声明未使用的变量
        __attribute__((unused)) esp_err_t ret = ESP_OK;
    }
    
    return bus_handle;
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
    
    // 检查I2C总线句柄
    if (controller->i2c_bus_handle == NULL) {
        ESP_LOGE(TAG_CTRL, "LU9685 init failed: I2C bus handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 如果使用多路复用器，切换到LU9685所在的通道
    // LU9685通常连接在PCA9548A的通道1(SC1,SD1)
    if (pca9548a_is_initialized()) {
        ESP_LOGI(TAG_CTRL, "Selecting multiplexer channel %d for LU9685", CONFIG_LU9685_PCA9548A_CHANNEL);
        esp_err_t ret = pca9548a_select_channel(1 << CONFIG_LU9685_PCA9548A_CHANNEL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_CTRL, "Failed to select multiplexer channel: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // 给通道切换一些时间稳定
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // 尝试探测LU9685设备是否存在
    ESP_LOGI(TAG_CTRL, "Probing for LU9685 at address 0x%02X", CONFIG_LU9685_I2C_ADDR);
    esp_err_t probe_ret = i2c_master_probe(controller->i2c_bus_handle, CONFIG_LU9685_I2C_ADDR, 200);
    if (probe_ret != ESP_OK) {
        ESP_LOGE(TAG_CTRL, "LU9685 device not found at address 0x%02X: %s", 
                CONFIG_LU9685_I2C_ADDR, esp_err_to_name(probe_ret));
        
        // 检查地址是否可能是8位格式
        if (CONFIG_LU9685_I2C_ADDR > 0x7F) {
            ESP_LOGE(TAG_CTRL, "LU9685 I2C address may be in 8-bit format. Try using 7-bit format (addr >> 1)");
            uint8_t addr_7bit = I2C_8BIT_TO_7BIT(CONFIG_LU9685_I2C_ADDR);
            
            // 尝试使用7位地址格式
            probe_ret = i2c_master_probe(controller->i2c_bus_handle, addr_7bit, 200);
            if (probe_ret == ESP_OK) {
                ESP_LOGW(TAG_CTRL, "LU9685 found at 7-bit address 0x%02X. Using this instead.", addr_7bit);
                ESP_LOGI(TAG_CTRL, "Please update CONFIG_LU9685_I2C_ADDR in Kconfig to use 7-bit address format");
                
                // 注意：这里我们不能直接修改CONFIG_LU9685_I2C_ADDR（它是一个编译时常量）
                // 但可以在后续使用addr_7bit代替CONFIG_LU9685_I2C_ADDR
                // 理想情况下，用户应该更新Kconfig设置
            } else {
                ESP_LOGE(TAG_CTRL, "LU9685 not found at 7-bit address 0x%02X either: %s", 
                        addr_7bit, esp_err_to_name(probe_ret));
                return ESP_ERR_NOT_FOUND;
            }
        } else {
            return ESP_ERR_NOT_FOUND;
        }
    }
    
    ESP_LOGI(TAG_CTRL, "LU9685 device found at address 0x%02X", CONFIG_LU9685_I2C_ADDR);
    
    // 初始化LU9685
    lu9685_config_t lu_config = {
        .i2c_port = controller->i2c_bus_handle,
        .i2c_addr = CONFIG_LU9685_I2C_ADDR,      // 使用配置中的地址，注意Kconfig中应设为7位格式(0x40)而非8位格式(0x80)
        .pwm_freq = 50,                          // 50Hz，标准舵机频率
        .use_pca9548a = pca9548a_is_initialized(),
        .pca9548a_channel = CONFIG_LU9685_PCA9548A_CHANNEL
    };
    
    controller->lu9685.handle = lu9685_init(&lu_config);
    if (controller->lu9685.handle == NULL) {
        ESP_LOGE(TAG_CTRL, "Failed to initialize LU9685");
        return ESP_FAIL;
    }
    
    // 设置LU9685频率为50Hz (标准舵机频率)
    esp_err_t ret = lu9685_set_frequency(controller->lu9685.handle, 50);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_CTRL, "Failed to set LU9685 frequency: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 设置舵机通道参数
    controller->lu9685.left_channel = controller->lu9685.left_channel;
    controller->lu9685.right_channel = controller->lu9685.right_channel;
    
    // 标记为初始化完成
    controller->lu9685.lu9685_initialized = true;
    
    // 重置所有舵机到中间位置
    for (int i = 0; i < 16; i++) {
        lu9685_set_channel_angle(controller->lu9685.handle, i, 90);
    }
    
    ESP_LOGI(TAG_CTRL, "LU9685 servo controller initialized successfully");
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

namespace iot {

// 定义舵机操作模式枚举
typedef enum {
    SERVO_MODE_STATIC,       // 静态模式 - 保持在固定位置
    SERVO_MODE_SWEEP,        // 扫描模式 - 在最小/最大角度之间来回移动
    SERVO_MODE_CONTINUOUS    // 连续旋转模式 - 不停旋转
} servo_mode_t;

// 定义舵机类型枚举
typedef enum {
    SERVO_TYPE_UNKNOWN,      // 未知类型
    SERVO_TYPE_STANDARD,     // 标准舵机
    SERVO_TYPE_CONTINUOUS,   // 连续旋转舵机
    SERVO_TYPE_LU9685        // LU9685控制器连接的舵机
} ServoType;

// 添加最大舵机数量常量
#define MAX_SERVOS              8       // 最多支持8个舵机

/**
 * @brief 舵机控制类，用于控制伺服电机的角度和运动模式
 */
class Servo {
private:
    static constexpr const char* TAG = "Servo"; // 添加TAG定义用于日志
    
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

/**
 * @brief ServoThing类，用于提供标准的IoT舵机控制接口
 */
class ServoThing : public Thing {
private:
    // 私有成员变量
    Servo* servos_[MAX_SERVOS];  // 最多支持8个舵机的数组
    int servo_count_;            // 实际舵机数量
    ServoType servo_types_[MAX_SERVOS]; // 舵机类型数组
    bool is_initialized_;        // 是否初始化
    char component_path_[64];    // 组件路径

public:
    // 公共方法
    ServoThing(const char* id = "servo") : Thing(std::string(id), "舵机控制器") {
        servo_count_ = 0;
        is_initialized_ = false;
        strncpy(component_path_, "/servo", sizeof(component_path_) - 1);
        
        // 初始化数组
        for (int i = 0; i < MAX_SERVOS; i++) {
            servos_[i] = nullptr;
            servo_types_[i] = SERVO_TYPE_UNKNOWN;
        }
    }

    ~ServoThing() {
        for (int i = 0; i < servo_count_; i++) {
            if (servos_[i]) {
                delete servos_[i];
                servos_[i] = nullptr;
            }
        }
        servo_count_ = 0;
    }

    bool Init();
};

// ServoThing::Init方法实现
bool ServoThing::Init() {
    static const char* TAG = "ServoThing";
    ESP_LOGI(TAG, "Initializing ServoThing");
    
    // 初始化标记
    is_initialized_ = true;
    
    // 根据配置初始化舵机
#ifdef CONFIG_SERVO_CONNECTION_DIRECT
    // 直接连接方式，使用GPIO控制舵机
    ESP_LOGI(TAG, "Using direct GPIO servo connection");
    
    // 获取舵机配置
    board_config_t* config = board_get_config();
    if (config == nullptr) {
        ESP_LOGE(TAG, "Failed to get board config");
        return false;
    }
    
    // 添加舵机
    for (int i = 0; i < config->servo_count && i < MAX_SERVOS; i++) {
        if (config->servo_pins[i] >= 0) {
            servos_[servo_count_] = new Servo();
            if (servos_[servo_count_]->initialize(config->servo_pins[i], LEDC_CHANNEL_0 + servo_count_)) {
                servo_types_[servo_count_] = SERVO_TYPE_STANDARD;
                servo_count_++;
                ESP_LOGI(TAG, "Initialized servo %d on pin %d", servo_count_, config->servo_pins[i]);
            } else {
                delete servos_[servo_count_];
                servos_[servo_count_] = nullptr;
                ESP_LOGE(TAG, "Failed to initialize servo on pin %d", config->servo_pins[i]);
            }
        }
    }
#elif defined(CONFIG_SERVO_CONNECTION_LU9685)
    // LU9685舵机控制器方式
    ESP_LOGI(TAG, "Using LU9685 servo controller");
    
    // 这里添加LU9685舵机控制器初始化代码
    // ...
#endif
    
    if (servo_count_ == 0) {
        ESP_LOGW(TAG, "No servos were initialized");
        is_initialized_ = false;
        return false;
    }
    
    ESP_LOGI(TAG, "ServoThing initialized with %d servos", servo_count_);
    return true;
}

} // namespace iot

// 根据配置决定是否注册舵机Thing
#ifdef CONFIG_ENABLE_SERVO_CONTROLLER
DECLARE_THING(ServoThing);
#endif 