/**
 * @file lu9685.c
 * @brief LU9685-20CU 16通道PWM/舵机控制器实现
 */

#include "ext/include/lu9685.h"
#include "ext/include/pca9548a.h"
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "LU9685"

// 引用来自multiplexer.c的I2C设备句柄
extern i2c_master_dev_handle_t i2c_dev_handle;

// LU9685-20CU默认I2C地址
#define LU9685_DEFAULT_ADDR    0x80
#define LU9685_DEFAULT_CHANNEL 1    // 默认连接在PCA9548A的通道1

// 舵机控制参数
#define SERVO_MIN_PULSE_WIDTH_US 500   // 最小脉冲宽度 (微秒)
#define SERVO_MAX_PULSE_WIDTH_US 2500  // 最大脉冲宽度 (微秒)
#define SERVO_DEFAULT_FREQUENCY_HZ 50   // 默认舵机控制频率 (Hz)
#define SERVO_MAX_FREQUENCY_HZ 300      // 最高支持频率 (Hz)

// LU9685命令定义
#define LU9685_CMD_SET_ANGLE   0x00     // 设置舵机角度命令
#define LU9685_CMD_SET_FREQ    0x10     // 设置频率命令

typedef struct lu9685_dev_t {
    uint8_t i2c_addr;          // I2C地址
    uint8_t pca9548a_channel;  // 连接的PCA9548A通道
    bool use_pca9548a;         // 是否使用PCA9548A
    bool is_initialized;       // 是否已初始化
    uint16_t frequency_hz;     // PWM频率 (Hz)
} lu9685_dev_t;

static lu9685_dev_t *lu9685_dev = NULL;

lu9685_handle_t lu9685_init(const lu9685_config_t *config)
{
    if (lu9685_dev != NULL) {
        ESP_LOGW(TAG, "LU9685 already initialized");
        return lu9685_dev;
    }

    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }

    lu9685_dev = (lu9685_dev_t *)calloc(1, sizeof(lu9685_dev_t));
    if (lu9685_dev == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for LU9685 device");
        return NULL;
    }

    // 设置参数
    lu9685_dev->i2c_addr = config->i2c_addr != 0 ? config->i2c_addr : LU9685_DEFAULT_ADDR;
    lu9685_dev->pca9548a_channel = config->pca9548a_channel;
    lu9685_dev->use_pca9548a = config->use_pca9548a;
    lu9685_dev->frequency_hz = config->frequency_hz != 0 ? config->frequency_hz : SERVO_DEFAULT_FREQUENCY_HZ;
    
    // 确保频率在有效范围内
    if (lu9685_dev->frequency_hz > SERVO_MAX_FREQUENCY_HZ) {
        ESP_LOGW(TAG, "Requested frequency %d Hz exceeds maximum of %d Hz, capping to maximum",
                 lu9685_dev->frequency_hz, SERVO_MAX_FREQUENCY_HZ);
        lu9685_dev->frequency_hz = SERVO_MAX_FREQUENCY_HZ;
    } else if (lu9685_dev->frequency_hz < SERVO_DEFAULT_FREQUENCY_HZ) {
        ESP_LOGW(TAG, "Requested frequency %d Hz is below standard 50Hz, setting to 50Hz",
                 lu9685_dev->frequency_hz);
        lu9685_dev->frequency_hz = SERVO_DEFAULT_FREQUENCY_HZ;
    }
    
    ESP_LOGI(TAG, "Initializing LU9685 at address 0x%02x, frequency: %d Hz", 
             lu9685_dev->i2c_addr, lu9685_dev->frequency_hz);
    
    if (lu9685_dev->use_pca9548a) {
        ESP_LOGI(TAG, "LU9685 is connected through PCA9548A channel %d", lu9685_dev->pca9548a_channel);
        if (!pca9548a_is_initialized()) {
            ESP_LOGE(TAG, "PCA9548A not initialized");
            free(lu9685_dev);
            lu9685_dev = NULL;
            return NULL;
        }
        
        // 切换到LU9685所在通道
        pca9548a_handle_t pca_handle = pca9548a_get_handle();
        if (pca_handle == NULL) {
            ESP_LOGE(TAG, "Failed to get PCA9548A handle");
            free(lu9685_dev);
            lu9685_dev = NULL;
            return NULL;
        }
        if (pca9548a_select_channels(pca_handle, (1 << lu9685_dev->pca9548a_channel)) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to select PCA9548A channel %d", lu9685_dev->pca9548a_channel);
            free(lu9685_dev);
            lu9685_dev = NULL;
            return NULL;
        }
    }

    // 初始化LU9685，设置频率
    esp_err_t ret = lu9685_set_frequency(lu9685_dev, lu9685_dev->frequency_hz);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set initial frequency, continuing anyway");
    }
    
    lu9685_dev->is_initialized = true;
    ESP_LOGI(TAG, "LU9685 initialized successfully");

    return lu9685_dev;
}

