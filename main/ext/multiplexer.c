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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "sdkconfig.h"

// Include board-specific configurations
#include "board_config.h"

// Include multiplexer drivers
#ifdef CONFIG_ENABLE_PCA9548A
#include "include/pca9548a.h"
#endif

#ifdef CONFIG_ENABLE_HW178
#include "include/hw178.h"
#endif

// 引入多路复用器头文件
#include "include/multiplexer.h"

static const char *TAG = "multiplexer";

// Multiplexer handles
#ifdef CONFIG_ENABLE_PCA9548A
static pca9548a_handle_t pca9548a_handle = NULL;
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t i2c_dev_handle = NULL;
static bool using_external_bus = false; // 标记是否使用外部总线
// 默认I2C端口，可通过函数参数修改
static int default_i2c_port = DEFAULT_MULTIPLEXER_I2C_PORT;
#endif

#ifdef CONFIG_ENABLE_HW178
static hw178_handle_t hw178_handle = NULL;
static adc_oneshot_unit_handle_t adc_handle = NULL;
#endif

// Initialize I2C bus
#ifdef CONFIG_ENABLE_PCA9548A
// 如果没有宏定义条件，该函数会在不支持PCA9548A时仍然存在但不被使用
static esp_err_t i2c_master_init(int i2c_port)
{
    // 如果已经有I2C总线句柄，不需要再初始化
    if (i2c_bus_handle != NULL) {
        return ESP_OK;
    }

    // 配置I2C总线
    i2c_master_bus_config_t bus_config = {
        .i2c_port = (i2c_port_t)i2c_port,  // 使用传入的i2c_port
        .sda_io_num = CONFIG_PCA9548A_SDA_PIN,
        .scl_io_num = CONFIG_PCA9548A_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
        },
    };
    
    ESP_LOGI(TAG, "Initializing I2C master on port %d with SDA:%d, SCL:%d", 
             i2c_port, CONFIG_PCA9548A_SDA_PIN, CONFIG_PCA9548A_SCL_PIN);
    
    esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C master initialized successfully");
    return ESP_OK;
}
#endif

#ifdef CONFIG_ENABLE_PCA9548A
// Configure PCA9548A device
static esp_err_t pca9548a_config_device(void)
{
    if (i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 配置PCA9548A设备 - 与OLED显示屏保持一致的I2C配置
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCA9548A_I2C_ADDR,
        .scl_speed_hz = 400 * 1000, // 设置为400 kHz，与OLED显示屏一致
    };
    
    ESP_LOGI(TAG, "Adding PCA9548A device to I2C bus at address 0x%02X with SCL speed %" PRIu32 " Hz", 
             PCA9548A_I2C_ADDR, dev_cfg.scl_speed_hz);
    
    // 创建设备句柄
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &i2c_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed: %s", esp_err_to_name(ret));
        // 如果不是使用外部总线，则清理已创建的资源
        if (!using_external_bus && i2c_bus_handle != NULL) {
            i2c_del_master_bus(i2c_bus_handle);
            i2c_bus_handle = NULL;
        }
        return ret;
    }

    return ESP_OK;
}
#endif

#ifdef CONFIG_ENABLE_HW178
// Initialize ADC
static adc_oneshot_unit_handle_t adc_init(void)
{
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    // Get the ADC channel from board config as an integer
    int adc_channel = HW178_ADC_CHANNEL;
    
    // Check if the channel is valid
    if (adc_channel < 0) {
        ESP_LOGE(TAG, "Invalid ADC channel: %d", adc_channel);
        return NULL;
    }
    
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    
    // Configure the ADC channel with the integer channel number
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, adc_channel, &config));
    
    return adc_handle;
}
#endif

