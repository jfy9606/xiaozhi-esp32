#include "camera_resource_manager.h"
#include "config.h"
#include <driver/gpio.h>
#include <esp_log.h>

#define TAG "CameraResourceManager"

CameraResourceManager& CameraResourceManager::GetInstance() {
    static CameraResourceManager instance;
    return instance;
}

CameraResourceManager::CameraResourceManager() 
    : resource_mutex_(nullptr), initialized_(false) {
    // Initialize switch state
    switch_state_.enabled = false;
    switch_state_.initialized = false;
    switch_state_.resource_state = RESOURCE_IDLE;
    switch_state_.detected_model = "Unknown";
}

CameraResourceManager::~CameraResourceManager() {
    Deinitialize();
}

bool CameraResourceManager::Initialize() {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }
    
    // Create mutex for resource management
    resource_mutex_ = xSemaphoreCreateMutex();
    if (!resource_mutex_) {
        ESP_LOGE(TAG, "Failed to create resource mutex");
        return false;
    }
    
    initialized_ = true;
    ESP_LOGI(TAG, "Camera resource manager initialized");
    return true;
}

void CameraResourceManager::Deinitialize() {
    if (!initialized_) {
        return;
    }
    
    // Release any held resources
    ReleaseResource();
    
    // Delete mutex
    if (resource_mutex_) {
        vSemaphoreDelete(resource_mutex_);
        resource_mutex_ = nullptr;
    }
    
    initialized_ = false;
    ESP_LOGI(TAG, "Camera resource manager deinitialized");
}

bool CameraResourceManager::LockResourceForCamera() {
    if (!initialized_ || !resource_mutex_) {
        ESP_LOGE(TAG, "Resource manager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(resource_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire resource mutex");
        return false;
    }
    
    bool success = false;
    
    if (switch_state_.resource_state == RESOURCE_IDLE) {
        success = TransitionToState(RESOURCE_CAMERA_ACTIVE);
        if (success) {
            ConfigureCameraPins();
            ESP_LOGI(TAG, "Camera resource locked");
        }
    } else if (switch_state_.resource_state == RESOURCE_CAMERA_ACTIVE) {
        // Already locked for camera
        success = true;
        ESP_LOGD(TAG, "Camera resource already locked");
    } else {
        ESP_LOGW(TAG, "Resource busy with state: %d", switch_state_.resource_state);
    }
    
    xSemaphoreGive(resource_mutex_);
    return success;
}

bool CameraResourceManager::LockResourceForAudio() {
    if (!initialized_ || !resource_mutex_) {
        ESP_LOGE(TAG, "Resource manager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(resource_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire resource mutex");
        return false;
    }
    
    bool success = false;
    
    if (switch_state_.resource_state == RESOURCE_IDLE) {
        success = TransitionToState(RESOURCE_AUDIO_ACTIVE);
        if (success) {
            ResetCameraPins();
            ESP_LOGI(TAG, "Audio resource locked");
        }
    } else if (switch_state_.resource_state == RESOURCE_AUDIO_ACTIVE) {
        // Already locked for audio
        success = true;
        ESP_LOGD(TAG, "Audio resource already locked");
    } else {
        ESP_LOGW(TAG, "Resource busy with state: %d", switch_state_.resource_state);
    }
    
    xSemaphoreGive(resource_mutex_);
    return success;
}

void CameraResourceManager::ReleaseResource() {
    if (!initialized_ || !resource_mutex_) {
        return;
    }
    
    if (xSemaphoreTake(resource_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire resource mutex for release");
        return;
    }
    
    if (switch_state_.resource_state != RESOURCE_IDLE) {
        TransitionToState(RESOURCE_IDLE);
        ResetCameraPins();
        ESP_LOGI(TAG, "Resource released");
    }
    
    xSemaphoreGive(resource_mutex_);
}

resource_state_t CameraResourceManager::GetResourceState() const {
    return switch_state_.resource_state;
}

bool CameraResourceManager::IsCameraEnabled() const {
    return switch_state_.enabled;
}

bool CameraResourceManager::SetCameraEnabled(bool enabled) {
    if (!initialized_) {
        ESP_LOGE(TAG, "Resource manager not initialized");
        return false;
    }
    
    if (switch_state_.enabled == enabled) {
        ESP_LOGD(TAG, "Camera already %s", enabled ? "enabled" : "disabled");
        return true;
    }
    
    if (enabled) {
        // Enable camera - lock resources
        if (LockResourceForCamera()) {
            switch_state_.enabled = true;
            ESP_LOGI(TAG, "Camera enabled");
            return true;
        } else {
            ESP_LOGE(TAG, "Failed to enable camera - resource lock failed");
            return false;
        }
    } else {
        // Disable camera - release resources
        ReleaseResource();
        switch_state_.enabled = false;
        switch_state_.initialized = false;
        ESP_LOGI(TAG, "Camera disabled");
        return true;
    }
}

bool CameraResourceManager::IsCameraInitialized() const {
    return switch_state_.initialized;
}

void CameraResourceManager::SetCameraInitialized(bool initialized) {
    switch_state_.initialized = initialized;
    ESP_LOGD(TAG, "Camera initialization state: %s", initialized ? "true" : "false");
}

camera_switch_state_t CameraResourceManager::GetSwitchState() const {
    return switch_state_;
}

void CameraResourceManager::SetDetectedModel(const char* model) {
    switch_state_.detected_model = model;
    ESP_LOGI(TAG, "Detected camera model: %s", model);
}

bool CameraResourceManager::TransitionToState(resource_state_t new_state) {
    ESP_LOGD(TAG, "Transitioning from state %d to %d", switch_state_.resource_state, new_state);
    
    // Set switching state during transition
    switch_state_.resource_state = RESOURCE_SWITCHING;
    
    // Add small delay for hardware settling
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Set final state
    switch_state_.resource_state = new_state;
    
    ESP_LOGD(TAG, "State transition complete: %d", new_state);
    return true;
}

void CameraResourceManager::ResetCameraPins() {
#ifdef CAMERA_PIN_PWDN
    if (CAMERA_PIN_PWDN != GPIO_NUM_NC) {
        gpio_reset_pin(CAMERA_PIN_PWDN);
        ESP_LOGD(TAG, "Reset camera PWDN pin");
    }
#endif

#ifdef CAMERA_PIN_RESET
    if (CAMERA_PIN_RESET != GPIO_NUM_NC) {
        gpio_reset_pin(CAMERA_PIN_RESET);
        ESP_LOGD(TAG, "Reset camera RESET pin");
    }
#endif

#ifdef CAMERA_PIN_XCLK
    if (CAMERA_PIN_XCLK != GPIO_NUM_NC) {
        gpio_reset_pin(CAMERA_PIN_XCLK);
        ESP_LOGD(TAG, "Reset camera XCLK pin");
    }
#endif
}

void CameraResourceManager::ConfigureCameraPins() {
    // Camera pins will be configured by the camera driver
    // This method is for any additional pin configuration needed
    ESP_LOGD(TAG, "Camera pins ready for configuration");
}