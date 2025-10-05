#include "mcp_camera_tools.h"
#include <esp_log.h>
#include <cJSON.h>

#define TAG "McpCameraTools"

McpCameraTools& McpCameraTools::GetInstance() {
    static McpCameraTools instance;
    return instance;
}

McpCameraTools::McpCameraTools() 
    : initialized_(false), resource_managed_(false),
      mcp_server_(nullptr), camera_(nullptr), enhanced_camera_(nullptr), 
      resource_manager_(nullptr) {
}

McpCameraTools::~McpCameraTools() {
    Deinitialize();
}

bool McpCameraTools::Initialize(McpServer* mcp_server) {
    if (initialized_) {
        ESP_LOGW(TAG, "MCP camera tools already initialized");
        return true;
    }
    
    if (!mcp_server) {
        ESP_LOGE(TAG, "MCP server is required");
        return false;
    }
    
    mcp_server_ = mcp_server;
    
    // Initialize resource manager if needed
    resource_manager_ = &CameraResourceManager::GetInstance();
    if (!resource_manager_->Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize resource manager");
        return false;
    }
    
    initialized_ = true;
    ESP_LOGI(TAG, "MCP camera tools initialized");
    return true;
}

void McpCameraTools::Deinitialize() {
    if (!initialized_) {
        return;
    }
    
    // Unregister all tools
    UnregisterAllTools();
    
    // Reset state
    mcp_server_ = nullptr;
    camera_ = nullptr;
    enhanced_camera_ = nullptr;
    resource_manager_ = nullptr;
    resource_managed_ = false;
    
    initialized_ = false;
    ESP_LOGI(TAG, "MCP camera tools deinitialized");
}

bool McpCameraTools::IsInitialized() const {
    return initialized_;
}

bool McpCameraTools::SetCamera(Camera* camera) {
    camera_ = camera;
    ESP_LOGI(TAG, "Camera set for MCP tools");
    return true;
}

Camera* McpCameraTools::GetCamera() const {
    return camera_;
}

bool McpCameraTools::SetEnhancedCamera(EnhancedEsp32Camera* camera) {
    enhanced_camera_ = camera;
    camera_ = camera; // Enhanced camera is also a base camera
    ESP_LOGI(TAG, "Enhanced camera set for MCP tools");
    return true;
}

EnhancedEsp32Camera* McpCameraTools::GetEnhancedCamera() const {
    return enhanced_camera_;
}

bool McpCameraTools::EnableResourceManagement() {
    resource_managed_ = true;
    ESP_LOGI(TAG, "Resource management enabled for MCP camera tools");
    return true;
}

void McpCameraTools::DisableResourceManagement() {
    resource_managed_ = false;
    ESP_LOGI(TAG, "Resource management disabled for MCP camera tools");
}

bool McpCameraTools::IsResourceManaged() const {
    return resource_managed_;
}

bool McpCameraTools::RegisterAllTools() {
    if (!initialized_ || !mcp_server_) {
        ESP_LOGE(TAG, "Cannot register tools - not initialized");
        return false;
    }
    
    bool success = true;
    
    success &= RegisterPhotoTool();
    success &= RegisterFlashTool();
    success &= RegisterConfigTool();
    success &= RegisterSwitchTool();
    success &= RegisterStatusTool();
    
    if (success) {
        ESP_LOGI(TAG, "All MCP camera tools registered successfully");
    } else {
        ESP_LOGE(TAG, "Some MCP camera tools failed to register");
    }
    
    return success;
}

void McpCameraTools::UnregisterAllTools() {
    if (!mcp_server_) {
        return;
    }
    
    // Tools are automatically unregistered when MCP server is destroyed
    ESP_LOGI(TAG, "MCP camera tools unregistered");
}

bool McpCameraTools::RegisterPhotoTool() {
    if (!mcp_server_) {
        return false;
    }
    
    mcp_server_->AddTool("self.camera.take_photo",
        "Take a photo and explain it. Use this tool after the user asks you to see something.",
        "Args:\n"
        "- question (string): Optional question about what to look for in the photo\n"
        "Returns: Description of what was captured in the photo",
        PropertyList({
            Property("question", kPropertyTypeString)
        }),
        TakePhotoTool);
    
    ESP_LOGI(TAG, "Photo tool registered");
    return true;
}

bool McpCameraTools::RegisterFlashTool() {
    if (!mcp_server_) {
        return false;
    }
    
    mcp_server_->AddTool("self.camera.flash",
        "Control the camera flash LED.",
        "Args:\n"
        "- level (number): Flash intensity level (0-100)\n"
        "Returns: Success status and current flash level",
        PropertyList({
            Property("level", kPropertyTypeNumber)
        }),
        FlashControlTool);
    
    ESP_LOGI(TAG, "Flash tool registered");
    return true;
}

