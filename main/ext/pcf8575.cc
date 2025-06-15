#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "../boards/common/board.h"
#include "include/pcf8575.h"
#include "include/pca9548a.h"
#include "include/multiplexer.h"
#include "driver/i2c_master.h"
#include "esp_timer.h" // 用于延时函数

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
static esp_err_t select_pcf8575_channel(pcf8575_dev_t* dev) {
    if (!dev || !dev->use_pca9548a) return ESP_OK;
    
    uint8_t channel_mask = (1 << dev->pca9548a_channel);
    ESP_LOGD(TAG, "Selecting PCA9548A channel: mask=0x%02x", channel_mask);
    
    // 获取PCA9548A句柄并使用正确的函数
    pca9548a_handle_t handle = pca9548a_get_handle();
    if (!handle) {
        ESP_LOGE(TAG, "PCA9548A handle is NULL");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = pca9548a_select_channels(handle, channel_mask);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select PCA9548A channel: %s", esp_err_to_name(ret));
        return ret;
    }
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

    // 使用新版I2C API
    i2c_master_bus_handle_t bus_handle = (i2c_master_bus_handle_t)dev->i2c_port;
    i2c_master_dev_handle_t dev_handle;

    // 配置I2C设备
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev->i2c_addr,
        .scl_speed_hz = 400000, // 400kHz
    };

    // 临时创建设备句柄
    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "无法创建I2C设备: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 读取数据
    ret = i2c_master_receive(dev_handle, data, sizeof(data), dev->i2c_timeout_ms);

    // 删除临时设备
    i2c_master_bus_rm_device(dev_handle);

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
    
    // 准备发送数据 - 低字节在前，高字节在后
    uint8_t data[2];
    data[0] = static_cast<uint8_t>(value & 0xFFU);         // 低8位
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);  // 高8位
    
    // 使用新版I2C API
    i2c_master_bus_handle_t bus_handle = (i2c_master_bus_handle_t)dev->i2c_port;
    i2c_master_dev_handle_t dev_handle;

    // 配置I2C设备
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev->i2c_addr,
        .scl_speed_hz = 400000, // 400kHz
    };

    // 临时创建设备句柄
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "无法创建I2C设备: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 发送数据
    ret = i2c_master_transmit(dev_handle, data, sizeof(data), dev->i2c_timeout_ms);

    // 删除临时设备
    i2c_master_bus_rm_device(dev_handle);

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
    if (config == NULL) {
        ESP_LOGE(TAG, "配置为NULL");
        return NULL;
    }
    
    // 分配内存
    pcf8575_dev_t *dev = (pcf8575_dev_t *)malloc(sizeof(pcf8575_dev_t));
    if (dev == NULL) {
        ESP_LOGE(TAG, "内存分配失败");
        return NULL;
    }
    
    // 复制配置
    dev->i2c_port = config->i2c_port;
    dev->i2c_addr = config->i2c_addr;
    dev->i2c_timeout_ms = config->i2c_timeout_ms;
    dev->output_state = 0xFFFF; // 默认全部设置为高电平(输入模式)
    dev->use_pca9548a = config->use_pca9548a;
    dev->pca9548a_channel = config->pca9548a_channel;
    
    // 检查PCF8575设备是否可访问
    i2c_master_bus_handle_t bus_handle = dev->i2c_port;
    
    // 尝试探测设备是否存在
    esp_err_t ret = i2c_master_probe(bus_handle, dev->i2c_addr, dev->i2c_timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCF8575设备探测失败(0x%02X), %s", dev->i2c_addr, esp_err_to_name(ret));
        free(dev);
        return NULL;
    }
    
    // 如果需要所有引脚设为输出模式，写入0
    if (config->all_output) {
        dev->output_state = 0x0000; // 全部设置为低电平（输出模式）
        
        // 向PCF8575写入输出值
        if (pcf8575_write_ports(dev, dev->output_state) != ESP_OK) {
            ESP_LOGE(TAG, "PCF8575设置输出模式失败");
            free(dev);
            return NULL;
        }
    }
    
    ESP_LOGI(TAG, "PCF8575设备创建成功，地址: 0x%02X", dev->i2c_addr);
    return dev;
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
    
    // 使用新版I2C API
    i2c_master_bus_handle_t bus_handle = (i2c_master_bus_handle_t)dev->i2c_port;
    i2c_master_dev_handle_t dev_handle;

    // 配置I2C设备
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev->i2c_addr,
        .scl_speed_hz = 400000, // 400kHz
    };

    // 临时创建设备句柄
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "无法创建I2C设备: %s", esp_err_to_name(ret));
        return ret;
    }

    // 发送数据
    ret = i2c_master_transmit(dev_handle, data, sizeof(data), dev->i2c_timeout_ms);

    // 删除临时设备
    i2c_master_bus_rm_device(dev_handle);
    
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
    
    // 使用新版I2C API
    i2c_master_bus_handle_t bus_handle = (i2c_master_bus_handle_t)dev->i2c_port;
    i2c_master_dev_handle_t dev_handle;

    // 配置I2C设备
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev->i2c_addr,
        .scl_speed_hz = 400000, // 400kHz
    };

    // 临时创建设备句柄
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "无法创建I2C设备: %s", esp_err_to_name(ret));
        return ret;
    }

    // 读取数据
    ret = i2c_master_receive(dev_handle, data, sizeof(data), dev->i2c_timeout_ms);

    // 删除临时设备
    i2c_master_bus_rm_device(dev_handle);
    
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
 * @param i2c_port I2C总线句柄
 * @param i2c_addr I2C地址
 * @param use_pca9548a 是否通过PCA9548A访问
 * @param pca9548a_channel 如果使用PCA9548A，指定通道号
 * @return pcf8575_handle_t 设备句柄，NULL表示创建失败
 */
