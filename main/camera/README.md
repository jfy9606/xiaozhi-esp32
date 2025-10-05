# Camera Components

This directory contains reusable camera components extracted for migration from bread-compact-wifi and bread-compact-wifi-lcd boards to bread-compact-wifi-s3cam board.

## Components Overview

### 1. Camera Resource Manager (`camera_resource_manager.h/cc`)
- **Purpose**: Manages shared resources between camera and audio systems
- **Features**:
  - Resource conflict prevention
  - Camera switch functionality (enable/disable)
  - Thread-safe resource locking
  - Graceful resource state transitions
  - Resource state monitoring

### 2. Enhanced ESP32 Camera (`enhanced_esp32_camera.h/cc`)
- **Purpose**: Extended camera driver with multi-model support
- **Features**:
  - Support for OV2640, OV3660, OV5640 camera models
  - Auto-detection capability
  - Resource management integration
  - Enhanced flash control with PWM
  - Model-specific optimizations
  - Configuration management

### 3. Vision Integration (`vision_integration.h/cc`)
- **Purpose**: Integrates vision processing with camera system
- **Features**:
  - Seamless camera-vision integration
  - Web server integration
  - Resource management integration
  - Automatic vision state management
  - Configuration management

### 4. MCP Camera Tools (`mcp_camera_tools.h/cc`)
- **Purpose**: AI control interface for camera operations
- **Features**:
  - Photo capture tool (`self.camera.take_photo`)
  - Flash control tool (`self.camera.flash`)
  - Parameter control tool (`self.camera.set_config`)
  - Camera switch tool (`self.camera.switch`)
  - Status reporting tool (`self.camera.get_status`)

### 5. Camera Components Factory (`camera_components.h/cc`)
- **Purpose**: Unified interface and factory for camera system
- **Features**:
  - Component factory methods
  - System initialization/deinitialization
  - Component access methods
  - Helper functions for common operations
  - System status reporting

## Usage

### Basic Setup

```cpp
#include "camera/camera_components.h"

// Create camera configuration
camera_config_t camera_config = CameraSystemHelpers::CreateS3CamConfig();
CameraComponentFactory::CameraSystemConfig system_config = 
    CameraSystemHelpers::CreateDefaultSystemConfig();

// Initialize camera system
bool success = CameraComponentFactory::InitializeCameraSystem(
    camera_config, system_config, webserver, mcp_server);
```

### Board-Specific Setup

```cpp
// Setup camera for specific board
bool success = CameraSystemHelpers::SetupCameraForBoard(
    "bread-compact-wifi-s3cam", webserver, mcp_server);
```

### Resource Management

```cpp
// Get resource manager
auto* resource_manager = CameraComponentFactory::GetResourceManager();

// Enable camera
resource_manager->SetCameraEnabled(true);

// Check resource state
resource_state_t state = resource_manager->GetResourceState();
```

### Vision Integration

```cpp
// Get vision integration
auto* vision_integration = CameraComponentFactory::GetVisionIntegration();

// Enable vision
vision_integration->EnableVision();

// Check if vision is active
bool active = vision_integration->IsVisionActive();
```

### MCP Tools

```cpp
// Get MCP tools
auto* mcp_tools = CameraComponentFactory::GetMcpTools();

// Tools are automatically registered with MCP server
// AI can now use:
// - self.camera.take_photo
// - self.camera.flash
// - self.camera.set_config
// - self.camera.switch
// - self.camera.get_status
```

## Configuration

### Enhanced Camera Configuration

```cpp
enhanced_camera_config_t config = {};
config.model = CAMERA_MODEL_OV2640;
config.auto_detect = true;
config.resource_managed = true;
config.vision_enabled = true;
config.flash_pin = GPIO_NUM_4;
config.flash_level = 0;
```

### System Configuration

```cpp
CameraComponentFactory::CameraSystemConfig system_config = {};
system_config.enhanced_config = enhanced_config;
system_config.enable_resource_management = true;
system_config.enable_vision_integration = true;
system_config.auto_start_vision = true;
system_config.enable_mcp_tools = true;
system_config.enable_web_integration = true;
```

## Integration with Boards

### For S3Cam Board

```cpp
class CompactWifiBoardS3Cam : public WifiBoard {
private:
    EnhancedEsp32Camera* enhanced_camera_;
    
public:
    CompactWifiBoardS3Cam() {
        // Initialize camera system
        CameraSystemHelpers::SetupCameraForBoard(
            "bread-compact-wifi-s3cam", GetWebServer(), GetMcpServer());
    }
    
    virtual Camera* GetCamera() override {
        return CameraComponentFactory::enhanced_camera_;
    }
};
```

## Dependencies

- ESP-IDF camera driver (`esp_camera.h`)
- FreeRTOS (for mutexes and tasks)
- cJSON (for status reporting)
- Vision system (`../vision/vision.h`)
- MCP server (`../mcp_server.h`)
- Web server (`../web/web.h`)

## Thread Safety

All components are designed to be thread-safe:
- Resource manager uses FreeRTOS mutexes
- Camera operations are protected by resource locks
- Vision integration handles concurrent access safely
- MCP tools validate resource access before operations

## Error Handling

Components implement comprehensive error handling:
- Resource conflicts are detected and reported
- Camera initialization failures are handled gracefully
- Vision integration failures don't affect camera operation
- MCP tools validate parameters and camera availability

## Status Monitoring

System provides detailed status information:
- Resource state monitoring
- Camera initialization status
- Vision integration status
- Component availability status
- Configuration status

Use `CameraComponentFactory::GetSystemStatusJson()` for complete system status.