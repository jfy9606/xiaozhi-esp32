#include "enhanced_esp32_camera.h"
#include "config.h"
#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/ledc.h>

#define TAG "EnhancedEsp32Camera"

EnhancedEsp32Camera::EnhancedEsp32Camera(const camera_config_t& config, const enhanced_camera_config_t& enhanced_config)
    : Esp32Camera(config), enhanced_config_(enhanced_config), detected_model_(CAMERA_MODEL_UNKNOWN), 
      initialized_(false), resource_manager_(nullptr) {
    
    if (enhanced_config_.resource_managed) {
        resource_manager_ = &CameraResourceManager::GetInstance();
    }
    
    ESP_LOGI(TAG, "Enhanced ESP32 camera created with model support");
}

EnhancedEsp32Camera::~EnhancedEsp32Camera() {
    Deinitialize();
}

bool EnhancedEsp32Camera::Initialize() {
    if (initialized_) {
        ESP_LOGW(TAG, "Camera already initialized");
        return true;
    }
    
    // Initialize resource manager if needed
    if (resource_manager_ && !resource_manager_->Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize resource manager");
        return false;
    }
    
    // Lock resources if managed
    if (resource_manager_ && !resource_manager_->LockResourceForCamera()) {
        ESP_LOGE(TAG, "Failed to lock camera resources");
        return false;
    }
    
    // Auto-detect sensor if enabled
    if (enhanced_config_.auto_detect) {
        ESP_LOGI(TAG, "Starting camera auto-detection...");
        if (!AutoDetectSensor()) {
            ESP_LOGW(TAG, "Auto-detection failed, using default model: %s", GetModelName(enhanced_config_.model));
            detected_model_ = enhanced_config_.model;
        } else {
            ESP_LOGI(TAG, "Auto-detection successful: %s", GetModelName(detected_model_));
        }
    } else {
        detected_model_ = enhanced_config_.model;
        ESP_LOGI(TAG, "Using configured camera model: %s", GetModelName(detected_model_));
    }
    
    // Apply model-specific settings
    if (!SetModelSpecificSettings(detected_model_)) {
        ESP_LOGE(TAG, "Failed to apply model-specific settings");
        return false;
    }
    
    // Note: Base Esp32Camera doesn't have Initialize method, 
    // initialization happens in constructor
    
    // Configure flash if available
    if (enhanced_config_.flash_pin != GPIO_NUM_NC) {
        ConfigureFlashPin();
    }
    
    // Apply model optimizations
    ApplyModelOptimizations();
    
    initialized_ = true;
    
    // Update resource manager state
    if (resource_manager_) {
        resource_manager_->SetCameraInitialized(true);
        resource_manager_->SetDetectedModel(GetModelName(detected_model_));
    }
    
    ESP_LOGI(TAG, "Enhanced camera initialized with model: %s", GetModelName(detected_model_));
    return true;
}

void EnhancedEsp32Camera::Deinitialize() {
    if (!initialized_) {
        return;
    }
    
    // Note: Base Esp32Camera doesn't have Deinitialize method
    
    // Release resources
    if (resource_manager_) {
        resource_manager_->SetCameraInitialized(false);
        resource_manager_->ReleaseResource();
    }
    
    initialized_ = false;
    ESP_LOGI(TAG, "Enhanced camera deinitialized");
}

bool EnhancedEsp32Camera::IsInitialized() const {
    return initialized_;
}

