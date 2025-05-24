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
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PCF8575 I2C GPIO扩展器默认I2C地址
 */
#define PCF8575_I2C_ADDRESS_DEFAULT   0x20

// 避免与 board_config.h 中的定义冲突
#ifndef PCF8575_I2C_ADDR
#define PCF8575_I2C_ADDR              PCF8575_I2C_ADDRESS_DEFAULT
#endif

/**
 * @brief PCF8575 I2C GPIO扩展器默认I2C超时（毫秒）
 */
#ifndef PCF8575_I2C_TIMEOUT_MS
#define PCF8575_I2C_TIMEOUT_MS        1000
#endif

/**
 * @brief PCF8575 GPIO定义，总共16个GPIO (P00-P07, P10-P17)
 */
typedef enum {
    PCF8575_GPIO_P00 = 0,   /*!< P00 引脚 */
    PCF8575_GPIO_P01 = 1,   /*!< P01 引脚 */
    PCF8575_GPIO_P02 = 2,   /*!< P02 引脚 */
    PCF8575_GPIO_P03 = 3,   /*!< P03 引脚 */
    PCF8575_GPIO_P04 = 4,   /*!< P04 引脚 */
    PCF8575_GPIO_P05 = 5,   /*!< P05 引脚 */
    PCF8575_GPIO_P06 = 6,   /*!< P06 引脚 */
    PCF8575_GPIO_P07 = 7,   /*!< P07 引脚 */
    PCF8575_GPIO_P10 = 8,   /*!< P10 引脚 */
    PCF8575_GPIO_P11 = 9,   /*!< P11 引脚 */
    PCF8575_GPIO_P12 = 10,  /*!< P12 引脚 */
    PCF8575_GPIO_P13 = 11,  /*!< P13 引脚 */
    PCF8575_GPIO_P14 = 12,  /*!< P14 引脚 */
    PCF8575_GPIO_P15 = 13,  /*!< P15 引脚 */
    PCF8575_GPIO_P16 = 14,  /*!< P16 引脚 */
    PCF8575_GPIO_P17 = 15,  /*!< P17 引脚 */
    PCF8575_GPIO_MAX
} pcf8575_gpio_t;

/**
 * @brief PCF8575引脚模式
 */
typedef enum {
    PCF8575_GPIO_MODE_OUTPUT = 0,   /*!< 输出模式 */
    PCF8575_GPIO_MODE_INPUT = 1,    /*!< 输入模式 */
} pcf8575_gpio_mode_t;

/**
 * @brief PCF8575设备句柄
 */
typedef struct pcf8575_dev_t *pcf8575_handle_t;

/**
 * @brief PCF8575配置
 */
typedef struct {
    uint8_t i2c_addr;                   /*!< I2C设备地址 */
    uint32_t i2c_timeout_ms;            /*!< I2C通信超时(毫秒) */
} pcf8575_config_t;

/**
 * @brief 创建PCF8575设备
 * 
 * @param config PCF8575配置
 * @return 成功返回PCF8575句柄，失败返回NULL
 */
pcf8575_handle_t pcf8575_create(const pcf8575_config_t *config);

/**
 * @brief 释放PCF8575设备资源
 * 
 * @param handle PCF8575句柄
 * @return ESP_OK成功，其他值失败
 */
esp_err_t pcf8575_delete(pcf8575_handle_t handle);

/**
 * @brief 设置PCF8575引脚模式
 * 
 * @param handle PCF8575句柄
 * @param gpio_num 引脚号 (0-15)
 * @param mode 引脚模式 (输入或输出)
 * @return ESP_OK成功，其他值失败
 */
esp_err_t pcf8575_set_gpio_mode(pcf8575_handle_t handle, pcf8575_gpio_t gpio_num, pcf8575_gpio_mode_t mode);

/**
 * @brief 设置PCF8575引脚电平
 * 
 * @param handle PCF8575句柄
 * @param gpio_num 引脚号 (0-15)
 * @param level 电平值 (0 或 1)
 * @return ESP_OK成功，其他值失败
 */
esp_err_t pcf8575_set_gpio_level(pcf8575_handle_t handle, pcf8575_gpio_t gpio_num, uint8_t level);

/**
 * @brief 读取PCF8575引脚电平
 * 
 * @param handle PCF8575句柄
 * @param gpio_num 引脚号 (0-15)
 * @param level 用于存储电平值的指针
 * @return ESP_OK成功，其他值失败
 */
esp_err_t pcf8575_get_gpio_level(pcf8575_handle_t handle, pcf8575_gpio_t gpio_num, uint8_t *level);

/**
 * @brief 检查PCF8575是否已初始化
 * 
 * @return 已初始化返回true，否则返回false
 */
bool pcf8575_is_initialized(void);

/**
 * @brief 获取PCF8575句柄
 * 
 * @return PCF8575句柄，未初始化返回NULL
 */
pcf8575_handle_t pcf8575_get_handle(void);

/**
 * @brief 初始化PCF8575设备
 * 
 * @param port I2C端口号
 * @return ESP_OK成功，其他值失败
 */
esp_err_t pcf8575_init(void);

#ifdef __cplusplus
}
#endif 