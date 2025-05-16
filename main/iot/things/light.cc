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

#include <memory>
#include <cstring>

#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "board_config.h"
#include "../thing.h"

static constexpr char TAG[] = "Light";

// BH1750 sensor address
#define BH1750_ADDR_L 0x23 // ADDR pin to GND
#define BH1750_ADDR_H 0x5C // ADDR pin to VCC

// BH1750 instruction set
#define BH1750_POWER_DOWN     0x00 // Power down
#define BH1750_POWER_ON       0x01 // Power on
#define BH1750_RESET          0x07 // Reset
#define BH1750_CONT_H_RES     0x10 // Continuous high resolution mode (1 lux resolution)
#define BH1750_CONT_H_RES2    0x11 // Continuous high resolution mode 2 (0.5 lux resolution)
#define BH1750_CONT_L_RES     0x13 // Continuous low resolution mode (4 lux resolution)
#define BH1750_ONE_H_RES      0x20 // One-time high resolution mode
#define BH1750_ONE_H_RES2     0x21 // One-time high resolution mode 2
#define BH1750_ONE_L_RES      0x23 // One-time low resolution mode

// I2C configuration
#define I2C_MASTER_FREQ_HZ 100000     // 100KHz
#define I2C_TIMEOUT_MS     1000       // 1 second

// Data update interval (milliseconds)
#define LIGHT_UPDATE_INTERVAL_MS 1000 // Update light data every second

// Define I2C configuration pins if not present in board config
#ifndef CONFIG_I2C_PORT
#define CONFIG_I2C_PORT ((i2c_port_t)0)
#endif

#ifndef CONFIG_I2C_SDA_PIN
#define CONFIG_I2C_SDA_PIN 21
#endif

#ifndef CONFIG_I2C_SCL_PIN
#define CONFIG_I2C_SCL_PIN 22
#endif

namespace iot {

class Light : public Thing {
public:
    Light() : Thing("light", "Light intensity sensor") {
        // Setup property for light measurement
        properties_.AddNumberProperty("lux", "Light level in lux", 
            [this]() { return static_cast<int>(light_level_); });
            
        // Add a configuration method
        ParameterList configParams;
        configParams.AddParameter(Parameter("i2c_port", "I2C port number", kValueTypeNumber, false));
        configParams.AddParameter(Parameter("sda_pin", "I2C SDA pin", kValueTypeNumber, false));
        configParams.AddParameter(Parameter("scl_pin", "I2C SCL pin", kValueTypeNumber, false));
        
        methods_.AddMethod("configure", "Configure the light sensor", configParams,
            [this](const ParameterList& params) {
                // Update configuration if parameters are provided
                try {
                    bool updated = false;
                    
                    if (params["i2c_port"].type() == kValueTypeNumber) {
                        i2c_port_ = static_cast<i2c_port_t>(params["i2c_port"].number());
                        updated = true;
                    }
                    
                    if (params["sda_pin"].type() == kValueTypeNumber && 
                        params["scl_pin"].type() == kValueTypeNumber) {
                        sda_pin_ = static_cast<gpio_num_t>(params["sda_pin"].number());
                        scl_pin_ = static_cast<gpio_num_t>(params["scl_pin"].number());
                        updated = true;
                    }
                    
                    // Re-initialize hardware if any parameters were updated
                    if (updated && initialized_) {
                        DeInit();
                        Init();
                    }
                } catch (const std::exception& e) {
                    ESP_LOGE(TAG, "Error in configure method: %s", e.what());
                }
            });
    }

    ~Light() {
        DeInit();
    }

    void Init() {
        ESP_LOGI(TAG, "Initializing Light Sensor...");

        // Use default I2C pins if not already set
        int sda = CONFIG_I2C_SDA_PIN;
        int scl = CONFIG_I2C_SCL_PIN;
        if (sda_pin_ == GPIO_NUM_NC && sda >= 0 && sda < GPIO_NUM_MAX) sda_pin_ = static_cast<gpio_num_t>(sda);
        if (scl_pin_ == GPIO_NUM_NC && scl >= 0 && scl < GPIO_NUM_MAX) scl_pin_ = static_cast<gpio_num_t>(scl);
        if (i2c_port_ < 0) i2c_port_ = CONFIG_I2C_PORT;

        if (sda_pin_ == GPIO_NUM_NC || scl_pin_ == GPIO_NUM_NC) {
            ESP_LOGE(TAG, "I2C pins not configured for this board");
            return;
        }

        ESP_LOGI(TAG, "Using I2C - Port: %d, SDA: %d, SCL: %d", 
                i2c_port_, sda_pin_, scl_pin_);

        // 新I2C驱动：配置总线
        i2c_master_bus_config_t bus_config = {
            .i2c_port = i2c_port_,
            .sda_io_num = sda_pin_,
            .scl_io_num = scl_pin_,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        esp_err_t ret = i2c_new_master_bus(&bus_config, &bus_handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
            return;
        }

        // 新I2C驱动：配置设备
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = BH1750_ADDR_L, // 先用低地址，后面探测
            .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        };
        ret = i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
            i2c_del_master_bus(bus_handle_);
            bus_handle_ = NULL;
            return;
        }

