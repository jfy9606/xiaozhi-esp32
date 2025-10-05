#include "vision_integration.h"
#include "../components.h"
#include <esp_log.h>

#define TAG "VisionIntegration"

VisionIntegration& VisionIntegration::GetInstance() {
    static VisionIntegration instance;
    return instance;
}

VisionIntegration::VisionIntegration() 
    : initialized_(false), vision_active_(false), resource_managed_(false),
      vision_component_(nullptr), camera_(nullptr), webserver_(nullptr), 
      resource_manager_(nullptr) {
    
    // Default configuration
    config_.auto_start_vision = true;
    config_.resource_managed = true;
    config_.web_integration = true;
    config_.vision_priority = 5;
}

VisionIntegration::~VisionIntegration() {
    Deinitialize();
}

bool VisionIntegration::Initialize(Web* webserver) {
    if (initialized_) {
        ESP_LOGW(TAG, "Vision integration already initialized");
        return true;
    }
    
    // Set web server if provided
    if (webserver) {
        webserver_ = webserver;
    }
    
    // Initialize resource manager if enabled
    if (config_.resource_managed) {
        resource_manager_ = &CameraResourceManager::GetInstance();
        if (!resource_manager_->Initialize()) {
            ESP_LOGE(TAG, "Failed to initialize resource manager");
            return false;
        }
        resource_managed_ = true;
    }
    
    // Create vision component
    if (!CreateVisionComponent()) {
        ESP_LOGE(TAG, "Failed to create vision component");
        return false;
    }
    
    // Register web handlers if web server is available
    if (webserver_ && config_.web_integration) {
        if (!RegisterWebHandlers()) {
            ESP_LOGW(TAG, "Failed to register web handlers");
        }
    }
    
    initialized_ = true;
    ESP_LOGI(TAG, "Vision integration initialized");
    return true;
}

void VisionIntegration::Deinitialize() {
    if (!initialized_) {
        return;
    }
    
    // Stop vision safely
    if (vision_active_) {
        StopVisionSafely();
    }
    
    // Unregister web handlers
    if (webserver_ && config_.web_integration) {
        UnregisterWebHandlers();
    }
    
    // Destroy vision component
    DestroyVisionComponent();
    
    // Reset state
    camera_ = nullptr;
    webserver_ = nullptr;
    resource_manager_ = nullptr;
    resource_managed_ = false;
    
    initialized_ = false;
    ESP_LOGI(TAG, "Vision integration deinitialized");
}

bool VisionIntegration::IsInitialized() const {
    return initialized_;
}

bool VisionIntegration::EnableVision() {
    if (!initialized_) {
        ESP_LOGE(TAG, "Vision integration not initialized");
        return false;
    }
    
    if (vision_active_) {
        ESP_LOGW(TAG, "Vision already active");
        return true;
    }
    
    // Check if camera is available
    if (!IsCameraAvailable()) {
        ESP_LOGE(TAG, "No camera available for vision");
        return false;
    }
    
    // Check resource state if managed
    if (resource_managed_ && resource_manager_) {
        resource_state_t state = resource_manager_->GetResourceState();
        if (state != RESOURCE_CAMERA_ACTIVE) {
            ESP_LOGE(TAG, "Camera resources not available (state: %d)", state);
            return false;
        }
    }
    
    // Start vision with camera
    if (!StartVisionWithCamera()) {
        ESP_LOGE(TAG, "Failed to start vision with camera");
        return false;
    }
    
    vision_active_ = true;
    ESP_LOGI(TAG, "Vision enabled");
    return true;
}

void VisionIntegration::DisableVision() {
    if (!vision_active_) {
        ESP_LOGD(TAG, "Vision already disabled");
        return;
    }
    
    StopVisionSafely();
    vision_active_ = false;
    ESP_LOGI(TAG, "Vision disabled");
}

bool VisionIntegration::IsVisionActive() const {
    return vision_active_;
}

bool VisionIntegration::SetCamera(Camera* camera) {
    if (vision_active_) {
        ESP_LOGE(TAG, "Cannot change camera while vision is active");
        return false;
    }
    
    camera_ = camera;
    ESP_LOGI(TAG, "Camera set for vision integration");
    
    // Auto-start vision if configured and camera is available
    if (config_.auto_start_vision && camera_ && initialized_) {
        EnableVision();
    }
    
    return true;
}

Camera* VisionIntegration::GetCamera() const {
    return camera_;
}

bool VisionIntegration::IsCameraAvailable() const {
    return camera_ != nullptr;
}

bool VisionIntegration::SetWebServer(Web* webserver) {
    if (initialized_ && webserver_) {
        ESP_LOGW(TAG, "Web server already set, unregistering old handlers");
        UnregisterWebHandlers();
    }
    
    webserver_ = webserver;
    
    // Register handlers if integration is initialized and web integration is enabled
    if (initialized_ && webserver_ && config_.web_integration) {
        RegisterWebHandlers();
    }
    
    ESP_LOGI(TAG, "Web server set for vision integration");
    return true;
}

