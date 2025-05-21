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
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PCA9548A I2C multiplexer default I2C address
 */
#define PCA9548A_I2C_ADDRESS_DEFAULT     0x70

// 避免与 board_config.h 中的定义冲突
#ifndef PCA9548A_I2C_ADDR
#define PCA9548A_I2C_ADDR                PCA9548A_I2C_ADDRESS_DEFAULT
#endif

/**
 * @brief PCA9548A I2C multiplexer default I2C timeout in milliseconds
 */
// 避免与 board_config.h 中的定义冲突
#ifndef PCA9548A_I2C_TIMEOUT_MS
#define PCA9548A_I2C_TIMEOUT_MS   1000
#endif

/**
 * @brief PCA9548A I2C multiplexer channel definitions
 * 
 * The PCA9548A has the following pin mapping:
 * - Bit 0: SC0, SD0
 * - Bit 1: SC1, SD1
 * ...etc
 */
typedef enum {
    PCA9548A_CHANNEL_0 = (1 << 0),    /*!< Channel 0 (SC0, SD0) */
    PCA9548A_CHANNEL_1 = (1 << 1),    /*!< Channel 1 (SC1, SD1) */
    PCA9548A_CHANNEL_2 = (1 << 2),    /*!< Channel 2 (SC2, SD2) */
    PCA9548A_CHANNEL_3 = (1 << 3),    /*!< Channel 3 (SC3, SD3) */
    PCA9548A_CHANNEL_4 = (1 << 4),    /*!< Channel 4 (SC4, SD4) */
    PCA9548A_CHANNEL_5 = (1 << 5),    /*!< Channel 5 (SC5, SD5) */
    PCA9548A_CHANNEL_6 = (1 << 6),    /*!< Channel 6 (SC6, SD6) */
    PCA9548A_CHANNEL_7 = (1 << 7),    /*!< Channel 7 (SC7, SD7) */
    PCA9548A_CHANNEL_NONE = 0x00,     /*!< No channel selected */
    PCA9548A_CHANNEL_ALL = 0xFF       /*!< All channels selected */
} pca9548a_channel_t;

/**
 * @brief PCA9548A I2C multiplexer handle
 */
typedef struct pca9548a_dev_t *pca9548a_handle_t;

/**
 * @brief PCA9548A I2C multiplexer configuration
 */
typedef struct {
    int i2c_port;                      /*!< I2C port number (0 or 1) - 仅用于兼容，新API不再使用 */
    uint8_t i2c_addr;                  /*!< I2C device address */
    uint32_t i2c_timeout_ms;           /*!< I2C timeout in milliseconds */
    gpio_num_t reset_pin;              /*!< Reset pin, or GPIO_NUM_NC if not used */
} pca9548a_config_t;

/**
 * @brief Initialize the PCA9548A I2C multiplexer
 * 
 * @param config Multiplexer configuration
 * @return Handle to the multiplexer device, or NULL on failure
 */
pca9548a_handle_t pca9548a_create(const pca9548a_config_t *config);

/**
 * @brief Free resources used by the PCA9548A I2C multiplexer
 * 
 * @param handle Multiplexer handle returned by pca9548a_create
 * @return ESP_OK on success, or an error code
 */
esp_err_t pca9548a_delete(pca9548a_handle_t handle);

/**
 * @brief Select channels on PCA9548A I2C multiplexer
 * 
 * @param handle Multiplexer handle
 * @param channels Channels to select (can be OR'ed together)
 * @return ESP_OK on success, or an error code
 */
esp_err_t pca9548a_select_channels(pca9548a_handle_t handle, uint8_t channels);

/**
 * @brief Read currently selected channels on the PCA9548A I2C multiplexer
 * 
 * @param handle Multiplexer handle
 * @param channels Pointer to store the currently selected channels
 * @return ESP_OK on success, or an error code
 */
esp_err_t pca9548a_get_selected_channels(pca9548a_handle_t handle, uint8_t *channels);

/**
 * @brief Reset the PCA9548A I2C multiplexer via reset pin
 * 
 * @param handle Multiplexer handle
 * @return ESP_OK on success, or an error code
 */
esp_err_t pca9548a_reset(pca9548a_handle_t handle);

/**
 * @brief Check if PCA9548A is initialized
 * 
 * @return true if PCA9548A is initialized, false otherwise
 */
bool pca9548a_is_initialized(void);

/**
 * @brief Get the handle for direct operations
 * 
 * @note This is exposed for use in low-level operations, 
 *       normally the higher-level functions should be used instead
 * 
 * @return Handle to the PCA9548A device, or NULL if not initialized
 */
pca9548a_handle_t pca9548a_get_handle(void);

#ifdef __cplusplus
}
#endif 