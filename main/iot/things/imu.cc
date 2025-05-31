#include <memory>
#include <cstring>
#include <cmath>

#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "board_config.h"
#include "../thing.h"

static constexpr char TAG[] = "IMU";

// GY-87 module address definitions
#define MPU6050_ADDR 0x68  // MPU6050 address, 0x68 if AD0 is GND, 0x69 if VCC
#define QMC5883L_ADDR 0x0D // QMC5883L magnetometer address
#define BMP180_ADDR 0x77   // BMP180 barometer address

// MPU6050 register addresses
#define MPU6050_PWR_MGMT_1 0x6B
#define MPU6050_CONFIG 0x1A
#define MPU6050_GYRO_CONFIG 0x1B
#define MPU6050_ACCEL_CONFIG 0x1C
#define MPU6050_INT_ENABLE 0x38
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_GYRO_XOUT_H 0x43
#define MPU6050_WHO_AM_I 0x75

// QMC5883L register addresses
#define QMC5883L_REG_CONFIG_1 0x09
#define QMC5883L_REG_CONFIG_2 0x0A
#define QMC5883L_REG_PERIOD   0x0B
#define QMC5883L_REG_DATA_X_LSB 0x00
#define QMC5883L_REG_STATUS   0x06
#define QMC5883L_REG_RESET    0x0B

// QMC5883L configuration values
#define QMC5883L_MODE_STANDBY 0x00
#define QMC5883L_MODE_CONTINUOUS 0x01
#define QMC5883L_ODR_10HZ 0x00  // 10Hz output rate
#define QMC5883L_ODR_50HZ 0x04  // 50Hz output rate
#define QMC5883L_ODR_100HZ 0x08 // 100Hz output rate
#define QMC5883L_ODR_200HZ 0x0C // 200Hz output rate
#define QMC5883L_RNG_2G 0x00    // ±2 gauss range
#define QMC5883L_RNG_8G 0x10    // ±8 gauss range
#define QMC5883L_OSR_512 0x00   // Oversampling ratio 512
#define QMC5883L_OSR_256 0x40   // Oversampling ratio 256
#define QMC5883L_OSR_128 0x80   // Oversampling ratio 128
#define QMC5883L_OSR_64 0xC0    // Oversampling ratio 64

// I2C configuration
#define I2C_MASTER_FREQ_HZ 100000     // 100KHz
#define I2C_TIMEOUT_MS 1000           // 1 second

// Data update intervals
#define IMU_UPDATE_INTERVAL_MS 100    // Update IMU data every 100ms
#define MAG_UPDATE_INTERVAL_MS 500    // Update magnetometer data every 500ms
#define BARO_UPDATE_INTERVAL_MS 1000  // Update barometer data every 1000ms

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

// Forward declare our sensor data structure
struct SensorData {
    // Accelerometer data in g (9.8 m/s^2)
    float accel_x;
    float accel_y;
    float accel_z;
    
    // Gyroscope data in degrees per second
    float gyro_x;
    float gyro_y;
    float gyro_z;
    
    // Magnetometer data in microtesla
    float mag_x;
    float mag_y;
    float mag_z;
    
    // Temperature in degrees Celsius
    float temperature;
    
    // Barometric pressure in hPa (hectopascal)
    float pressure;
    
    // Altitude in meters (derived from pressure)
    float altitude;
};

