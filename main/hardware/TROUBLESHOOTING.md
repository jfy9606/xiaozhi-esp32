# Hardware Manager Troubleshooting Guide

This guide helps diagnose and resolve common issues with the hardware management system.

## Quick Diagnostics

### Check System Status

```bash
# Get overall hardware status
curl http://device-ip/api/hardware/status

# Check for errors
curl http://device-ip/api/errors

# Get specific component errors
curl http://device-ip/api/errors?component=HW178
```

### Check Configuration

```bash
# Validate current configuration
curl http://device-ip/api/hardware/config
```

## Common Issues

### 1. Multiplexer Initialization Failed

**Symptoms:**
- Error: "Multiplexer initialization failed"
- No sensor readings available
- Hardware status shows expanders as unavailable

**Possible Causes:**
- I2C bus not properly initialized
- PCA9548A not connected or powered
- I2C address conflicts
- Faulty I2C connections

**Solutions:**

1. **Check I2C Connections**
   ```
   SDA: GPIO 21 (or board-specific)
   SCL: GPIO 22 (or board-specific)
   VCC: 3.3V or 5V (check PCA9548A specs)
   GND: Ground
   ```

2. **Verify Power Supply**
   - Check PCA9548A power LED (if available)
   - Measure voltage at VCC pin (should be 3.3V or 5V)
   - Ensure adequate current supply

3. **Test I2C Communication**
   ```bash
   # Use i2cdetect if available
   i2cdetect -y 1
   ```

4. **Check I2C Address**
   - Default PCA9548A address: 0x70
   - Verify address jumpers/configuration

**Recovery Steps:**
1. Power cycle the device
2. Check all I2C connections
3. Try different I2C address if configurable
4. Replace PCA9548A module if faulty

### 2. Sensor Reading Errors

**Symptoms:**
- Sensor readings return invalid/zero values
- Error: "Failed to read sensor data"
- Inconsistent sensor values

**Possible Causes:**
- HW178 not properly connected
- Wrong channel configuration
- Sensor not connected or faulty
- Incorrect calibration settings

**Solutions:**

1. **Verify HW178 Connection**
   ```
   Check I2C connection through PCA9548A
   Verify HW178 power supply
   Check channel selection signals
   ```

2. **Test Individual Channels**
   ```bash
   # Test specific sensor
   curl http://device-ip/api/sensors/temperature_01
   ```

3. **Check Sensor Connections**
   - Verify sensor power supply
   - Check analog signal connections
   - Test sensor with multimeter

4. **Validate Configuration**
   ```json
   {
     "channel": 0,  // Must be 0-15 for HW178
     "calibration": {
       "offset": 0.0,
       "scale": 1.0  // Must not be zero
     }
   }
   ```

**Recovery Steps:**
1. Test with known good sensor
2. Try different HW178 channel
3. Reset calibration to defaults
4. Check for loose connections

### 3. Motor Control Issues

**Symptoms:**
- Motors don't respond to commands
- Error: "Failed to control motor"
- Motors run at wrong speed/direction

**Possible Causes:**
- PCF8575 not properly connected
- Wrong pin configuration
- Motor driver issues
- Insufficient power supply

**Solutions:**

1. **Verify PCF8575 Connection**
   ```
   Check I2C connection through PCA9548A
   Verify PCF8575 power supply
   Test GPIO output with LED
   ```

2. **Check Motor Driver Connections**
   ```
   ENA pin: PWM speed control
   IN1/IN2: Direction control
   Motor power: Adequate voltage/current
   ```

3. **Test Motor Driver**
   ```bash
   # Test motor control
   curl -X POST http://device-ip/api/motors/control \
     -H "Content-Type: application/json" \
     -d '{"motor_id": 0, "speed": 50}'
   ```

4. **Validate Pin Configuration**
   ```json
   {
     "pins": {
       "ena": 2,  // Must be 0-15 for PCF8575
       "in1": 0,  // Must be unique
       "in2": 1   // Must be unique
     }
   }
   ```

**Recovery Steps:**
1. Test with multimeter on driver pins
2. Try different PCF8575 pins
3. Check motor driver datasheet
4. Verify motor power supply

