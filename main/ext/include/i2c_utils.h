/**
 * @file i2c_utils.h
 * @brief I2C工具函数和地址常量定义
 * 
 * ESP-IDF使用7位地址格式，但许多设备数据手册中提供的是8位格式（包含读/写位）
 * 本文件提供地址转换工具和常见设备的7位地址定义
 */

#pragma once

#include <stdint.h>

/**
 * @brief 将8位I2C地址转换为7位格式
 * 
 * ESP-IDF要求使用7位地址格式，但一些设备数据手册提供的是8位格式（包含读/写位）
 * 此宏将8位格式的地址转换为7位格式
 * 
 * 使用示例:
 *   - 数据手册给出8位地址：0x80 
 *   - ESP-IDF需要的7位地址：I2C_8BIT_TO_7BIT(0x80) = 0x40
 * 
 * @param addr_8bit 8位格式地址 (如0x80)
 * @return 7位格式地址 (如0x40)
 */
#define I2C_8BIT_TO_7BIT(addr_8bit) ((addr_8bit) >> 1)

/**
 * @brief 将7位I2C地址转换为8位格式（带写标志）
 * 
 * 此宏将7位格式的地址转换为8位格式，最低位为写标志（0）
 * 
 * @param addr_7bit 7位格式地址 (如0x40)
 * @return 8位格式地址，写模式 (如0x80)
 */
#define I2C_7BIT_TO_8BIT_W(addr_7bit) ((addr_7bit) << 1)

/**
 * @brief 将7位I2C地址转换为8位格式（带读标志）
 * 
 * 此宏将7位格式的地址转换为8位格式，最低位为读标志（1）
 * 
 * @param addr_7bit 7位格式地址 (如0x40)
 * @return 8位格式地址，读模式 (如0x81)
 */
#define I2C_7BIT_TO_8BIT_R(addr_7bit) (((addr_7bit) << 1) | 0x01)

/**
 * @brief 常见I2C设备的7位地址定义
 * 注：所有以下地址均为ESP-IDF兼容的7位格式
 */

/* 多路复用器 */
#define I2C_ADDR_PCA9548A_BASE      0x70    // PCA9548A多路复用器的基础地址 (7位格式，范围0x70-0x77)
#define I2C_ADDR_PCF8575_BASE       0x20    // PCF8575 GPIO多路复用器的基础地址 (7位格式，范围0x20-0x27)
#define I2C_ADDR_PCF8574_BASE       0x20    // PCF8574 GPIO多路复用器的基础地址 (7位格式，范围0x20-0x27)
#define I2C_ADDR_PCF8574A_BASE      0x38    // PCF8574A GPIO多路复用器的基础地址 (7位格式，范围0x38-0x3F)
#define I2C_ADDR_TCA9554_BASE       0x20    // TCA9554 GPIO多路复用器的基础地址 (7位格式，范围0x20-0x27)

/* 舵机和电机控制器 */
#define I2C_ADDR_LU9685             0x40    // LU9685-20CU舵机控制器地址 (7位格式，8位格式为0x80)
#define I2C_ADDR_PCA9685_DEFAULT    0x40    // PCA9685 PWM控制器默认地址 (7位格式，8位格式为0x80)

/* 显示屏控制器 */
#define I2C_ADDR_SSD1306            0x3C    // SSD1306 OLED显示屏地址 (7位格式，8位格式为0x78)
#define I2C_ADDR_SH1106             0x3C    // SH1106 OLED显示屏地址 (7位格式，8位格式为0x78)
#define I2C_ADDR_ILI9341            0x3C    // ILI9341 LCD控制器地址 (7位格式)

/* 传感器 */
#define I2C_ADDR_MPU6050            0x68    // MPU6050 6轴传感器地址 (7位格式，8位格式为0xD0)
#define I2C_ADDR_BMP280             0x76    // BMP280 气压传感器地址 (7位格式，8位格式为0xEC)
#define I2C_ADDR_BME280             0x76    // BME280 环境传感器地址 (7位格式，8位格式为0xEC)
#define I2C_ADDR_AHT10              0x38    // AHT10 温湿度传感器地址 (7位格式，8位格式为0x70)
#define I2C_ADDR_HTU21D             0x40    // HTU21D 温湿度传感器地址 (7位格式，8位格式为0x80)
#define I2C_ADDR_SHT30_DEFAULT      0x44    // SHT30 温湿度传感器默认地址 (7位格式，8位格式为0x88)
#define I2C_ADDR_SHT30_ALT          0x45    // SHT30 温湿度传感器备选地址 (7位格式，8位格式为0x8A)

/* 音频编解码器 */
#define I2C_ADDR_ES8388             0x10    // ES8388 音频编解码器地址 (7位格式)
#define I2C_ADDR_ES7210             0x40    // ES7210 ADC地址 (7位格式)

/* I/O接口 */
#define I2C_ADDR_FT5X06             0x38    // FT5X06 触摸控制器地址 (7位格式)
#define I2C_ADDR_GT911              0x5D    // GT911/GT9271 触摸控制器默认地址 (7位格式)

/* 全局I2C句柄声明 - 在multiplexer.cc中定义 */
#ifdef CONFIG_ENABLE_PCA9548A
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

extern i2c_master_bus_handle_t i2c_bus_handle;
extern i2c_master_dev_handle_t i2c_dev_handle;

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_ENABLE_PCA9548A */ 