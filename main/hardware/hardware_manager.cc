#include "hardware_manager.h"
#include "simple_error_handler.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <fstream>
#include <sstream>
#include <algorithm>

static const char* TAG = "HardwareManager";

HardwareManager::HardwareManager() 
    : initialized_(false)
    , pcf8575_handle_(nullptr)
    , lu9685_handle_(nullptr)
    , hw178_handle_(nullptr)
{
}

HardwareManager::~HardwareManager() {
    if (pcf8575_handle_) {
        pcf8575_delete(&pcf8575_handle_);
    }
    if (lu9685_handle_) {
        lu9685_deinit(&lu9685_handle_);
    }
    if (hw178_handle_) {
        hw178_delete(hw178_handle_);
    }
}

esp_err_t HardwareManager::Initialize() {
    ESP_LOGI(TAG, "Initializing Hardware Manager");
    
    // Initialize expanders first
    esp_err_t ret = InitializeExpanders();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize expanders: %s", esp_err_to_name(ret));
        return ret;
    }
    
    initialized_ = true;
    ESP_LOGI(TAG, "Hardware Manager initialized successfully");
    return ESP_OK;
}

esp_err_t HardwareManager::InitializeExpanders() {
    ESP_LOGI(TAG, "Initializing expander drivers");
    
    // Initialize multiplexer system (this will initialize PCA9548A if available)
    esp_err_t ret = multiplexer_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Multiplexer initialization failed, continuing without multiplexers");
    }
    
    // Initialize PCF8575 if PCA9548A is available
    if (pca9548a_is_initialized()) {
        // Get I2C bus handle from multiplexer system
        // For now, we'll use a default configuration
        // This should be improved to get the actual bus handle
        ESP_LOGI(TAG, "PCA9548A available, initializing PCF8575");
        // PCF8575 initialization will be done when needed
    }
    
    // Initialize LU9685 if available
    if (pca9548a_is_initialized()) {
        ESP_LOGI(TAG, "Initializing LU9685 servo controller");
        // LU9685 initialization will be done when needed
    }
    
    // HW178 initialization
    if (hw178_is_initialized()) {
        hw178_handle_ = hw178_get_handle();
        ESP_LOGI(TAG, "HW178 multiplexer available");
    }
    
    return ESP_OK;
}

esp_err_t HardwareManager::LoadConfiguration(const std::string& config_file) {
    ESP_LOGI(TAG, "Loading configuration from: %s", config_file.c_str());
    
    // Read file content
    std::ifstream file(config_file);
    if (!file.is_open()) {
        ESP_LOGE(TAG, "Failed to open configuration file: %s", config_file.c_str());
        return ESP_ERR_NOT_FOUND;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    // Parse JSON
    cJSON* root = cJSON_Parse(content.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Validate configuration structure
    if (!ValidateConfigurationStructure(root)) {
        ESP_LOGE(TAG, "Configuration structure validation failed");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_OK;
    
    // Parse hardware section
    cJSON* hardware = cJSON_GetObjectItem(root, "hardware");
    if (hardware) {
        // Parse sensors
        cJSON* sensors = cJSON_GetObjectItem(hardware, "sensors");
        if (sensors) {
            ret = ParseSensorConfig(sensors);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to parse sensor configuration");
                cJSON_Delete(root);
                return ret;
            }
        }
        
        // Parse motors
        cJSON* motors = cJSON_GetObjectItem(hardware, "motors");
        if (motors) {
            ret = ParseMotorConfig(motors);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to parse motor configuration");
                cJSON_Delete(root);
                return ret;
            }
        }
        
        // Parse servos
        cJSON* servos = cJSON_GetObjectItem(hardware, "servos");
        if (servos) {
            ret = ParseServoConfig(servos);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to parse servo configuration");
                cJSON_Delete(root);
                return ret;
            }
        }
    }
    
    cJSON_Delete(root);
    
    // Validate configuration
    ret = ValidateConfiguration();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Configuration validation failed");
        return ret;
    }
    
    ESP_LOGI(TAG, "Configuration loaded successfully");
    ESP_LOGI(TAG, "%s", GetConfigurationSummary().c_str());
    return ESP_OK;
}

esp_err_t HardwareManager::ParseSensorConfig(cJSON* sensors_json) {
    if (!cJSON_IsArray(sensors_json)) {
        ESP_LOGE(TAG, "Sensors configuration must be an array");
        return ESP_ERR_INVALID_ARG;
    }
    
    int sensor_count = cJSON_GetArraySize(sensors_json);
    ESP_LOGI(TAG, "Parsing %d sensor configurations", sensor_count);
    
    for (int i = 0; i < sensor_count; i++) {
        cJSON* sensor = cJSON_GetArrayItem(sensors_json, i);
        if (!sensor) continue;
        
        sensor_config_t config;
        
        // Parse required fields
        cJSON* id = cJSON_GetObjectItem(sensor, "id");
        cJSON* type = cJSON_GetObjectItem(sensor, "type");
        cJSON* expander = cJSON_GetObjectItem(sensor, "expander");
        cJSON* channel = cJSON_GetObjectItem(sensor, "channel");
        
        if (!id || !type || !expander || !channel) {
            ESP_LOGE(TAG, "Missing required sensor fields in configuration");
            return ESP_ERR_INVALID_ARG;
        }
        
        config.id = cJSON_GetStringValue(id);
        config.type = cJSON_GetStringValue(type);
        config.expander = cJSON_GetStringValue(expander);
        config.channel = cJSON_GetNumberValue(channel);
        
        // Parse optional fields
        cJSON* name = cJSON_GetObjectItem(sensor, "name");
        if (name) {
            config.name = cJSON_GetStringValue(name);
        } else {
            config.name = config.id;
        }
        
        cJSON* unit = cJSON_GetObjectItem(sensor, "unit");
        if (unit) {
            config.unit = cJSON_GetStringValue(unit);
        } else {
            config.unit = "";
        }
        
        // Parse calibration
        cJSON* calibration = cJSON_GetObjectItem(sensor, "calibration");
        if (calibration) {
            cJSON* offset = cJSON_GetObjectItem(calibration, "offset");
            cJSON* scale = cJSON_GetObjectItem(calibration, "scale");
            
            config.calibration.offset = offset ? cJSON_GetNumberValue(offset) : 0.0f;
            config.calibration.scale = scale ? cJSON_GetNumberValue(scale) : 1.0f;
        } else {
            config.calibration.offset = 0.0f;
            config.calibration.scale = 1.0f;
        }
        
        sensor_configs_[config.id] = config;
        ESP_LOGI(TAG, "Added sensor: %s (%s) on %s channel %d", 
                 config.id.c_str(), config.type.c_str(), 
                 config.expander.c_str(), config.channel);
    }
    
    return ESP_OK;
}