### 4. Servo Control Issues

**Symptoms:**
- Servos don't move to commanded position
- Error: "Failed to control servo"
- Servo jitters or moves erratically

**Possible Causes:**
- LU9685 not properly connected
- Wrong PWM channel configuration
- Servo power supply issues
- Invalid angle commands

**Solutions:**

1. **Verify LU9685 Connection**
   ```
   Check I2C connection through PCA9548A
   Verify LU9685 power supply
   Check PWM output with oscilloscope
   ```

2. **Check Servo Power Supply**
   ```
   Servo VCC: Usually 5V or 6V
   Current: Adequate for servo load
   Ground: Common ground with controller
   ```

3. **Test Servo Control**
   ```bash
   # Test servo movement
   curl -X POST http://device-ip/api/servos/control \
     -H "Content-Type: application/json" \
     -d '{"servo_id": 0, "angle": 90}'
   ```

4. **Validate Angle Limits**
   ```json
   {
     "min_angle": 0,    // Must be >= 0
     "max_angle": 180,  // Must be > min_angle
     "center_angle": 90 // Must be between min/max
   }
   ```

**Recovery Steps:**
1. Test servo with known good signal
2. Try different LU9685 channel
3. Check servo specifications
4. Verify PWM signal timing

### 5. Configuration Loading Errors

**Symptoms:**
- Error: "Failed to load configuration"
- Error: "Configuration validation failed"
- Default configuration used instead

**Possible Causes:**
- Configuration file not found
- Invalid JSON syntax
- Configuration validation errors
- File system issues

**Solutions:**

1. **Check File Existence**
   ```bash
   # List files in SPIFFS
   ls /spiffs/
   ```

2. **Validate JSON Syntax**
   ```bash
   # Use online JSON validator or:
   cat hardware_config.json | python -m json.tool
   ```

3. **Check Configuration Structure**
   ```json
   {
     "hardware": {
       "sensors": [...],
       "motors": [...],
       "servos": [...]
     }
   }
   ```

4. **Review Validation Errors**
   ```bash
   # Check error logs
   curl http://device-ip/api/errors?component=HardwareManager
   ```

**Recovery Steps:**
1. Use example configuration as template
2. Add devices incrementally
3. Validate each section separately
4. Check for duplicate IDs

## Error Codes and Messages

### ESP Error Codes

| Code | Name | Description | Solution |
|------|------|-------------|----------|
| `ESP_OK` | Success | Operation completed successfully | - |
| `ESP_ERR_NOT_FOUND` | Not Found | Configuration file or device not found | Check file path and device connections |
| `ESP_ERR_INVALID_ARG` | Invalid Argument | Invalid parameter or configuration | Validate input parameters |
| `ESP_ERR_NO_MEM` | No Memory | Insufficient memory | Reduce configuration size or restart |
| `ESP_ERR_TIMEOUT` | Timeout | Communication timeout | Check connections and power |

### Hardware Manager Error Messages

| Message | Cause | Solution |
|---------|-------|----------|
| "Multiplexer not initialized" | PCA9548A initialization failed | Check I2C connections and power |
| "Invalid sensor type" | Unsupported sensor type | Use supported sensor types |
| "Channel out of range" | Invalid expander channel | Use valid channel numbers (0-15) |
| "Pin conflict detected" | Multiple devices using same pin | Assign unique pins |
| "Calibration scale cannot be zero" | Invalid calibration | Use non-zero scale values |

## Diagnostic Tools

### Hardware Status Check

```cpp
// Get hardware manager status
if (hardware_manager->IsInitialized()) {
    ESP_LOGI(TAG, "Hardware manager is running");
    
    // Get actuator status
    auto status = hardware_manager->GetActuatorStatus();
    for (const auto& actuator : status) {
        ESP_LOGI(TAG, "Actuator %s: %s", 
                 actuator.actuator_id.c_str(),
                 actuator.enabled ? "enabled" : "disabled");
    }
}
```

### Sensor Validation

