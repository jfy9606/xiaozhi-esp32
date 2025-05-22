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

#include "include/pca9548a.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "pca9548a";

// 外部引用 I2C 设备句柄和总线句柄，这些在 multiplexer.c 中定义和初始化
extern i2c_master_dev_handle_t i2c_dev_handle;
extern i2c_master_bus_handle_t i2c_bus_handle;

/**
 * @brief PCA9548A device structure
 */
struct pca9548a_dev_t {
    uint16_t i2c_addr;                      // I2C device address
    uint32_t i2c_timeout_ms;                // I2C timeout in ms
    gpio_num_t reset_pin;                   // Reset pin (GPIO_NUM_NC if not used)
    uint8_t current_channels;               // Currently selected channels
};

/**
 * @brief Write control register to select channels
 */
static esp_err_t pca9548a_write_control(pca9548a_handle_t handle, uint8_t value)
{
    if (handle == NULL || i2c_dev_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 使用新的 I2C master API 发送数据
    uint8_t write_buf[1] = {value};
    esp_err_t ret = i2c_master_transmit(i2c_dev_handle, write_buf, sizeof(write_buf), handle->i2c_timeout_ms);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed (0x%02X): %s", value, esp_err_to_name(ret));
        return ret;
    }

    // 保存当前通道选择状态
    handle->current_channels = value;
    ESP_LOGD(TAG, "Selected channels: 0x%02X", value);

    return ESP_OK;
}

/**
 * @brief Read control register to get current channel selection
 */
static esp_err_t pca9548a_read_control(pca9548a_handle_t handle, uint8_t *value)
{
    if (handle == NULL || value == NULL || i2c_dev_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 使用新的 I2C master API 读取数据
    esp_err_t ret = i2c_master_receive(i2c_dev_handle, value, 1, handle->i2c_timeout_ms);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 更新当前通道选择状态缓存
    handle->current_channels = *value;
    ESP_LOGD(TAG, "Read channels: 0x%02X", *value);

    return ESP_OK;
}

pca9548a_handle_t pca9548a_create(const pca9548a_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }

    // 检查 I2C 设备句柄是否已初始化
    if (i2c_dev_handle == NULL) {
        ESP_LOGE(TAG, "I2C device handle is not initialized");
        return NULL;
    }

    // Allocate and initialize device structure
    pca9548a_handle_t handle = calloc(1, sizeof(struct pca9548a_dev_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for device");
        return NULL;
    }

    // Copy configuration
    // handle->i2c_port = config->i2c_port; // 不再需要，使用全局 i2c_dev_handle
    handle->i2c_addr = config->i2c_addr;
    handle->i2c_timeout_ms = config->i2c_timeout_ms > 0 ? config->i2c_timeout_ms : 1000; // Default to 1000ms
    handle->reset_pin = config->reset_pin;
    handle->current_channels = 0;

    // Configure reset pin if specified
    if (handle->reset_pin != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << handle->reset_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        if (gpio_config(&io_conf) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure reset pin");
            free(handle);
            return NULL;
        }

        // Set reset pin high (inactive)
        if (gpio_set_level(handle->reset_pin, 1) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set reset pin high");
            free(handle);
            return NULL;
        }
    }

    // Try to read the current channel setting
    uint8_t channels;
    if (pca9548a_read_control(handle, &channels) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read initial channel state, device might not be present");
        // We continue anyway, as the device might be in a strange state or reset
    }

    ESP_LOGI(TAG, "PCA9548A initialized successfully");
    return handle;
}

esp_err_t pca9548a_delete(pca9548a_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Free memory
    free(handle);
    return ESP_OK;
}

esp_err_t pca9548a_select_channels(pca9548a_handle_t handle, uint8_t channels)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return pca9548a_write_control(handle, channels);
}

esp_err_t pca9548a_get_selected_channels(pca9548a_handle_t handle, uint8_t *channels)
{
    if (handle == NULL || channels == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read control register to get current channel setting
    return pca9548a_read_control(handle, channels);
}

esp_err_t pca9548a_reset(pca9548a_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check if reset pin is configured
    if (handle->reset_pin == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "Reset pin not configured, using soft reset");
        // Soft reset by writing 0 to control register
        return pca9548a_select_channels(handle, 0);
    }

    // Hardware reset by toggling reset pin
    ESP_LOGD(TAG, "Performing hardware reset");
    if (gpio_set_level(handle->reset_pin, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set reset pin low");
        return ESP_FAIL;
    }

    // Wait for reset to take effect
    vTaskDelay(pdMS_TO_TICKS(10));

    if (gpio_set_level(handle->reset_pin, 1) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set reset pin high");
        return ESP_FAIL;
    }

    // Wait for device to stabilize after reset
    vTaskDelay(pdMS_TO_TICKS(10));

    // Update current channels state
    handle->current_channels = 0;

    return ESP_OK;
} 