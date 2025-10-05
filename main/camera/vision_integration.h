#pragma once

#include "../vision/vision.h"
#include "../web/web.h"
#include "../boards/common/camera.h"
#include "camera_resource_manager.h"

/**
 * Vision Integration Component
 * 
 * Integrates advanced vision processing with camera system and resource management.
 * Provides seamless integration between camera, vision, and web server components.
 */

class VisionIntegration {
public:
    static VisionIntegration& GetInstance();
    
    // Initialization and lifecycle
    bool Initialize(Web* webserver = nullptr);
    void Deinitialize();
    bool IsInitialized() const;
    
    // Vision control
    bool EnableVision();
    void DisableVision();
    bool IsVisionActive() const;
    
    // Camera integration
    bool SetCamera(Camera* camera);
    Camera* GetCamera() const;
    bool IsCameraAvailable() const;
    
    // Web server integration
    bool SetWebServer(Web* webserver);
    Web* GetWebServer() const;
    
    // Resource management integration
    bool EnableResourceManagement();
    void DisableResourceManagement();
    bool IsResourceManaged() const;
    
    // Vision component access
    Vision* GetVisionComponent() const;
    
    // State management
    bool HandleCameraStateChange(bool camera_enabled);
    void UpdateVisionState();
    
    // Configuration
    struct Config {
        bool auto_start_vision;
        bool resource_managed;
        bool web_integration;
        int vision_priority;
    };
    
    bool SetConfig(const Config& config);
    Config GetConfig() const;

private:
    VisionIntegration();
    ~VisionIntegration();
    
    // Prevent copying
    VisionIntegration(const VisionIntegration&) = delete;
    VisionIntegration& operator=(const VisionIntegration&) = delete;
    
    // Internal state
    bool initialized_;
    bool vision_active_;
    bool resource_managed_;
    
    // Components
    Vision* vision_component_;
    Camera* camera_;
    Web* webserver_;
    CameraResourceManager* resource_manager_;
    
    // Configuration
    Config config_;
    
    // Internal methods
    bool CreateVisionComponent();
    void DestroyVisionComponent();
    bool RegisterWebHandlers();
    void UnregisterWebHandlers();
    bool StartVisionWithCamera();
    void StopVisionSafely();
};