pcf8575_handle_t pcf8575_init_with_defaults(i2c_master_bus_handle_t i2c_port, uint8_t i2c_addr, bool use_pca9548a, uint8_t pca9548a_channel) {
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

/**
 * @brief 初始化PCF8575
 * 
 * @return esp_err_t 
 */
esp_err_t pcf8575_init(void) {
    // 如果已经初始化，则直接返回
    if (pcf8575_global_handle != NULL) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "初始化PCF8575设备");
    
    // 创建PCF8575设备
    pcf8575_handle_t handle = (pcf8575_handle_t)malloc(sizeof(pcf8575_dev_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "无法分配内存");
        return ESP_ERR_NO_MEM;
    }
    
    // 配置I2C端口
    // 注意：这里我们需要先从BoardGetI2CMasterBusHandle获取总线句柄
    // 如果你的项目中有提供获取I2C总线句柄的函数，应该使用该函数
    // 此处仅为示例，实际应根据项目情况调整
    
    // 由于无法直接将i2c_port_t转换为i2c_master_bus_handle_t，
    // 这里需要通过外部函数获取总线句柄，或者修改BoardConfig中的代码
    // 临时解决方案：让用户在使用pcf8575_init_with_defaults时提供有效的句柄
    
    // 配置PCF8575
    handle->i2c_addr = PCF8575_I2C_ADDR;          // 使用默认地址
    handle->i2c_timeout_ms = PCF8575_I2C_TIMEOUT_MS; // 设置超时时间
    handle->output_state = 0xFFFF;                // 默认全高电平
    handle->use_pca9548a = true;                 // 默认使用多路复用器
    handle->pca9548a_channel = PCF8575_PCA9548A_CHANNEL; // 默认通道
    
    // 存储全局句柄
    pcf8575_global_handle = handle;
    
    ESP_LOGI(TAG, "PCF8575初始化完成");
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
esp_err_t pcf8575_get_gpio_level(pcf8575_handle_t handle, pcf8575_gpio_t gpio_num, uint32_t *level) {
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