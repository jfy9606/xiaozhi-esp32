#pragma once

/**
 * @file multiplexer.h
 * @brief 多路复用器组件API
 * 
 * 支持的多路复用器:
 * 1. PCA9548A - I2C总线多路复用器
 * 2. HW-178 - 模拟信号多路复用器
 * 
 * 正确使用I2C多路复用器的步骤:
 * 
 * 1. 首先初始化显示屏等使用I2C总线的组件
 * 2. 获取显示屏初始化的I2C总线句柄
 * 3. 使用显示屏的I2C总线句柄初始化多路复用器，避免总线冲突
 * 
 * 示例代码:
 * ```c
 * // 初始化显示屏
 * esp_err_t ret = display_init();
 * if (ret != ESP_OK) {
 *     ESP_LOGE(TAG, "Display initialization failed");
 *     return ret;
 * }
 * 
 * // 获取显示屏的I2C总线句柄
 * i2c_master_bus_handle_t display_i2c_handle = display_get_i2c_bus_handle();
 * if (display_i2c_handle == NULL) {
 *     ESP_LOGE(TAG, "Failed to get display I2C bus handle");
 *     return ESP_FAIL;
 * }
 * 
 * // 使用显示屏的I2C总线初始化多路复用器
 * ret = multiplexer_init_with_bus(display_i2c_handle);
 * if (ret != ESP_OK) {
 *     ESP_LOGE(TAG, "Multiplexer initialization failed");
 *     return ret;
 * }
 * 
 * // 选择I2C多路复用器通道并使用设备
 * pca9548a_select_channel(PCA9548A_CHANNEL_0);
 * // 现在可以访问连接到通道0的I2C设备
 * ```
 */

#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"

// 定义默认I2C端口
#define DEFAULT_MULTIPLEXER_I2C_PORT 0

#ifdef CONFIG_ENABLE_HW178
#include "include/hw178.h"
#endif

#ifdef CONFIG_ENABLE_PCA9548A
#include "include/pca9548a.h"

/**
 * @brief Initialize PCA9548A with existing I2C bus handle
 * 
 * @param external_bus_handle External I2C bus handle
 * @return ESP_OK on success, or an error code if initialization failed
 */
esp_err_t pca9548a_init(i2c_master_bus_handle_t external_bus_handle);
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
 * 推荐的初始化方法，使用已经初始化好的显示屏I2C总线句柄来初始化多路复用器，
 * 这样可以避免I2C总线冲突问题。
 * 
 * 使用示例:
 * ```c
 * // 从LVGL或其他显示组件获取I2C总线句柄
 * i2c_master_bus_handle_t display_i2c_handle = lvgl_port_get_i2c_bus_handle();
 * if (display_i2c_handle != NULL) {
 *     // 使用显示屏的I2C总线初始化多路复用器
 *     multiplexer_init_with_bus(display_i2c_handle);
 * } else {
 *     ESP_LOGE(TAG, "无法获取显示屏I2C总线句柄");
 * }
 * ```
 * 
 * @param external_bus_handle Existing I2C bus handle
 * @return ESP_OK on success, or an error code if initialization failed
 */
esp_err_t multiplexer_init_with_bus(i2c_master_bus_handle_t external_bus_handle);

/**
 * @brief Deinitialize the multiplexer components
 * 
 * This function releases resources used by the multiplexer components.
 */
void multiplexer_deinit(void);

#ifdef CONFIG_ENABLE_PCA9548A
/**
 * @brief 级联复用器路径结构体
 * 
 * 用于配置多级级联复用器的选择路径
 */
typedef struct {
    uint8_t level_count;              // 级联层数
    uint8_t channels[4];              // 最多支持4级级联，每级的通道号
} pca9548a_cascade_path_t;

/**
 * @brief Select a specific channel on the PCA9548A multiplexer
 * 
 * @param channel The channel to select (can be OR'ed together)
 * @return ESP_OK on success, or an error code if selection failed
 */
esp_err_t pca9548a_select_channel(uint8_t channel);

/**
 * @brief Select specific channels on the PCA9548A multiplexer (compatibility with newer API)
 * 
 * @param handle PCA9548A handle
 * @param channels The channels to select (can be OR'ed together)
 * @return ESP_OK on success, or an error code if selection failed
 */
esp_err_t pca9548a_select_channels(pca9548a_handle_t handle, uint8_t channels);

/**
 * @brief Select a cascade path through multiple PCA9548A multiplexers
 * 
 * @param path Cascade path configuration
 * @return ESP_OK on success, or an error code if selection failed
 */
esp_err_t pca9548a_select_cascade_path(const pca9548a_cascade_path_t *path);

/**
 * @brief Get current selected channel on PCA9548A
 * 
 * @param channel Pointer to store the current channel
 * @return ESP_OK on success, or an error code if reading failed
 */
esp_err_t pca9548a_get_current_channel(uint8_t *channel);

/**
 * @brief Disable all channels on PCA9548A
 * 
 * @return ESP_OK on success, or an error code if failed
 */
esp_err_t pca9548a_disable_all_channels(void);

/**
 * @brief 获取当前PCA9548A多路复用器初始化状态
 * 
 * @return true 如果已初始化
 * @return false 如果未初始化
 */
bool pca9548a_is_initialized(void);

/**
 * @brief 获取PCA9548A设备句柄
 * 
 * @return pca9548a_handle_t 设备句柄，如果未初始化则返回NULL
 */
pca9548a_handle_t pca9548a_get_handle(void);
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

/**
 * @brief Set HW-178 channel without reading the ADC
 * 
 * @param channel The channel to select
 * @return ESP_OK on success, or an error code if selection failed
 */
esp_err_t hw178_set_channel(hw178_channel_t channel);

/**
 * @brief Check if HW-178 is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool hw178_is_initialized(void);

/**
 * @brief Get the HW-178 handle for direct access
 * 
 * @return HW-178 handle or NULL if not initialized
 */
hw178_handle_t hw178_get_handle(void);

/**
 * @brief Get the ADC handle used by HW-178
 * 
 * @return ADC handle or NULL if not initialized
 */
adc_oneshot_unit_handle_t hw178_get_adc_handle(void);
#endif

#ifdef __cplusplus
}
#endif 