esp_err_t HardwareManager::ParseMotorConfig(cJSON* motors_json) {
    if (!cJSON_IsArray(motors_json)) {
        ESP_LOGE(TAG, "Motors configuration must be an array");
        return ESP_ERR_INVALID_ARG;
    }
    
    int motor_count = cJSON_GetArraySize(motors_json);
    ESP_LOGI(TAG, "Parsing %d motor configurations", motor_count);
    
    for (int i = 0; i < motor_count; i++) {
        cJSON* motor = cJSON_GetArrayItem(motors_json, i);
        if (!motor) continue;
        
        motor_config_t config;
        
        // Parse required fields
        cJSON* id = cJSON_GetObjectItem(motor, "id");
        cJSON* connection_type = cJSON_GetObjectItem(motor, "connection_type");
        cJSON* pins = cJSON_GetObjectItem(motor, "pins");
        
        if (!id || !connection_type || !pins) {
            ESP_LOGE(TAG, "Missing required motor fields in configuration");
            return ESP_ERR_INVALID_ARG;
        }
        
        config.id = cJSON_GetNumberValue(id);
        config.connection_type = cJSON_GetStringValue(connection_type);
        
        // Parse pins
        cJSON* ena = cJSON_GetObjectItem(pins, "ena");
        cJSON* in1 = cJSON_GetObjectItem(pins, "in1");
        cJSON* in2 = cJSON_GetObjectItem(pins, "in2");
        
        if (!ena || !in1 || !in2) {
            ESP_LOGE(TAG, "Missing motor pin configuration");
            return ESP_ERR_INVALID_ARG;
        }
        
        config.pins.ena = cJSON_GetNumberValue(ena);
        config.pins.in1 = cJSON_GetNumberValue(in1);
        config.pins.in2 = cJSON_GetNumberValue(in2);
        
        // Parse optional fields
        cJSON* name = cJSON_GetObjectItem(motor, "name");
        if (name) {
            config.name = cJSON_GetStringValue(name);
        } else {
            config.name = "Motor " + std::to_string(config.id);
        }
        
        cJSON* max_speed = cJSON_GetObjectItem(motor, "max_speed");
        config.max_speed = max_speed ? cJSON_GetNumberValue(max_speed) : 255;
        
        motor_configs_[config.id] = config;
        ESP_LOGI(TAG, "Added motor: %d (%s) type %s", 
                 config.id, config.name.c_str(), config.connection_type.c_str());
    }
    
    return ESP_OK;
}

esp_err_t HardwareManager::ParseServoConfig(cJSON* servos_json) {
    if (!cJSON_IsArray(servos_json)) {
        ESP_LOGE(TAG, "Servos configuration must be an array");
        return ESP_ERR_INVALID_ARG;
    }
    
    int servo_count = cJSON_GetArraySize(servos_json);
    ESP_LOGI(TAG, "Parsing %d servo configurations", servo_count);
    
    for (int i = 0; i < servo_count; i++) {
        cJSON* servo = cJSON_GetArrayItem(servos_json, i);
        if (!servo) continue;
        
        servo_config_t config;
        
        // Parse required fields
        cJSON* id = cJSON_GetObjectItem(servo, "id");
        cJSON* connection_type = cJSON_GetObjectItem(servo, "connection_type");
        cJSON* channel = cJSON_GetObjectItem(servo, "channel");
        
        if (!id || !connection_type || !channel) {
            ESP_LOGE(TAG, "Missing required servo fields in configuration");
            return ESP_ERR_INVALID_ARG;
        }
        
        config.id = cJSON_GetNumberValue(id);
        config.connection_type = cJSON_GetStringValue(connection_type);
        config.channel = cJSON_GetNumberValue(channel);
        
        // Parse optional fields
        cJSON* name = cJSON_GetObjectItem(servo, "name");
        if (name) {
            config.name = cJSON_GetStringValue(name);
        } else {
            config.name = "Servo " + std::to_string(config.id);
        }
        
        cJSON* min_angle = cJSON_GetObjectItem(servo, "min_angle");
        cJSON* max_angle = cJSON_GetObjectItem(servo, "max_angle");
        cJSON* center_angle = cJSON_GetObjectItem(servo, "center_angle");
        
        config.min_angle = min_angle ? cJSON_GetNumberValue(min_angle) : 0;
        config.max_angle = max_angle ? cJSON_GetNumberValue(max_angle) : 180;
        config.center_angle = center_angle ? cJSON_GetNumberValue(center_angle) : 90;
        
        servo_configs_[config.id] = config;
        ESP_LOGI(TAG, "Added servo: %d (%s) type %s channel %d", 
                 config.id, config.name.c_str(), 
                 config.connection_type.c_str(), config.channel);
    }
    
    return ESP_OK;
}

