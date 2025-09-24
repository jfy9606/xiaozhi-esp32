# Hardware Manager API Reference

This document provides a complete reference for the Hardware Manager API endpoints.

## Base URL

All API endpoints are relative to your device's base URL:
```
http://your-device-ip/api/
```

## Authentication

Currently, no authentication is required for API access.

## Response Format

All API responses follow this standard format:

```json
{
  "success": true,
  "timestamp": 1697654321000,
  "data": {
    // Response data here
  },
  "error": {
    "code": 0,
    "message": "Success"
  }
}
```

## Sensor Endpoints

### GET /api/sensors

Get data from all configured sensors.

**Response:**
```json
{
  "success": true,
  "timestamp": 1697654321000,
  "data": {
    "sensors": [
      {
        "sensor_id": "temperature_01",
        "name": "Environment Temperature",
        "type": "temperature",
        "value": 25.6,
        "unit": "°C",
        "timestamp": 1697654321000,
        "valid": true
      }
    ]
  }
}
```

### GET /api/sensors/{sensor_id}

Get data from a specific sensor.

**Parameters:**
- `sensor_id` (path): The sensor identifier

**Response:**
```json
{
  "success": true,
  "timestamp": 1697654321000,
  "data": {
    "sensor_id": "temperature_01",
    "name": "Environment Temperature",
    "type": "temperature",
    "value": 25.6,
    "unit": "°C",
    "timestamp": 1697654321000,
    "valid": true
  }
}
```

## Motor Control Endpoints

### POST /api/motors/control

Control a motor's speed and direction.

**Request Body:**
```json
{
  "motor_id": 0,
  "speed": 128
}
```

**Parameters:**
- `motor_id` (integer): Motor identifier (0-based)
- `speed` (integer): Speed value (-255 to 255, negative for reverse)

**Response:**
```json
{
  "success": true,
  "timestamp": 1697654321000,
  "data": {
    "motor_id": 0,
    "speed": 128,
    "status": "success"
  }
}
```

## Servo Control Endpoints

### POST /api/servos/control

Control a servo's angle.

**Request Body:**
```json
{
  "servo_id": 0,
  "angle": 90
}
```

**Parameters:**
- `servo_id` (integer): Servo identifier (0-based)
- `angle` (integer): Angle in degrees (0-180)

**Response:**
```json
{
  "success": true,
  "timestamp": 1697654321000,
  "data": {
    "servo_id": 0,
    "angle": 90,
    "status": "success"
  }
}
```

## Hardware Status Endpoints

### GET /api/hardware/status

Get overall hardware system status.

**Response:**
```json
{
  "success": true,
  "timestamp": 1697654321000,
  "data": {
    "hardware_manager": {
      "initialized": true,
      "version": "1.0.0"
    },
    "expanders": {
      "pca9548a": {
        "initialized": true,
        "channels": 8
      },
      "pcf8575": {
        "initialized": true,
        "gpio_count": 16
      },
      "lu9685": {
        "initialized": true,
        "pwm_channels": 16
      },
      "hw178": {
        "initialized": true,
        "analog_channels": 16
      }
    },
    "sensors": {
      "total": 2,
      "active": 2,
      "errors": 0
    },
    "actuators": {
      "motors": 2,
      "servos": 1,
      "active": 3
    }
  }
}
```

### GET /api/hardware/config

Get current hardware configuration.

**Response:**
```json
{
  "success": true,
  "timestamp": 1697654321000,
  "data": {
    "hardware": {
      "sensors": [
        {
          "id": "temperature_01",
          "name": "Environment Temperature",
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
          "name": "Left Wheel Motor",
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
          "name": "Pan Servo",
          "connection_type": "lu9685",
          "channel": 0,
          "min_angle": 0,
          "max_angle": 180,
          "center_angle": 90
        }
      ]
    }
  }
}
```

## Error Handling Endpoints

### GET /api/errors

Get system error logs.

**Query Parameters:**
- `component` (optional): Filter by component name
- `level` (optional): Filter by error level (info, warning, error)
- `limit` (optional): Maximum number of errors to return (default: 50)

**Response:**
```json
{
  "success": true,
  "timestamp": 1697654321000,
  "data": {
    "errors": [
      {
        "level": "error",
        "component": "HW178",
        "message": "Failed to read channel 0",
        "timestamp": 1697654321000
      }
    ],
    "statistics": {
      "total": 1,
      "info": 0,
      "warning": 0,
      "error": 1
    }
  }
}
```

### DELETE /api/errors

Clear all error logs.

**Response:**
```json
{
  "success": true,
  "timestamp": 1697654321000,
  "data": {
    "message": "Error logs cleared"
  }
}
```

## Error Codes

| Code | Description |
|------|-------------|
| 0    | Success |
| 400  | Bad Request - Invalid parameters |
| 404  | Not Found - Resource not found |
| 500  | Internal Server Error |
| 503  | Service Unavailable - Hardware manager not available |

## Rate Limiting

Currently, no rate limiting is implemented. However, it's recommended to:
- Limit sensor readings to once per second
- Avoid rapid motor/servo control commands
- Use WebSocket connections for real-time updates

## WebSocket Support

For real-time updates, consider using WebSocket connections:
- `/ws/sensors` - Real-time sensor data
- `/ws/hardware` - Hardware status updates

## Examples

### Read All Sensors
```bash
curl http://192.168.1.100/api/sensors
```

### Control Motor
```bash
curl -X POST http://192.168.1.100/api/motors/control \
  -H "Content-Type: application/json" \
  -d '{"motor_id": 0, "speed": 128}'
```

### Set Servo Angle
```bash
curl -X POST http://192.168.1.100/api/servos/control \
  -H "Content-Type: application/json" \
  -d '{"servo_id": 0, "angle": 90}'
```

### Check System Status
```bash
curl http://192.168.1.100/api/hardware/status
```

### Get Error Logs
```bash
curl http://192.168.1.100/api/errors
```

## Integration Examples

### JavaScript/Web
```javascript
// Read sensors
fetch('/api/sensors')
  .then(response => response.json())
  .then(data => {
    console.log('Sensors:', data.data.sensors);
  });

// Control motor
fetch('/api/motors/control', {
  method: 'POST',
  headers: {
    'Content-Type': 'application/json',
  },
  body: JSON.stringify({
    motor_id: 0,
    speed: 128
  })
})
.then(response => response.json())
.then(data => {
  console.log('Motor control result:', data);
});
```

### Python
```python
import requests

# Read sensors
response = requests.get('http://192.168.1.100/api/sensors')
sensors = response.json()['data']['sensors']

# Control motor
motor_data = {'motor_id': 0, 'speed': 128}
response = requests.post('http://192.168.1.100/api/motors/control', 
                        json=motor_data)
result = response.json()
```

### cURL Scripts
```bash
#!/bin/bash
DEVICE_IP="192.168.1.100"

# Function to read sensors
read_sensors() {
  curl -s "http://$DEVICE_IP/api/sensors" | jq '.data.sensors'
}

# Function to control motor
control_motor() {
  local motor_id=$1
  local speed=$2
  curl -s -X POST "http://$DEVICE_IP/api/motors/control" \
    -H "Content-Type: application/json" \
    -d "{\"motor_id\": $motor_id, \"speed\": $speed}"
}

# Usage
read_sensors
control_motor 0 128
```