        // Try to detect the BH1750 sensor on both possible addresses
        if (DetectSensor(BH1750_ADDR_L)) {
            sensor_addr_ = BH1750_ADDR_L;
            ESP_LOGI(TAG, "BH1750 sensor detected at address 0x%02X", sensor_addr_);
        } else if (DetectSensor(BH1750_ADDR_H)) {
            sensor_addr_ = BH1750_ADDR_H;
            ESP_LOGI(TAG, "BH1750 sensor detected at address 0x%02X", sensor_addr_);
            // 重新配置设备地址
            i2c_master_bus_rm_device(dev_handle_);
            dev_cfg.device_address = BH1750_ADDR_H;
            i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_);
        } else {
            ESP_LOGE(TAG, "BH1750 sensor not detected");
            i2c_master_bus_rm_device(dev_handle_);
            i2c_del_master_bus(bus_handle_);
            dev_handle_ = NULL;
            bus_handle_ = NULL;
            return;
        }

        // Initialize the sensor
        ret = I2CWrite(sensor_addr_, BH1750_POWER_ON);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to power on BH1750: %s", esp_err_to_name(ret));
            DeInit();
            return;
        }
        ret = I2CWrite(sensor_addr_, BH1750_RESET);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reset BH1750: %s", esp_err_to_name(ret));
            DeInit();
            return;
        }
        ret = I2CWrite(sensor_addr_, BH1750_CONT_H_RES);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set BH1750 mode: %s", esp_err_to_name(ret));
            DeInit();
            return;
        }
        vTaskDelay(180 / portTICK_PERIOD_MS);
        light_level_ = 0.0f;
        xTaskCreate(LightUpdateTask, "light_update_task", 2048, this, 5, &update_task_);
        initialized_ = true;
        ESP_LOGI(TAG, "Light sensor initialized successfully");
    }

    void DeInit() {
        if (!initialized_) return;
        ESP_LOGI(TAG, "De-initializing Light Sensor...");
        if (update_task_ != nullptr) {
            vTaskDelete(update_task_);
            update_task_ = nullptr;
        }
        I2CWrite(sensor_addr_, BH1750_POWER_DOWN);
        if (dev_handle_) {
            i2c_master_bus_rm_device(dev_handle_);
            dev_handle_ = NULL;
        }
        if (bus_handle_) {
            i2c_del_master_bus(bus_handle_);
            bus_handle_ = NULL;
        }
        initialized_ = false;
    }

private:
    i2c_port_t i2c_port_ = I2C_NUM_0;
    gpio_num_t sda_pin_ = GPIO_NUM_NC;
    gpio_num_t scl_pin_ = GPIO_NUM_NC;
    uint8_t sensor_addr_ = 0;
    float light_level_ = 0.0f; // Light level in lux
    TaskHandle_t update_task_ = nullptr;
    bool initialized_ = false;
    i2c_master_bus_handle_t bus_handle_ = NULL;
    i2c_master_dev_handle_t dev_handle_ = NULL;

    // Check if the sensor responds at the given address
    bool DetectSensor(uint8_t addr) {
        esp_err_t ret = I2CWrite(addr, BH1750_POWER_ON);
        if (ret != ESP_OK) {
            return false;
        }
        
        ret = I2CWrite(addr, BH1750_POWER_DOWN);
        return (ret == ESP_OK);
    }

    // I2C utility functions - simplified for BH1750 which uses single-byte commands
    esp_err_t I2CWrite(uint8_t dev_addr, uint8_t cmd) {
        uint8_t buf[1] = {cmd};
        return i2c_master_transmit(dev_handle_, buf, 1, I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    }

    esp_err_t I2CRead(uint8_t dev_addr, uint8_t* data, size_t len) {
        return i2c_master_receive(dev_handle_, data, len, I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    }
    
    // Read light level from BH1750 sensor
    bool ReadLightLevel() {
        uint8_t data[2];
        esp_err_t ret = I2CRead(sensor_addr_, data, 2);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read light level: %s", esp_err_to_name(ret));
            return false;
        }
        
        // Convert the data to lux
        uint16_t raw_value = (data[0] << 8) | data[1];
        light_level_ = raw_value / 1.2f;  // Convert raw value to lux (per datasheet)
        
        ESP_LOGD(TAG, "Light level: %.2f lux", light_level_);
        return true;
    }

    // Update task to periodically read light data
    static void LightUpdateTask(void* arg) {
        Light* light = static_cast<Light*>(arg);
        TickType_t last_update = xTaskGetTickCount();
        
        while (true) {
            light->ReadLightLevel();
            
            // Wait until next update interval
            vTaskDelayUntil(&last_update, LIGHT_UPDATE_INTERVAL_MS / portTICK_PERIOD_MS);
        }
    }
};

// Register the Light thing
DECLARE_THING(Light)

}  // namespace iot 