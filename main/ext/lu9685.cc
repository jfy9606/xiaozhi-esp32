/**
 * @file lu9685.c
 * @brief LU9685-20CU 16通道PWM/舵机控制器实现
 */

#include "include/lu9685.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_err.h"
#include "include/multiplexer.h" // 添加对多路复用器的支持

static const char *TAG = "LU9685";

// 全局 LU9685 句柄
static lu9685_handle_t lu9685_global_handle = NULL;

/**
 * @brief 通过PCA9548A选择I2C通道（如果启用）
 * 
 * @param dev LU9685设备结构体指针
 * @return esp_err_t 操作结果
 */
static esp_err_t select_pca9548a_channel(lu9685_dev_t *dev)
{
    if (dev == NULL || !dev->use_pca9548a) {
        return ESP_OK; // 不使用PCA9548A，直接返回OK
    }
    
    // 检查PCA9548A是否初始化
    if (!pca9548a_is_initialized()) {
        ESP_LOGE(TAG, "PCA9548A未初始化，无法选择通道");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 使用PCA9548A选择通道
    uint8_t channel_mask = (1 << dev->pca9548a_channel);
    esp_err_t ret = pca9548a_select_channel(channel_mask);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCA9548A选择通道 %d 失败: %s", dev->pca9548a_channel, esp_err_to_name(ret));
        return ret;
    }
    
    // 通道选择后短暂延时以稳定
    vTaskDelay(1 / portTICK_PERIOD_MS);
    return ESP_OK;
}

/**
 * @brief 初始化LU9685设备
 * 
 * @param config 配置参数
 * @return lu9685_handle_t 返回设备句柄，如果失败则返回NULL
 */
lu9685_handle_t lu9685_init(const lu9685_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "配置参数为NULL");
        return NULL;
    }

    // 分配内存
    lu9685_dev_t *dev = static_cast<lu9685_dev_t*>(calloc(1, sizeof(lu9685_dev_t)));
    if (dev == NULL) {
        ESP_LOGE(TAG, "内存分配失败");
        return NULL;
    }

    // 复制配置参数
    dev->i2c_port = config->i2c_port;
    dev->i2c_addr = config->i2c_addr;
    dev->pwm_freq = config->pwm_freq > 0 ? config->pwm_freq : 50; // 默认50Hz
    dev->use_pca9548a = config->use_pca9548a;
    dev->pca9548a_channel = config->pca9548a_channel;
    
    // 如果使用PCA9548A，检查PCA9548A是否已经初始化
    if (dev->use_pca9548a) {
        if (!pca9548a_is_initialized()) {
            ESP_LOGW(TAG, "PCA9548A未初始化，尝试使用默认方式初始化多路复用器");
            
            // 尝试初始化多路复用器
            esp_err_t ret = multiplexer_init();
            if (ret != ESP_OK || !pca9548a_is_initialized()) {
                ESP_LOGE(TAG, "无法初始化PCA9548A，请确保先初始化多路复用器");
                free(dev);
                return NULL;
            }
        }
        
        // 选择PCA9548A通道
        if (select_pca9548a_channel(dev) != ESP_OK) {
            ESP_LOGE(TAG, "选择PCA9548A通道失败");
            free(dev);
            return NULL;
        }
    }

    // 检查设备是否存在
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        ESP_LOGE(TAG, "创建I2C命令链接失败");
        free(dev);
        return NULL;
    }

    if (i2c_master_start(cmd) != ESP_OK ||
        i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_WRITE, true) != ESP_OK ||
        i2c_master_stop(cmd) != ESP_OK) {
        ESP_LOGE(TAG, "构造I2C命令失败");
        i2c_cmd_link_delete(cmd);
        free(dev);
        return NULL;
    }
    
    esp_err_t ret = i2c_master_cmd_begin(dev->i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LU9685设备不存在，地址：0x%02x，错误：%s", dev->i2c_addr, esp_err_to_name(ret));
        free(dev);
        return NULL;
    }

    // 初始化设备
    ret = lu9685_reset(dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "重置LU9685设备失败");
        free(dev);
        return NULL;
    }

    // 设置PWM频率
    ret = lu9685_set_frequency(dev, dev->pwm_freq);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置PWM频率失败");
        free(dev);
        return NULL;
    }

    // 保存全局句柄
    lu9685_global_handle = dev;
    return dev;
}

