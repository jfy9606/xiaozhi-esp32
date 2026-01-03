#pragma once

/**
 * Camera Components - Reusable Camera Component Interfaces
 * 
 * This header provides a unified interface for all camera-related components
 * that can be used across different board implementations.
 */

// Core camera components
#include "camera_resource_manager.h"
#include "vision_integration.h"
#include "mcp_camera_tools.h"

// Base camera interfaces
#include "../boards/common/camera.h"
#include "../boards/common/esp32_camera.h"

// Vision system
#include "../vision/vision.h"

/**
 * Camera Component Factory
 * 
 * Factory class for creating and configuring camera components
 * with proper integration between all subsystems.
 */
class CameraComponentFactory {
public:
    struct CameraSystemConfig {
        // Enhanced camera configuration
        enhanced_camera_config_t enhanced_config;
        
        // Resource management
        bool enable_resource_management;
        
        // Vision integration
        bool enable_vision_integration;
        bool auto_start_vision;
        
        // MCP tools
        bool enable_mcp_tools;
        
        // Web integration
        bool enable_web_integration;
    };
    
    // Factory methods
    static Esp32Camera* CreateEsp32Camera(
        const camera_config_t& camera_config,
        const enhanced_camera_config_t& enhanced_config
    );
    
    static bool InitializeCameraSystem(
        const camera_config_t& camera_config,
        const CameraSystemConfig& system_config,
        Web* webserver = nullptr,
        McpServer* mcp_server = nullptr
    );
    
    static void DeinitializeCameraSystem();
    
    // Component access
    static CameraResourceManager* GetResourceManager();
    static VisionIntegration* GetVisionIntegration();
    static McpCameraTools* GetMcpTools();
    static Esp32Camera* GetEsp32Camera();
    
    // System status
    static bool IsCameraSystemInitialized();
    static std::string GetSystemStatusJson();

private:
    static bool system_initialized_;
    static Esp32Camera* esp32_camera_;
    static CameraSystemConfig system_config_;
};

/**
 * Camera System Helper Functions
 * 
 * Utility functions for common camera system operations.
 */
namespace CameraSystemHelpers {
    // Configuration helpers
    enhanced_camera_config_t CreateDefaultEnhancedConfig();
    CameraComponentFactory::CameraSystemConfig CreateDefaultSystemConfig();
    
    // Board-specific helpers
    camera_config_t CreateS3CamConfig();
    enhanced_camera_config_t CreateS3CamEnhancedConfig();
    
    // Integration helpers
    bool SetupCameraForBoard(const char* board_name, Web* webserver = nullptr, McpServer* mcp_server = nullptr);
    bool EnableCameraWithVision(bool enable);
    bool SwitchCameraState(bool enabled);
    
    // Status helpers
    bool IsCameraAvailable();
    bool IsVisionActive();
    resource_state_t GetResourceState();
}