class IMU : public Thing {
public:
    IMU() : Thing("imu", "Inertial Measurement Unit sensor") {
        // Setup properties for the sensor readings
        properties_.AddNumberProperty("accel_x", "Acceleration X-axis (g)", 
            [this]() { return static_cast<int>(sensor_data_.accel_x * 1000); });
        properties_.AddNumberProperty("accel_y", "Acceleration Y-axis (g)", 
            [this]() { return static_cast<int>(sensor_data_.accel_y * 1000); });
        properties_.AddNumberProperty("accel_z", "Acceleration Z-axis (g)", 
            [this]() { return static_cast<int>(sensor_data_.accel_z * 1000); });
            
        properties_.AddNumberProperty("gyro_x", "Gyroscope X-axis (deg/s)", 
            [this]() { return static_cast<int>(sensor_data_.gyro_x * 10); });
        properties_.AddNumberProperty("gyro_y", "Gyroscope Y-axis (deg/s)", 
            [this]() { return static_cast<int>(sensor_data_.gyro_y * 10); });
        properties_.AddNumberProperty("gyro_z", "Gyroscope Z-axis (deg/s)", 
            [this]() { return static_cast<int>(sensor_data_.gyro_z * 10); });
            
        properties_.AddNumberProperty("mag_x", "Magnetic field X-axis (μT)", 
            [this]() { return static_cast<int>(sensor_data_.mag_x * 10); });
        properties_.AddNumberProperty("mag_y", "Magnetic field Y-axis (μT)", 
            [this]() { return static_cast<int>(sensor_data_.mag_y * 10); });
        properties_.AddNumberProperty("mag_z", "Magnetic field Z-axis (μT)", 
            [this]() { return static_cast<int>(sensor_data_.mag_z * 10); });
            
        properties_.AddNumberProperty("temperature", "Temperature (°C)", 
            [this]() { return static_cast<int>(sensor_data_.temperature * 10); });
        properties_.AddNumberProperty("pressure", "Barometric pressure (hPa)", 
            [this]() { return static_cast<int>(sensor_data_.pressure * 10); });
        properties_.AddNumberProperty("altitude", "Altitude (m)", 
            [this]() { return static_cast<int>(sensor_data_.altitude * 10); });
            
        // Add a configuration method
        ParameterList configParams;
        configParams.AddParameter(Parameter("i2c_port", "I2C port number", kValueTypeNumber, false));
        configParams.AddParameter(Parameter("sda_pin", "I2C SDA pin", kValueTypeNumber, false));
        configParams.AddParameter(Parameter("scl_pin", "I2C SCL pin", kValueTypeNumber, false));
        
        methods_.AddMethod("configure", "Configure the IMU sensor", configParams,
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

    ~IMU() {
        DeInit();
    }

    void Init() {
        ESP_LOGI(TAG, "Initializing IMU...");

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
        
        // 新I2C驱动：配置设备（默认用MPU6050地址，后续可动态切换）
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = MPU6050_ADDR,
            .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        };
        ret = i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
            i2c_del_master_bus(bus_handle_);
            bus_handle_ = NULL;
            return;
        }
        
        // Initialize sensors
        mpu6050_initialized_ = InitMPU6050();
        if (mpu6050_initialized_) {
            // Enable access to the QMC5883L through the MPU6050 bypass
            EnableHMC5883LAccess();
            // 切换设备地址为QMC5883L
            i2c_master_bus_rm_device(dev_handle_);
            dev_cfg.device_address = QMC5883L_ADDR;
            i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_);
            qmc5883l_initialized_ = InitQMC5883L();
            // 切换设备地址为BMP180
            i2c_master_bus_rm_device(dev_handle_);
            dev_cfg.device_address = BMP180_ADDR;
            i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_);
            bmp180_initialized_ = InitBMP180();
            // 恢复为MPU6050
            i2c_master_bus_rm_device(dev_handle_);
            dev_cfg.device_address = MPU6050_ADDR;
            i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_);
        }
        memset(&sensor_data_, 0, sizeof(sensor_data_));
        if (mpu6050_initialized_ || qmc5883l_initialized_ || bmp180_initialized_) {
            xTaskCreate(IMUUpdateTask, "imu_update_task", 4096, this, 5, &update_task_);
            initialized_ = true;
            ESP_LOGI(TAG, "IMU initialized successfully");
            return;
        }
        ESP_LOGE(TAG, "Failed to initialize IMU sensors");
    }

    void DeInit() {
        if (!initialized_) return;
        ESP_LOGI(TAG, "De-initializing IMU...");
        if (update_task_ != nullptr) {
            vTaskDelete(update_task_);
            update_task_ = nullptr;
        }
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
    TaskHandle_t update_task_ = nullptr;
    bool initialized_ = false;
    i2c_master_bus_handle_t bus_handle_ = NULL;
    i2c_master_dev_handle_t dev_handle_ = NULL;
    
    bool mpu6050_initialized_ = false;
    bool qmc5883l_initialized_ = false;
    bool bmp180_initialized_ = false;
    
    // BMP180 calibration coefficients
    int16_t ac1_ = 0, ac2_ = 0, ac3_ = 0;
    uint16_t ac4_ = 0, ac5_ = 0, ac6_ = 0;
    int16_t b1_ = 0, b2_ = 0;
    int16_t mb_ = 0, mc_ = 0, md_ = 0;
    uint8_t oss_ = 0; // Oversampling setting (0-3), 0 is fastest but lowest precision
    
    SensorData sensor_data_ = {};
    
    // MPU6050 initialization
    bool InitMPU6050() {
        uint8_t who_am_i = 0;
        esp_err_t ret = I2CRead(MPU6050_ADDR, MPU6050_WHO_AM_I, &who_am_i, 1);
        
        if (ret != ESP_OK || who_am_i != 0x68) {
            ESP_LOGE(TAG, "MPU6050 identification failed: 0x%02x, error: %s", who_am_i, esp_err_to_name(ret));
            return false;
        }
        
        ESP_LOGI(TAG, "MPU6050 device ID verified: 0x%02x", who_am_i);

        // Reset MPU6050
        ret = I2CWrite(MPU6050_ADDR, MPU6050_PWR_MGMT_1, 0x80);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reset MPU6050: %s", esp_err_to_name(ret));
            return false;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS); // Wait for reset to complete
        
        // Wake up MPU6050
        ret = I2CWrite(MPU6050_ADDR, MPU6050_PWR_MGMT_1, 0x00);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to wake up MPU6050: %s", esp_err_to_name(ret));
            return false;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        
        // Configure gyroscope - Set full scale range to ±250°/s
        ret = I2CWrite(MPU6050_ADDR, MPU6050_GYRO_CONFIG, 0x00);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure gyroscope: %s", esp_err_to_name(ret));
            return false;
        }
        
        // Configure accelerometer - Set full scale range to ±2g
        ret = I2CWrite(MPU6050_ADDR, MPU6050_ACCEL_CONFIG, 0x00);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure accelerometer: %s", esp_err_to_name(ret));
            return false;
        }
        
        // Configure digital low pass filter
        ret = I2CWrite(MPU6050_ADDR, MPU6050_CONFIG, 0x03);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure DLPF: %s", esp_err_to_name(ret));
            return false;
        }
        
        // Set sample rate
        ret = I2CWrite(MPU6050_ADDR, 0x19, 0x07);  // 1000/(1+7) = 125Hz
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set sample rate: %s", esp_err_to_name(ret));
            return false;
        }

        ESP_LOGI(TAG, "MPU6050 initialized successfully");
        return true;
    }
    
    // Enable HMC5883L bypass
    void EnableHMC5883LAccess() {
        // Disable I2C master mode
        I2CWrite(MPU6050_ADDR, 0x6A, 0x00);
        vTaskDelay(10 / portTICK_PERIOD_MS);
        
        // Enable I2C bypass mode
        I2CWrite(MPU6050_ADDR, 0x37, 0x02);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    // QMC5883L initialization
    bool InitQMC5883L() {
        esp_err_t ret;
        
        // Check if QMC5883L responds
        uint8_t status = 0;
        ret = I2CRead(QMC5883L_ADDR, QMC5883L_REG_STATUS, &status, 1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "QMC5883L communication failed: %s", esp_err_to_name(ret));
            return false;
        }
        
        // Reset QMC5883L
        ret = I2CWrite(QMC5883L_ADDR, QMC5883L_REG_RESET, 0x01);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "QMC5883L reset failed: %s", esp_err_to_name(ret));
            return false;
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
        
        // Configure QMC5883L control register 1
        // Mode: Continuous
        // ODR: 50Hz
        // Range: 8 gauss
        // OSR: 512
        uint8_t config = QMC5883L_MODE_CONTINUOUS | 
                         QMC5883L_ODR_50HZ | 
                         QMC5883L_RNG_8G | 
                         QMC5883L_OSR_512;
        
        ret = I2CWrite(QMC5883L_ADDR, QMC5883L_REG_CONFIG_1, config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "QMC5883L configuration failed: %s", esp_err_to_name(ret));
            return false;
        }
        
        // Configure control register 2 (interrupts, soft reset, etc.)
        ret = I2CWrite(QMC5883L_ADDR, QMC5883L_REG_CONFIG_2, 0x00);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "QMC5883L configuration 2 failed: %s", esp_err_to_name(ret));
            return false;
        }
        
        // Set period register
        ret = I2CWrite(QMC5883L_ADDR, QMC5883L_REG_PERIOD, 0x01);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "QMC5883L period configuration failed: %s", esp_err_to_name(ret));
            return false;
        }

        // Read status register to verify chip is working
        ret = I2CRead(QMC5883L_ADDR, QMC5883L_REG_STATUS, &status, 1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "QMC5883L status read failed: %s", esp_err_to_name(ret));
            return false;
        }
        
        ESP_LOGI(TAG, "QMC5883L status register: 0x%02x", status);
        ESP_LOGI(TAG, "QMC5883L magnetometer initialized successfully");
        return true;
    }
    
    // BMP180 initialization
    bool InitBMP180() {
        uint8_t chip_id = 0;
        esp_err_t ret = I2CRead(BMP180_ADDR, 0xD0, &chip_id, 1);
        
        if (ret != ESP_OK || chip_id != 0x55) {
            ESP_LOGE(TAG, "BMP180 identification failed: 0x%02x, error: %s", chip_id, esp_err_to_name(ret));
            return false;
        }
        
        ESP_LOGI(TAG, "BMP180 chip ID verified: 0x%02x", chip_id);

        // Read calibration data
        uint8_t cal_data[22];
        ret = I2CRead(BMP180_ADDR, 0xAA, cal_data, 22);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read BMP180 calibration data: %s", esp_err_to_name(ret));
            return false;
        }
        
        // Parse calibration data
        ac1_ = (int16_t)((cal_data[0] << 8) | cal_data[1]);
        ac2_ = (int16_t)((cal_data[2] << 8) | cal_data[3]);
        ac3_ = (int16_t)((cal_data[4] << 8) | cal_data[5]);
        ac4_ = (uint16_t)((cal_data[6] << 8) | cal_data[7]);
        ac5_ = (uint16_t)((cal_data[8] << 8) | cal_data[9]);
        ac6_ = (uint16_t)((cal_data[10] << 8) | cal_data[11]);
        b1_ = (int16_t)((cal_data[12] << 8) | cal_data[13]);
        b2_ = (int16_t)((cal_data[14] << 8) | cal_data[15]);
        mb_ = (int16_t)((cal_data[16] << 8) | cal_data[17]);
        mc_ = (int16_t)((cal_data[18] << 8) | cal_data[19]);
        md_ = (int16_t)((cal_data[20] << 8) | cal_data[21]);
        
        ESP_LOGI(TAG, "BMP180 calibration data loaded");
        ESP_LOGI(TAG, "BMP180 barometer initialized successfully");
        return true;
    }

    // I2C utility functions
    esp_err_t I2CWrite(uint8_t dev_addr, uint8_t reg_addr, uint8_t data) {
        // 切换设备地址
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = dev_addr,
            .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        };
        i2c_master_bus_rm_device(dev_handle_);
        i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_);
        uint8_t buf[2] = {reg_addr, data};
        return i2c_master_transmit(dev_handle_, buf, 2, I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    }
    
    esp_err_t I2CRead(uint8_t dev_addr, uint8_t reg_addr, uint8_t* data, size_t len) {
        // 切换设备地址
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = dev_addr,
            .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        };
        i2c_master_bus_rm_device(dev_handle_);
        i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_);
        // 先写寄存器地址，再读数据
        esp_err_t ret = i2c_master_transmit(dev_handle_, &reg_addr, 1, I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
        if (ret != ESP_OK) return ret;
        return i2c_master_receive(dev_handle_, data, len, I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    }
    
    // Read sensor data
    void ReadMPU6050Data() {
        if (!mpu6050_initialized_) {
            return;
        }
        
        uint8_t data[6];
        esp_err_t ret = I2CRead(MPU6050_ADDR, MPU6050_ACCEL_XOUT_H, data, 6);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read accelerometer data: %s", esp_err_to_name(ret));
            return;
        }
        
        int16_t accelX = (data[0] << 8) | data[1];
        int16_t accelY = (data[2] << 8) | data[3];
        int16_t accelZ = (data[4] << 8) | data[5];
        
        // Convert raw values to g (±2g range)
        sensor_data_.accel_x = accelX / 16384.0f;
        sensor_data_.accel_y = accelY / 16384.0f;
        sensor_data_.accel_z = accelZ / 16384.0f;
    }
    
    void ReadQMC5883LData() {
        if (!qmc5883l_initialized_) {
            return;
        }
        
        uint8_t data[6];
        esp_err_t ret = I2CRead(QMC5883L_ADDR, QMC5883L_REG_DATA_X_LSB, data, 6);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read magnetometer data: %s", esp_err_to_name(ret));
            return;
        }
        
        // QMC5883L uses little-endian format
        int16_t magX = (data[1] << 8) | data[0];
        int16_t magY = (data[3] << 8) | data[2];
        int16_t magZ = (data[5] << 8) | data[4];
        
        // Convert raw values to microtesla
        sensor_data_.mag_x = magX * 0.002f;
        sensor_data_.mag_y = magY * 0.002f;
        sensor_data_.mag_z = magZ * 0.002f;
    }
    
    void ReadBMP180Data() {
        if (!bmp180_initialized_) {
            return;
        }
        
        // Read uncompensated temperature
        I2CWrite(BMP180_ADDR, 0xF4, 0x2E);
        vTaskDelay(5 / portTICK_PERIOD_MS);
        
        uint8_t data[2];
        esp_err_t ret = I2CRead(BMP180_ADDR, 0xF6, data, 2);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read BMP180 temperature: %s", esp_err_to_name(ret));
            return;
        }
        
        uint16_t UT = (data[0] << 8) | data[1];
        
        // Read uncompensated pressure
        I2CWrite(BMP180_ADDR, 0xF4, 0x34 + (oss_ << 6));
        vTaskDelay((oss_ == 0) ? 5 : (oss_ == 1) ? 8 : (oss_ == 2) ? 14 : 26 / portTICK_PERIOD_MS);
        
        uint8_t p_data[3];
        ret = I2CRead(BMP180_ADDR, 0xF6, p_data, 3);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read BMP180 pressure: %s", esp_err_to_name(ret));
            return;
        }
        
        uint32_t UP = ((uint32_t)p_data[0] << 16 | (uint32_t)p_data[1] << 8 | p_data[2]) >> (8 - oss_);
        
        // Calculate true temperature
        int32_t X1 = ((UT - ac6_) * ac5_) >> 15;
        int32_t X2 = ((int32_t)mc_ << 11) / (X1 + md_);
        int32_t B5 = X1 + X2;
        float T = (B5 + 8) >> 4;
        T = T / 10.0f; // Convert to Celsius
        
        // Calculate pressure
        int32_t B6 = B5 - 4000;
        X1 = (b2_ * ((B6 * B6) >> 12)) >> 11;
        X2 = (ac2_ * B6) >> 11;
        int32_t X3 = X1 + X2;
        int32_t B3 = (((ac1_ * 4 + X3) << oss_) + 2) >> 2;
        X1 = (ac3_ * B6) >> 13;
        X2 = (b1_ * ((B6 * B6) >> 12)) >> 16;
        X3 = ((X1 + X2) + 2) >> 2;
        uint32_t B4 = (ac4_ * (uint32_t)(X3 + 32768)) >> 15;
        uint32_t B7 = ((uint32_t)UP - B3) * (50000 >> oss_);
        
        int32_t p;
        if (B7 < 0x80000000) {
            p = (B7 * 2) / B4;
        } else {
            p = (B7 / B4) * 2;
        }
        
        X1 = (p >> 8) * (p >> 8);
        X1 = (X1 * 3038) >> 16;
        X2 = (-7357 * p) >> 16;
        p = p + ((X1 + X2 + 3791) >> 4);
        
        float pressure = p / 100.0f; // Convert to hPa
        
        // Calculate altitude (using international height formula)
        float altitude = 44330.0f * (1.0f - pow(pressure / 1013.25f, 0.1903f));
        
        // Update sensor data
        sensor_data_.temperature = T;
        sensor_data_.pressure = pressure;
        sensor_data_.altitude = altitude;
    }
    
    // Periodic update task
    static void IMUUpdateTask(void* arg) {
        IMU* imu = static_cast<IMU*>(arg);
        TickType_t last_imu_update = xTaskGetTickCount();
        TickType_t last_mag_update = xTaskGetTickCount();
        TickType_t last_baro_update = xTaskGetTickCount();
        
        while (true) {
            // Update MPU6050 data (accelerometer and gyroscope)
            if ((xTaskGetTickCount() - last_imu_update) * portTICK_PERIOD_MS >= IMU_UPDATE_INTERVAL_MS) {
                if (imu->mpu6050_initialized_) {
                    imu->ReadMPU6050Data();
                }
                last_imu_update = xTaskGetTickCount();
            }
            
            // Update QMC5883L data (magnetometer)
            if ((xTaskGetTickCount() - last_mag_update) * portTICK_PERIOD_MS >= MAG_UPDATE_INTERVAL_MS) {
                if (imu->qmc5883l_initialized_) {
                    imu->ReadQMC5883LData();
                }
                last_mag_update = xTaskGetTickCount();
            }
            
            // Update BMP180 data (barometer)
            if ((xTaskGetTickCount() - last_baro_update) * portTICK_PERIOD_MS >= BARO_UPDATE_INTERVAL_MS) {
                if (imu->bmp180_initialized_) {
                    imu->ReadBMP180Data();
                }
                last_baro_update = xTaskGetTickCount();
            }
            
            // Short delay to prevent hogging CPU
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }
};

// Register the IMU thing
DECLARE_THING(IMU)

}  // namespace iot 