/**
 * @brief 释放LU9685设备资源
 * 
 * @param handle 设备句柄
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_deinit(lu9685_handle_t *handle)
{
    if (handle == NULL || *handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lu9685_dev_t *dev = static_cast<lu9685_dev_t*>(*handle);
    
    // 如果是全局句柄，清空
    if (*handle == lu9685_global_handle) {
        lu9685_global_handle = NULL;
    }
    
    free(dev);
    *handle = NULL;

    return ESP_OK;
}

/**
 * @brief 重置LU9685设备
 * 
 * @param handle 设备句柄
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_reset(lu9685_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lu9685_dev_t *dev = static_cast<lu9685_dev_t*>(handle);
    
    // 如果启用了多路复用器，先选择正确的通道
    if (dev->use_pca9548a) {
        esp_err_t ret = select_pca9548a_channel(dev);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // 准备要写入的数据
    uint8_t reg_addr = LU9685_MODE1;
    uint8_t data = LU9685_RESET;
    
    // 创建I2C通信
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        ESP_LOGE(TAG, "创建I2C命令链接失败");
        return ESP_ERR_NO_MEM;
    }

    if (i2c_master_start(cmd) != ESP_OK ||
        i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_WRITE, true) != ESP_OK ||
        i2c_master_write_byte(cmd, reg_addr, true) != ESP_OK ||
        i2c_master_write_byte(cmd, data, true) != ESP_OK ||
        i2c_master_stop(cmd) != ESP_OK) {
        ESP_LOGE(TAG, "构造I2C命令失败");
        i2c_cmd_link_delete(cmd);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2c_master_cmd_begin(dev->i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "重置LU9685设备失败：%s", esp_err_to_name(ret));
    }

    // 等待重置完成
    vTaskDelay(10 / portTICK_PERIOD_MS);

    return ret;
}

/**
 * @brief 读取寄存器值
 * 
 * @param handle LU9685设备句柄
 * @param reg_addr 寄存器地址
 * @param value 用于存储读取的值
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_read_register(lu9685_handle_t handle, uint8_t reg_addr, uint8_t *value)
{
    if (handle == NULL || value == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    lu9685_dev_t *dev = static_cast<lu9685_dev_t*>(handle);
    
    // 如果启用了多路复用器，先选择正确的通道
    if (dev->use_pca9548a) {
        esp_err_t ret = select_pca9548a_channel(dev);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C command");
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t ret;
    
    // 写入寄存器地址
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    
    // 重复开始，读取数据
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, value, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    
    ret = i2c_master_cmd_begin(dev->i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read register 0x%02x: %s", reg_addr, esp_err_to_name(ret));
    }
    
    return ret;
}

/**
 * @brief 写入寄存器值
 * 
 * @param handle LU9685设备句柄
 * @param reg_addr 寄存器地址
 * @param value 要写入的值
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_write_register(lu9685_handle_t handle, uint8_t reg_addr, uint8_t value)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_ERR_INVALID_ARG;
    }

    lu9685_dev_t *dev = static_cast<lu9685_dev_t*>(handle);
    
    // 如果启用了多路复用器，先选择正确的通道
    if (dev->use_pca9548a) {
        esp_err_t ret = select_pca9548a_channel(dev);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C command");
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t ret;
    
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    
    ret = i2c_master_cmd_begin(dev->i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write register 0x%02x: %s", reg_addr, esp_err_to_name(ret));
    }
    
    return ret;
}

/**
 * @brief 设置LU9685的PWM频率
 * 
 * @param handle LU9685设备句柄
 * @param freq_hz 频率(Hz)，有效范围24-1526Hz
 * @return esp_err_t ESP_OK:成功 其他:失败
 */
