#pragma once

#include "../mcp_server.h"
#include "../boards/common/camera.h"
#include "camera_resource_manager.h"
#include "enhanced_esp32_camera.h"

/**
 * MCP Camera Control Tools
 * 
 * Provides AI control interface for camera operations through MCP protocol.
 * Includes tools for photo capture, parameter control, flash control, and camera switching.
 */

class McpCameraTools {
public:
    static McpCameraTools& GetInstance();
    
    // Initialization
    bool Initialize(McpServer* mcp_server);
    void Deinitialize();
    bool IsInitialized() const;
    
    // Camera management
    bool SetCamera(Camera* camera);
    Camera* GetCamera() const;
    bool SetEnhancedCamera(EnhancedEsp32Camera* camera);
    EnhancedEsp32Camera* GetEnhancedCamera() const;
    
    // Resource management integration
    bool EnableResourceManagement();
    void DisableResourceManagement();
    bool IsResourceManaged() const;
    
    // Tool registration
    bool RegisterAllTools();
    void UnregisterAllTools();
    
    // Individual tool registration
    bool RegisterPhotoTool();
    bool RegisterFlashTool();
    bool RegisterConfigTool();
    bool RegisterSwitchTool();
    bool RegisterStatusTool();

private:
    McpCameraTools();
    ~McpCameraTools();
    
    // Prevent copying
    McpCameraTools(const McpCameraTools&) = delete;
    McpCameraTools& operator=(const McpCameraTools&) = delete;
    
    // Internal state
    bool initialized_;
    bool resource_managed_;
    
    // Components
    McpServer* mcp_server_;
    Camera* camera_;
    EnhancedEsp32Camera* enhanced_camera_;
    CameraResourceManager* resource_manager_;
    
    // Tool implementations
    static ReturnValue TakePhotoTool(const PropertyList& properties);
    static ReturnValue FlashControlTool(const PropertyList& properties);
    static ReturnValue ConfigControlTool(const PropertyList& properties);
    static ReturnValue SwitchControlTool(const PropertyList& properties);
    static ReturnValue StatusTool(const PropertyList& properties);
    
    // Helper methods
    static McpCameraTools* GetInstanceForTool();
    bool ValidateCameraAccess() const;
    bool ValidateResourceAccess() const;
    std::string GetCameraStatusJson() const;
};