bool EnhancedEsp32Camera::AutoDetectSensor() {
    ESP_LOGI(TAG, "Starting camera sensor auto-detection");
    
    // Try to detect different camera models in order of preference
    // Only try detection for models that are enabled in Kconfig
    
#ifdef CONFIG_CAMERA_OV5640_SUPPORT
    if (DetectOV5640()) {
        detected_model_ = CAMERA_MODEL_OV5640;
        ESP_LOGI(TAG, "Detected OV5640 camera sensor");
        return true;
    }
#endif
    
#ifdef CONFIG_CAMERA_OV3660_SUPPORT
    if (DetectOV3660()) {
        detected_model_ = CAMERA_MODEL_OV3660;
        ESP_LOGI(TAG, "Detected OV3660 camera sensor");
        return true;
    }
#endif
    
#ifdef CONFIG_CAMERA_OV2640_SUPPORT
    if (DetectOV2640()) {
        detected_model_ = CAMERA_MODEL_OV2640;
        ESP_LOGI(TAG, "Detected OV2640 camera sensor");
        return true;
    }
#endif
    
    ESP_LOGW(TAG, "No supported camera sensor detected");
    return false;
}

bool EnhancedEsp32Camera::SetCameraModel(camera_model_t model) {
    if (initialized_) {
        ESP_LOGE(TAG, "Cannot change model while camera is initialized");
        return false;
    }
    
    enhanced_config_.model = model;
    detected_model_ = model;
    
    ESP_LOGI(TAG, "Camera model set to: %s", GetModelName(model));
    return true;
}

camera_model_t EnhancedEsp32Camera::GetDetectedModel() const {
    return detected_model_;
}

const char* EnhancedEsp32Camera::GetModelName(camera_model_t model) const {
    switch (model) {
        case CAMERA_MODEL_OV2640: return "OV2640";
        case CAMERA_MODEL_OV3660: return "OV3660";
        case CAMERA_MODEL_OV5640: return "OV5640";
        default: return "Unknown";
    }
}

bool EnhancedEsp32Camera::EnableWithResourceManagement() {
    if (!resource_manager_) {
        ESP_LOGE(TAG, "Resource management not enabled");
        return false;
    }
    
    return resource_manager_->SetCameraEnabled(true);
}

void EnhancedEsp32Camera::DisableWithResourceManagement() {
    if (resource_manager_) {
        resource_manager_->SetCameraEnabled(false);
    }
}

bool EnhancedEsp32Camera::IsResourceManaged() const {
    return enhanced_config_.resource_managed && resource_manager_;
}

bool EnhancedEsp32Camera::HasFlash() {
    return enhanced_config_.flash_pin != GPIO_NUM_NC;
}

bool EnhancedEsp32Camera::SetFlashLevel(int level) {
    if (!HasFlash()) {
        return false;
    }
    
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    
    enhanced_config_.flash_level = level;
    
    // Set flash using LEDC PWM
    uint32_t duty = (level * 8191) / 100; // 13-bit resolution
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    
    ESP_LOGD(TAG, "Flash level set to %d%% (duty: %lu)", level, duty);
    return err == ESP_OK;
}

int EnhancedEsp32Camera::GetFlashLevel() const {
    return enhanced_config_.flash_level;
}

bool EnhancedEsp32Camera::ApplyModelOptimizations() {
    switch (detected_model_) {
        case CAMERA_MODEL_OV2640:
            // OV2640 optimizations
            SetBrightness(0);
            SetContrast(0);
            SetSaturation(0);
            break;
            
        case CAMERA_MODEL_OV3660:
            // OV3660 optimizations
            SetBrightness(1);
            SetContrast(1);
            SetSaturation(0);
            break;
            
        case CAMERA_MODEL_OV5640:
            // OV5640 optimizations
            SetBrightness(0);
            SetContrast(2);
            SetSaturation(1);
            break;
            
        default:
            ESP_LOGW(TAG, "No optimizations available for unknown model");
            return false;
    }
    
    ESP_LOGI(TAG, "Applied optimizations for %s", GetModelName(detected_model_));
    return true;
}

