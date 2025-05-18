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
#include "sdkconfig.h"
#include "driver/i2c_master.h"

// 定义默认I2C端口
#define DEFAULT_MULTIPLEXER_I2C_PORT 0

#ifdef CONFIG_ENABLE_HW178
#include "include/hw178.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the multiplexer components
 * 
 * This function initializes both PCA9548A and HW-178 multiplexers
 * if they are enabled in the Kconfig.
 * 
 * @return ESP_OK on success, or an error code if initialization failed
 */
esp_err_t multiplexer_init(void);

/**
 * @brief Initialize the multiplexer components with specific I2C port
 * 
 * This function initializes both PCA9548A and HW-178 multiplexers
 * and allows specifying which I2C port to use.
 * 
 * @param i2c_port I2C port number to use (0 or 1)
 * @return ESP_OK on success, or an error code if initialization failed
 */
esp_err_t multiplexer_init_with_i2c_port(int i2c_port);

/**
 * @brief Initialize the multiplexer components with existing I2C bus
 * 
 * This function initializes the multiplexers using an existing I2C bus handle
 * instead of creating a new one. This is useful when sharing I2C bus with
 * other components like displays.
 * 
 * @param i2c_bus_handle Existing I2C bus handle
 * @return ESP_OK on success, or an error code if initialization failed
 */
esp_err_t multiplexer_init_with_bus(i2c_master_bus_handle_t i2c_bus_handle);

/**
 * @brief Deinitialize the multiplexer components
 * 
 * This function releases resources used by the multiplexer components.
 */
void multiplexer_deinit(void);

/**
 * @brief Reset the multiplexer components
 * 
 * This function resets all multiplexers using their reset pins
 * (if configured).
 * 
 * @return ESP_OK on success, or an error code if reset failed
 */
esp_err_t multiplexer_reset(void);

#ifdef CONFIG_ENABLE_PCA9548A
/**
 * @brief Select a specific channel on the PCA9548A multiplexer
 * 
 * @param channel The channel to select (can be OR'ed together)
 * @return ESP_OK on success, or an error code if selection failed
 */
esp_err_t pca9548a_select_channel(uint8_t channel);

/**
 * @brief Check if PCA9548A is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool pca9548a_is_initialized(void);
#endif

#ifdef CONFIG_ENABLE_HW178
/**
 * @brief Read analog value from a specific channel on HW-178
 * 
 * @param channel The channel to read from
 * @param value Pointer to store the ADC reading
 * @return ESP_OK on success, or an error code if reading failed
 */
esp_err_t hw178_read_channel(hw178_channel_t channel, int *value);
#endif

#ifdef __cplusplus
}
#endif 