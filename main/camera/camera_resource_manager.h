#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_log.h>

/**
 * Camera Resource Manager
 * 
 * Manages shared resources between camera and audio systems to prevent conflicts.
 * Provides camera switch functionality with proper resource locking.
 */

typedef enum {
    RESOURCE_IDLE = 0,
    RESOURCE_AUDIO_ACTIVE,
    RESOURCE_CAMERA_ACTIVE,
    RESOURCE_SWITCHING
} resource_state_t;

typedef struct {
    bool enabled;
    bool initialized;
    resource_state_t resource_state;
    const char* detected_model;
} camera_switch_state_t;

class CameraResourceManager {
public:
    static CameraResourceManager& GetInstance();
    
    // Resource management
    bool LockResourceForCamera();
    bool LockResourceForAudio();
    void ReleaseResource();
    resource_state_t GetResourceState() const;
    
    // Camera switch functionality
    bool IsCameraEnabled() const;
    bool SetCameraEnabled(bool enabled);
    bool IsCameraInitialized() const;
    void SetCameraInitialized(bool initialized);
    
    // State management
    camera_switch_state_t GetSwitchState() const;
    void SetDetectedModel(const char* model);
    
    // Initialization
    bool Initialize();
    void Deinitialize();

private:
    CameraResourceManager();
    ~CameraResourceManager();
    
    // Prevent copying
    CameraResourceManager(const CameraResourceManager&) = delete;
    CameraResourceManager& operator=(const CameraResourceManager&) = delete;
    
    // Internal state
    SemaphoreHandle_t resource_mutex_;
    camera_switch_state_t switch_state_;
    bool initialized_;
    
    // Internal methods
    bool TransitionToState(resource_state_t new_state);
    void ResetCameraPins();
    void ConfigureCameraPins();
};