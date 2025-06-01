/**
 * @file lu9685.h
 * @brief LU9685-20CU 16通道PWM/舵机控制器API
 * 
 * 该文件定义了与LU9685-20CU 16通道PWM/舵机控制器通信的API。
 * LU9685-20CU基于STC8H微控制器，支持I2C和UART通信，最高支持300Hz PWM频率。
 * 默认I2C地址为0x80，连接在PCA9548A的通道1上。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// LU9685寄存器定义
#define LU9685_MODE1         0x00
#define LU9685_MODE2         0x01
#define LU9685_SUBADR1       0x02
#define LU9685_SUBADR2       0x03
#define LU9685_SUBADR3       0x04
#define LU9685_ALLCALLADR    0x05
#define LU9685_LED0_ON_L     0x06
#define LU9685_LED0_ON_H     0x07
#define LU9685_LED0_OFF_L    0x08
#define LU9685_LED0_OFF_H    0x09
#define LU9685_ALL_LED_ON_L  0xFA
#define LU9685_ALL_LED_ON_H  0xFB
#define LU9685_ALL_LED_OFF_L 0xFC
#define LU9685_ALL_LED_OFF_H 0xFD
#define LU9685_PRE_SCALE     0xFE    // 频率预分频器寄存器
#define LU9685_TESTMODE      0xFF

// 模式寄存器位定义
#define LU9685_RESTART       0x80
#define LU9685_EXTCLK        0x40
#define LU9685_AI            0x20
#define LU9685_SLEEP         0x10
#define LU9685_SUB1          0x08
#define LU9685_SUB2          0x04
#define LU9685_SUB3          0x02
#define LU9685_ALLCALL       0x01

// 重置指令
#define LU9685_RESET         0x06

/**
 * @brief LU9685设备结构体
 */
typedef struct {
    i2c_master_bus_handle_t i2c_port;        // I2C总线句柄
    uint8_t i2c_addr;           // I2C地址
    uint16_t pwm_freq;          // PWM频率(Hz)
    bool use_pca9548a;          // 是否使用PCA9548A
    uint8_t pca9548a_channel;   // PCA9548A通道
} lu9685_dev_t;

/**
 * @brief LU9685设备句柄
 */
typedef lu9685_dev_t* lu9685_handle_t;

/**
 * @brief LU9685初始化配置结构体
 */
typedef struct {
    i2c_master_bus_handle_t i2c_port;        // I2C总线句柄
    uint8_t i2c_addr;           // I2C地址
    uint16_t pwm_freq;          // PWM频率(Hz)，默认为50Hz
    bool use_pca9548a;          // 是否通过PCA9548A访问
    uint8_t pca9548a_channel;   // 如果使用PCA9548A，指定通道号
} lu9685_config_t;

/**
 * @brief 初始化LU9685设备
 * 
 * @param config 配置参数
 * @return lu9685_handle_t 返回设备句柄，如果失败则返回NULL
 */
lu9685_handle_t lu9685_init(const lu9685_config_t *config);

/**
 * @brief 释放LU9685设备资源
 * 
 * @param handle 设备句柄指针
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_deinit(lu9685_handle_t *handle);

/**
 * @brief 重置LU9685设备
 * 
 * @param handle 设备句柄
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_reset(lu9685_handle_t handle);

/**
 * @brief 设置LU9685的PWM频率
 * 
 * @param handle LU9685设备句柄
 * @param freq_hz 频率(Hz)，有效范围24-1526Hz
 * @return esp_err_t ESP_OK:成功 其他:失败
 */
esp_err_t lu9685_set_frequency(lu9685_handle_t handle, uint16_t freq_hz);

/**
 * @brief 设置单个PWM通道的占空比
 * 
 * @param handle 设备句柄
 * @param channel 通道号（0-15）
 * @param on_value 开始时间（0-4095）
 * @param off_value 结束时间（0-4095）
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_set_pwm_channel(lu9685_handle_t handle, uint8_t channel, uint16_t on_value, uint16_t off_value);

/**
 * @brief 设置单个通道的PWM占空比（百分比）
 * 
 * @param handle 设备句柄
 * @param channel 通道号（0-15）
 * @param duty_percent 占空比（0.0-100.0）
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_set_duty_percent(lu9685_handle_t handle, uint8_t channel, float duty_percent);

/**
 * @brief 设置所有PWM通道的占空比
 * 
 * @param handle 设备句柄
 * @param on_value 开始时间（0-4095）
 * @param off_value 结束时间（0-4095）
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_set_all_pwm(lu9685_handle_t handle, uint16_t on_value, uint16_t off_value);

/**
 * @brief 设置所有通道的PWM占空比（百分比）
 * 
 * @param handle 设备句柄
 * @param duty_percent 占空比（0.0-100.0）
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_set_all_duty_percent(lu9685_handle_t handle, float duty_percent);

/**
 * @brief 设置器件模式（睡眠/唤醒）
 * 
 * @param handle 设备句柄
 * @param sleep 是否进入睡眠模式
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_set_sleep_mode(lu9685_handle_t handle, bool sleep);

/**
 * @brief 设置舵机通道的角度
 * 
 * @param handle 设备句柄
 * @param channel 通道号（0-15）
 * @param angle 角度（0-180）
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_set_channel_angle(lu9685_handle_t handle, uint8_t channel, uint8_t angle);

/**
 * @brief 判断LU9685是否已经初始化
 * 
 * @return true LU9685已初始化
 * @return false LU9685未初始化
 */
bool lu9685_is_initialized(void);

/**
 * @brief 获取LU9685设备句柄
 * 
 * @return lu9685_handle_t 设备句柄指针，如果未初始化则返回NULL
 */
lu9685_handle_t lu9685_get_handle(void);

/**
 * @brief 读取寄存器值
 * 
 * @param handle LU9685设备句柄
 * @param reg_addr 寄存器地址
 * @param value 用于存储读取的值指针
 * @return esp_err_t ESP_OK:成功 其他:失败
 */
esp_err_t lu9685_read_register(lu9685_handle_t handle, uint8_t reg_addr, uint8_t *value);

/**
 * @brief 写入寄存器值
 * 
 * @param handle LU9685设备句柄
 * @param reg_addr 寄存器地址
 * @param value 要写入的值
 * @return esp_err_t ESP_OK:成功 其他:失败
 */
esp_err_t lu9685_write_register(lu9685_handle_t handle, uint8_t reg_addr, uint8_t value);

#ifdef __cplusplus
}
#endif 