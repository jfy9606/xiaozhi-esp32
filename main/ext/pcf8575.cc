#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "board_config.h"
#include "include/pcf8575.h"
#include "include/pca9548a.h"
#include "include/multiplexer.h"
#include "driver/i2c.h"

static const char* TAG = "PCF8575";

// PCF8575默认配置
#define PCF8575_I2C_ADDR          0x20
#define PCF8575_I2C_PORT          I2C_NUM_0
#define PCF8575_I2C_TIMEOUT_MS    50

// PCF8575默认连接在PCA9548A的通道0上
#define PCF8575_PCA9548A_CHANNEL      PCA9548A_CHANNEL_0

// 全局变量
static pcf8575_handle_t pcf8575_global_handle = NULL;

/**
 * @brief 通过PCA9548A选择I2C通道（如果启用）
 * 
 * @param dev PCF8575设备结构体指针
 * @return esp_err_t 操作结果
 */
static esp_err_t select_pcf8575_channel(pcf8575_dev_t *dev)
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
 * @brief 读取PCF8575的端口状态
 * 
 * @param handle PCF8575设备句柄
 * @param value 用于存储读取的值
 * @return esp_err_t 
 */
esp_err_t pcf8575_read_ports(pcf8575_handle_t handle, uint16_t *value) {
    if (!handle || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    pcf8575_dev_t *dev = static_cast<pcf8575_dev_t*>(handle);
    
    // 如果启用了多路复用器，先选择正确的通道
    if (dev->use_pca9548a) {
        esp_err_t ret = select_pcf8575_channel(dev);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    uint8_t data[2] = {0};
    esp_err_t ret;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &data[0], I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &data[1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(dev->i2c_port, cmd, dev->i2c_timeout_ms / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        *value = ((uint16_t)data[1] << 8) | data[0];
    } else {
        ESP_LOGE(TAG, "读取PCF8575端口状态失败: %s", esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief 写入PCF8575的端口状态
 * 
 * @param handle PCF8575设备句柄
 * @param value 待写入的值
 * @return esp_err_t
 */
esp_err_t pcf8575_write_ports(pcf8575_handle_t handle, uint16_t value) {
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    pcf8575_dev_t *dev = static_cast<pcf8575_dev_t*>(handle);
    
    // 如果启用了多路复用器，先选择正确的通道
    if (dev->use_pca9548a) {
        esp_err_t ret = select_pcf8575_channel(dev);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    esp_err_t ret;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    
    // 准备发送数据 - 低字节在前，高字节在后
    uint8_t data[2];
    data[0] = static_cast<uint8_t>(value & 0xFFU);         // 低8位
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);  // 高8位
    
    i2c_master_write(cmd, data, 2, true);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(dev->i2c_port, cmd, dev->i2c_timeout_ms / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "写入PCF8575端口状态失败: %s", esp_err_to_name(ret));
    } else {
        // 更新当前输出状态
        dev->output_state = value;
    }

    return ret;
}

/**
 * @brief 创建PCF8575设备
 * 
 * @param config PCF8575配置
 * @return pcf8575_handle_t 设备句柄，NULL表示创建失败
 */
pcf8575_handle_t pcf8575_create(const pcf8575_config_t *config) {
    if (!config) {
        ESP_LOGE(TAG, "PCF8575配置为NULL");
        return NULL;
    }

    pcf8575_dev_t *dev = static_cast<pcf8575_dev_t*>(calloc(1, sizeof(pcf8575_dev_t)));
    if (!dev) {
        ESP_LOGE(TAG, "内存分配失败");
        return NULL;
    }

    dev->i2c_port = config->i2c_port;
    dev->i2c_addr = config->i2c_addr;
    dev->i2c_timeout_ms = config->i2c_timeout_ms;
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
        if (select_pcf8575_channel(dev) != ESP_OK) {
            ESP_LOGE(TAG, "选择PCA9548A通道失败");
            free(dev);
            return NULL;
        }
    }

    // 检查I2C设备是否存在
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(dev->i2c_port, cmd, dev->i2c_timeout_ms / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCF8575设备不存在，地址: 0x%02x，错误: %s", dev->i2c_addr, esp_err_to_name(ret));
        free(dev);
        return NULL;
    }

    // 如果启用了多路复用器，再次选择正确的通道
    if (dev->use_pca9548a) {
        ret = select_pcf8575_channel(dev);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "选择PCA9548A通道失败");
            free(dev);
            return NULL;
        }
    }

    // 默认将所有引脚设置为高电平（输入模式）
    if (config->all_output) {
        if (pcf8575_write_ports(static_cast<pcf8575_handle_t>(dev), 0x0000) != ESP_OK) {
            ESP_LOGE(TAG, "设置全部端口为输出模式失败");
            free(dev);
            return NULL;
        }
        dev->output_state = 0x0000;
    } else {
        if (pcf8575_write_ports(static_cast<pcf8575_handle_t>(dev), 0xFFFF) != ESP_OK) {
            ESP_LOGE(TAG, "设置默认端口状态失败");
            free(dev);
            return NULL;
        }
        dev->output_state = 0xFFFF;
    }

    return static_cast<pcf8575_handle_t>(dev);
}

/**
 * @brief 销毁PCF8575设备
 * 
 * @param handle PCF8575设备句柄指针
 * @return esp_err_t
 */
esp_err_t pcf8575_delete(pcf8575_handle_t *handle) {
    if (!handle || !(*handle)) {
        return ESP_ERR_INVALID_ARG;
    }

    pcf8575_dev_t *dev = static_cast<pcf8575_dev_t*>(*handle);
    free(dev);
    *handle = NULL;

    return ESP_OK;
}

/**
 * @brief 设置PCF8575引脚电平
 * 
 * @param handle PCF8575设备句柄
 * @param pin 引脚编号 (0-15)
 * @param level 要设置的电平 (0=低, 1=高)
 * @return esp_err_t ESP_OK:成功 其他:失败
 */
esp_err_t pcf8575_set_level(pcf8575_handle_t handle, int pin, uint32_t level)
{
    if (handle == NULL || pin < 0 || pin > 15) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    pcf8575_dev_t *dev = static_cast<pcf8575_dev_t*>(handle);
    
    // 选择PCA9548A通道（如果启用）
    if (dev->use_pca9548a) {
        esp_err_t ret = select_pcf8575_channel(dev);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // 更新输出状态
    if (level) {
        dev->output_state |= (1 << pin);  // 设置为高电平
    } else {
        dev->output_state &= ~(1 << pin); // 设置为低电平
    }
    
    // 向PCF8575写入输出状态
    uint8_t data[2];
    data[0] = dev->output_state & 0xFF;       // 低字节
    data[1] = (dev->output_state >> 8) & 0xFF; // 高字节
    
    esp_err_t ret = i2c_master_write_to_device(dev->i2c_port, dev->i2c_addr, data, sizeof(data), pdMS_TO_TICKS(dev->i2c_timeout_ms));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set pin %d to level %ld: %s", pin, level, esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "Set pin %d to level %ld", pin, level);
    return ESP_OK;
}

/**
 * @brief 读取PCF8575引脚电平
 * 
 * @param handle PCF8575设备句柄
 * @param pin 引脚编号 (0-15)
 * @param level 用于存储读取到的电平值的指针 (0=低, 1=高)
 * @return esp_err_t ESP_OK:成功 其他:失败
 */
esp_err_t pcf8575_get_level(pcf8575_handle_t handle, int pin, uint32_t *level)
{
    if (handle == NULL || pin < 0 || pin > 15 || level == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    pcf8575_dev_t *dev = static_cast<pcf8575_dev_t*>(handle);
    
    // 选择PCA9548A通道（如果启用）
    if (dev->use_pca9548a) {
        esp_err_t ret = select_pcf8575_channel(dev);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // 从PCF8575读取状态
    uint8_t data[2] = {0};
    esp_err_t ret = i2c_master_read_from_device(dev->i2c_port, dev->i2c_addr, data, sizeof(data), pdMS_TO_TICKS(dev->i2c_timeout_ms));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read device state: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 组合低字节和高字节
    uint16_t port_state = ((uint16_t)data[1] << 8) | data[0];
    
    // 提取指定引脚的电平
    *level = (port_state & (1 << pin)) ? 1 : 0;
    
    ESP_LOGD(TAG, "Read pin %d level: %ld", pin, *level);
    return ESP_OK;
}

/**
 * @brief 获取当前端口状态的缓存值（不进行I2C读取）
 * 
 * @param handle PCF8575设备句柄
 * @param value 用于存储端口状态的变量
 * @return esp_err_t 
 */
esp_err_t pcf8575_get_port_state(pcf8575_handle_t handle, uint16_t *value) {
    if (!handle || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    pcf8575_dev_t *dev = static_cast<pcf8575_dev_t*>(handle);
    *value = dev->output_state;

    return ESP_OK;
}

/**
 * @brief 设置多个引脚的状态
 * 
 * @param handle PCF8575设备句柄
 * @param pin_mask 引脚掩码，每个位对应一个引脚 (0-15)
 * @param levels 电平值，每个位对应一个引脚的状态
 * @return esp_err_t 
 */
esp_err_t pcf8575_set_pins(pcf8575_handle_t handle, uint16_t pin_mask, uint16_t levels) {
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    pcf8575_dev_t *dev = static_cast<pcf8575_dev_t*>(handle);
    
    // 如果启用了多路复用器，先选择正确的通道
    if (dev->use_pca9548a) {
        esp_err_t ret = select_pcf8575_channel(dev);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    // 获取当前状态并只修改指定的引脚
    uint16_t current_state = dev->output_state;
    
    // 清除要更改的位，只保留未包含在掩码中的位
    current_state &= ~pin_mask;
    
    // 设置新的电平，只修改掩码中包含的位
    current_state |= (levels & pin_mask);
    
    // 写入更新后的状态
    esp_err_t ret = pcf8575_write_ports(handle, current_state);
    
    ESP_LOGD(TAG, "设置引脚 掩码=0x%04X, 状态=0x%04X, 结果状态=0x%04X", 
             pin_mask, levels, current_state);
    
    return ret;
}

/**
 * @brief 初始化PCF8575并设置默认配置
 * 
 * @param i2c_port I2C端口号
 * @param i2c_addr I2C地址
 * @param use_pca9548a 是否通过PCA9548A访问
 * @param pca9548a_channel 如果使用PCA9548A，指定通道号
 * @return pcf8575_handle_t 设备句柄，NULL表示创建失败
 */
pcf8575_handle_t pcf8575_init_with_defaults(i2c_port_t i2c_port, uint8_t i2c_addr, bool use_pca9548a, uint8_t pca9548a_channel) {
    // 如果使用PCA9548A，确保已初始化
    if (use_pca9548a && !pca9548a_is_initialized()) {
        ESP_LOGI(TAG, "PCA9548A未初始化，尝试初始化多路复用器");
        if (multiplexer_init() != ESP_OK || !pca9548a_is_initialized()) {
            ESP_LOGE(TAG, "无法初始化PCA9548A");
            return NULL;
        }
    }
    
    // 配置PCF8575设备
    pcf8575_config_t config = {
        .i2c_port = i2c_port,
        .i2c_addr = i2c_addr,
        .i2c_timeout_ms = 50,          // 使用较短的超时时间
        .all_output = false,           // 默认为输入模式（高电平）
        .use_pca9548a = use_pca9548a,
        .pca9548a_channel = pca9548a_channel
    };
    
    // 创建设备并返回句柄
    pcf8575_handle_t handle = pcf8575_create(&config);
    
    if (handle == NULL) {
        ESP_LOGE(TAG, "无法创建PCF8575设备");
        return NULL;
    }
    
    ESP_LOGI(TAG, "PCF8575初始化成功，地址=0x%02X%s", 
             i2c_addr, use_pca9548a ? ", 使用PCA9548A" : "");
    
    return handle;
}

// 检查PCF8575是否已初始化
bool pcf8575_is_initialized(void)
{
    return (pcf8575_global_handle != NULL);
}

// 获取PCF8575句柄
pcf8575_handle_t pcf8575_get_handle(void)
{
    return pcf8575_global_handle;
}

// 初始化PCF8575
esp_err_t pcf8575_init(void)
{
    if (pcf8575_global_handle != NULL) {
        ESP_LOGW(TAG, "PCF8575 already initialized");
        return ESP_OK;
    }

    // 检查PCA9548A是否已初始化
    if (!pca9548a_is_initialized()) {
        ESP_LOGE(TAG, "PCA9548A not initialized, cannot proceed with PCF8575 initialization");
        return ESP_ERR_INVALID_STATE;
    }

    // 分配PCF8575设备结构体内存
    pcf8575_handle_t handle = (pcf8575_handle_t)calloc(1, sizeof(pcf8575_dev_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for PCF8575");
        return ESP_ERR_NO_MEM;
    }

    // 配置PCF8575设备参数
    handle->i2c_port = PCF8575_I2C_PORT;
    handle->i2c_addr = PCF8575_I2C_ADDR;
    handle->i2c_timeout_ms = PCF8575_I2C_TIMEOUT_MS;
    handle->output_state = 0xFFFF; // 默认所有IO为输入模式
    handle->use_pca9548a = true;
    handle->pca9548a_channel = PCF8575_PCA9548A_CHANNEL;

    ESP_LOGI(TAG, "Initializing PCF8575 with I2C address 0x%X on PCA9548A channel %d", 
            handle->i2c_addr, handle->pca9548a_channel);
    
    // 初始测试：写入全1，然后读回，确保通信正常
    esp_err_t ret = pcf8575_write_ports(handle, 0xFFFF);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to communicate with PCF8575");
        free(handle);
        return ret;
    }
    
    // 将句柄保存在全局变量中
    pcf8575_global_handle = handle;
    ESP_LOGI(TAG, "PCF8575 initialized successfully");
    return ESP_OK;
}

// 设置GPIO模式（PCF8575实际上通过读写操作来控制方向）
esp_err_t pcf8575_set_gpio_mode(pcf8575_handle_t handle, pcf8575_gpio_t gpio_num, pcf8575_gpio_mode_t mode)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "PCF8575设备句柄为NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // PCF8575中，输入模式写1，输出模式写0然后设置输出值
    if (gpio_num < 0 || gpio_num >= PCF8575_IO_MAX) {
        ESP_LOGE(TAG, "PCF8575 GPIO号无效: %d", gpio_num);
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t mask = (1 << gpio_num);
    
    if (mode == PCF8575_GPIO_MODE_INPUT) {
        // 对于输入引脚，将对应位设置为1
        handle->output_state |= mask;
    } else {
        // 对于输出引脚，将对应位清零
        handle->output_state &= ~mask;
    }
    
    // 更新PCF8575状态
    return pcf8575_write_ports(handle, handle->output_state);
}

// 设置GPIO电平，兼容函数，调用pcf8575_set_level
esp_err_t pcf8575_set_gpio_level(pcf8575_handle_t handle, pcf8575_gpio_t gpio_num, uint32_t level)
{
    return pcf8575_set_level(handle, gpio_num, level);
}

// 获取GPIO电平，兼容函数，调用pcf8575_get_level
esp_err_t pcf8575_get_gpio_level(pcf8575_handle_t handle, pcf8575_gpio_t gpio_num, uint32_t *level)
{
    return pcf8575_get_level(handle, gpio_num, level);
}

// 获取所有端口状态
esp_err_t pcf8575_get_all(pcf8575_handle_t handle, uint16_t *value)
{
    return pcf8575_read_ports(handle, value);
}

// 设置所有端口状态
esp_err_t pcf8575_set_all(pcf8575_handle_t handle, uint16_t value)
{
    return pcf8575_write_ports(handle, value);
}