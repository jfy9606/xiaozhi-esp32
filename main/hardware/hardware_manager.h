#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "esp_err.h"
#include "cJSON.h"

// Forward declarations for expander drivers
#include "../ext/include/multiplexer.h"
#include "../ext/include/pca9548a.h"
#include "../ext/include/pcf8575.h"
#include "../ext/include/lu9685.h"
#include "../ext/include/hw178.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sensor reading data structure
 */
typedef struct {
    std::string sensor_id;
    std::string name;
    std::string type;
    float value;
    std::string unit;
    uint64_t timestamp;
    bool valid;
} sensor_reading_t;

/**
 * @brief Actuator status data structure
 */
typedef struct {
    std::string actuator_id;
    std::string name;
    std::string type;  // "motor" or "servo"
    std::map<std::string, float> parameters;
    bool enabled;
    uint64_t last_update;
} actuator_status_t;

/**
 * @brief Sensor configuration structure
 */
typedef struct {
    std::string id;
    std::string name;
    std::string type;
    std::string expander;
    int channel;
    std::string unit;
    struct {
        float offset;
        float scale;
    } calibration;
} sensor_config_t;

/**
 * @brief Motor configuration structure
 */
typedef struct {
    int id;
    std::string name;
    std::string connection_type;  // "pcf8575" or "direct"
    struct {
        int ena;  // Enable pin
        int in1;  // Direction pin 1
        int in2;  // Direction pin 2
    } pins;
    int max_speed;
} motor_config_t;

/**
 * @brief Servo configuration structure
 */
typedef struct {
    int id;
    std::string name;
    std::string connection_type;  // "lu9685" or "direct"
    int channel;
    int min_angle;
    int max_angle;
    int center_angle;
} servo_config_t;

/**
 * @brief Hardware Manager class for unified sensor and actuator management
 */
class HardwareManager {
public:
    HardwareManager();
    ~HardwareManager();

    /**
     * @brief Initialize the hardware manager
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t Initialize();

    /**
     * @brief Load configuration from JSON file
     * @param config_file Path to configuration file
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t LoadConfiguration(const std::string& config_file);

    /**
     * @brief Validate the current configuration
     * @return ESP_OK if configuration is valid, error code otherwise
     */
    esp_err_t ValidateConfiguration();

    /**
     * @brief Save configuration to JSON file
     * @param config_file Path to configuration file
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t SaveConfiguration(const std::string& config_file);

    /**
     * @brief Create default configuration template
     * @param config_file Path to configuration file to create
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t CreateDefaultConfiguration(const std::string& config_file);

    /**
     * @brief Reload configuration at runtime
     * @param config_file Path to configuration file
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t ReloadConfiguration(const std::string& config_file);

    // Sensor interface methods
    /**
     * @brief Read all sensors
     * @return Vector of sensor readings
     */
    std::vector<sensor_reading_t> ReadAllSensors();

    /**
     * @brief Read a specific sensor
     * @param sensor_id Sensor identifier
     * @return Sensor reading
     */
    sensor_reading_t ReadSensor(const std::string& sensor_id);

    /**
     * @brief Get sensor configuration
     * @param sensor_id Sensor identifier
     * @return Sensor configuration, or empty config if not found
     */
    sensor_config_t GetSensorConfig(const std::string& sensor_id);

    /**
     * @brief Get all sensor configurations
     * @return Map of sensor configurations
     */
    std::map<std::string, sensor_config_t> GetAllSensorConfigs() const;

    /**
     * @brief Validate sensor reading
     * @param reading Sensor reading to validate
     * @return true if reading is valid, false otherwise
     */
    bool ValidateSensorReading(const sensor_reading_t& reading);

    // Actuator interface methods
    /**
     * @brief Set motor speed
     * @param motor_id Motor identifier
     * @param speed Speed value (-255 to 255)
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t SetMotorSpeed(int motor_id, int speed);

    /**
     * @brief Set servo angle
     * @param servo_id Servo identifier
     * @param angle Angle in degrees (0-180)
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t SetServoAngle(int servo_id, int angle);

    /**
     * @brief Stop motor
     * @param motor_id Motor identifier
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t StopMotor(int motor_id);

    /**
     * @brief Stop all motors
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t StopAllMotors();

    /**
     * @brief Set servo to center position
     * @param servo_id Servo identifier
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t CenterServo(int servo_id);

    /**
     * @brief Get actuator status
     * @return Vector of actuator status
     */
    std::vector<actuator_status_t> GetActuatorStatus();

    /**
     * @brief Get motor configuration
     * @param motor_id Motor identifier
     * @return Motor configuration, or empty config if not found
     */
    motor_config_t GetMotorConfig(int motor_id);

    /**
     * @brief Get servo configuration
     * @param servo_id Servo identifier
     * @return Servo configuration, or empty config if not found
     */
    servo_config_t GetServoConfig(int servo_id);

    /**
     * @brief Check if hardware manager is initialized
     * @return true if initialized, false otherwise
     */
    bool IsInitialized() const { return initialized_; }

private:
    bool initialized_;
    
    // Configuration storage
    std::map<std::string, sensor_config_t> sensor_configs_;
    std::map<int, motor_config_t> motor_configs_;
    std::map<int, servo_config_t> servo_configs_;
    
    // Expander handles
    pcf8575_handle_t pcf8575_handle_;
    lu9685_handle_t lu9685_handle_;
    hw178_handle_t hw178_handle_;
    
    // Private helper methods
    esp_err_t InitializeExpanders();
    esp_err_t ParseSensorConfig(cJSON* sensors_json);
    esp_err_t ParseMotorConfig(cJSON* motors_json);
    esp_err_t ParseServoConfig(cJSON* servos_json);
    
    sensor_reading_t ReadHW178Sensor(const sensor_config_t& config);
    esp_err_t SetPCF8575Motor(const motor_config_t& config, int speed);
    esp_err_t SetLU9685Servo(const servo_config_t& config, int angle);
    esp_err_t SetDirectMotor(const motor_config_t& config, int speed);
    esp_err_t SetDirectServo(const servo_config_t& config, int angle);
    
    // Actuator helper methods
    bool ValidateMotorSpeed(int speed, const motor_config_t& config);
    bool ValidateServoAngle(int angle, const servo_config_t& config);
    esp_err_t InitializePCF8575();
    esp_err_t InitializeLU9685();
    
    // Configuration helper methods
    cJSON* SerializeSensorConfig(const sensor_config_t& config);
    cJSON* SerializeMotorConfig(const motor_config_t& config);
    cJSON* SerializeServoConfig(const servo_config_t& config);
    bool ValidateConfigurationStructure(cJSON* root);
    std::string GetConfigurationSummary();
    
    // Sensor helper methods
    esp_err_t SelectExpander(const std::string& expander_type, int channel);
    float ApplyCalibration(int raw_value, const sensor_config_t& config);
    bool IsSensorTypeSupported(const std::string& sensor_type);
    
    void LogError(const std::string& component, const std::string& message);
    uint64_t GetTimestamp();
};

#ifdef __cplusplus
}
#endif