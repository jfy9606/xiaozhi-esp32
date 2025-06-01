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

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// PCF8575最大IO数
#define PCF8575_IO_MAX 18

/**
 * @brief PCF8575设备结构体
 */
typedef struct {
    i2c_master_bus_handle_t i2c_port;    // I2C总线句柄
    uint8_t i2c_addr;       // I2C地址
    uint32_t i2c_timeout_ms; // I2C超时时间(毫秒)
    uint16_t output_state;  // 输出端口状态(16位)
    bool use_pca9548a;      // 是否使用PCA9548A复用器
    uint8_t pca9548a_channel; // 连接在PCA9548A的通道号
} pcf8575_dev_t;

/**
 * @brief PCF8575设备句柄
 */
typedef pcf8575_dev_t* pcf8575_handle_t;

/**
 * @brief PCF8575配置结构体
 */
typedef struct {
    i2c_master_bus_handle_t i2c_port;    // I2C总线句柄
    uint8_t i2c_addr;       // I2C地址
    uint32_t i2c_timeout_ms; // I2C超时时间(毫秒)
    bool all_output;        // 是否全部设置为输出
    bool use_pca9548a;      // 是否通过PCA9548A访问
    uint8_t pca9548a_channel; // 如果使用PCA9548A，指定通道号
} pcf8575_config_t;

/**
 * @brief 读取PCF8575的端口状态
 * 
 * @param handle PCF8575设备句柄
 * @param value 用于存储读取的值
 * @return esp_err_t 
 */
esp_err_t pcf8575_read_ports(pcf8575_handle_t handle, uint16_t *value);

/**
 * @brief 写入PCF8575的端口状态
 * 
 * @param handle PCF8575设备句柄
 * @param value 待写入的值
 * @return esp_err_t
 */
esp_err_t pcf8575_write_ports(pcf8575_handle_t handle, uint16_t value);

/**
 * @brief 创建PCF8575设备
 * 
 * @param config PCF8575配置
 * @return pcf8575_handle_t 设备句柄，NULL表示创建失败
 */
pcf8575_handle_t pcf8575_create(const pcf8575_config_t *config);

/**
 * @brief 销毁PCF8575设备
 * 
 * @param handle PCF8575设备句柄指针
 * @return esp_err_t 
 */
esp_err_t pcf8575_delete(pcf8575_handle_t *handle);

/**
 * @brief 设置PCF8575的单个引脚电平
 * 
 * @param handle PCF8575设备句柄
 * @param pin 引脚编号(0-15)
 * @param level 引脚电平(0/1)
 * @return esp_err_t ESP_OK:成功 其他:失败
 */
esp_err_t pcf8575_set_level(pcf8575_handle_t handle, int pin, uint32_t level);

/**
 * @brief 获取PCF8575的单个引脚电平
 * 
 * @param handle PCF8575设备句柄
 * @param pin 引脚编号(0-15)
 * @param level 引脚电平指针(返回0/1)
 * @return esp_err_t ESP_OK:成功 其他:失败
 */
esp_err_t pcf8575_get_level(pcf8575_handle_t handle, int pin, uint32_t *level);

/**
 * @brief 获取当前端口状态的缓存值（不进行I2C读取）
 * 
 * @param handle PCF8575设备句柄
 * @param value 用于存储端口状态的变量
 * @return esp_err_t 
 */
esp_err_t pcf8575_get_port_state(pcf8575_handle_t handle, uint16_t *value);

/**
 * @brief 设置多个引脚的状态
 * 
 * @param handle PCF8575设备句柄
 * @param pin_mask 引脚掩码，每个位对应一个引脚 (0-15)
 * @param levels 电平值，每个位对应一个引脚的状态
 * @return esp_err_t 
 */
esp_err_t pcf8575_set_pins(pcf8575_handle_t handle, uint16_t pin_mask, uint16_t levels);

/**
 * @brief 初始化PCF8575并设置默认配置
 * 
 * @param i2c_port I2C总线句柄
 * @param i2c_addr I2C地址
 * @param use_pca9548a 是否通过PCA9548A访问
 * @param pca9548a_channel 如果使用PCA9548A，指定通道号
 * @return pcf8575_handle_t 设备句柄，NULL表示创建失败
 */
pcf8575_handle_t pcf8575_init_with_defaults(i2c_master_bus_handle_t i2c_port, uint8_t i2c_addr, bool use_pca9548a, uint8_t pca9548a_channel);

/**
 * @brief 判断PCF8575是否已经初始化
 * 
 * @return true PCF8575已初始化
 * @return false PCF8575未初始化
 */
bool pcf8575_is_initialized(void);

/**
 * @brief 获取PCF8575设备句柄
 * 
 * @return pcf8575_handle_t 设备句柄指针，如果未初始化则返回NULL
 */
pcf8575_handle_t pcf8575_get_handle(void);

/**
 * @brief 初始化PCF8575
 * 
 * @return esp_err_t 
 */
esp_err_t pcf8575_init(void);

/**
 * @brief PCF8575 GPIO模式
 */
typedef enum {
    PCF8575_GPIO_MODE_INPUT = 0,     // 输入模式
    PCF8575_GPIO_MODE_OUTPUT         // 输出模式
} pcf8575_gpio_mode_t;

/**
 * @brief PCF8575 GPIO引脚
 */
typedef int pcf8575_gpio_t;

/**
 * @brief 设置PCF8575的GPIO模式
 * 
 * @param handle PCF8575设备句柄
 * @param gpio_num GPIO编号(0-15)
 * @param mode GPIO模式(输入/输出)
 * @return esp_err_t ESP_OK:成功 其他:失败
 */
esp_err_t pcf8575_set_gpio_mode(pcf8575_handle_t handle, pcf8575_gpio_t gpio_num, pcf8575_gpio_mode_t mode);

/**
 * @brief 设置PCF8575的GPIO电平 (兼容性包装函数，内部调用pcf8575_set_level)
 * 
 * @param handle PCF8575设备句柄
 * @param gpio_num GPIO编号(0-15)
 * @param level GPIO电平(0/1)
 * @return esp_err_t ESP_OK:成功 其他:失败
 */
esp_err_t pcf8575_set_gpio_level(pcf8575_handle_t handle, pcf8575_gpio_t gpio_num, uint32_t level);

/**
 * @brief 获取PCF8575的GPIO电平 (兼容性包装函数，内部调用pcf8575_get_level)
 * 
 * @param handle PCF8575设备句柄
 * @param gpio_num GPIO编号(0-15)
 * @param level GPIO电平指针(返回0/1)
 * @return esp_err_t ESP_OK:成功 其他:失败
 */
esp_err_t pcf8575_get_gpio_level(pcf8575_handle_t handle, pcf8575_gpio_t gpio_num, uint32_t *level);

#ifdef __cplusplus
}
#endif 