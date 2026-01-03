#pragma once

#include "../mcp_server.h"
#include "../boards/common/camera.h"
#include "../boards/common/esp32_camera.h"
#include "camera_resource_manager.h"

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
    bool SetEsp32Camera(Esp32Camera* camera);
    Esp32Camera* GetEsp32Camera() const;
    
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
    Esp32Camera* esp32_camera_;
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