bool EnhancedEsp32Camera::SetModelSpecificSettings(camera_model_t model) {
    ESP_LOGI(TAG, "Configuring settings for %s", GetModelName(model));
    
    switch (model) {
        case CAMERA_MODEL_OV2640:
#ifdef CONFIG_CAMERA_OV2640_SUPPORT
            return InitializeOV2640();
#else
            ESP_LOGW(TAG, "OV2640 support not enabled in configuration");
            return false;
#endif
        case CAMERA_MODEL_OV3660:
#ifdef CONFIG_CAMERA_OV3660_SUPPORT
            return InitializeOV3660();
#else
            ESP_LOGW(TAG, "OV3660 support not enabled in configuration");
            return false;
#endif
        case CAMERA_MODEL_OV5640:
#ifdef CONFIG_CAMERA_OV5640_SUPPORT
            return InitializeOV5640();
#else
            ESP_LOGW(TAG, "OV5640 support not enabled in configuration");
            return false;
#endif
        default:
            ESP_LOGW(TAG, "Unknown camera model, using default settings");
            return true;
    }
}

enhanced_camera_config_t EnhancedEsp32Camera::GetEnhancedConfig() const {
    return enhanced_config_;
}

bool EnhancedEsp32Camera::UpdateEnhancedConfig(const enhanced_camera_config_t& config) {
    if (initialized_) {
        ESP_LOGE(TAG, "Cannot update config while camera is initialized");
        return false;
    }
    
    enhanced_config_ = config;
    ESP_LOGI(TAG, "Enhanced camera config updated");
    return true;
}

// Static methods for model support queries
bool EnhancedEsp32Camera::IsModelSupported(camera_model_t model) {
    switch (model) {
        case CAMERA_MODEL_OV2640:
#ifdef CONFIG_CAMERA_OV2640_SUPPORT
            return true;
#else
            return false;
#endif
        case CAMERA_MODEL_OV3660:
#ifdef CONFIG_CAMERA_OV3660_SUPPORT
            return true;
#else
            return false;
#endif
        case CAMERA_MODEL_OV5640:
#ifdef CONFIG_CAMERA_OV5640_SUPPORT
            return true;
#else
            return false;
#endif
        default:
            return false;
    }
}

int EnhancedEsp32Camera::GetSupportedModelsCount() {
    int count = 0;
#ifdef CONFIG_CAMERA_OV2640_SUPPORT
    count++;
#endif
#ifdef CONFIG_CAMERA_OV3660_SUPPORT
    count++;
#endif
#ifdef CONFIG_CAMERA_OV5640_SUPPORT
    count++;
#endif
    return count;
}

void EnhancedEsp32Camera::GetSupportedModels(camera_model_t* models, int max_count) {
    int index = 0;
    
#ifdef CONFIG_CAMERA_OV2640_SUPPORT
    if (index < max_count) {
        models[index++] = CAMERA_MODEL_OV2640;
    }
#endif
#ifdef CONFIG_CAMERA_OV3660_SUPPORT
    if (index < max_count) {
        models[index++] = CAMERA_MODEL_OV3660;
    }
#endif
#ifdef CONFIG_CAMERA_OV5640_SUPPORT
    if (index < max_count) {
        models[index++] = CAMERA_MODEL_OV5640;
    }
#endif
}

// Private methods

bool EnhancedEsp32Camera::DetectOV2640() {
    // OV2640 detection logic - check sensor ID registers
    ESP_LOGD(TAG, "Attempting to detect OV2640");
    
    // OV2640 has manufacturer ID 0x7FA2 at registers 0x1C and 0x1D
    // Product ID 0x2642 at registers 0x0A and 0x0B
    sensor_id_t* sensor = esp_camera_sensor_get();
    if (sensor == nullptr) {
        ESP_LOGW(TAG, "Cannot get camera sensor for OV2640 detection");
        return false;
    }
    
    // Read manufacturer ID
    uint8_t mid_h = sensor->get_reg(sensor, 0x1C);
    uint8_t mid_l = sensor->get_reg(sensor, 0x1D);
    uint16_t manufacturer_id = (mid_h << 8) | mid_l;
    
    // Read product ID
    uint8_t pid_h = sensor->get_reg(sensor, 0x0A);
    uint8_t pid_l = sensor->get_reg(sensor, 0x0B);
    uint16_t product_id = (pid_h << 8) | pid_l;
    
    ESP_LOGD(TAG, "OV2640 detection - MID: 0x%04X, PID: 0x%04X", manufacturer_id, product_id);
    
    // Check for OV2640 signature
    if (manufacturer_id == 0x7FA2 && product_id == 0x2642) {
        ESP_LOGI(TAG, "OV2640 camera sensor detected");
        return true;
    }
    
    return false;
}

