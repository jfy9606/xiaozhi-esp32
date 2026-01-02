# Hardware Manager

The Hardware Manager provides a unified interface for managing sensors and actuators connected through various multiplexer chips.

## Features

- **Sensor Management**: Read data from sensors connected through HW178 analog multiplexer
- **Motor Control**: Control motors through PCF8575 GPIO multiplexer
- **Servo Control**: Control servos through LU9685 PWM controller
- **Configuration Management**: JSON-based configuration with validation
- **Error Handling**: Comprehensive error handling and logging

## Supported Hardware

### Multiplexers
- **PCA9548A**: I2C multiplexer for channel selection
- **PCF8575**: 16-bit GPIO multiplexer for motor control
- **LU9685**: 16-channel PWM controller for servo control
- **HW178**: 16-channel analog multiplexer for sensor reading

### Sensors
- Temperature sensors
- Voltage sensors
- Current sensors
- Pressure sensors
- Humidity sensors
- Light sensors
- Distance sensors
- Generic analog sensors

### Actuators
- DC motors (via PCF8575)
- Servo motors (via LU9685)

## Configuration

The hardware manager uses JSON configuration files to define sensor and actuator mappings. See `hardware_config_example.json` for a complete example.

### Sensor Configuration
```json
{
  "id": "temperature_01",
  "name": "Environment Temperature",
  "type": "temperature",
  "multiplexer": "hw178",
  "channel": 0,
  "unit": "°C",
  "calibration": {
    "offset": 0.0,
    "scale": 1.0
  }
}
```

### Motor Configuration
```json
{
  "id": 0,
  "name": "Left Wheel Motor",
  "connection_type": "pcf8575",
  "pins": {
    "ena": 2,
    "in1": 0,
    "in2": 1
  },
  "max_speed": 255
}
```

### Servo Configuration
```json
{
  "id": 0,
  "name": "Pan Servo",
  "connection_type": "lu9685",
  "channel": 0,
  "min_angle": 0,
  "max_angle": 180,
  "center_angle": 90
}
```

## Usage

### Basic Initialization
```cpp
#include "hardware/hardware_manager.h"

HardwareManager hardware_manager;

// Initialize the hardware manager
esp_err_t ret = hardware_manager.Initialize();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize hardware manager");
    return ret;
}

// Load configuration
ret = hardware_manager.LoadConfiguration("/spiffs/hardware_config.json");
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to load configuration");
    return ret;
}
```

### Reading Sensors
```cpp
// Read all sensors
std::vector<sensor_reading_t> readings = hardware_manager.ReadAllSensors();
for (const auto& reading : readings) {
    if (reading.valid) {
        ESP_LOGI(TAG, "Sensor %s: %.2f %s", 
                 reading.sensor_id.c_str(), reading.value, reading.unit.c_str());
    }
}

// Read specific sensor
sensor_reading_t temp_reading = hardware_manager.ReadSensor("temperature_01");
if (temp_reading.valid) {
    ESP_LOGI(TAG, "Temperature: %.2f°C", temp_reading.value);
}
```

### Controlling Motors
```cpp
// Set motor speed (range: -255 to 255)
esp_err_t ret = hardware_manager.SetMotorSpeed(0, 128);  // Half speed forward
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set motor speed");
}

// Stop motor
hardware_manager.StopMotor(0);

// Stop all motors
hardware_manager.StopAllMotors();
```

### Controlling Servos
```cpp
// Set servo angle (range: 0-180 degrees)
esp_err_t ret = hardware_manager.SetServoAngle(0, 90);  // Center position
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set servo angle");
}

// Center servo
hardware_manager.CenterServo(0);
```

### Configuration Management
```cpp
// Create default configuration
hardware_manager.CreateDefaultConfiguration("/spiffs/hardware_config.json");

// Save current configuration
hardware_manager.SaveConfiguration("/spiffs/hardware_config.json");

// Reload configuration at runtime
hardware_manager.ReloadConfiguration("/spiffs/hardware_config.json");
```

## Error Handling

The hardware manager includes comprehensive error handling:

- Configuration validation
- Hardware initialization checks
- Communication error recovery
- Parameter validation
- Sensor reading validation

All methods return ESP error codes, and detailed logging is provided for troubleshooting.

## Integration

The hardware manager is designed to integrate with:
- Web API endpoints for remote control
- AI modules for intelligent control
- WebSocket interfaces for real-time updates
- Configuration management systems

See the main application code for integration examples.

## Documentation

- [Quick Start Guide](QUICK_START.md) - Get started quickly with basic setup
- [Configuration Guide](CONFIGURATION_GUIDE.md) - Detailed configuration options
- [API Reference](API_REFERENCE.md) - Complete API documentation
- [Troubleshooting Guide](TROUBLESHOOTING.md) - Common issues and solutions