```cpp
// Test sensor reading
auto reading = hardware_manager->ReadSensor("temperature_01");
if (reading.valid) {
    ESP_LOGI(TAG, "Sensor OK: %.2f %s", reading.value, reading.unit.c_str());
} else {
    ESP_LOGE(TAG, "Sensor reading failed");
}
```

### Error Log Analysis

```bash
# Get recent errors
curl http://device-ip/api/errors | jq '.data.errors'

# Get error statistics
curl http://device-ip/api/errors | jq '.data.statistics'

# Clear errors after fixing
curl -X DELETE http://device-ip/api/errors
```

## Performance Issues

### Slow Sensor Reading

**Symptoms:**
- Sensor readings take too long
- System appears unresponsive

**Solutions:**
1. Reduce retry count in sensor reading
2. Optimize I2C bus speed
3. Read sensors in parallel
4. Cache frequently accessed readings

### Memory Issues

**Symptoms:**
- System crashes or restarts
- Error: "No memory available"

**Solutions:**
1. Reduce error log size
2. Optimize configuration size
3. Free unused resources
4. Monitor heap usage

### I2C Bus Conflicts

**Symptoms:**
- Intermittent communication errors
- Device initialization failures

**Solutions:**
1. Use proper I2C pull-up resistors
2. Reduce I2C bus speed
3. Check for address conflicts
4. Minimize bus length

## Recovery Procedures

### Soft Recovery

1. **Clear Errors**
   ```bash
   curl -X DELETE http://device-ip/api/errors
   ```

2. **Reload Configuration**
   ```bash
   curl -X POST http://device-ip/api/hardware/config \
     -H "Content-Type: application/json" \
     -d @hardware_config.json
   ```

3. **Restart Hardware Manager**
   ```cpp
   hardware_manager->Stop();
   hardware_manager->Initialize();
   hardware_manager->LoadConfiguration("/spiffs/hardware_config.json");
   ```

### Hard Recovery

1. **System Restart**
   ```bash
   curl -X POST http://device-ip/api/system/restart
   ```

2. **Factory Reset Configuration**
   ```cpp
   hardware_manager->CreateDefaultConfiguration("/spiffs/hardware_config.json");
   ```

3. **Hardware Reset**
   - Power cycle the entire system
   - Check all physical connections
   - Verify power supplies

## Prevention

### Best Practices

1. **Regular Monitoring**
   - Check error logs daily
   - Monitor system performance
   - Validate sensor readings

2. **Configuration Management**
   - Keep backup configurations
   - Version control changes
   - Test before deployment

3. **Hardware Maintenance**
   - Check connections regularly
   - Monitor power supplies
   - Replace aging components

4. **Documentation**
   - Document all changes
   - Keep wiring diagrams updated
   - Record calibration procedures

### Monitoring Setup

```cpp
// Set up periodic health checks
void hardware_health_check() {
    // Check error count
    if (SimpleErrorHandler::GetErrorCount(SimpleErrorHandler::ErrorLevel::CRITICAL) > 0) {
        ESP_LOGW(TAG, "Critical errors detected");
        // Take corrective action
    }
    
    // Check sensor validity
    auto readings = hardware_manager->ReadAllSensors();
    int invalid_count = 0;
    for (const auto& reading : readings) {
        if (!reading.valid) invalid_count++;
    }
    
    if (invalid_count > readings.size() / 2) {
        ESP_LOGW(TAG, "Many sensors failing, checking hardware");
        // Investigate hardware issues
    }
}
```

## Getting Help

### Log Information to Collect

When reporting issues, include:

1. **System Information**
   ```bash
   curl http://device-ip/api/system/info
   ```

2. **Hardware Status**
   ```bash
   curl http://device-ip/api/hardware/status
   ```

3. **Error Logs**
   ```bash
   curl http://device-ip/api/errors
   ```

4. **Configuration**
   ```bash
   curl http://device-ip/api/hardware/config
   ```

5. **ESP32 Logs**
   - Serial monitor output
   - Core dump if available
   - Memory usage information

### Support Resources

- Hardware Manager Documentation
- ESP-IDF Documentation
- Component Datasheets
- Community Forums
- Issue Tracker