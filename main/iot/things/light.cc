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
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
                        sda_pin_ = params["sda_pin"].number();
                        scl_pin_ = params["scl_pin"].number();
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
        if (sda_pin_ < 0) sda_pin_ = CONFIG_I2C_SDA_PIN;
        if (scl_pin_ < 0) scl_pin_ = CONFIG_I2C_SCL_PIN;
        if (i2c_port_ < 0) i2c_port_ = CONFIG_I2C_PORT;

        if (sda_pin_ < 0 || scl_pin_ < 0) {
            ESP_LOGE(TAG, "I2C pins not configured for this board");
            return;
        }

        ESP_LOGI(TAG, "Using I2C - Port: %d, SDA: %d, SCL: %d", 
                i2c_port_, sda_pin_, scl_pin_);

        // Configure I2C
        i2c_config_t i2c_conf = {};
        i2c_conf.mode = I2C_MODE_MASTER;
        i2c_conf.sda_io_num = static_cast<gpio_num_t>(sda_pin_);
        i2c_conf.scl_io_num = static_cast<gpio_num_t>(scl_pin_);
        i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
        i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
        i2c_conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
        
        esp_err_t ret = i2c_param_config(i2c_port_, &i2c_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure I2C parameters: %s", esp_err_to_name(ret));
            return;
        }
        
        ret = i2c_driver_install(i2c_port_, I2C_MODE_MASTER, 0, 0, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to install I2C driver: %s", esp_err_to_name(ret));
            return;
        }
        
        // Try to detect the BH1750 sensor on both possible addresses
        if (DetectSensor(BH1750_ADDR_L)) {
            sensor_addr_ = BH1750_ADDR_L;
            ESP_LOGI(TAG, "BH1750 sensor detected at address 0x%02X", sensor_addr_);
        } else if (DetectSensor(BH1750_ADDR_H)) {
            sensor_addr_ = BH1750_ADDR_H;
            ESP_LOGI(TAG, "BH1750 sensor detected at address 0x%02X", sensor_addr_);
        } else {
            ESP_LOGE(TAG, "BH1750 sensor not detected");
            i2c_driver_delete(i2c_port_);
            return;
        }
        
        // Initialize the sensor
        ret = I2CWrite(sensor_addr_, BH1750_POWER_ON);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to power on BH1750: %s", esp_err_to_name(ret));
            i2c_driver_delete(i2c_port_);
            return;
        }
        
        ret = I2CWrite(sensor_addr_, BH1750_RESET);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reset BH1750: %s", esp_err_to_name(ret));
            i2c_driver_delete(i2c_port_);
            return;
        }
        
        // Set continuous high resolution mode (1 lux precision)
        ret = I2CWrite(sensor_addr_, BH1750_CONT_H_RES);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set BH1750 mode: %s", esp_err_to_name(ret));
            i2c_driver_delete(i2c_port_);
            return;
        }
        
        // Wait for sensor to be ready after mode change
        vTaskDelay(180 / portTICK_PERIOD_MS);
        
        // Initialize light value
        light_level_ = 0.0f;
        
        // Start task to update light data periodically
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
        
        // Power down the sensor
        I2CWrite(sensor_addr_, BH1750_POWER_DOWN);
        
        i2c_driver_delete(i2c_port_);
        initialized_ = false;
    }

private:
    i2c_port_t i2c_port_ = I2C_NUM_0;
    int sda_pin_ = -1;
    int scl_pin_ = -1;
    uint8_t sensor_addr_ = 0;
    float light_level_ = 0.0f; // Light level in lux
    TaskHandle_t update_task_ = nullptr;
    bool initialized_ = false;

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
        i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
        i2c_master_start(cmd_handle);
        i2c_master_write_byte(cmd_handle, (dev_addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd_handle, cmd, true);
        i2c_master_stop(cmd_handle);
        esp_err_t ret = i2c_master_cmd_begin(i2c_port_, cmd_handle, I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd_handle);
        return ret;
    }

    esp_err_t I2CRead(uint8_t dev_addr, uint8_t* data, size_t len) {
        i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
        i2c_master_start(cmd_handle);
        i2c_master_write_byte(cmd_handle, (dev_addr << 1) | I2C_MASTER_READ, true);
        if (len > 1) {
            i2c_master_read(cmd_handle, data, len - 1, I2C_MASTER_ACK);
        }
        i2c_master_read_byte(cmd_handle, data + len - 1, I2C_MASTER_NACK);
        i2c_master_stop(cmd_handle);
        esp_err_t ret = i2c_master_cmd_begin(i2c_port_, cmd_handle, I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd_handle);
        return ret;
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