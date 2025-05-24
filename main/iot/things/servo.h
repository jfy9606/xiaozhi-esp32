/**
 * @file servo.h
 * @brief 舵机控制模块，包含舵机Thing类和舵机控制器API
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 舵机控制器类型
 */
typedef enum {
    SERVO_CONTROLLER_TYPE_DIRECT,  // 直接GPIO控制
    SERVO_CONTROLLER_TYPE_LU9685   // 通过LU9685-20CU控制
} servo_controller_type_t;

/**
 * @brief 舵机控制器配置
 */
typedef struct {
    servo_controller_type_t type;  // 控制器类型
    
    // 直接GPIO控制时的配置
    struct {
        int left_pin;   // 左侧舵机引脚
        int right_pin;  // 右侧舵机引脚
        int up_pin;     // 上部舵机引脚
        int down_pin;   // 下部舵机引脚
    } gpio;
    
    // LU9685控制时的配置
    struct {
        uint8_t left_channel;   // 左侧舵机通道
        uint8_t right_channel;  // 右侧舵机通道
        uint8_t up_channel;     // 上部舵机通道
        uint8_t down_channel;   // 下部舵机通道
    } lu9685;
    
} servo_controller_config_t;

/**
 * @brief 舵机控制器句柄
 */
typedef struct servo_controller_t* servo_controller_handle_t;

/**
 * @brief 初始化舵机控制器
 * 
 * @param config 舵机控制器配置
 * @return servo_controller_handle_t 控制器句柄，失败返回NULL
 */
servo_controller_handle_t servo_controller_init(const servo_controller_config_t *config);

/**
 * @brief 释放舵机控制器资源
 * 
 * @param handle 舵机控制器句柄
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_controller_deinit(servo_controller_handle_t handle);

/**
 * @brief 设置左侧舵机角度
 * 
 * @param handle 舵机控制器句柄
 * @param angle 角度，范围0-180
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_controller_set_left_angle(servo_controller_handle_t handle, uint8_t angle);

/**
 * @brief 设置右侧舵机角度
 * 
 * @param handle 舵机控制器句柄
 * @param angle 角度，范围0-180
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_controller_set_right_angle(servo_controller_handle_t handle, uint8_t angle);

/**
 * @brief 设置上部舵机角度
 * 
 * @param handle 舵机控制器句柄
 * @param angle 角度，范围0-180
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_controller_set_up_angle(servo_controller_handle_t handle, uint8_t angle);

/**
 * @brief 设置下部舵机角度
 * 
 * @param handle 舵机控制器句柄
 * @param angle 角度，范围0-180
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_controller_set_down_angle(servo_controller_handle_t handle, uint8_t angle);

/**
 * @brief 同步设置左右舵机角度
 * 
 * @param handle 舵机控制器句柄
 * @param angle 角度，范围0-180
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_controller_set_horizontal_angle(servo_controller_handle_t handle, uint8_t angle);

/**
 * @brief 同步设置上下舵机角度
 * 
 * @param handle 舵机控制器句柄
 * @param angle 角度，范围0-180
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_controller_set_vertical_angle(servo_controller_handle_t handle, uint8_t angle);

/**
 * @brief 将所有舵机重置到中间位置(90度)
 * 
 * @param handle 舵机控制器句柄
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_controller_reset(servo_controller_handle_t handle);

/**
 * @brief 检查舵机控制器是否已初始化
 * 
 * @return true 已初始化
 * @return false 未初始化
 */
bool servo_controller_is_initialized(void);

/**
 * @brief 获取舵机控制器句柄
 * 
 * @return servo_controller_handle_t 舵机控制器句柄，未初始化则返回NULL
 */
servo_controller_handle_t servo_controller_get_handle(void);

#ifdef __cplusplus
}
#endif

namespace iot {

/**
 * @brief 舵机控制类
 * 
 * 用于控制舵机的角度、扫描、连续旋转等功能。
 */
class ServoThing;

/**
 * @brief 注册舵机控制Thing
 * 
 * 创建并注册舵机控制Thing实例。
 */
void RegisterServo();

} // namespace iot 