Web* VisionIntegration::GetWebServer() const {
    return webserver_;
}

bool VisionIntegration::EnableResourceManagement() {
    if (initialized_) {
        ESP_LOGE(TAG, "Cannot enable resource management after initialization");
        return false;
    }
    
    config_.resource_managed = true;
    ESP_LOGI(TAG, "Resource management enabled for vision integration");
    return true;
}

void VisionIntegration::DisableResourceManagement() {
    if (initialized_) {
        ESP_LOGW(TAG, "Cannot disable resource management after initialization");
        return;
    }
    
    config_.resource_managed = false;
    ESP_LOGI(TAG, "Resource management disabled for vision integration");
}

bool VisionIntegration::IsResourceManaged() const {
    return resource_managed_;
}

Vision* VisionIntegration::GetVisionComponent() const {
    return vision_component_;
}

bool VisionIntegration::HandleCameraStateChange(bool camera_enabled) {
    ESP_LOGI(TAG, "Handling camera state change: %s", camera_enabled ? "enabled" : "disabled");
    
    if (camera_enabled) {
        // Camera enabled - try to start vision if configured
        if (config_.auto_start_vision && !vision_active_) {
            return EnableVision();
        }
    } else {
        // Camera disabled - stop vision
        if (vision_active_) {
            DisableVision();
        }
    }
    
    return true;
}

void VisionIntegration::UpdateVisionState() {
    if (!initialized_) {
        return;
    }
    
    // Check if vision should be active based on current conditions
    bool should_be_active = IsCameraAvailable();
    
    if (resource_managed_ && resource_manager_) {
        should_be_active = should_be_active && 
                          (resource_manager_->GetResourceState() == RESOURCE_CAMERA_ACTIVE);
    }
    
    // Update vision state if needed
    if (should_be_active && !vision_active_ && config_.auto_start_vision) {
        EnableVision();
    } else if (!should_be_active && vision_active_) {
        DisableVision();
    }
}

bool VisionIntegration::SetConfig(const Config& config) {
    if (initialized_) {
        ESP_LOGW(TAG, "Some config changes may not take effect until reinitialization");
    }
    
    config_ = config;
    ESP_LOGI(TAG, "Vision integration config updated");
    return true;
}

VisionIntegration::Config VisionIntegration::GetConfig() const {
    return config_;
}

// Private methods

bool VisionIntegration::CreateVisionComponent() {
    if (vision_component_) {
        ESP_LOGW(TAG, "Vision component already exists");
        return true;
    }
    
    // Create vision component with web server
    vision_component_ = new Vision(webserver_);
    if (!vision_component_) {
        ESP_LOGE(TAG, "Failed to create vision component");
        return false;
    }
    
    // Register with component manager
    auto& manager = ComponentManager::GetInstance();
    manager.RegisterComponent(vision_component_);
    
    ESP_LOGI(TAG, "Vision component created");
    return true;
}

void VisionIntegration::DestroyVisionComponent() {
    if (!vision_component_) {
        return;
    }
    
    // Unregister from component manager
    auto& manager = ComponentManager::GetInstance();
    manager.UnregisterComponent(vision_component_);
    
    // Stop and delete component
    vision_component_->Stop();
    delete vision_component_;
    vision_component_ = nullptr;
    
    ESP_LOGI(TAG, "Vision component destroyed");
}

bool VisionIntegration::RegisterWebHandlers() {
    if (!webserver_) {
        ESP_LOGE(TAG, "No web server available for handler registration");
        return false;
    }
    
    // Vision integration specific handlers would be registered here
    // For now, the Vision component handles its own web registration
    
    ESP_LOGI(TAG, "Web handlers registered for vision integration");
    return true;
}

void VisionIntegration::UnregisterWebHandlers() {
    if (!webserver_) {
        return;
    }
    
    // Unregister vision integration specific handlers
    ESP_LOGI(TAG, "Web handlers unregistered for vision integration");
}

bool VisionIntegration::StartVisionWithCamera() {
    if (!vision_component_) {
        ESP_LOGE(TAG, "No vision component available");
        return false;
    }
    
    if (!camera_) {
        ESP_LOGE(TAG, "No camera available");
        return false;
    }
    
    // Start vision component
    if (!vision_component_->Start()) {
        ESP_LOGE(TAG, "Failed to start vision component");
        return false;
    }
    
    ESP_LOGI(TAG, "Vision started with camera");
    return true;
}

void VisionIntegration::StopVisionSafely() {
    if (!vision_component_) {
        return;
    }
    
    // Stop vision component safely
    vision_component_->Stop();
    
    ESP_LOGI(TAG, "Vision stopped safely");
}