bool McpCameraTools::RegisterConfigTool() {
    if (!mcp_server_) {
        return false;
    }
    
    mcp_server_->AddTool("self.camera.set_config",
        "Configure camera parameters like brightness, contrast, saturation.",
        "Args:\n"
        "- parameter (string): Parameter name (brightness, contrast, saturation, hmirror, vflip)\n"
        "- value (number/boolean): Parameter value\n"
        "Returns: Success status and current parameter value",
        PropertyList({
            Property("parameter", kPropertyTypeString),
            Property("value", kPropertyTypeAny)
        }),
        ConfigControlTool);
    
    ESP_LOGI(TAG, "Config tool registered");
    return true;
}

bool McpCameraTools::RegisterSwitchTool() {
    if (!mcp_server_) {
        return false;
    }
    
    mcp_server_->AddTool("self.camera.switch",
        "Enable or disable the camera system.",
        "Args:\n"
        "- enabled (boolean): True to enable camera, false to disable\n"
        "Returns: Success status and current camera state",
        PropertyList({
            Property("enabled", kPropertyTypeBoolean)
        }),
        SwitchControlTool);
    
    ESP_LOGI(TAG, "Switch tool registered");
    return true;
}

bool McpCameraTools::RegisterStatusTool() {
    if (!mcp_server_) {
        return false;
    }
    
    mcp_server_->AddTool("self.camera.get_status",
        "Get current camera status and configuration.",
        "Returns: JSON object with camera status, configuration, and capabilities",
        PropertyList(),
        StatusTool);
    
    ESP_LOGI(TAG, "Status tool registered");
    return true;
}

// Tool implementations

ReturnValue McpCameraTools::TakePhotoTool(const PropertyList& properties) {
    McpCameraTools* instance = GetInstanceForTool();
    if (!instance || !instance->ValidateCameraAccess()) {
        throw std::runtime_error("Camera not available");
    }
    
    // Check resource access if managed
    if (instance->resource_managed_ && !instance->ValidateResourceAccess()) {
        throw std::runtime_error("Camera resources not available");
    }
    
    // Get question parameter
    std::string question = "";
    if (properties.find("question") != properties.end()) {
        question = properties.at("question").value<std::string>();
    }
    
    // Take photo
    if (!instance->camera_->Capture()) {
        throw std::runtime_error("Failed to capture photo");
    }
    
    // Get explanation
    std::string explanation = instance->camera_->Explain(question);
    
    ESP_LOGI(TAG, "Photo captured and explained");
    return explanation;
}

ReturnValue McpCameraTools::FlashControlTool(const PropertyList& properties) {
    McpCameraTools* instance = GetInstanceForTool();
    if (!instance || !instance->ValidateCameraAccess()) {
        throw std::runtime_error("Camera not available");
    }
    
    if (!instance->camera_->HasFlash()) {
        throw std::runtime_error("Camera does not have flash capability");
    }
    
    // Get level parameter
    int level = 0;
    if (properties.find("level") != properties.end()) {
        level = static_cast<int>(properties.at("level").value<double>());
    }
    
    // Set flash level
    bool success = instance->camera_->SetFlashLevel(level);
    if (!success) {
        throw std::runtime_error("Failed to set flash level");
    }
    
    // Return status
    cJSON* result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "success", true);
    cJSON_AddNumberToObject(result, "level", level);
    
    char* json_str = cJSON_PrintUnformatted(result);
    std::string response(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(result);
    
    ESP_LOGI(TAG, "Flash level set to %d", level);
    return response;
}

ReturnValue McpCameraTools::ConfigControlTool(const PropertyList& properties) {
    McpCameraTools* instance = GetInstanceForTool();
    if (!instance || !instance->ValidateCameraAccess()) {
        throw std::runtime_error("Camera not available");
    }
    
    // Get parameters
    if (properties.find("parameter") == properties.end()) {
        throw std::runtime_error("Parameter name is required");
    }
    
    std::string parameter = properties.at("parameter").value<std::string>();
    
    if (properties.find("value") == properties.end()) {
        throw std::runtime_error("Parameter value is required");
    }
    
    bool success = false;
    
    // Set parameter based on type
    if (parameter == "brightness") {
        int value = static_cast<int>(properties.at("value").value<double>());
        success = instance->camera_->SetBrightness(value);
    } else if (parameter == "contrast") {
        int value = static_cast<int>(properties.at("value").value<double>());
        success = instance->camera_->SetContrast(value);
    } else if (parameter == "saturation") {
        int value = static_cast<int>(properties.at("value").value<double>());
        success = instance->camera_->SetSaturation(value);
    } else if (parameter == "hmirror") {
        bool value = properties.at("value").value<bool>();
        success = instance->camera_->SetHMirror(value);
    } else if (parameter == "vflip") {
        bool value = properties.at("value").value<bool>();
        success = instance->camera_->SetVFlip(value);
    } else {
        throw std::runtime_error("Unknown parameter: " + parameter);
    }
    
    if (!success) {
        throw std::runtime_error("Failed to set parameter: " + parameter);
    }
    
    // Return status
    cJSON* result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "success", true);
    cJSON_AddStringToObject(result, "parameter", parameter.c_str());
    
    char* json_str = cJSON_PrintUnformatted(result);
    std::string response(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(result);
    
    ESP_LOGI(TAG, "Camera parameter %s configured", parameter.c_str());
    return response;
}

