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

#include "include/hw178.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "hw178";

/**
 * @brief HW-178 device structure
 */
struct hw178_dev_t {
    gpio_num_t s0_pin;           // Select pin S0
    gpio_num_t s1_pin;           // Select pin S1
    gpio_num_t s2_pin;           // Select pin S2
    gpio_num_t s3_pin;           // Select pin S3
    gpio_num_t en_pin;           // Enable pin
    bool en_active_high;         // Enable pin logic
    hw178_channel_t channel;     // Current channel
};

hw178_handle_t hw178_create(const hw178_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }

    // Validate pins
    if (config->s0_pin == GPIO_NUM_NC || 
        config->s1_pin == GPIO_NUM_NC || 
        config->s2_pin == GPIO_NUM_NC ||
        config->s3_pin == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "Select pins not properly configured");
        return NULL;
    }

    // Allocate and initialize device structure
    hw178_handle_t handle = calloc(1, sizeof(struct hw178_dev_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for device");
        return NULL;
    }

    // Copy configuration
    handle->s0_pin = config->s0_pin;
    handle->s1_pin = config->s1_pin;
    handle->s2_pin = config->s2_pin;
    handle->s3_pin = config->s3_pin;
    handle->en_pin = config->en_pin;
    handle->en_active_high = config->en_active_high;
    handle->channel = HW178_CHANNEL_C0;  // Default channel

    // Configure GPIO pins
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    // Configure select pins
    io_conf.pin_bit_mask = (1ULL << handle->s0_pin) | 
                          (1ULL << handle->s1_pin) | 
                          (1ULL << handle->s2_pin) |
                          (1ULL << handle->s3_pin);
    if (gpio_config(&io_conf) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure select pins");
        free(handle);
        return NULL;
    }

    // Configure enable pin if specified
    if (handle->en_pin != GPIO_NUM_NC) {
        io_conf.pin_bit_mask = (1ULL << handle->en_pin);
        if (gpio_config(&io_conf) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure enable pin");
            free(handle);
            return NULL;
        }
        
        // Set default state - disabled
        int en_level = handle->en_active_high ? 0 : 1;
        if (gpio_set_level(handle->en_pin, en_level) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set enable pin level");
            free(handle);
            return NULL;
        }
    }

    // Select default channel (C0)
    if (hw178_select_channel(handle, HW178_CHANNEL_C0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set default channel");
        free(handle);
        return NULL;
    }

    ESP_LOGI(TAG, "HW-178 initialized (S0:%d, S1:%d, S2:%d, S3:%d, EN:%d)",
             handle->s0_pin, handle->s1_pin, handle->s2_pin, handle->s3_pin,
             handle->en_pin);
    return handle;
}

esp_err_t hw178_delete(hw178_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Disable the multiplexer if enable pin is configured
    if (handle->en_pin != GPIO_NUM_NC) {
        int en_level = handle->en_active_high ? 0 : 1;
        gpio_set_level(handle->en_pin, en_level);
    }

    // Free memory
    free(handle);
    return ESP_OK;
}

esp_err_t hw178_select_channel(hw178_handle_t handle, hw178_channel_t channel)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (channel >= HW178_CHANNEL_MAX) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }

    // Set the select pins according to the channel binary value
    // S0 = LSB, S3 = MSB
    gpio_set_level(handle->s0_pin, (channel & 0x01) ? 1 : 0);
    gpio_set_level(handle->s1_pin, (channel & 0x02) ? 1 : 0);
    gpio_set_level(handle->s2_pin, (channel & 0x04) ? 1 : 0);
    gpio_set_level(handle->s3_pin, (channel & 0x08) ? 1 : 0);

    // Update current channel
    handle->channel = channel;
    
    ESP_LOGD(TAG, "Selected channel: C%d (S0:%d, S1:%d, S2:%d, S3:%d)",
             channel,
             (channel & 0x01) ? 1 : 0,
             (channel & 0x02) ? 1 : 0,
             (channel & 0x04) ? 1 : 0,
             (channel & 0x08) ? 1 : 0);

    return ESP_OK;
}

esp_err_t hw178_get_selected_channel(hw178_handle_t handle, hw178_channel_t *channel)
{
    if (handle == NULL || channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *channel = handle->channel;
    return ESP_OK;
}

esp_err_t hw178_enable(hw178_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->en_pin == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "Enable pin not configured");
        return ESP_ERR_NOT_SUPPORTED;
    }

    int en_level = handle->en_active_high ? 1 : 0;
    esp_err_t ret = gpio_set_level(handle->en_pin, en_level);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Multiplexer enabled");
    } else {
        ESP_LOGE(TAG, "Failed to enable multiplexer: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t hw178_disable(hw178_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->en_pin == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "Enable pin not configured");
        return ESP_ERR_NOT_SUPPORTED;
    }

    int en_level = handle->en_active_high ? 0 : 1;
    esp_err_t ret = gpio_set_level(handle->en_pin, en_level);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Multiplexer disabled");
    } else {
        ESP_LOGE(TAG, "Failed to disable multiplexer: %s", esp_err_to_name(ret));
    }
    
    return ret;
} 