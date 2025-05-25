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
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HW-178 analog multiplexer channel count
 */
#define HW178_CHANNEL_COUNT   16

/**
 * @brief HW-178 multiplexer channel definitions
 */
typedef enum {
    HW178_CHANNEL_C0 = 0,
    HW178_CHANNEL_C1,
    HW178_CHANNEL_C2,
    HW178_CHANNEL_C3,
    HW178_CHANNEL_C4,
    HW178_CHANNEL_C5,
    HW178_CHANNEL_C6,
    HW178_CHANNEL_C7,
    HW178_CHANNEL_C8,
    HW178_CHANNEL_C9,
    HW178_CHANNEL_C10,
    HW178_CHANNEL_C11,
    HW178_CHANNEL_C12,
    HW178_CHANNEL_C13,
    HW178_CHANNEL_C14,
    HW178_CHANNEL_C15,
    HW178_CHANNEL_MAX
} hw178_channel_t;

/**
 * @brief HW-178 multiplexer handle
 */
typedef struct hw178_dev_t *hw178_handle_t;

/**
 * @brief HW-178 multiplexer configuration
 */
typedef struct {
    gpio_num_t s0_pin;             /*!< S0 select pin */
    gpio_num_t s1_pin;             /*!< S1 select pin */
    gpio_num_t s2_pin;             /*!< S2 select pin */
    gpio_num_t s3_pin;             /*!< S3 select pin */
    gpio_num_t sig_pin;            /*!< SIG output pin (connects to ADC) */
} hw178_config_t;

/**
 * @brief Initialize the HW-178 analog multiplexer
 * 
 * @param config Configuration parameters
 * @return Handle to the multiplexer device, or NULL on failure
 */
hw178_handle_t hw178_create(const hw178_config_t *config);

/**
 * @brief Free resources used by the HW-178 multiplexer
 * 
 * @param handle Multiplexer handle returned by hw178_create
 * @return ESP_OK on success
 */
esp_err_t hw178_delete(hw178_handle_t handle);

/**
 * @brief Select a channel on HW-178 multiplexer
 * 
 * @param handle Multiplexer handle
 * @param channel Channel to select
 * @return ESP_OK on success
 */
esp_err_t hw178_select_channel(hw178_handle_t handle, hw178_channel_t channel);

/**
 * @brief Get currently selected channel on the HW-178 multiplexer
 * 
 * @param handle Multiplexer handle
 * @param channel Pointer to variable to store current channel selection
 * @return ESP_OK on success
 */
esp_err_t hw178_get_selected_channel(hw178_handle_t handle, hw178_channel_t *channel);

#ifdef __cplusplus
}
#endif 