ReturnValue McpCameraTools::SwitchControlTool(const PropertyList& properties) {
    McpCameraTools* instance = GetInstanceForTool();
    if (!instance) {
        throw std::runtime_error("MCP camera tools not available");
    }
    
    if (!instance->resource_managed_ || !instance->resource_manager_) {
        throw std::runtime_error("Camera switch requires resource management");
    }
    
    // Get enabled parameter
    if (properties.find("enabled") == properties.end()) {
        throw std::runtime_error("Enabled parameter is required");
    }
    
    bool enabled = properties.at("enabled").value<bool>();
    
    // Set camera enabled state
    bool success = instance->resource_manager_->SetCameraEnabled(enabled);
    if (!success) {
        throw std::runtime_error("Failed to change camera state");
    }
    
    // Return status
    cJSON* result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "success", true);
    cJSON_AddBoolToObject(result, "enabled", enabled);
    cJSON_AddNumberToObject(result, "resource_state", instance->resource_manager_->GetResourceState());
    
    char* json_str = cJSON_PrintUnformatted(result);
    std::string response(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(result);
    
    ESP_LOGI(TAG, "Camera switch %s", enabled ? "enabled" : "disabled");
    return response;
}

ReturnValue McpCameraTools::StatusTool(const PropertyList& properties) {
    McpCameraTools* instance = GetInstanceForTool();
    if (!instance) {
        throw std::runtime_error("MCP camera tools not available");
    }
    
    std::string status = instance->GetCameraStatusJson();
    
    ESP_LOGI(TAG, "Camera status requested");
    return status;
}

// Helper methods

McpCameraTools* McpCameraTools::GetInstanceForTool() {
    return &GetInstance();
}

bool McpCameraTools::ValidateCameraAccess() const {
    return camera_ != nullptr;
}

bool McpCameraTools::ValidateResourceAccess() const {
    if (!resource_managed_ || !resource_manager_) {
        return true; // No resource management, allow access
    }
    
    resource_state_t state = resource_manager_->GetResourceState();
    return state == RESOURCE_CAMERA_ACTIVE;
}

std::string McpCameraTools::GetCameraStatusJson() const {
    cJSON* root = cJSON_CreateObject();
    
    // Basic status
    cJSON_AddBoolToObject(root, "available", camera_ != nullptr);
    cJSON_AddBoolToObject(root, "resource_managed", resource_managed_);
    
    if (resource_manager_) {
        camera_switch_state_t switch_state = resource_manager_->GetSwitchState();
        cJSON_AddBoolToObject(root, "enabled", switch_state.enabled);
        cJSON_AddBoolToObject(root, "initialized", switch_state.initialized);
        cJSON_AddNumberToObject(root, "resource_state", switch_state.resource_state);
        cJSON_AddStringToObject(root, "detected_model", switch_state.detected_model);
    }
    
    if (camera_) {
        cJSON_AddStringToObject(root, "sensor", camera_->GetSensorName());
        cJSON_AddBoolToObject(root, "has_flash", camera_->HasFlash());
        cJSON_AddNumberToObject(root, "brightness", camera_->GetBrightness());
        cJSON_AddNumberToObject(root, "contrast", camera_->GetContrast());
        cJSON_AddNumberToObject(root, "saturation", camera_->GetSaturation());
        cJSON_AddBoolToObject(root, "hmirror", camera_->GetHMirror());
        cJSON_AddBoolToObject(root, "vflip", camera_->GetVFlip());
    }
    
    if (enhanced_camera_) {
        cJSON_AddStringToObject(root, "type", "enhanced");
        cJSON_AddStringToObject(root, "model", enhanced_camera_->GetModelName(enhanced_camera_->GetDetectedModel()));
        cJSON_AddNumberToObject(root, "flash_level", enhanced_camera_->GetFlashLevel());
    } else {
        cJSON_AddStringToObject(root, "type", "basic");
    }
    
    char* json_str = cJSON_PrintUnformatted(root);
    std::string result(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(root);
    
    return result;
}