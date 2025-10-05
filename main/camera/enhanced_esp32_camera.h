#pragma once

#include "../boards/common/esp32_camera.h"
#include "camera_resource_manager.h"
#include <esp_camera.h>

/**
 * Enhanced ESP32 Camera Driver
 * 
 * Extends the basic ESP32 camera with:
 * - Multi-model support (OV2640, OV3660, OV5640)
 * - Auto-detection capability
 * - Resource management integration
 * - Enhanced configuration options
 */

typedef enum {
    CAMERA_MODEL_UNKNOWN = 0,
    CAMERA_MODEL_OV2640,
    CAMERA_MODEL_OV3660,
    CAMERA_MODEL_OV5640
} camera_model_t;

typedef struct {
    camera_model_t model;
    bool auto_detect;
    bool resource_managed;
    bool vision_enabled;
    gpio_num_t flash_pin;
    int flash_level;
} enhanced_camera_config_t;

class EnhancedEsp32Camera : public Esp32Camera {
public:
    EnhancedEsp32Camera(const camera_config_t& config, const enhanced_camera_config_t& enhanced_config);
    virtual ~EnhancedEsp32Camera();

    // Enhanced functionality
    bool AutoDetectSensor();
    bool SetCameraModel(camera_model_t model);
    camera_model_t GetDetectedModel() const;
    const char* GetModelName(camera_model_t model) const;
    
    // Resource management integration
    bool EnableWithResourceManagement();
    void DisableWithResourceManagement();
    bool IsResourceManaged() const;
    
    // Enhanced camera control
    virtual bool Initialize() override;
    virtual void Deinitialize() override;
    virtual bool IsInitialized() const override;
    
    // Flash control with enhanced features
    virtual bool HasFlash() override;
    virtual bool SetFlashLevel(int level) override;
    virtual int GetFlashLevel() const;
    
    // Model-specific optimizations
    bool ApplyModelOptimizations();
    bool SetModelSpecificSettings(camera_model_t model);
    
    // Configuration management
    enhanced_camera_config_t GetEnhancedConfig() const;
    bool UpdateEnhancedConfig(const enhanced_camera_config_t& config);
    
    // Model support queries
    static bool IsModelSupported(camera_model_t model);
    static int GetSupportedModelsCount();
    static void GetSupportedModels(camera_model_t* models, int max_count);

private:
    enhanced_camera_config_t enhanced_config_;
    camera_model_t detected_model_;
    bool initialized_;
    CameraResourceManager* resource_manager_;
    
    // Model detection methods
    bool DetectOV2640();
    bool DetectOV3660();
    bool DetectOV5640();
    
    // Model-specific initialization
    bool InitializeOV2640();
    bool InitializeOV3660();
    bool InitializeOV5640();
    
    // Flash control
    void ConfigureFlashPin();
    void SetFlashState(bool on);
};