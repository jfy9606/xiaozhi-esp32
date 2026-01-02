# Hardware Configuration Guide

This guide explains how to configure the hardware management system for different sensor and actuator setups.

## Configuration File Structure

The hardware configuration uses JSON format with the following structure:

```json
{
  "hardware": {
    "sensors": [
      // Sensor definitions
    ],
    "motors": [
      // Motor definitions
    ],
    "servos": [
      // Servo definitions
    ]
  }
}
```

## Sensor Configuration

### Basic Sensor Configuration

```json
{
  "id": "unique_sensor_id",
  "name": "Human Readable Name",
  "type": "sensor_type",
  "multiplexer": "hw178",
  "channel": 0,
  "unit": "measurement_unit",
  "calibration": {
    "offset": 0.0,
    "scale": 1.0
  }
}
```

### Supported Sensor Types

| Type | Description | Typical Unit |
|------|-------------|--------------|
| `temperature` | Temperature sensors | °C, °F |
| `voltage` | Voltage measurement | V, mV |
| `current` | Current measurement | A, mA |
| `light` | Light/illuminance | lux, % |
| `pressure` | Pressure sensors | Pa, bar, psi |
| `humidity` | Humidity sensors | % |
| `distance` | Distance sensors | cm, mm |
| `generic` | Generic analog | raw, custom |

### Calibration

The calibration system applies the formula: `calibrated_value = (raw_value * scale) + offset`

#### Examples:

**Voltage Divider (1:100 ratio)**:
```json
"calibration": {
  "offset": 0.0,
  "scale": 0.01
}
```

**Temperature Sensor (LM35, 10mV/°C)**:
```json
"calibration": {
  "offset": 0.0,
  "scale": 100.0
}
```

**Current Sensor (ACS712-5A, 185mV/A, 2.5V offset)**:
```json
"calibration": {
  "offset": -2.5,
  "scale": 5.405
}
```

### Complete Sensor Examples

**Temperature Sensor**:
```json
{
  "id": "temp_ambient",
  "name": "环境温度传感器",
  "type": "temperature",
  "multiplexer": "hw178",
  "channel": 0,
  "unit": "°C",
  "calibration": {
    "offset": 0.0,
    "scale": 100.0
  }
}
```

**Battery Voltage Monitor**:
```json
{
  "id": "battery_voltage",
  "name": "电池电压监测",
  "type": "voltage",
  "multiplexer": "hw178",
  "channel": 1,
  "unit": "V",
  "calibration": {
    "offset": 0.0,
    "scale": 0.01
  }
}
```

## Motor Configuration

### Basic Motor Configuration

```json
{
  "id": 0,
  "name": "Motor Name",
  "connection_type": "pcf8575",
  "pins": {
    "ena": 2,
    "in1": 0,
    "in2": 1
  },
  "max_speed": 255
}
```

### Connection Types

#### PCF8575 Connection
For motors connected through PCF8575 GPIO multiplexer:

```json
{
  "id": 0,
  "name": "左轮电机",
  "connection_type": "pcf8575",
  "pins": {
    "ena": 2,    // Enable pin (PWM speed control)
    "in1": 0,    // Direction pin 1
    "in2": 1     // Direction pin 2
  },
  "max_speed": 255
}
```

#### Direct Connection
For motors connected directly to ESP32 GPIO:

```json
{
  "id": 2,
  "name": "升降电机",
  "connection_type": "direct",
  "pins": {
    "ena": 18,   // GPIO 18 for enable/speed
    "in1": 19,   // GPIO 19 for direction 1
    "in2": 21    // GPIO 21 for direction 2
  },
  "max_speed": 200
}
```

### Motor Control Logic

- **Speed Range**: -255 to +255 (negative = reverse)
- **Enable Pin**: PWM for speed control (0-255)
- **Direction Pins**: 
  - Forward: IN1=HIGH, IN2=LOW
  - Reverse: IN1=LOW, IN2=HIGH
  - Brake: IN1=LOW, IN2=LOW

## Servo Configuration

### Basic Servo Configuration

```json
{
  "id": 0,
  "name": "Servo Name",
  "connection_type": "lu9685",
  "channel": 0,
  "min_angle": 0,
  "max_angle": 180,
  "center_angle": 90
}
```

### Connection Types

#### LU9685 Connection
For servos connected through LU9685 PWM controller:

```json
{
  "id": 0,
  "name": "云台水平舵机",
  "connection_type": "lu9685",
  "channel": 0,
  "min_angle": 0,
  "max_angle": 180,
  "center_angle": 90
}
```

#### Direct Connection
For servos connected directly to ESP32 PWM:

```json
{
  "id": 2,
  "name": "机械臂舵机",
  "connection_type": "direct",
  "channel": 22,        // GPIO 22
  "min_angle": 0,
  "max_angle": 270,
  "center_angle": 135
}
```

### Servo Parameters

- **min_angle**: Minimum allowed angle (typically 0°)
- **max_angle**: Maximum allowed angle (typically 180° or 270°)
- **center_angle**: Default/center position
- **channel**: PWM channel (0-15 for LU9685, GPIO pin for direct)