bool EnhancedEsp32Camera::DetectOV3660() {
    // OV3660 detection logic - check sensor ID registers
    ESP_LOGD(TAG, "Attempting to detect OV3660");
    
    sensor_id_t* sensor = esp_camera_sensor_get();
    if (sensor == nullptr) {
        ESP_LOGW(TAG, "Cannot get camera sensor for OV3660 detection");
        return false;
    }
    
    // OV3660 has chip ID 0x3660 at registers 0x300A and 0x300B
    uint8_t chip_id_h = sensor->get_reg(sensor, 0x300A);
    uint8_t chip_id_l = sensor->get_reg(sensor, 0x300B);
    uint16_t chip_id = (chip_id_h << 8) | chip_id_l;
    
    ESP_LOGD(TAG, "OV3660 detection - Chip ID: 0x%04X", chip_id);
    
    // Check for OV3660 signature
    if (chip_id == 0x3660) {
        ESP_LOGI(TAG, "OV3660 camera sensor detected");
        return true;
    }
    
    return false;
}

bool EnhancedEsp32Camera::DetectOV5640() {
    // OV5640 detection logic - check sensor ID registers
    ESP_LOGD(TAG, "Attempting to detect OV5640");
    
    sensor_id_t* sensor = esp_camera_sensor_get();
    if (sensor == nullptr) {
        ESP_LOGW(TAG, "Cannot get camera sensor for OV5640 detection");
        return false;
    }
    
    // OV5640 has chip ID 0x5640 at registers 0x300A and 0x300B
    uint8_t chip_id_h = sensor->get_reg(sensor, 0x300A);
    uint8_t chip_id_l = sensor->get_reg(sensor, 0x300B);
    uint16_t chip_id = (chip_id_h << 8) | chip_id_l;
    
    ESP_LOGD(TAG, "OV5640 detection - Chip ID: 0x%04X", chip_id);
    
    // Check for OV5640 signature
    if (chip_id == 0x5640) {
        ESP_LOGI(TAG, "OV5640 camera sensor detected");
        return true;
    }
    
    return false;
}

bool EnhancedEsp32Camera::InitializeOV2640() {
    ESP_LOGD(TAG, "Initializing OV2640 specific settings");
    
    sensor_id_t* sensor = esp_camera_sensor_get();
    if (sensor == nullptr) {
        ESP_LOGE(TAG, "Cannot get camera sensor for OV2640 initialization");
        return false;
    }
    
    // OV2640 specific register configurations
    // Set JPEG quality and format
    sensor->set_quality(sensor, 12);
    sensor->set_colorbar(sensor, 0);
    sensor->set_whitebal(sensor, 1);
    sensor->set_gain_ctrl(sensor, 1);
    sensor->set_exposure_ctrl(sensor, 1);
    sensor->set_hmirror(sensor, 0);
    sensor->set_vflip(sensor, 0);
    
    // OV2640 optimal settings for different frame sizes
    framesize_t frame_size = sensor->status.framesize;
    if (frame_size >= FRAMESIZE_SVGA) {
        // Higher resolution settings
        sensor->set_brightness(sensor, 0);
        sensor->set_contrast(sensor, 0);
        sensor->set_saturation(sensor, 0);
    } else {
        // Lower resolution settings
        sensor->set_brightness(sensor, 1);
        sensor->set_contrast(sensor, 1);
        sensor->set_saturation(sensor, -1);
    }
    
    ESP_LOGI(TAG, "OV2640 initialized with frame size: %d", frame_size);
    return true;
}

