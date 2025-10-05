#include "camera_components.h"
#include <esp_log.h>
#include <cJSON.h>

#define TAG "CameraComponents"

// Static members
bool CameraComponentFactory::system_initialized_ = false;
EnhancedEsp32Camera* CameraComponentFactory::enhanced_camera_ = nullptr;
CameraComponentFactory::CameraSystemConfig CameraComponentFactory::system_config_ = {};

// Factory methods

EnhancedEsp32Camera* CameraComponentFactory::CreateEnhancedCamera(
    const camera_config_t& camera_config,
    const enhanced_camera_config_t& enhanced_config) {
    
    ESP_LOGI(TAG, "Creating enhanced camera");
    
    EnhancedEsp32Camera* camera = new EnhancedEsp32Camera(camera_config, enhanced_config);
    if (!camera) {
        ESP_LOGE(TAG, "Failed to create enhanced camera");
        return nullptr;
    }
    
    ESP_LOGI(TAG, "Enhanced camera created successfully");
    return camera;
}

bool CameraComponentFactory::InitializeCameraSystem(
    const camera_config_t& camera_config,
    const CameraSystemConfig& system_config,
    Web* webserver,
    McpServer* mcp_server) {
    
    if (system_initialized_) {
        ESP_LOGW(TAG, "Camera system already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing camera system");
    
    // Store configuration
    system_config_ = system_config;
    
    // Create enhanced camera
    enhanced_camera_ = CreateEnhancedCamera(camera_config, system_config.enhanced_config);
    if (!enhanced_camera_) {
        ESP_LOGE(TAG, "Failed to create enhanced camera");
        return false;
    }
    
    // Initialize resource management if enabled
    if (system_config.enable_resource_management) {
        auto& resource_manager = CameraResourceManager::GetInstance();
        if (!resource_manager.Initialize()) {
            ESP_LOGE(TAG, "Failed to initialize resource manager");
            delete enhanced_camera_;
            enhanced_camera_ = nullptr;
            return false;
        }
        ESP_LOGI(TAG, "Resource management initialized");
    }
    
    // Initialize vision integration if enabled
    if (system_config.enable_vision_integration) {
        auto& vision_integration = VisionIntegration::GetInstance();
        
        // Configure vision integration
        VisionIntegration::Config vision_config = vision_integration.GetConfig();
        vision_config.auto_start_vision = system_config.auto_start_vision;
        vision_config.resource_managed = system_config.enable_resource_management;
        vision_config.web_integration = system_config.enable_web_integration;
        vision_integration.SetConfig(vision_config);
        
        if (!vision_integration.Initialize(webserver)) {
            ESP_LOGE(TAG, "Failed to initialize vision integration");
            // Continue without vision integration
        } else {
            // Set camera for vision integration
            vision_integration.SetCamera(enhanced_camera_);
            ESP_LOGI(TAG, "Vision integration initialized");
        }
    }
    
    // Initialize MCP tools if enabled
    if (system_config.enable_mcp_tools && mcp_server) {
        auto& mcp_tools = McpCameraTools::GetInstance();
        if (!mcp_tools.Initialize(mcp_server)) {
            ESP_LOGE(TAG, "Failed to initialize MCP tools");
            // Continue without MCP tools
        } else {
            // Set camera and enable resource management if configured
            mcp_tools.SetEnhancedCamera(enhanced_camera_);
            if (system_config.enable_resource_management) {
                mcp_tools.EnableResourceManagement();
            }
            
            // Register all tools
            if (!mcp_tools.RegisterAllTools()) {
                ESP_LOGW(TAG, "Some MCP tools failed to register");
            }
            ESP_LOGI(TAG, "MCP camera tools initialized");
        }
    }
    
    // Initialize the camera
    if (!enhanced_camera_->Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize enhanced camera");
        DeinitializeCameraSystem();
        return false;
    }
    
    system_initialized_ = true;
    ESP_LOGI(TAG, "Camera system initialized successfully");
    return true;
}

void CameraComponentFactory::DeinitializeCameraSystem() {
    if (!system_initialized_) {
        return;
    }
    
    ESP_LOGI(TAG, "Deinitializing camera system");
    
    // Deinitialize MCP tools
    if (system_config_.enable_mcp_tools) {
        auto& mcp_tools = McpCameraTools::GetInstance();
        mcp_tools.Deinitialize();
    }
    
    // Deinitialize vision integration
    if (system_config_.enable_vision_integration) {
        auto& vision_integration = VisionIntegration::GetInstance();
        vision_integration.Deinitialize();
    }
    
    // Deinitialize enhanced camera
    if (enhanced_camera_) {
        enhanced_camera_->Deinitialize();
        delete enhanced_camera_;
        enhanced_camera_ = nullptr;
    }
    
    // Deinitialize resource management
    if (system_config_.enable_resource_management) {
        auto& resource_manager = CameraResourceManager::GetInstance();
        resource_manager.Deinitialize();
    }
    
    system_initialized_ = false;
    ESP_LOGI(TAG, "Camera system deinitialized");
}

// Component access

CameraResourceManager* CameraComponentFactory::GetResourceManager() {
    if (!system_config_.enable_resource_management) {
        return nullptr;
    }
    return &CameraResourceManager::GetInstance();
}

VisionIntegration* CameraComponentFactory::GetVisionIntegration() {
    if (!system_config_.enable_vision_integration) {
        return nullptr;
    }
    return &VisionIntegration::GetInstance();
}

McpCameraTools* CameraComponentFactory::GetMcpTools() {
    if (!system_config_.enable_mcp_tools) {
        return nullptr;
    }
    return &McpCameraTools::GetInstance();
}

EnhancedEsp32Camera* CameraComponentFactory::GetEnhancedCamera() {
    return enhanced_camera_;
}

// System status

bool CameraComponentFactory::IsCameraSystemInitialized() {
    return system_initialized_;
}

std::string CameraComponentFactory::GetSystemStatusJson() {
    cJSON* root = cJSON_CreateObject();
    
    cJSON_AddBoolToObject(root, "initialized", system_initialized_);
    cJSON_AddBoolToObject(root, "has_enhanced_camera", enhanced_camera_ != nullptr);
    
    // System configuration
    cJSON* config = cJSON_CreateObject();
    cJSON_AddBoolToObject(config, "resource_management", system_config_.enable_resource_management);
    cJSON_AddBoolToObject(config, "vision_integration", system_config_.enable_vision_integration);
    cJSON_AddBoolToObject(config, "mcp_tools", system_config_.enable_mcp_tools);
    cJSON_AddBoolToObject(config, "web_integration", system_config_.enable_web_integration);
    cJSON_AddItemToObject(root, "config", config);
    
    // Component status
    if (system_initialized_) {
        if (system_config_.enable_resource_management) {
            auto* rm = GetResourceManager();
            if (rm) {
                camera_switch_state_t state = rm->GetSwitchState();
                cJSON* resource_status = cJSON_CreateObject();
                cJSON_AddBoolToObject(resource_status, "enabled", state.enabled);
                cJSON_AddBoolToObject(resource_status, "initialized", state.initialized);
                cJSON_AddNumberToObject(resource_status, "resource_state", state.resource_state);
                cJSON_AddStringToObject(resource_status, "detected_model", state.detected_model);
                cJSON_AddItemToObject(root, "resource_manager", resource_status);
            }
        }
        
        if (system_config_.enable_vision_integration) {
            auto* vi = GetVisionIntegration();
            if (vi) {
                cJSON* vision_status = cJSON_CreateObject();
                cJSON_AddBoolToObject(vision_status, "initialized", vi->IsInitialized());
                cJSON_AddBoolToObject(vision_status, "active", vi->IsVisionActive());
                cJSON_AddBoolToObject(vision_status, "camera_available", vi->IsCameraAvailable());
                cJSON_AddItemToObject(root, "vision_integration", vision_status);
            }
        }
        
        if (enhanced_camera_) {
            cJSON* camera_status = cJSON_CreateObject();
            cJSON_AddBoolToObject(camera_status, "initialized", enhanced_camera_->IsInitialized());
            cJSON_AddStringToObject(camera_status, "model", enhanced_camera_->GetModelName(enhanced_camera_->GetDetectedModel()));
            cJSON_AddStringToObject(camera_status, "sensor", enhanced_camera_->GetSensorName());
            cJSON_AddBoolToObject(camera_status, "has_flash", enhanced_camera_->HasFlash());
            cJSON_AddItemToObject(root, "enhanced_camera", camera_status);
        }
    }
    
    char* json_str = cJSON_PrintUnformatted(root);
    std::string result(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(root);
    
    return result;
}

// Helper functions implementation

namespace CameraSystemHelpers {

enhanced_camera_config_t CreateDefaultEnhancedConfig() {
    enhanced_camera_config_t config = {};
    config.model = CAMERA_OV2640;
    config.auto_detect = true;
    config.resource_managed = true;
    config.vision_enabled = true;
    config.flash_pin = GPIO_NUM_NC;
    config.flash_level = 0;
    return config;
}

CameraComponentFactory::CameraSystemConfig CreateDefaultSystemConfig() {
    CameraComponentFactory::CameraSystemConfig config = {};
    config.enhanced_config = CreateDefaultEnhancedConfig();
    config.enable_resource_management = true;
    config.enable_vision_integration = true;
    config.auto_start_vision = true;
    config.enable_mcp_tools = true;
    config.enable_web_integration = true;
    return config;
}

camera_config_t CreateS3CamConfig() {
    camera_config_t config = {};
    
#ifdef CAMERA_PIN_D0
    config.pin_d0 = CAMERA_PIN_D0;
    config.pin_d1 = CAMERA_PIN_D1;
    config.pin_d2 = CAMERA_PIN_D2;
    config.pin_d3 = CAMERA_PIN_D3;
    config.pin_d4 = CAMERA_PIN_D4;
    config.pin_d5 = CAMERA_PIN_D5;
    config.pin_d6 = CAMERA_PIN_D6;
    config.pin_d7 = CAMERA_PIN_D7;
    config.pin_xclk = CAMERA_PIN_XCLK;
    config.pin_pclk = CAMERA_PIN_PCLK;
    config.pin_vsync = CAMERA_PIN_VSYNC;
    config.pin_href = CAMERA_PIN_HREF;
    config.pin_sccb_sda = CAMERA_PIN_SIOD;
    config.pin_sccb_scl = CAMERA_PIN_SIOC;
    config.sccb_i2c_port = 0;
    config.pin_pwdn = CAMERA_PIN_PWDN;
    config.pin_reset = CAMERA_PIN_RESET;
    config.xclk_freq_hz = XCLK_FREQ_HZ;
#endif
    
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    
    return config;
}

enhanced_camera_config_t CreateS3CamEnhancedConfig() {
    enhanced_camera_config_t config = CreateDefaultEnhancedConfig();
    
    // S3Cam specific configuration
    config.model = CAMERA_OV2640;
    config.auto_detect = true;
    config.resource_managed = true;
    config.vision_enabled = true;
    
#ifdef CAMERA_FLASH_PIN
    config.flash_pin = CAMERA_FLASH_PIN;
#else
    config.flash_pin = GPIO_NUM_NC;
#endif
    
    config.flash_level = 0;
    
    return config;
}

bool SetupCameraForBoard(const char* board_name, Web* webserver, McpServer* mcp_server) {
    ESP_LOGI(TAG, "Setting up camera for board: %s", board_name);
    
    // Create configuration based on board type
    camera_config_t camera_config;
    CameraComponentFactory::CameraSystemConfig system_config;
    
    if (strcmp(board_name, "bread-compact-wifi-s3cam") == 0) {
        camera_config = CreateS3CamConfig();
        system_config = CreateDefaultSystemConfig();
        system_config.enhanced_config = CreateS3CamEnhancedConfig();
    } else {
        ESP_LOGE(TAG, "Unsupported board for camera setup: %s", board_name);
        return false;
    }
    
    // Initialize camera system
    return CameraComponentFactory::InitializeCameraSystem(
        camera_config, system_config, webserver, mcp_server);
}

bool EnableCameraWithVision(bool enable) {
    auto* vision_integration = CameraComponentFactory::GetVisionIntegration();
    if (!vision_integration) {
        ESP_LOGE(TAG, "Vision integration not available");
        return false;
    }
    
    if (enable) {
        return vision_integration->EnableVision();
    } else {
        vision_integration->DisableVision();
        return true;
    }
}

bool SwitchCameraState(bool enabled) {
    auto* resource_manager = CameraComponentFactory::GetResourceManager();
    if (!resource_manager) {
        ESP_LOGE(TAG, "Resource manager not available");
        return false;
    }
    
    return resource_manager->SetCameraEnabled(enabled);
}

bool IsCameraAvailable() {
    auto* vision_integration = CameraComponentFactory::GetVisionIntegration();
    if (vision_integration) {
        return vision_integration->IsCameraAvailable();
    }
    
    return CameraComponentFactory::GetEnhancedCamera() != nullptr;
}

bool IsVisionActive() {
    auto* vision_integration = CameraComponentFactory::GetVisionIntegration();
    if (vision_integration) {
        return vision_integration->IsVisionActive();
    }
    
    return false;
}

resource_state_t GetResourceState() {
    auto* resource_manager = CameraComponentFactory::GetResourceManager();
    if (resource_manager) {
        return resource_manager->GetResourceState();
    }
    
    return RESOURCE_IDLE;
}

} // namespace CameraSystemHelpers