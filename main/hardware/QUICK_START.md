# Hardware Manager Quick Start Guide

Get up and running with the Hardware Manager in 5 minutes.

## Prerequisites

- ESP32 development board
- PCA9548A I2C multiplexer (optional but recommended)
- One or more of: HW178, PCF8575, LU9685 modules
- Sensors and/or actuators to connect

## Step 1: Hardware Connections

### Basic I2C Setup

Connect PCA9548A to ESP32:
```
ESP32    PCA9548A
GPIO21 → SDA
GPIO22 → SCL
3.3V   → VCC
GND    → GND
```

### Connect Expander Modules

Connect expanders to PCA9548A channels:
```
PCA9548A Channel 0 → HW178 (sensors)
PCA9548A Channel 1 → PCF8575 (motors)
PCA9548A Channel 2 → LU9685 (servos)
```

## Step 2: Basic Configuration

Create `hardware_config.json`:

```json
{
  "hardware": {
    "sensors": [
      {
        "id": "temp_01",
        "name": "Temperature Sensor",
        "type": "temperature",
        "expander": "hw178",
        "channel": 0,
        "unit": "°C",
        "calibration": {
          "offset": 0.0,
          "scale": 1.0
        }
      }
    ],
    "motors": [
      {
        "id": 0,
        "name": "Test Motor",
        "connection_type": "pcf8575",
        "pins": {
          "ena": 2,
          "in1": 0,
          "in2": 1
        },
        "max_speed": 255
      }
    ],
    "servos": [
      {
        "id": 0,
        "name": "Test Servo",
        "connection_type": "lu9685",
        "channel": 0,
        "min_angle": 0,
        "max_angle": 180,
        "center_angle": 90
      }
    ]
  }
}
```

## Step 3: Code Integration

### Initialize Hardware Manager

```cpp
#include "hardware/hardware_manager.h"

// Create hardware manager instance
HardwareManager* hw_manager = new HardwareManager();

// Initialize
esp_err_t ret = hw_manager->Initialize();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Hardware manager initialization failed");
    return;
}

// Load configuration
ret = hw_manager->LoadConfiguration("/spiffs/hardware_config.json");
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Configuration loading failed");
    // Create default configuration
    hw_manager->CreateDefaultConfiguration("/spiffs/hardware_config.json");
}

ESP_LOGI(TAG, "Hardware manager ready!");
```

## Step 4: Test Basic Operations

### Read Sensors

```cpp
// Read all sensors
auto readings = hw_manager->ReadAllSensors();
for (const auto& reading : readings) {
    if (reading.valid) {
        ESP_LOGI(TAG, "Sensor %s: %.2f %s", 
                 reading.sensor_id.c_str(), 
                 reading.value, 
                 reading.unit.c_str());
    }
}

// Read specific sensor
auto temp_reading = hw_manager->ReadSensor("temp_01");
if (temp_reading.valid) {
    ESP_LOGI(TAG, "Temperature: %.2f°C", temp_reading.value);
}
```

### Control Motors

```cpp
// Move motor forward at half speed
ret = hw_manager->SetMotorSpeed(0, 128);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Motor started");
}

// Wait 2 seconds
vTaskDelay(pdMS_TO_TICKS(2000));

// Stop motor
hw_manager->StopMotor(0);
ESP_LOGI(TAG, "Motor stopped");
```

### Control Servos

```cpp
// Move servo to center position
ret = hw_manager->SetServoAngle(0, 90);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Servo moved to center");
}

// Move to different positions
hw_manager->SetServoAngle(0, 0);    // Minimum
vTaskDelay(pdMS_TO_TICKS(1000));
hw_manager->SetServoAngle(0, 180);  // Maximum
vTaskDelay(pdMS_TO_TICKS(1000));
hw_manager->CenterServo(0);         // Back to center
```

## Step 5: Web API Testing

### Start Web Server

The hardware manager automatically integrates with the web server. Test the API endpoints:

### Get System Status

```bash
curl http://your-device-ip/api/hardware/status
```

Expected response:
```json
{
  "success": true,
  "message": "Hardware status retrieved successfully",
  "data": {
    "expanders": {
      "pca9548a_initialized": true,
      "hw178_available": true,
      "pcf8575_available": true,
      "lu9685_available": true
    },
    "sensors_summary": {
      "total": 1,
      "active": 1
    },
    "actuators": [...]
  }
}
```

### Read Sensor Data

```bash
curl http://your-device-ip/api/sensors
```

### Control Motor