esp_err_t lu9685_set_frequency(lu9685_handle_t handle, uint16_t freq_hz)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "Invalid LU9685 handle");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 限制频率范围
    if (freq_hz < 24) {
        freq_hz = 24;  // 最小频率24Hz
        ESP_LOGW(TAG, "Frequency too low, using minimum 24Hz");
    } else if (freq_hz > 1526) {
        freq_hz = 1526;  // 最大频率1526Hz
        ESP_LOGW(TAG, "Frequency too high, using maximum 1526Hz");
    }
    
    // 计算预分频值
    // 预分频值 = round(25MHz / (4096 * freq)) - 1
    uint8_t prescale_value = (uint8_t)(25000000 / (4096.0f * freq_hz) - 1 + 0.5f);
    
    // 保存原始模式
    uint8_t mode1;
    esp_err_t ret = lu9685_read_register(handle, LU9685_MODE1, &mode1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MODE1 register: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 进入睡眠模式（设置SLEEP位）
    uint8_t sleep_mode = (mode1 & ~LU9685_RESTART) | LU9685_SLEEP;
    ret = lu9685_write_register(handle, LU9685_MODE1, sleep_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set sleep mode: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 写入预分频值
    ret = lu9685_write_register(handle, LU9685_PRE_SCALE, prescale_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write prescaler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 恢复原始模式（清除SLEEP位）
    ret = lu9685_write_register(handle, LU9685_MODE1, mode1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore mode: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 等待稳定
    vTaskDelay(pdMS_TO_TICKS(5));
    
    // 设置RESTART位
    ret = lu9685_write_register(handle, LU9685_MODE1, mode1 | LU9685_RESTART);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set restart mode: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Set PWM frequency to %d Hz (prescale value: %d)", freq_hz, prescale_value);
    return ESP_OK;
}

/**
 * @brief 设置单个PWM通道的占空比
 * 
 * @param handle 设备句柄
 * @param channel 通道号（0-15）
 * @param on_value 开始时间（0-4095）
 * @param off_value 结束时间（0-4095）
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_set_pwm_channel(lu9685_handle_t handle, uint8_t channel, uint16_t on_value, uint16_t off_value)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (channel > 15) {
        ESP_LOGE(TAG, "通道编号错误：%d", channel);
        return ESP_ERR_INVALID_ARG;
    }

    lu9685_dev_t *dev = static_cast<lu9685_dev_t*>(handle);
    
    // 如果启用了多路复用器，先选择正确的通道
    if (dev->use_pca9548a) {
        esp_err_t ret = select_pca9548a_channel(dev);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // 准备寄存器地址
    uint8_t reg_addr = LU9685_LED0_ON_L + 4 * channel;
    
    // 发送设置命令
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write_byte(cmd, on_value & 0xFF, true);
    i2c_master_write_byte(cmd, (on_value >> 8) & 0x0F, true);
    i2c_master_write_byte(cmd, off_value & 0xFF, true);
    i2c_master_write_byte(cmd, (off_value >> 8) & 0x0F, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(dev->i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置PWM通道 %d 失败：%s", channel, esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief 设置单个通道的PWM占空比（百分比）
 * 
 * @param handle 设备句柄
 * @param channel 通道号（0-15）
 * @param duty_percent 占空比（0.0-100.0）
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_set_duty_percent(lu9685_handle_t handle, uint8_t channel, float duty_percent)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (channel > 15) {
        ESP_LOGE(TAG, "通道编号错误：%d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (duty_percent < 0.0f) duty_percent = 0.0f;
    if (duty_percent > 100.0f) duty_percent = 100.0f;

    uint16_t off_value = (uint16_t)(duty_percent * 40.96f); // 4096 * (duty_percent / 100.0)
    
    lu9685_dev_t *dev = static_cast<lu9685_dev_t*>(handle);
    
    // 如果启用了多路复用器，先选择正确的通道
    if (dev->use_pca9548a) {
        esp_err_t ret = select_pca9548a_channel(dev);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    return lu9685_set_pwm_channel(handle, channel, 0, off_value);
}

/**
 * @brief 设置所有PWM通道的占空比
 * 
 * @param handle 设备句柄
 * @param on_value 开始时间（0-4095）
 * @param off_value 结束时间（0-4095）
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_set_all_pwm(lu9685_handle_t handle, uint16_t on_value, uint16_t off_value)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lu9685_dev_t *dev = static_cast<lu9685_dev_t*>(handle);

    // 如果启用了多路复用器，先选择正确的通道
    if (dev->use_pca9548a) {
        esp_err_t ret = select_pca9548a_channel(dev);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // 发送设置命令
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, LU9685_ALL_LED_ON_L, true);
    i2c_master_write_byte(cmd, on_value & 0xFF, true);
    i2c_master_write_byte(cmd, (on_value >> 8) & 0x0F, true);
    i2c_master_write_byte(cmd, off_value & 0xFF, true);
    i2c_master_write_byte(cmd, (off_value >> 8) & 0x0F, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(dev->i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置所有PWM通道失败：%s", esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief 设置所有通道的PWM占空比（百分比）
 * 
 * @param handle 设备句柄
 * @param duty_percent 占空比（0.0-100.0）
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_set_all_duty_percent(lu9685_handle_t handle, float duty_percent)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (duty_percent < 0.0f) duty_percent = 0.0f;
    if (duty_percent > 100.0f) duty_percent = 100.0f;

    uint16_t off_value = (uint16_t)(duty_percent * 40.96f); // 4096 * (duty_percent / 100.0)
    
    lu9685_dev_t *dev = static_cast<lu9685_dev_t*>(handle);
    
    // 如果启用了多路复用器，先选择正确的通道
    if (dev->use_pca9548a) {
        esp_err_t ret = select_pca9548a_channel(dev);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    return lu9685_set_all_pwm(handle, 0, off_value);
}

/**
 * @brief 设置器件模式（睡眠/唤醒）
 * 
 * @param handle 设备句柄
 * @param sleep 是否进入睡眠模式
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_set_sleep_mode(lu9685_handle_t handle, bool sleep)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lu9685_dev_t *dev = static_cast<lu9685_dev_t*>(handle);

    // 如果启用了多路复用器，先选择正确的通道
    if (dev->use_pca9548a) {
        esp_err_t ret = select_pca9548a_channel(dev);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // 获取当前模式
    uint8_t mode1;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, LU9685_MODE1, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &mode1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(dev->i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取MODE1寄存器失败：%s", esp_err_to_name(ret));
        return ret;
    }

    // 如果启用了多路复用器，再次选择正确的通道
    if (dev->use_pca9548a) {
        ret = select_pca9548a_channel(dev);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // 设置或清除SLEEP位
    uint8_t newmode;
    if (sleep) {
        newmode = mode1 | LU9685_SLEEP;  // 设置SLEEP位
    } else {
        newmode = mode1 & ~LU9685_SLEEP; // 清除SLEEP位
    }

    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, LU9685_MODE1, true);
    i2c_master_write_byte(cmd, newmode, true);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(dev->i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置睡眠模式失败：%s", esp_err_to_name(ret));
        return ret;
    }

    // 如果从睡眠模式唤醒，等待500us
    if (!sleep) {
        esp_rom_delay_us(500);
    }

    return ESP_OK;
}

/**
 * @brief 设置舵机通道的角度
 * 
 * @param handle 设备句柄
 * @param channel 通道号（0-15）
 * @param angle 角度（0-180）
 * @return esp_err_t 操作结果
 */
esp_err_t lu9685_set_channel_angle(lu9685_handle_t handle, uint8_t channel, uint8_t angle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (channel > 15) {
        ESP_LOGE(TAG, "无效的通道编号: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (angle > 180) {
        angle = 180;
    }
    
    // 角度转换为脉冲宽度
    // 舵机通常使用1ms-2ms的脉冲宽度对应0-180度
    // 在50Hz下，一个周期是20ms，所以1ms对应4096的204.8
    // 因此脉冲宽度范围约为204-410
    uint16_t pulse_width = 204 + (angle * 206) / 180;
    
    // 如果启用了多路复用器，先选择正确的通道
    lu9685_dev_t *dev = static_cast<lu9685_dev_t*>(handle);
    if (dev->use_pca9548a) {
        select_pca9548a_channel(dev);
    }
    
    // 设置PWM
    return lu9685_set_pwm_channel(handle, channel, 0, pulse_width);
}

/**
 * @brief 检查LU9685是否已初始化
 * 
 * @return bool 
 */
bool lu9685_is_initialized(void)
{
    return (lu9685_global_handle != NULL);
}

/**
 * @brief 获取LU9685全局句柄
 * 
 * @return lu9685_handle_t 
 */
lu9685_handle_t lu9685_get_handle(void)
{
    if (!lu9685_global_handle) {
        ESP_LOGW(TAG, "LU9685 handle requested but not initialized");
    }
    return lu9685_global_handle;
} 