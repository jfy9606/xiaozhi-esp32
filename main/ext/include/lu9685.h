/**
 * @file lu9685.h
 * @brief LU9685-20CU 16通道PWM/舵机控制器API
 * 
 * 该文件定义了与LU9685-20CU 16通道PWM/舵机控制器通信的API。
 * LU9685-20CU基于STC8H微控制器，支持I2C和UART通信，最高支持300Hz PWM频率。
 * 默认I2C地址为0x80，连接在PCA9548A的通道1上。
 */

#ifndef LU9685_H
#define LU9685_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 
 * @brief LU9685控制器句柄
 */
typedef struct lu9685_dev_t* lu9685_handle_t;

/** 
 * @brief LU9685配置参数
 */
typedef struct {
    uint8_t i2c_addr;       // LU9685 I2C地址，默认为0x80
    uint8_t pca9548a_channel; // LU9685连接的PCA9548A通道，默认为1
    bool use_pca9548a;      // 是否使用PCA9548A多路复用器
    uint16_t frequency_hz;  // PWM频率 (Hz)，默认为50Hz，最高支持300Hz
} lu9685_config_t;

/**
 * @brief 初始化LU9685-20CU
 * 
 * @param config 配置参数
 * @return lu9685_handle_t 返回LU9685控制器句柄，失败则返回NULL
 */
lu9685_handle_t lu9685_init(const lu9685_config_t *config);

/**
 * @brief 释放LU9685-20CU相关资源
 * 
 * @param handle LU9685控制器句柄
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t lu9685_deinit(lu9685_handle_t handle);

/**
 * @brief 设置指定通道的舵机角度
 * 
 * @param handle LU9685控制器句柄
 * @param channel 通道号 (0-15)
 * @param angle 角度 (0-180)
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t lu9685_set_channel_angle(lu9685_handle_t handle, uint8_t channel, uint8_t angle);

/**
 * @brief 设置PWM频率
 * 
 * @param handle LU9685控制器句柄
 * @param freq_hz 频率 (Hz)，范围：50-300Hz
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t lu9685_set_frequency(lu9685_handle_t handle, uint16_t freq_hz);

/**
 * @brief 检查LU9685是否已初始化
 * 
 * @return true 已初始化
 * @return false 未初始化
 */
bool lu9685_is_initialized(void);

/**
 * @brief 获取LU9685控制器句柄
 * 
 * @return lu9685_handle_t LU9685控制器句柄，未初始化则返回NULL
 */
lu9685_handle_t lu9685_get_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* LU9685_H */ 