```bash
curl -X POST http://your-device-ip/api/motors/control \
  -H "Content-Type: application/json" \
  -d '{"motor_id": 0, "speed": 100}'
```

### Control Servo

```bash
curl -X POST http://your-device-ip/api/servos/control \
  -H "Content-Type: application/json" \
  -d '{"servo_id": 0, "angle": 45}'
```

## Step 6: Error Checking

### Check for Errors

```bash
curl http://your-device-ip/api/errors
```

### Clear Errors

```bash
curl -X DELETE http://your-device-ip/api/errors
```

## Common First-Time Issues

### 1. No Sensor Readings

**Problem**: Sensors return invalid values
**Solution**: 
- Check I2C connections
- Verify HW178 power supply
- Test with multimeter

### 2. Motors Don't Move

**Problem**: Motor control commands don't work
**Solution**:
- Check PCF8575 connections
- Verify motor driver wiring
- Test with LED on motor pins

### 3. Servos Don't Respond

**Problem**: Servo control doesn't work
**Solution**:
- Check LU9685 connections
- Verify servo power supply (usually 5V)
- Test PWM signal with oscilloscope

### 4. Configuration Errors

**Problem**: Configuration loading fails
**Solution**:
- Validate JSON syntax
- Check for duplicate IDs
- Use example configuration as template

## Next Steps

### Expand Your Setup

1. **Add More Sensors**
   - Connect additional sensors to HW178 channels
   - Update configuration file
   - Test each sensor individually

2. **Add More Actuators**
   - Connect more motors to PCF8575 pins
   - Connect more servos to LU9685 channels
   - Update configuration accordingly

3. **Implement Control Logic**
   - Create sensor-based control loops
   - Implement safety limits
   - Add automated behaviors

### Advanced Features

1. **Custom Calibration**
   - Measure sensor characteristics
   - Calculate calibration coefficients
   - Update configuration

2. **Error Handling**
   - Monitor error logs
   - Implement recovery procedures
   - Set up alerts

3. **Performance Optimization**
   - Optimize sensor reading frequency
   - Implement caching
   - Reduce I2C traffic

## Example Projects

### 1. Temperature Monitor

```cpp
void temperature_monitor_task(void* param) {
    HardwareManager* hw = (HardwareManager*)param;
    
    while (1) {
        auto reading = hw->ReadSensor("temp_01");
        if (reading.valid) {
            ESP_LOGI(TAG, "Temperature: %.2f°C", reading.value);
            
            // Control fan based on temperature
            if (reading.value > 30.0) {
                hw->SetMotorSpeed(0, 200);  // Turn on fan
            } else {
                hw->StopMotor(0);           // Turn off fan
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));  // Check every 5 seconds
    }
}
```

### 2. Servo Sweep

```cpp
void servo_sweep_task(void* param) {
    HardwareManager* hw = (HardwareManager*)param;
    
    int angle = 0;
    int direction = 1;
    
    while (1) {
        hw->SetServoAngle(0, angle);
        
        angle += direction * 10;
        if (angle >= 180 || angle <= 0) {
            direction *= -1;  // Reverse direction
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));  // Smooth movement
    }
}
```

### 3. Sensor Dashboard

```html
<!DOCTYPE html>
<html>
<head>
    <title>Sensor Dashboard</title>
    <script>
        function updateSensors() {
            fetch('/api/sensors')
                .then(response => response.json())
                .then(data => {
                    if (data.success) {
                        const sensors = data.data.sensors;
                        let html = '<h2>Sensor Readings</h2>';
                        sensors.forEach(sensor => {
                            html += `<p>${sensor.name}: ${sensor.value} ${sensor.unit}</p>`;
                        });
                        document.getElementById('sensors').innerHTML = html;
                    }
                });
        }
        
        setInterval(updateSensors, 1000);  // Update every second
        updateSensors();  // Initial load
    </script>
</head>
<body>
    <h1>Hardware Dashboard</h1>
    <div id="sensors">Loading...</div>
</body>
</html>
```

## Support

If you encounter issues:

1. Check the [Troubleshooting Guide](TROUBLESHOOTING.md)
2. Review the [Configuration Guide](CONFIGURATION_GUIDE.md)
3. Check error logs: `curl http://device-ip/api/errors`
4. Verify hardware connections
5. Test with minimal configuration

## Resources

- [Full Documentation](README.md)
- [Configuration Guide](CONFIGURATION_GUIDE.md)
- [Troubleshooting Guide](TROUBLESHOOTING.md)
- [API Reference](../web/api.h)
- [Example Configurations](hardware_config_example.json)