esp_err_t lu9685_deinit(lu9685_handle_t handle)
{
    if (handle == NULL || handle != lu9685_dev) {
        ESP_LOGE(TAG, "Invalid LU9685 handle");
        return ESP_ERR_INVALID_ARG;
    }

    free(lu9685_dev);
    lu9685_dev = NULL;
    ESP_LOGI(TAG, "LU9685 deinitialized");
    
    return ESP_OK;
}

esp_err_t lu9685_set_frequency(lu9685_handle_t handle, uint16_t freq_hz)
{
    if (handle == NULL || handle != lu9685_dev) {
        ESP_LOGE(TAG, "Invalid LU9685 handle");
        return ESP_ERR_INVALID_ARG;
    }

    if (!handle->is_initialized) {
        ESP_LOGE(TAG, "LU9685 not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 验证频率范围
    if (freq_hz > SERVO_MAX_FREQUENCY_HZ) {
        ESP_LOGW(TAG, "Requested frequency %d Hz exceeds maximum of %d Hz, capping to maximum",
                 freq_hz, SERVO_MAX_FREQUENCY_HZ);
        freq_hz = SERVO_MAX_FREQUENCY_HZ;
    } else if (freq_hz < SERVO_DEFAULT_FREQUENCY_HZ) {
        ESP_LOGW(TAG, "Requested frequency %d Hz is below standard 50Hz, setting to 50Hz", freq_hz);
        freq_hz = SERVO_DEFAULT_FREQUENCY_HZ;
    }
    
    // 如果使用PCA9548A，切换到LU9685所在通道
    if (handle->use_pca9548a) {
        pca9548a_handle_t pca_handle = pca9548a_get_handle();
        if (pca_handle == NULL) {
            ESP_LOGE(TAG, "Failed to get PCA9548A handle");
            return ESP_FAIL;
        }
        esp_err_t ret = pca9548a_select_channels(pca_handle, (1 << handle->pca9548a_channel));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to select PCA9548A channel %d: %s", 
                     handle->pca9548a_channel, esp_err_to_name(ret));
            return ret;
        }
        
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // 准备发送频率设置命令
    // 格式: [命令, 频率高字节, 频率低字节]
    uint8_t data[3] = {
        LU9685_CMD_SET_FREQ,
        (uint8_t)((freq_hz >> 8) & 0xFF),  // 频率高字节
        (uint8_t)(freq_hz & 0xFF)          // 频率低字节
    };
    
    esp_err_t ret = i2c_master_transmit(i2c_dev_handle, data, sizeof(data), 100);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "LU9685 communication timeout when setting frequency");
        } else {
            ESP_LOGE(TAG, "Failed to set frequency to %d Hz: %s", freq_hz, esp_err_to_name(ret));
        }
        return ret;
    }
    
    handle->frequency_hz = freq_hz;
    ESP_LOGI(TAG, "Set LU9685 frequency to %d Hz", freq_hz);
    
    return ESP_OK;
}

esp_err_t lu9685_set_channel_angle(lu9685_handle_t handle, uint8_t channel, uint8_t angle)
{
    if (handle == NULL || handle != lu9685_dev) {
        ESP_LOGE(TAG, "Invalid LU9685 handle");
        return ESP_ERR_INVALID_ARG;
    }

    if (!handle->is_initialized) {
        ESP_LOGE(TAG, "LU9685 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 确保角度在有效范围内 (0-180度)
    if (angle > 180) {
        angle = 180;
    }

    // 如果使用PCA9548A，切换到LU9685所在通道
    if (handle->use_pca9548a) {
        pca9548a_handle_t pca_handle = pca9548a_get_handle();
        if (pca_handle == NULL) {
            ESP_LOGE(TAG, "Failed to get PCA9548A handle");
            return ESP_FAIL;
        }
        esp_err_t ret = pca9548a_select_channels(pca_handle, (1 << handle->pca9548a_channel));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to select PCA9548A channel %d: %s", 
                     handle->pca9548a_channel, esp_err_to_name(ret));
            return ret;
        }
        
        // 减少通道切换等待时间
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // 准备要发送的数据
    // 对于LU9685-20CU: 发送 [命令, 通道号, 角度值]
    uint8_t data[3] = {
        LU9685_CMD_SET_ANGLE,  // 设置角度命令
        channel,               // 通道号
        angle                  // 角度值
    };
    
    // 发送数据到LU9685，使用较短的超时时间
    esp_err_t ret;
    ret = i2c_master_transmit(i2c_dev_handle, data, sizeof(data), 100);
    
    if (ret != ESP_OK) {
        // 如果是通信超时，可能是设备不存在
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "LU9685 communication timeout, device may not be connected");
        } else {
            ESP_LOGE(TAG, "Failed to set channel %d to angle %d: %s", channel, angle, esp_err_to_name(ret));
        }
        return ret;
    }
    
    ESP_LOGD(TAG, "Set LU9685 channel %d to angle %d", channel, angle);
    return ESP_OK;
}

bool lu9685_is_initialized(void)
{
    return (lu9685_dev != NULL && lu9685_dev->is_initialized);
}

lu9685_handle_t lu9685_get_handle(void)
{
    return lu9685_dev;
} 