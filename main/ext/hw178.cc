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
    gpio_num_t sig_pin;          // Signal output pin (connects to ADC)
    hw178_channel_t channel;     // Current channel
    void (*set_level_cb)(int pin, int level, void* user_data);
    void* user_data;
};

hw178_handle_t hw178_create(const hw178_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }

    // 验证至少有一个选择引脚被配置，允许单通道使用
    if (config->s0_pin == GPIO_NUM_NC && 
        config->s1_pin == GPIO_NUM_NC && 
        config->s2_pin == GPIO_NUM_NC &&
        config->s3_pin == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "At least one select pin must be configured");
        return NULL;
    }

    // 验证SIG引脚已配置
    if (config->sig_pin == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "SIG pin must be configured");
        return NULL;
    }

    // 分配并初始化设备结构
    hw178_handle_t handle = static_cast<hw178_dev_t*>(calloc(1, sizeof(struct hw178_dev_t)));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for device");
        return NULL;
    }

    // 复制配置
    handle->s0_pin = config->s0_pin;
    handle->s1_pin = config->s1_pin;
    handle->s2_pin = config->s2_pin;
    handle->s3_pin = config->s3_pin;
    handle->sig_pin = config->sig_pin;
    handle->channel = HW178_CHANNEL_C0;  // 默认通道
    handle->set_level_cb = config->set_level_cb;
    handle->user_data = config->user_data;

    // 如果提供了回调，则不初始化选择引脚的GPIO（由回调处理）
    if (handle->set_level_cb != NULL) {
        ESP_LOGI(TAG, "Using callback for select pins");
    } else {
        // 配置GPIO引脚
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        // 配置实际连接的选择引脚
        uint64_t pin_bit_mask = 0;
        if (handle->s0_pin != GPIO_NUM_NC) {
            pin_bit_mask |= (1ULL << handle->s0_pin);
        }
        if (handle->s1_pin != GPIO_NUM_NC) {
            pin_bit_mask |= (1ULL << handle->s1_pin);
        }
        if (handle->s2_pin != GPIO_NUM_NC) {
            pin_bit_mask |= (1ULL << handle->s2_pin);
        }
        if (handle->s3_pin != GPIO_NUM_NC) {
            pin_bit_mask |= (1ULL << handle->s3_pin);
        }
        
        if (pin_bit_mask != 0) {
            io_conf.pin_bit_mask = pin_bit_mask;
            if (gpio_config(&io_conf) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to configure select pins");
                free(handle);
                return NULL;
            }
        }
    }

    // 选择默认通道 (C0)
    if (hw178_select_channel(handle, HW178_CHANNEL_C0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set default channel");
        free(handle);
        return NULL;
    }

    ESP_LOGI(TAG, "HW-178 initialized (S0:%d, S1:%d, S2:%d, S3:%d)",
             handle->s0_pin, handle->s1_pin, handle->s2_pin, handle->s3_pin);
    return handle;
}

esp_err_t hw178_delete(hw178_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
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

    // 仅为实际配置的选择引脚设置电平
    if (handle->set_level_cb != NULL) {
        if (handle->s0_pin != GPIO_NUM_NC) {
            handle->set_level_cb(handle->s0_pin, (channel & 0x01) ? 1 : 0, handle->user_data);
        }
        if (handle->s1_pin != GPIO_NUM_NC) {
            handle->set_level_cb(handle->s1_pin, (channel & 0x02) ? 1 : 0, handle->user_data);
        }
        if (handle->s2_pin != GPIO_NUM_NC) {
            handle->set_level_cb(handle->s2_pin, (channel & 0x04) ? 1 : 0, handle->user_data);
        }
        if (handle->s3_pin != GPIO_NUM_NC) {
            handle->set_level_cb(handle->s3_pin, (channel & 0x08) ? 1 : 0, handle->user_data);
        }
    } else {
        if (handle->s0_pin != GPIO_NUM_NC) {
            gpio_set_level(handle->s0_pin, (channel & 0x01) ? 1 : 0);
        }
        
        if (handle->s1_pin != GPIO_NUM_NC) {
            gpio_set_level(handle->s1_pin, (channel & 0x02) ? 1 : 0);
        }
        
        if (handle->s2_pin != GPIO_NUM_NC) {
            gpio_set_level(handle->s2_pin, (channel & 0x04) ? 1 : 0);
        }
        
        if (handle->s3_pin != GPIO_NUM_NC) {
            gpio_set_level(handle->s3_pin, (channel & 0x08) ? 1 : 0);
        }
    }

    // 更新当前通道
    handle->channel = channel;
    
    ESP_LOGD(TAG, "Selected channel: C%d (S0:%d, S1:%d, S2:%d, S3:%d)",
             channel,
             (handle->s0_pin != GPIO_NUM_NC) ? ((channel & 0x01) ? 1 : 0) : -1,
             (handle->s1_pin != GPIO_NUM_NC) ? ((channel & 0x02) ? 1 : 0) : -1,
             (handle->s2_pin != GPIO_NUM_NC) ? ((channel & 0x04) ? 1 : 0) : -1,
             (handle->s3_pin != GPIO_NUM_NC) ? ((channel & 0x08) ? 1 : 0) : -1);

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