## Complete Configuration Examples

### Small Robot Configuration

```json
{
  "hardware": {
    "sensors": [
      {
        "id": "temp_01",
        "name": "CPU温度",
        "type": "temperature",
        "multiplexer": "hw178",
        "channel": 0,
        "unit": "°C",
        "calibration": {"offset": 0.0, "scale": 1.0}
      },
      {
        "id": "battery_voltage",
        "name": "电池电压",
        "type": "voltage",
        "multiplexer": "hw178",
        "channel": 1,
        "unit": "V",
        "calibration": {"offset": 0.0, "scale": 0.01}
      }
    ],
    "motors": [
      {
        "id": 0,
        "name": "左轮电机",
        "connection_type": "pcf8575",
        "pins": {"ena": 2, "in1": 0, "in2": 1},
        "max_speed": 255
      },
      {
        "id": 1,
        "name": "右轮电机",
        "connection_type": "pcf8575",
        "pins": {"ena": 5, "in1": 3, "in2": 4},
        "max_speed": 255
      }
    ],
    "servos": [
      {
        "id": 0,
        "name": "云台舵机",
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

### Sensor Station Configuration

```json
{
  "hardware": {
    "sensors": [
      {
        "id": "temp_ambient",
        "name": "环境温度",
        "type": "temperature",
        "multiplexer": "hw178",
        "channel": 0,
        "unit": "°C",
        "calibration": {"offset": 0.0, "scale": 100.0}
      },
      {
        "id": "humidity",
        "name": "环境湿度",
        "type": "humidity",
        "multiplexer": "hw178",
        "channel": 1,
        "unit": "%",
        "calibration": {"offset": 0.0, "scale": 1.0}
      },
      {
        "id": "light_level",
        "name": "光照强度",
        "type": "light",
        "multiplexer": "hw178",
        "channel": 2,
        "unit": "lux",
        "calibration": {"offset": 0.0, "scale": 10.0}
      },
      {
        "id": "pressure",
        "name": "大气压力",
        "type": "pressure",
        "multiplexer": "hw178",
        "channel": 3,
        "unit": "Pa",
        "calibration": {"offset": 0.0, "scale": 1000.0}
      }
    ],
    "motors": [],
    "servos": []
  }
}
```

## Configuration Validation

The system validates configurations for:

### Sensor Validation
- ✅ Unique sensor IDs
- ✅ Valid multiplexer types
- ✅ Channel range (0-15 for HW178)
- ✅ Supported sensor types
- ✅ Valid calibration values

### Motor Validation
- ✅ Unique motor IDs
- ✅ Valid connection types
- ✅ Pin range validation
- ✅ No pin conflicts
- ✅ Valid speed limits

### Servo Validation
- ✅ Unique servo IDs
- ✅ Valid connection types
- ✅ Channel range validation
- ✅ Valid angle ranges
- ✅ Center angle within limits

## Troubleshooting Configuration

### Common Configuration Errors

1. **Duplicate IDs**
   ```
   Error: Sensor ID 'temp_01' already exists
   ```
   Solution: Use unique IDs for all devices

2. **Invalid Channel**
   ```
   Error: HW178 channel 16 out of range (0-15)
   ```
   Solution: Use valid channel numbers

3. **Pin Conflicts**
   ```
   Error: Pin 2 used by multiple motors
   ```
   Solution: Assign unique pins to each device

4. **Invalid Calibration**
   ```
   Error: Scale factor cannot be zero
   ```
   Solution: Use non-zero scale values

### Configuration Testing

Use the API to test configurations:

```bash
# Test sensor reading
curl http://device-ip/api/sensors/temp_01

# Test motor control
curl -X POST http://device-ip/api/motors/control \
  -H "Content-Type: application/json" \
  -d '{"motor_id": 0, "speed": 100}'

# Test servo control
curl -X POST http://device-ip/api/servos/control \
  -H "Content-Type: application/json" \
  -d '{"servo_id": 0, "angle": 90}'
```

## Best Practices

1. **Use Descriptive Names**: Make device names clear and descriptive
2. **Document Calibration**: Comment calibration formulas
3. **Test Incrementally**: Add devices one at a time
4. **Backup Configurations**: Keep working configurations backed up
5. **Use Version Control**: Track configuration changes
6. **Validate Before Deploy**: Test configurations thoroughly

## Advanced Configuration

### Custom Sensor Types

To add custom sensor types:

1. Add type to `IsSensorTypeSupported()` in hardware_manager.cc
2. Implement reading logic in `ReadHW178Sensor()`
3. Add calibration support if needed
4. Update documentation

### Multiple Multiplexer Support

For systems with multiple multiplexers:

```json
{
  "sensors": [
    {
      "id": "temp_board1",
      "multiplexer": "hw178_1",
      "channel": 0
    },
    {
      "id": "temp_board2", 
      "multiplexer": "hw178_2",
      "channel": 0
    }
  ]
}
```

Note: Multiple multiplexer support requires code modifications.