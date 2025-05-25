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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "board_config.h"
#include "include/pcf8575.h"
#include "include/pca9548a.h"

static const char* TAG = "PCF8575";

// PCF8575是通过PCA9548A连接的，所以我们需要使用PCA9548A提供的I2C总线
extern i2c_master_dev_handle_t i2c_dev_handle;

// PCF8575设备定义
typedef struct pcf8575_dev_t {
    uint8_t i2c_addr;                 // I2C地址
    uint32_t i2c_timeout_ms;          // I2C超时时间
    uint16_t port_mode;               // 端口模式 (0=输出, 1=输入)
    uint16_t output_state;            // 输出状态
} pcf8575_dev_t;

// 全局句柄
static pcf8575_handle_t pcf8575_handle = NULL;

// PCF8575默认连接在PCA9548A的通道0上
#define PCF8575_PCA9548A_CHANNEL      PCA9548A_CHANNEL_0

// 选择PCF8575所在通道
static esp_err_t select_pcf8575_channel(void)
{
    // 确保PCA9548A已初始化
    if (!pca9548a_is_initialized()) {
        ESP_LOGE(TAG, "PCA9548A not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 选择PCF8575所在通道
    pca9548a_handle_t pca_handle = pca9548a_get_handle();
    if (pca_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get PCA9548A handle");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = pca9548a_select_channels(pca_handle, (1 << PCF8575_PCA9548A_CHANNEL));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select PCA9548A channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // 等待通道切换完成，减少等待时间
    vTaskDelay(pdMS_TO_TICKS(5));
    
    return ESP_OK;
}

// 读取PCF8575的所有端口
static esp_err_t pcf8575_read_ports(pcf8575_handle_t handle, uint16_t *value)
{
    if (!handle || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 选择PCF8575所在通道
    esp_err_t ret = select_pcf8575_channel();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 读取16位数据，PCF8575只需要发送地址，然后读取两个字节
    uint8_t data[2] = {0};
    ret = i2c_master_receive(i2c_dev_handle, data, 2, handle->i2c_timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read from PCF8575: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 合并两个字节为16位数据
    *value = (data[1] << 8) | data[0];
    return ESP_OK;
}

// 写入PCF8575的所有端口
static esp_err_t pcf8575_write_ports(pcf8575_handle_t handle, uint16_t value)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 选择PCF8575所在通道
    esp_err_t ret = select_pcf8575_channel();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 写入16位数据，分为两个字节发送
    uint8_t data[2] = {
        value & 0xFF,          // 低8位
        (value >> 8) & 0xFF    // 高8位
    };
    
    ret = i2c_master_transmit(i2c_dev_handle, data, 2, handle->i2c_timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write to PCF8575: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 更新内存中保存的输出状态
    handle->output_state = value;
    return ESP_OK;
}

// 创建PCF8575设备
pcf8575_handle_t pcf8575_create(const pcf8575_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    if (!i2c_dev_handle) {
        ESP_LOGE(TAG, "I2C device handle not initialized");
        return NULL;
    }
    
    // 分配内存
    pcf8575_dev_t *dev = calloc(1, sizeof(pcf8575_dev_t));
    if (!dev) {
        ESP_LOGE(TAG, "Failed to allocate memory for PCF8575 device");
        return NULL;
    }
    
    // 初始化设备
    dev->i2c_addr = config->i2c_addr;
    dev->i2c_timeout_ms = config->i2c_timeout_ms;
    dev->port_mode = 0xFFFF;   // 默认全部设为输入模式
    dev->output_state = 0xFFFF; // 默认输出高电平
    
    // 写入初始状态
    esp_err_t ret = pcf8575_write_ports(dev, dev->output_state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PCF8575 ports: %s", esp_err_to_name(ret));
        free(dev);
        return NULL;
    }
    
    return dev;
}

// 释放PCF8575设备资源
esp_err_t pcf8575_delete(pcf8575_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    free(handle);
    return ESP_OK;
}

// 设置PCF8575引脚模式
esp_err_t pcf8575_set_gpio_mode(pcf8575_handle_t handle, pcf8575_gpio_t gpio_num, pcf8575_gpio_mode_t mode)
{
    if (!handle || gpio_num >= PCF8575_GPIO_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 更新端口模式
    if (mode == PCF8575_GPIO_MODE_INPUT) {
        handle->port_mode |= (1 << gpio_num);
    } else {
        handle->port_mode &= ~(1 << gpio_num);
    }
    
    // 输入引脚需要写1，输出引脚根据状态写0或1
    uint16_t new_state = handle->output_state;
    if (mode == PCF8575_GPIO_MODE_INPUT) {
        new_state |= (1 << gpio_num);
    }
    
    // 更新引脚状态
    return pcf8575_write_ports(handle, new_state);
}

// 设置PCF8575引脚电平
esp_err_t pcf8575_set_gpio_level(pcf8575_handle_t handle, pcf8575_gpio_t gpio_num, uint8_t level)
{
    if (!handle || gpio_num >= PCF8575_GPIO_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查是否为输出模式
    if (handle->port_mode & (1 << gpio_num)) {
        ESP_LOGW(TAG, "GPIO %d is configured as input, cannot set level", gpio_num);
        return ESP_ERR_INVALID_STATE;
    }
    
    // 更新输出状态
    uint16_t new_state = handle->output_state;
    if (level) {
        new_state |= (1 << gpio_num);
    } else {
        new_state &= ~(1 << gpio_num);
    }
    
    // 只有状态改变时才进行写操作
    if (new_state != handle->output_state) {
        return pcf8575_write_ports(handle, new_state);
    }
    
    return ESP_OK;
}

// 读取PCF8575引脚电平
esp_err_t pcf8575_get_gpio_level(pcf8575_handle_t handle, pcf8575_gpio_t gpio_num, uint8_t *level)
{
    if (!handle || gpio_num >= PCF8575_GPIO_MAX || !level) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 读取所有端口状态
    uint16_t ports_value = 0;
    esp_err_t ret = pcf8575_read_ports(handle, &ports_value);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 获取指定引脚的状态
    *level = (ports_value & (1 << gpio_num)) ? 1 : 0;
    return ESP_OK;
}

// 检查PCF8575是否已初始化
bool pcf8575_is_initialized(void)
{
    return pcf8575_handle != NULL;
}

// 获取PCF8575句柄
pcf8575_handle_t pcf8575_get_handle(void)
{
    if (!pcf8575_handle) {
        ESP_LOGW(TAG, "PCF8575 handle requested but not initialized");
    }
    return pcf8575_handle;
}

// 初始化PCF8575
esp_err_t pcf8575_init(void)
{
    // 确保PCA9548A已初始化
    if (!pca9548a_is_initialized()) {
        ESP_LOGE(TAG, "PCA9548A not initialized, PCF8575 initialization failed");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 确保i2c_dev_handle有效
    if (!i2c_dev_handle) {
        ESP_LOGE(TAG, "I2C device handle not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Initializing PCF8575 GPIO expander");
    
    // 如果已初始化，则先释放资源
    if (pcf8575_handle) {
        pcf8575_delete(pcf8575_handle);
        pcf8575_handle = NULL;
    }
    
    // 首先尝试选择通道，这是一个快速操作，如果失败则立即返回
    esp_err_t ret = select_pcf8575_channel();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select channel for PCF8575: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "PCF8575 may not be physically connected or PCA9548A not working");
        return ret;
    }
    
    // 尝试快速读取数据来检测设备是否存在，使用更短的超时时间
    uint8_t test_data[2];
    ret = i2c_master_receive(i2c_dev_handle, test_data, 2, 50);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to communicate with PCF8575: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "PCF8575 device may not be present or has a different I2C address");
        return ESP_ERR_TIMEOUT;
    }
    
    // 创建PCF8575设备，使用更低的超时值
    pcf8575_config_t config = {
        .i2c_addr = PCF8575_I2C_ADDR,
        .i2c_timeout_ms = 50  // 使用更低的超时值，避免长时间等待
    };
    
    pcf8575_handle = pcf8575_create(&config);
    if (!pcf8575_handle) {
        ESP_LOGE(TAG, "Failed to create PCF8575 device");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "PCF8575 initialized successfully");
    return ESP_OK;
} 