bool EnhancedEsp32Camera::InitializeOV3660() {
    ESP_LOGD(TAG, "Initializing OV3660 specific settings");
    
    sensor_id_t* sensor = esp_camera_sensor_get();
    if (sensor == nullptr) {
        ESP_LOGE(TAG, "Cannot get camera sensor for OV3660 initialization");
        return false;
    }
    
    // OV3660 specific register configurations
    // OV3660 supports higher quality settings
    sensor->set_quality(sensor, 10);
    sensor->set_colorbar(sensor, 0);
    sensor->set_whitebal(sensor, 1);
    sensor->set_gain_ctrl(sensor, 1);
    sensor->set_exposure_ctrl(sensor, 1);
    sensor->set_hmirror(sensor, 0);
    sensor->set_vflip(sensor, 0);
    
    // OV3660 enhanced settings
    sensor->set_brightness(sensor, 1);
    sensor->set_contrast(sensor, 1);
    sensor->set_saturation(sensor, 0);
    sensor->set_sharpness(sensor, 0);
    sensor->set_denoise(sensor, 0);
    
    // OV3660 supports better auto-exposure
    sensor->set_ae_level(sensor, 0);
    sensor->set_aec_value(sensor, 300);
    sensor->set_aec2(sensor, 0);
    
    ESP_LOGI(TAG, "OV3660 initialized with enhanced settings");
    return true;
}

bool EnhancedEsp32Camera::InitializeOV5640() {
    ESP_LOGD(TAG, "Initializing OV5640 specific settings");
    
    sensor_id_t* sensor = esp_camera_sensor_get();
    if (sensor == nullptr) {
        ESP_LOGE(TAG, "Cannot get camera sensor for OV5640 initialization");
        return false;
    }
    
    // OV5640 specific register configurations
    // OV5640 supports the highest quality settings
    sensor->set_quality(sensor, 8);
    sensor->set_colorbar(sensor, 0);
    sensor->set_whitebal(sensor, 1);
    sensor->set_gain_ctrl(sensor, 1);
    sensor->set_exposure_ctrl(sensor, 1);
    sensor->set_hmirror(sensor, 0);
    sensor->set_vflip(sensor, 0);
    
    // OV5640 premium settings
    sensor->set_brightness(sensor, 0);
    sensor->set_contrast(sensor, 2);
    sensor->set_saturation(sensor, 1);
    sensor->set_sharpness(sensor, 1);
    sensor->set_denoise(sensor, 1);
    
    // OV5640 advanced auto-exposure and focus
    sensor->set_ae_level(sensor, 0);
    sensor->set_aec_value(sensor, 400);
    sensor->set_aec2(sensor, 0);
    
    // OV5640 supports auto-focus
    if (sensor->set_lens_correction) {
        sensor->set_lens_correction(sensor, 1);
    }
    
    ESP_LOGI(TAG, "OV5640 initialized with premium settings");
    return true;
}

void EnhancedEsp32Camera::ConfigureFlashPin() {
    if (enhanced_config_.flash_pin == GPIO_NUM_NC) {
        return;
    }
    
    // Configure LEDC for PWM flash control
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);
    
    ledc_channel_config_t ledc_channel = {
        .gpio_num = enhanced_config_.flash_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);
    
    ESP_LOGI(TAG, "Flash pin configured on GPIO %d", enhanced_config_.flash_pin);
}

void EnhancedEsp32Camera::SetFlashState(bool on) {
    if (!HasFlash()) {
        return;
    }
    
    if (on) {
        SetFlashLevel(enhanced_config_.flash_level);
    } else {
        SetFlashLevel(0);
    }
}