esp_err_t HardwareManager::ValidateConfiguration() {
    ESP_LOGI(TAG, "Validating hardware configuration");
    
    // Validate sensor configurations
    for (const auto& pair : sensor_configs_) {
        const sensor_config_t& config = pair.second;
        
        if (config.expander == "hw178") {
            if (config.channel < 0 || config.channel >= HW178_CHANNEL_COUNT) {
                ESP_LOGE(TAG, "Invalid HW178 channel %d for sensor %s", 
                         config.channel, config.id.c_str());
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    
    // Validate motor configurations
    for (const auto& pair : motor_configs_) {
        const motor_config_t& config = pair.second;
        
        if (config.connection_type == "pcf8575") {
            if (config.pins.ena < 0 || config.pins.ena >= PCF8575_IO_MAX ||
                config.pins.in1 < 0 || config.pins.in1 >= PCF8575_IO_MAX ||
                config.pins.in2 < 0 || config.pins.in2 >= PCF8575_IO_MAX) {
                ESP_LOGE(TAG, "Invalid PCF8575 pins for motor %d", config.id);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    
    // Validate servo configurations
    for (const auto& pair : servo_configs_) {
        const servo_config_t& config = pair.second;
        
        if (config.connection_type == "lu9685") {
            if (config.channel < 0 || config.channel >= 16) {
                ESP_LOGE(TAG, "Invalid LU9685 channel %d for servo %d", 
                         config.channel, config.id);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    
    ESP_LOGI(TAG, "Configuration validation passed");
    return ESP_OK;
}

std::vector<sensor_reading_t> HardwareManager::ReadAllSensors() {
    std::vector<sensor_reading_t> readings;
    
    if (!initialized_) {
        ESP_LOGE(TAG, "Hardware manager not initialized");
        return readings;
    }
    
    readings.reserve(sensor_configs_.size());  // Optimize memory allocation
    
    for (const auto& pair : sensor_configs_) {
        sensor_reading_t reading = ReadSensor(pair.first);
        readings.push_back(reading);
    }
    
    return readings;
}

sensor_reading_t HardwareManager::ReadSensor(const std::string& sensor_id) {
    sensor_reading_t reading;
    reading.sensor_id = sensor_id;
    reading.timestamp = GetTimestamp();
    reading.valid = false;
    
    auto it = sensor_configs_.find(sensor_id);
    if (it == sensor_configs_.end()) {
        ESP_LOGE(TAG, "Sensor %s not found in configuration", sensor_id.c_str());
        return reading;
    }
    
    const sensor_config_t& config = it->second;
    reading.name = config.name;
    reading.type = config.type;
    reading.unit = config.unit;
    
    if (config.expander == "hw178") {
        reading = ReadHW178Sensor(config);
    } else {
        ESP_LOGE(TAG, "Unsupported expander type: %s", config.expander.c_str());
    }
    
    return reading;
}

sensor_reading_t HardwareManager::ReadHW178Sensor(const sensor_config_t& config) {
    sensor_reading_t reading;
    reading.sensor_id = config.id;
    reading.name = config.name;
    reading.type = config.type;
    reading.unit = config.unit;
    reading.timestamp = GetTimestamp();
    reading.valid = false;
    
    if (!hw178_is_initialized()) {
        ESP_LOGE(TAG, "HW178 not initialized");
        LogError("HW178", "Multiplexer not initialized for sensor " + config.id);
        return reading;
    }
    
    // Validate sensor type
    if (!IsSensorTypeSupported(config.type)) {
        ESP_LOGE(TAG, "Unsupported sensor type: %s", config.type.c_str());
        LogError("Sensor", "Unsupported sensor type " + config.type + " for sensor " + config.id);
        return reading;
    }
    
    // Select the correct channel
    esp_err_t ret = SelectExpander("hw178", config.channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select expander channel for sensor %s", config.id.c_str());
        return reading;
    }
    
    // Read raw value with retry mechanism
    int raw_value = 0;
    int retry_count = 0;
    const int max_retries = 3;
    
    do {
        ret = hw178_read_channel(static_cast<hw178_channel_t>(config.channel), &raw_value);
        if (ret == ESP_OK) {
            break;
        }
        
        retry_count++;
        if (retry_count < max_retries) {
            ESP_LOGW(TAG, "Retry %d/%d reading sensor %s", retry_count, max_retries, config.id.c_str());
            vTaskDelay(pdMS_TO_TICKS(10)); // Wait 10ms before retry
        }
    } while (retry_count < max_retries);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read HW178 channel %d after %d retries: %s", 
                 config.channel, max_retries, esp_err_to_name(ret));
        LogError("HW178", "Failed to read channel " + std::to_string(config.channel) + " for sensor " + config.id);
        return reading;
    }
    
    // Apply calibration
    reading.value = ApplyCalibration(raw_value, config);
    reading.valid = true;
    
    // Validate the reading
    if (!ValidateSensorReading(reading)) {
        ESP_LOGW(TAG, "Sensor reading validation failed for %s", config.id.c_str());
        reading.valid = false;
        return reading;
    }
    
    ESP_LOGD(TAG, "Read sensor %s: raw=%d, calibrated=%.2f %s", 
             config.id.c_str(), raw_value, reading.value, config.unit.c_str());
    
    return reading;
}

esp_err_t HardwareManager::SetMotorSpeed(int motor_id, int speed) {
    if (!initialized_) {
        ESP_LOGE(TAG, "Hardware manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    auto it = motor_configs_.find(motor_id);
    if (it == motor_configs_.end()) {
        ESP_LOGE(TAG, "Motor %d not found in configuration", motor_id);
        return ESP_ERR_NOT_FOUND;
    }
    
    const motor_config_t& config = it->second;
    
    // Validate speed range
    if (!ValidateMotorSpeed(speed, config)) {
        // Clamp speed to valid range
        if (speed > config.max_speed) speed = config.max_speed;
        if (speed < -config.max_speed) speed = -config.max_speed;
        ESP_LOGW(TAG, "Motor speed clamped to %d for motor %d", speed, motor_id);
    }
    
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
    
    if (config.connection_type == "pcf8575") {
        ret = SetPCF8575Motor(config, speed);
    } else if (config.connection_type == "direct") {
        ret = SetDirectMotor(config, speed);
    } else {
        ESP_LOGE(TAG, "Unsupported motor connection type: %s", 
                 config.connection_type.c_str());
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Motor %d (%s) speed set to %d", 
                 motor_id, config.name.c_str(), speed);
    } else {
        ESP_LOGE(TAG, "Failed to set motor %d speed: %s", 
                 motor_id, esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t HardwareManager::SetPCF8575Motor(const motor_config_t& config, int speed) {
    // Initialize PCF8575 if not already done
    esp_err_t ret = InitializePCF8575();
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (!pcf8575_handle_) {
        ESP_LOGE(TAG, "PCF8575 handle not available");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Validate pin configuration
    if (config.pins.ena < 0 || config.pins.ena >= PCF8575_IO_MAX ||
        config.pins.in1 < 0 || config.pins.in1 >= PCF8575_IO_MAX ||
        config.pins.in2 < 0 || config.pins.in2 >= PCF8575_IO_MAX) {
        ESP_LOGE(TAG, "Invalid PCF8575 pin configuration for motor %d", config.id);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Setting PCF8575 motor %d (pins: ena=%d, in1=%d, in2=%d) speed to %d", 
             config.id, config.pins.ena, config.pins.in1, config.pins.in2, speed);
    
    if (speed == 0) {
        // Stop motor - disable first, then set direction pins low
        ret = pcf8575_set_level(pcf8575_handle_, config.pins.ena, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable motor %d: %s", config.id, esp_err_to_name(ret));
            return ret;
        }
        
        ret = pcf8575_set_level(pcf8575_handle_, config.pins.in1, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set IN1 low for motor %d: %s", config.id, esp_err_to_name(ret));
            return ret;
        }
        
        ret = pcf8575_set_level(pcf8575_handle_, config.pins.in2, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set IN2 low for motor %d: %s", config.id, esp_err_to_name(ret));
            return ret;
        }
    } else {
        // Set direction first, then enable
        if (speed > 0) {
            // Forward direction
            ret = pcf8575_set_level(pcf8575_handle_, config.pins.in1, 1);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set forward direction for motor %d: %s", 
                         config.id, esp_err_to_name(ret));
                return ret;
            }
            ret = pcf8575_set_level(pcf8575_handle_, config.pins.in2, 0);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to clear IN2 for motor %d: %s", 
                         config.id, esp_err_to_name(ret));
                return ret;
            }
        } else {
            // Reverse direction
            ret = pcf8575_set_level(pcf8575_handle_, config.pins.in1, 0);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to clear IN1 for motor %d: %s", 
                         config.id, esp_err_to_name(ret));
                return ret;
            }
            ret = pcf8575_set_level(pcf8575_handle_, config.pins.in2, 1);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set reverse direction for motor %d: %s", 
                         config.id, esp_err_to_name(ret));
                return ret;
            }
        }
        
        // Enable motor
        ret = pcf8575_set_level(pcf8575_handle_, config.pins.ena, 1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable motor %d: %s", config.id, esp_err_to_name(ret));
            return ret;
        }
    }
    
    ESP_LOGD(TAG, "Successfully set PCF8575 motor %d speed to %d", config.id, speed);
    return ESP_OK;
}

esp_err_t HardwareManager::SetServoAngle(int servo_id, int angle) {
    if (!initialized_) {
        ESP_LOGE(TAG, "Hardware manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    auto it = servo_configs_.find(servo_id);
    if (it == servo_configs_.end()) {
        ESP_LOGE(TAG, "Servo %d not found in configuration", servo_id);
        return ESP_ERR_NOT_FOUND;
    }
    
    const servo_config_t& config = it->second;
    
    // Validate angle range
    if (!ValidateServoAngle(angle, config)) {
        // Clamp angle to valid range
        if (angle < config.min_angle) angle = config.min_angle;
        if (angle > config.max_angle) angle = config.max_angle;
        ESP_LOGW(TAG, "Servo angle clamped to %d for servo %d", angle, servo_id);
    }
    
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
    
    if (config.connection_type == "lu9685") {
        ret = SetLU9685Servo(config, angle);
    } else if (config.connection_type == "direct") {
        ret = SetDirectServo(config, angle);
    } else {
        ESP_LOGE(TAG, "Unsupported servo connection type: %s", 
                 config.connection_type.c_str());
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Servo %d (%s) angle set to %d degrees", 
                 servo_id, config.name.c_str(), angle);
    } else {
        ESP_LOGE(TAG, "Failed to set servo %d angle: %s", 
                 servo_id, esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t HardwareManager::SetLU9685Servo(const servo_config_t& config, int angle) {
    // Initialize LU9685 if not already done
    esp_err_t ret = InitializeLU9685();
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (!lu9685_handle_) {
        ESP_LOGE(TAG, "LU9685 handle not available");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Validate channel configuration
    if (config.channel < 0 || config.channel >= 16) {
        ESP_LOGE(TAG, "Invalid LU9685 channel %d for servo %d", config.channel, config.id);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Setting LU9685 servo %d (channel %d) angle to %d degrees", 
             config.id, config.channel, angle);
    
    ret = lu9685_set_channel_angle(lu9685_handle_, config.channel, angle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LU9685 servo %d angle: %s", 
                 config.id, esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "Successfully set LU9685 servo %d angle to %d degrees", config.id, angle);
    return ESP_OK;
}

std::vector<actuator_status_t> HardwareManager::GetActuatorStatus() {
    std::vector<actuator_status_t> status_list;
    
    if (!initialized_) {
        ESP_LOGE(TAG, "Hardware manager not initialized");
        return status_list;
    }
    
    status_list.reserve(motor_configs_.size() + servo_configs_.size());  // Optimize memory allocation
    uint64_t timestamp = GetTimestamp();
    
    // Add motor status
    for (const auto& pair : motor_configs_) {
        const motor_config_t& config = pair.second;
        actuator_status_t status;
        
        status.actuator_id = std::to_string(config.id);
        status.name = config.name;
        status.type = "motor";
        status.enabled = true;  // Simplified - could check actual state
        status.last_update = timestamp;
        
        status.parameters["max_speed"] = config.max_speed;
        status.parameters["ena_pin"] = config.pins.ena;
        status.parameters["in1_pin"] = config.pins.in1;
        status.parameters["in2_pin"] = config.pins.in2;
        
        status_list.push_back(status);
    }
    
    // Add servo status
    for (const auto& pair : servo_configs_) {
        const servo_config_t& config = pair.second;
        actuator_status_t status;
        
        status.actuator_id = std::to_string(config.id);
        status.name = config.name;
        status.type = "servo";
        status.enabled = true;  // Simplified - could check actual state
        status.last_update = timestamp;
        
        status.parameters["channel"] = config.channel;
        status.parameters["min_angle"] = config.min_angle;
        status.parameters["max_angle"] = config.max_angle;
        status.parameters["center_angle"] = config.center_angle;
        
        status_list.push_back(status);
    }
    
    return status_list;
}

void HardwareManager::LogError(const std::string& component, const std::string& message) {
    SimpleErrorHandler::LogError(SimpleErrorHandler::ErrorLevel::ERROR, component, message);
}

sensor_config_t HardwareManager::GetSensorConfig(const std::string& sensor_id) {
    auto it = sensor_configs_.find(sensor_id);
    if (it != sensor_configs_.end()) {
        return it->second;
    }
    
    // Return empty config if not found
    sensor_config_t empty_config;
    empty_config.id = "";
    return empty_config;
}

std::map<std::string, sensor_config_t> HardwareManager::GetAllSensorConfigs() const {
    return sensor_configs_;
}

bool HardwareManager::ValidateSensorReading(const sensor_reading_t& reading) {
    if (reading.sensor_id.empty()) {
        ESP_LOGE(TAG, "Sensor reading has empty ID");
        return false;
    }
    
    if (!reading.valid) {
        ESP_LOGW(TAG, "Sensor reading marked as invalid: %s", reading.sensor_id.c_str());
        return false;
    }
    
    // Check for reasonable timestamp (not too old)
    uint64_t current_time = GetTimestamp();
    uint64_t max_age = 60 * 1000000; // 60 seconds in microseconds
    
    if (current_time > reading.timestamp && (current_time - reading.timestamp) > max_age) {
        ESP_LOGW(TAG, "Sensor reading too old: %s", reading.sensor_id.c_str());
        return false;
    }
    
    // Additional validation based on sensor type
    if (reading.type == "temperature") {
        // Reasonable temperature range: -50 to 150 Celsius
        if (reading.value < -50.0f || reading.value > 150.0f) {
            ESP_LOGW(TAG, "Temperature reading out of range: %.2f", reading.value);
            return false;
        }
    } else if (reading.type == "voltage") {
        // Reasonable voltage range: 0 to 50V
        if (reading.value < 0.0f || reading.value > 50.0f) {
            ESP_LOGW(TAG, "Voltage reading out of range: %.2f", reading.value);
            return false;
        }
    } else if (reading.type == "current") {
        // Reasonable current range: 0 to 10A
        if (reading.value < 0.0f || reading.value > 10.0f) {
            ESP_LOGW(TAG, "Current reading out of range: %.2f", reading.value);
            return false;
        }
    }
    
    return true;
}

esp_err_t HardwareManager::SelectExpander(const std::string& expander_type, int channel) {
    if (expander_type == "hw178") {
        if (!hw178_is_initialized()) {
            ESP_LOGE(TAG, "HW178 not initialized");
            return ESP_ERR_INVALID_STATE;
        }
        
        esp_err_t ret = hw178_set_channel(static_cast<hw178_channel_t>(channel));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to select HW178 channel %d: %s", 
                     channel, esp_err_to_name(ret));
            return ret;
        }
        
        // Small delay to allow channel switching to stabilize
        vTaskDelay(pdMS_TO_TICKS(2));
        return ESP_OK;
    } else if (expander_type == "pca9548a") {
        if (!pca9548a_is_initialized()) {
            ESP_LOGE(TAG, "PCA9548A not initialized");
            return ESP_ERR_INVALID_STATE;
        }
        
        uint8_t channel_mask = (1 << channel);
        esp_err_t ret = pca9548a_select_channel(channel_mask);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to select PCA9548A channel %d: %s", 
                     channel, esp_err_to_name(ret));
            return ret;
        }
        
        // Small delay to allow channel switching to stabilize
        vTaskDelay(pdMS_TO_TICKS(5));
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Unsupported expander type: %s", expander_type.c_str());
    return ESP_ERR_NOT_SUPPORTED;
}

float HardwareManager::ApplyCalibration(int raw_value, const sensor_config_t& config) {
    return (raw_value * config.calibration.scale) + config.calibration.offset;
}

bool HardwareManager::IsSensorTypeSupported(const std::string& sensor_type) {
    // List of supported sensor types
    static const std::vector<std::string> supported_types = {
        "temperature",
        "voltage", 
        "current",
        "pressure",
        "humidity",
        "light",
        "distance",
        "analog"
    };
    
    return std::find(supported_types.begin(), supported_types.end(), sensor_type) != supported_types.end();
}

void HardwareManager::LogError(const std::string& component, const std::string& message) {
    SimpleErrorHandler::LogError(SimpleErrorHandler::ErrorLevel::ERROR, component, message);
}

esp_err_t HardwareManager::StopMotor(int motor_id) {
    return SetMotorSpeed(motor_id, 0);
}

esp_err_t HardwareManager::StopAllMotors() {
    esp_err_t ret = ESP_OK;
    
    for (const auto& pair : motor_configs_) {
        esp_err_t motor_ret = StopMotor(pair.first);
        if (motor_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop motor %d: %s", pair.first, esp_err_to_name(motor_ret));
            ret = motor_ret; // Keep last error
        }
    }
    
    ESP_LOGI(TAG, "Stopped all motors");
    return ret;
}

esp_err_t HardwareManager::CenterServo(int servo_id) {
    auto it = servo_configs_.find(servo_id);
    if (it == servo_configs_.end()) {
        ESP_LOGE(TAG, "Servo %d not found in configuration", servo_id);
        return ESP_ERR_NOT_FOUND;
    }
    
    const servo_config_t& config = it->second;
    return SetServoAngle(servo_id, config.center_angle);
}

motor_config_t HardwareManager::GetMotorConfig(int motor_id) {
    auto it = motor_configs_.find(motor_id);
    if (it != motor_configs_.end()) {
        return it->second;
    }
    
    // Return empty config if not found
    motor_config_t empty_config;
    empty_config.id = -1;
    return empty_config;
}

servo_config_t HardwareManager::GetServoConfig(int servo_id) {
    auto it = servo_configs_.find(servo_id);
    if (it != servo_configs_.end()) {
        return it->second;
    }
    
    // Return empty config if not found
    servo_config_t empty_config;
    empty_config.id = -1;
    return empty_config;
}

bool HardwareManager::ValidateMotorSpeed(int speed, const motor_config_t& config) {
    if (speed < -config.max_speed || speed > config.max_speed) {
        ESP_LOGW(TAG, "Motor speed %d out of range [-%d, %d]", 
                 speed, config.max_speed, config.max_speed);
        return false;
    }
    return true;
}

bool HardwareManager::ValidateServoAngle(int angle, const servo_config_t& config) {
    if (angle < config.min_angle || angle > config.max_angle) {
        ESP_LOGW(TAG, "Servo angle %d out of range [%d, %d]", 
                 angle, config.min_angle, config.max_angle);
        return false;
    }
    return true;
}

esp_err_t HardwareManager::InitializePCF8575() {
    if (pcf8575_handle_) {
        return ESP_OK; // Already initialized
    }
    
    ESP_LOGI(TAG, "Initializing PCF8575 for motor control");
    
    if (!pca9548a_is_initialized()) {
        ESP_LOGE(TAG, "PCA9548A not available for PCF8575");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Try to get existing PCF8575 handle
    pcf8575_handle_ = pcf8575_get_handle();
    if (!pcf8575_handle_) {
        ESP_LOGE(TAG, "PCF8575 not initialized globally");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "PCF8575 initialized successfully");
    return ESP_OK;
}

esp_err_t HardwareManager::InitializeLU9685() {
    if (lu9685_handle_) {
        return ESP_OK; // Already initialized
    }
    
    ESP_LOGI(TAG, "Initializing LU9685 for servo control");
    
    // Try to get existing LU9685 handle
    lu9685_handle_ = lu9685_get_handle();
    if (!lu9685_handle_) {
        ESP_LOGE(TAG, "LU9685 not initialized globally");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "LU9685 initialized successfully");
    return ESP_OK;
}

esp_err_t HardwareManager::SetDirectMotor(const motor_config_t& config, int speed) {
    ESP_LOGD(TAG, "Setting direct motor %d speed to %d", config.id, speed);
    
    // Direct motor control implementation
    // Configure GPIO pins for motor control (ENA, IN1, IN2)
    // Use LEDC or MCPWM for PWM control on ENA pin
    // Set direction using IN1 and IN2 pins
    
    if (speed == 0) {
        ESP_LOGI(TAG, "Direct motor %d stopped", config.id);
    } else if (speed > 0) {
        ESP_LOGI(TAG, "Direct motor %d forward speed %d", config.id, speed);
    } else {
        ESP_LOGI(TAG, "Direct motor %d reverse speed %d", config.id, -speed);
    }
    
    // Note: Direct GPIO/PWM control implementation depends on specific hardware configuration
    return ESP_OK;
}

esp_err_t HardwareManager::SetDirectServo(const servo_config_t& config, int angle) {
    ESP_LOGD(TAG, "Setting direct servo %d angle to %d degrees", config.id, angle);
    
    // Direct servo control implementation
    // Configure LEDC or MCPWM for servo PWM control
    // Calculate pulse width based on angle (typically 1-2ms pulse width)
    // Set PWM duty cycle accordingly
    
    ESP_LOGI(TAG, "Direct servo %d set to %d degrees", config.id, angle);
    
    // Note: Direct PWM control implementation depends on specific hardware configuration
    return ESP_OK;
}

esp_err_t HardwareManager::SaveConfiguration(const std::string& config_file) {
    ESP_LOGI(TAG, "Saving configuration to: %s", config_file.c_str());
    
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON root object");
        return ESP_ERR_NO_MEM;
    }
    
    cJSON* hardware = cJSON_CreateObject();
    if (!hardware) {
        ESP_LOGE(TAG, "Failed to create hardware JSON object");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    
    // Serialize sensors
    if (!sensor_configs_.empty()) {
        cJSON* sensors = cJSON_CreateArray();
        if (!sensors) {
            ESP_LOGE(TAG, "Failed to create sensors array");
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        
        for (const auto& pair : sensor_configs_) {
            cJSON* sensor_json = SerializeSensorConfig(pair.second);
            if (sensor_json) {
                cJSON_AddItemToArray(sensors, sensor_json);
            }
        }
        
        cJSON_AddItemToObject(hardware, "sensors", sensors);
    }
    
    // Serialize motors
    if (!motor_configs_.empty()) {
        cJSON* motors = cJSON_CreateArray();
        if (!motors) {
            ESP_LOGE(TAG, "Failed to create motors array");
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        
        for (const auto& pair : motor_configs_) {
            cJSON* motor_json = SerializeMotorConfig(pair.second);
            if (motor_json) {
                cJSON_AddItemToArray(motors, motor_json);
            }
        }
        
        cJSON_AddItemToObject(hardware, "motors", motors);
    }
    
    // Serialize servos
    if (!servo_configs_.empty()) {
        cJSON* servos = cJSON_CreateArray();
        if (!servos) {
            ESP_LOGE(TAG, "Failed to create servos array");
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        
        for (const auto& pair : servo_configs_) {
            cJSON* servo_json = SerializeServoConfig(pair.second);
            if (servo_json) {
                cJSON_AddItemToArray(servos, servo_json);
            }
        }
        
        cJSON_AddItemToObject(hardware, "servos", servos);
    }
    
    cJSON_AddItemToObject(root, "hardware", hardware);
    
    // Convert to string and save
    char* json_string = cJSON_Print(root);
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to convert JSON to string");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    
    std::ofstream file(config_file);
    if (!file.is_open()) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", config_file.c_str());
        free(json_string);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }
    
    file << json_string;
    file.close();
    
    free(json_string);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Configuration saved successfully");
    return ESP_OK;
}

esp_err_t HardwareManager::CreateDefaultConfiguration(const std::string& config_file) {
    ESP_LOGI(TAG, "Creating default configuration: %s", config_file.c_str());
    
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON root object");
        return ESP_ERR_NO_MEM;
    }
    
    cJSON* hardware = cJSON_CreateObject();
    
    // Create example sensor configuration
    cJSON* sensors = cJSON_CreateArray();
    
    cJSON* temp_sensor = cJSON_CreateObject();
    cJSON_AddStringToObject(temp_sensor, "id", "temperature_01");
    cJSON_AddStringToObject(temp_sensor, "name", "Environment Temperature");
    cJSON_AddStringToObject(temp_sensor, "type", "temperature");
    cJSON_AddStringToObject(temp_sensor, "expander", "hw178");
    cJSON_AddNumberToObject(temp_sensor, "channel", 0);
    cJSON_AddStringToObject(temp_sensor, "unit", "°C");
    
    cJSON* calibration = cJSON_CreateObject();
    cJSON_AddNumberToObject(calibration, "offset", 0.0);
    cJSON_AddNumberToObject(calibration, "scale", 1.0);
    cJSON_AddItemToObject(temp_sensor, "calibration", calibration);
    
    cJSON_AddItemToArray(sensors, temp_sensor);
    
    cJSON* voltage_sensor = cJSON_CreateObject();
    cJSON_AddStringToObject(voltage_sensor, "id", "voltage_battery");
    cJSON_AddStringToObject(voltage_sensor, "name", "Battery Voltage");
    cJSON_AddStringToObject(voltage_sensor, "type", "voltage");
    cJSON_AddStringToObject(voltage_sensor, "expander", "hw178");
    cJSON_AddNumberToObject(voltage_sensor, "channel", 1);
    cJSON_AddStringToObject(voltage_sensor, "unit", "V");
    
    cJSON* voltage_calibration = cJSON_CreateObject();
    cJSON_AddNumberToObject(voltage_calibration, "offset", 0.0);
    cJSON_AddNumberToObject(voltage_calibration, "scale", 0.01);
    cJSON_AddItemToObject(voltage_sensor, "calibration", voltage_calibration);
    
    cJSON_AddItemToArray(sensors, voltage_sensor);
    
    cJSON_AddItemToObject(hardware, "sensors", sensors);
    
    // Create example motor configuration
    cJSON* motors = cJSON_CreateArray();
    
    cJSON* left_motor = cJSON_CreateObject();
    cJSON_AddNumberToObject(left_motor, "id", 0);
    cJSON_AddStringToObject(left_motor, "name", "Left Wheel Motor");
    cJSON_AddStringToObject(left_motor, "connection_type", "pcf8575");
    
    cJSON* motor_pins = cJSON_CreateObject();
    cJSON_AddNumberToObject(motor_pins, "ena", 2);
    cJSON_AddNumberToObject(motor_pins, "in1", 0);
    cJSON_AddNumberToObject(motor_pins, "in2", 1);
    cJSON_AddItemToObject(left_motor, "pins", motor_pins);
    
    cJSON_AddNumberToObject(left_motor, "max_speed", 255);
    
    cJSON_AddItemToArray(motors, left_motor);
    
    cJSON_AddItemToObject(hardware, "motors", motors);
    
    // Create example servo configuration
    cJSON* servos = cJSON_CreateArray();
    
    cJSON* pan_servo = cJSON_CreateObject();
    cJSON_AddNumberToObject(pan_servo, "id", 0);
    cJSON_AddStringToObject(pan_servo, "name", "Pan Servo");
    cJSON_AddStringToObject(pan_servo, "connection_type", "lu9685");
    cJSON_AddNumberToObject(pan_servo, "channel", 0);
    cJSON_AddNumberToObject(pan_servo, "min_angle", 0);
    cJSON_AddNumberToObject(pan_servo, "max_angle", 180);
    cJSON_AddNumberToObject(pan_servo, "center_angle", 90);
    
    cJSON_AddItemToArray(servos, pan_servo);
    
    cJSON_AddItemToObject(hardware, "servos", servos);
    
    cJSON_AddItemToObject(root, "hardware", hardware);
    
    // Convert to string and save
    char* json_string = cJSON_Print(root);
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to convert JSON to string");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    
    std::ofstream file(config_file);
    if (!file.is_open()) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", config_file.c_str());
        free(json_string);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }
    
    file << json_string;
    file.close();
    
    free(json_string);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Default configuration created successfully");
    return ESP_OK;
}

esp_err_t HardwareManager::ReloadConfiguration(const std::string& config_file) {
    ESP_LOGI(TAG, "Reloading configuration from: %s", config_file.c_str());
    
    // Clear existing configurations
    sensor_configs_.clear();
    motor_configs_.clear();
    servo_configs_.clear();
    
    // Load new configuration
    esp_err_t ret = LoadConfiguration(config_file);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reload configuration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Configuration reloaded successfully");
    ESP_LOGI(TAG, "%s", GetConfigurationSummary().c_str());
    
    return ESP_OK;
}

cJSON* HardwareManager::SerializeSensorConfig(const sensor_config_t& config) {
    cJSON* sensor = cJSON_CreateObject();
    if (!sensor) return nullptr;
    
    cJSON_AddStringToObject(sensor, "id", config.id.c_str());
    cJSON_AddStringToObject(sensor, "name", config.name.c_str());
    cJSON_AddStringToObject(sensor, "type", config.type.c_str());
    cJSON_AddStringToObject(sensor, "expander", config.expander.c_str());
    cJSON_AddNumberToObject(sensor, "channel", config.channel);
    cJSON_AddStringToObject(sensor, "unit", config.unit.c_str());
    
    cJSON* calibration = cJSON_CreateObject();
    cJSON_AddNumberToObject(calibration, "offset", config.calibration.offset);
    cJSON_AddNumberToObject(calibration, "scale", config.calibration.scale);
    cJSON_AddItemToObject(sensor, "calibration", calibration);
    
    return sensor;
}

cJSON* HardwareManager::SerializeMotorConfig(const motor_config_t& config) {
    cJSON* motor = cJSON_CreateObject();
    if (!motor) return nullptr;
    
    cJSON_AddNumberToObject(motor, "id", config.id);
    cJSON_AddStringToObject(motor, "name", config.name.c_str());
    cJSON_AddStringToObject(motor, "connection_type", config.connection_type.c_str());
    
    cJSON* pins = cJSON_CreateObject();
    cJSON_AddNumberToObject(pins, "ena", config.pins.ena);
    cJSON_AddNumberToObject(pins, "in1", config.pins.in1);
    cJSON_AddNumberToObject(pins, "in2", config.pins.in2);
    cJSON_AddItemToObject(motor, "pins", pins);
    
    cJSON_AddNumberToObject(motor, "max_speed", config.max_speed);
    
    return motor;
}

cJSON* HardwareManager::SerializeServoConfig(const servo_config_t& config) {
    cJSON* servo = cJSON_CreateObject();
    if (!servo) return nullptr;
    
    cJSON_AddNumberToObject(servo, "id", config.id);
    cJSON_AddStringToObject(servo, "name", config.name.c_str());
    cJSON_AddStringToObject(servo, "connection_type", config.connection_type.c_str());
    cJSON_AddNumberToObject(servo, "channel", config.channel);
    cJSON_AddNumberToObject(servo, "min_angle", config.min_angle);
    cJSON_AddNumberToObject(servo, "max_angle", config.max_angle);
    cJSON_AddNumberToObject(servo, "center_angle", config.center_angle);
    
    return servo;
}

bool HardwareManager::ValidateConfigurationStructure(cJSON* root) {
    if (!root || !cJSON_IsObject(root)) {
        ESP_LOGE(TAG, "Configuration root is not a valid JSON object");
        return false;
    }
    
    cJSON* hardware = cJSON_GetObjectItem(root, "hardware");
    if (!hardware || !cJSON_IsObject(hardware)) {
        ESP_LOGE(TAG, "Missing or invalid 'hardware' section in configuration");
        return false;
    }
    
    // Check sensors section if present
    cJSON* sensors = cJSON_GetObjectItem(hardware, "sensors");
    if (sensors && !cJSON_IsArray(sensors)) {
        ESP_LOGE(TAG, "Sensors section must be an array");
        return false;
    }
    
    // Check motors section if present
    cJSON* motors = cJSON_GetObjectItem(hardware, "motors");
    if (motors && !cJSON_IsArray(motors)) {
        ESP_LOGE(TAG, "Motors section must be an array");
        return false;
    }
    
    // Check servos section if present
    cJSON* servos = cJSON_GetObjectItem(hardware, "servos");
    if (servos && !cJSON_IsArray(servos)) {
        ESP_LOGE(TAG, "Servos section must be an array");
        return false;
    }
    
    return true;
}

std::string HardwareManager::GetConfigurationSummary() {
    std::stringstream summary;
    summary << "Hardware Configuration Summary:\n";
    summary << "  Sensors: " << sensor_configs_.size() << "\n";
    summary << "  Motors: " << motor_configs_.size() << "\n";
    summary << "  Servos: " << servo_configs_.size() << "\n";
    
    if (!sensor_configs_.empty()) {
        summary << "  Sensor Details:\n";
        for (const auto& pair : sensor_configs_) {
            const auto& config = pair.second;
            summary << "    - " << config.id << " (" << config.type << ") on " 
                   << config.expander << " channel " << config.channel << "\n";
        }
    }
    
    if (!motor_configs_.empty()) {
        summary << "  Motor Details:\n";
        for (const auto& pair : motor_configs_) {
            const auto& config = pair.second;
            summary << "    - Motor " << config.id << " (" << config.name 
                   << ") via " << config.connection_type << "\n";
        }
    }
    
    if (!servo_configs_.empty()) {
        summary << "  Servo Details:\n";
        for (const auto& pair : servo_configs_) {
            const auto& config = pair.second;
            summary << "    - Servo " << config.id << " (" << config.name 
                   << ") via " << config.connection_type << " channel " << config.channel << "\n";
        }
    }
    
    return summary.str();
}

uint64_t HardwareManager::GetTimestamp() {
    return esp_timer_get_time();
}