#ifdef CONFIG_ENABLE_PCA9548A
// Initialize PCA9548A
esp_err_t pca9548a_init(int i2c_port)
{
    ESP_LOGI(TAG, "Initializing PCA9548A multiplexer on port %d", i2c_port);

    esp_err_t ret = ESP_OK;
    
    // 保存当前使用的I2C端口号
    default_i2c_port = i2c_port;
    
    // 如果没有设置外部总线，则自行初始化I2C
    if (!using_external_bus) {
        ret = i2c_master_init(i2c_port);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize I2C: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // 配置PCA9548A设备
    ret = pca9548a_config_device();
    if (ret != ESP_OK) {
        return ret;
    }

    // Create PCA9548A device
    pca9548a_config_t pca_config = {
        .i2c_dev_handle = i2c_dev_handle,
        .i2c_timeout_ms = PCA9548A_I2C_TIMEOUT_MS,
        .reset_pin = PCA9548A_RESET_PIN,
    };
    
    pca9548a_handle = pca9548a_create(&pca_config);
    if (pca9548a_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create PCA9548A device");
        // 清理已创建的资源
        if (i2c_dev_handle != NULL) {
            i2c_master_bus_rm_device(i2c_dev_handle);
            i2c_dev_handle = NULL;
        }
        if (!using_external_bus && i2c_bus_handle != NULL) {
            i2c_del_master_bus(i2c_bus_handle);
            i2c_bus_handle = NULL;
        }
        return ESP_FAIL;
    }

    // Reset the device
    if (pca9548a_reset(pca9548a_handle) == ESP_OK) {
        ESP_LOGI(TAG, "PCA9548A reset successful");
    }

    // Default to no channels selected - 使用普通错误处理代替ESP_ERROR_CHECK
    esp_err_t ret_select = pca9548a_select_channels(pca9548a_handle, PCA9548A_CHANNEL_NONE);
    if (ret_select != ESP_OK) {
        ESP_LOGW(TAG, "Failed to select default channels: %s", esp_err_to_name(ret_select));
        // 不中断程序，继续执行
    }
    
    ESP_LOGI(TAG, "PCA9548A initialized successfully");
    return ESP_OK;
}

// Function to select a specific I2C channel
esp_err_t pca9548a_select_channel(uint8_t channel)
{
    if (pca9548a_handle == NULL) {
        ESP_LOGE(TAG, "PCA9548A not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = pca9548a_select_channels(pca9548a_handle, channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Read back the selected channels to confirm
    uint8_t current_channel;
    ret = pca9548a_get_selected_channels(pca9548a_handle, &current_channel);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Current PCA9548A channel: 0x%02X", current_channel);
    }
    
    return ESP_OK;
}

// Function to check if PCA9548A is initialized
bool pca9548a_is_initialized(void)
{
    return pca9548a_handle != NULL;
}
#endif

#ifdef CONFIG_ENABLE_HW178
// Initialize HW178
esp_err_t hw178_init(void)
{
    ESP_LOGI(TAG, "Initializing HW-178 multiplexer");

    // Initialize ADC
    adc_handle = adc_init();
    if (adc_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize ADC");
        return ESP_FAIL;
    }

    // Create HW-178 device
    hw178_config_t hw178_config = {
        .s0_pin = HW178_S0_PIN,
        .s1_pin = HW178_S1_PIN,
        .s2_pin = HW178_S2_PIN,
        .s3_pin = HW178_S3_PIN,
        .en_pin = HW178_EN_PIN,
        .en_active_high = HW178_EN_ACTIVE_HIGH,
    };
    
    hw178_handle = hw178_create(&hw178_config);
    if (hw178_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create HW-178 device");
        return ESP_FAIL;
    }

    // Enable the multiplexer
    if (hw178_enable(hw178_handle) == ESP_OK) {
        ESP_LOGI(TAG, "HW-178 enabled");
    }
    
    ESP_LOGI(TAG, "HW-178 initialized successfully");
    return ESP_OK;
}

// Function to read analog value from a specific channel
esp_err_t hw178_read_channel(hw178_channel_t channel, int *value)
{
    if (hw178_handle == NULL || adc_handle == NULL) {
        ESP_LOGE(TAG, "HW-178 not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Select channel
    esp_err_t ret = hw178_select_channel(hw178_handle, channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for channel switching to stabilize
    vTaskDelay(pdMS_TO_TICKS(2));
    
    // Get the ADC channel from board config as integer
    int adc_channel = HW178_ADC_CHANNEL;
    
    // Read ADC value
    ret = adc_oneshot_read(adc_handle, adc_channel, value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "HW-178 Channel C%d, ADC Value: %d", channel, *value);
    return ESP_OK;
}
#endif

// Initialize multiplexer components with provided bus
esp_err_t multiplexer_init_with_bus(i2c_master_bus_handle_t external_bus_handle)
{
    esp_err_t ret = ESP_OK;

#ifdef CONFIG_ENABLE_PCA9548A
    if (external_bus_handle == NULL) {
        ESP_LOGE(TAG, "Invalid external I2C bus handle");
        return ESP_ERR_INVALID_ARG;
    }

    // 保存外部总线句柄
    i2c_bus_handle = external_bus_handle;
    using_external_bus = true;

    // 初始化PCA9548A，此处端口号实际不会被使用，因为使用的是外部总线
    ret = pca9548a_init(default_i2c_port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PCA9548A: %s", esp_err_to_name(ret));
        // 不要删除外部总线句柄，因为不是我们创建的
        i2c_bus_handle = NULL;
        // 继续初始化其他组件
    }
#endif

#ifdef CONFIG_ENABLE_HW178
    ret = hw178_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HW-178: %s", esp_err_to_name(ret));
        // 继续运行
    }
#endif

    return ret;
}

// Initialize multiplexer components
esp_err_t multiplexer_init(void)
{
    return multiplexer_init_with_i2c_port(default_i2c_port);
}

// Initialize multiplexer components with specific I2C port
esp_err_t multiplexer_init_with_i2c_port(int i2c_port)
{
    esp_err_t ret = ESP_OK;

#ifdef CONFIG_ENABLE_PCA9548A
    // 标记使用内部初始化的总线
    using_external_bus = false;
    ret = pca9548a_init(i2c_port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PCA9548A: %s", esp_err_to_name(ret));
        // Continue with other initializations even if PCA9548A fails
    }
#endif

#ifdef CONFIG_ENABLE_HW178
    ret = hw178_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HW-178: %s", esp_err_to_name(ret));
        // Continue even if HW-178 fails
    }
#endif

    return ret;
}

// Deinitialize multiplexer components
void multiplexer_deinit(void)
{
#ifdef CONFIG_ENABLE_PCA9548A
    // Clean up PCA9548A resources
    if (pca9548a_handle != NULL) {
        pca9548a_delete(pca9548a_handle);
        pca9548a_handle = NULL;
    }

    if (i2c_dev_handle != NULL) {
        i2c_master_bus_rm_device(i2c_dev_handle);
        i2c_dev_handle = NULL;
    }

    if (!using_external_bus && i2c_bus_handle != NULL) {
        i2c_del_master_bus(i2c_bus_handle);
        i2c_bus_handle = NULL;
    }
#endif

#ifdef CONFIG_ENABLE_HW178
    // Clean up HW-178 resources
    if (hw178_handle != NULL) {
        hw178_delete(hw178_handle);
        hw178_handle = NULL;
    }

    if (adc_handle != NULL) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
    }
#endif

    ESP_LOGI(TAG, "Multiplexer components deinitialized");
}

/**
 * @brief Reset the I2C multiplexer (PCA9548A)
 * 
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized, 
 *         or other error if reset fails
 */
esp_err_t multiplexer_reset(void)
{
#ifdef CONFIG_ENABLE_PCA9548A
    if (pca9548a_handle == NULL) {
        ESP_LOGE(TAG, "PCA9548A not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = pca9548a_reset(pca9548a_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset PCA9548A: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 重置后清除所有通道
    ret = pca9548a_select_channels(pca9548a_handle, PCA9548A_CHANNEL_NONE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to clear channels after reset: %s", esp_err_to_name(ret));
        // 不中断程序，继续执行
    }
    
    ESP_LOGI(TAG, "PCA9548A reset successful");
    return ESP_OK;
#else
    // 如果没有编译PCA9548A支持，返回不支持
    ESP_LOGW(TAG, "PCA9548A support not enabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif
} 