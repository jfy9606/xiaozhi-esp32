#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "camera/camera_components.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif
 
#define TAG "CompactWifiBoardS3Cam"

class CompactWifiBoardS3Cam : public WifiBoard {
private:
 
    Button boot_button_;
    LcdDisplay* display_;
    bool camera_system_initialized_;
    bool camera_enabled_;

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };        
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeCameraSystem() {
        ESP_LOGI(TAG, "Initializing camera system with vision integration");
        
        // Get web server from component manager
        Web* web_server = nullptr;
        auto& manager = ComponentManager::GetInstance();
        Component* web_component = manager.GetComponent("Web");
        if (web_component) {
            web_server = static_cast<Web*>(web_component);
            ESP_LOGI(TAG, "Found web server for camera system");
        } else {
            ESP_LOGW(TAG, "Web server not found, camera system will work without web interface");
        }
        
        // Get MCP server
        auto& mcp_server = McpServer::GetInstance();
        
        // Use the factory system to set up camera for s3cam board
        if (CameraSystemHelpers::SetupCameraForBoard("bread-compact-wifi-s3cam", web_server, &mcp_server)) {
            camera_system_initialized_ = true;
            camera_enabled_ = true;
            ESP_LOGI(TAG, "Camera system with vision integration initialized successfully");
        } else {
            camera_system_initialized_ = false;
            camera_enabled_ = false;
            ESP_LOGE(TAG, "Failed to initialize camera system");
        }
    }

    void DeinitializeCameraSystem() {
        if (camera_system_initialized_) {
            CameraComponentFactory::DeinitializeCameraSystem();
            camera_system_initialized_ = false;
            camera_enabled_ = false;
            ESP_LOGI(TAG, "Camera system with vision integration deinitialized");
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
        
        // Add long press for camera toggle functionality
        boot_button_.OnLongPress([this]() {
            ToggleCameraState();
        });
    }

    void ToggleCameraState() {
        auto* resource_manager = CameraComponentFactory::GetResourceManager();
        if (!resource_manager) {
            ESP_LOGW(TAG, "Resource manager not available");
            GetDisplay()->ShowNotification("Resource Manager Error");
            return;
        }
        
        bool current_state = resource_manager->IsCameraEnabled();
        bool new_state = !current_state;
        
        ESP_LOGI(TAG, "Toggling camera state from %s to %s", 
                 current_state ? "enabled" : "disabled",
                 new_state ? "enabled" : "disabled");
        
        // Log current resource state before transition
        resource_state_t current_resource_state = resource_manager->GetResourceState();
        ESP_LOGI(TAG, "Current resource state: %d", current_resource_state);
        
        if (new_state) {
            // Enable camera with graceful resource transition
            if (PerformGracefulCameraEnable()) {
                GetDisplay()->ShowNotification("Camera Enabled");
                ESP_LOGI(TAG, "Camera enabled successfully");
            } else {
                GetDisplay()->ShowNotification("Camera Enable Failed");
                ESP_LOGE(TAG, "Failed to enable camera");
            }
        } else {
            // Disable camera with graceful resource transition
            if (PerformGracefulCameraDisable()) {
                GetDisplay()->ShowNotification("Camera Disabled");
                ESP_LOGI(TAG, "Camera disabled successfully");
            } else {
                GetDisplay()->ShowNotification("Camera Disable Failed");
                ESP_LOGE(TAG, "Failed to disable camera");
            }
        }
        
        // Log final resource state after transition
        resource_state_t final_resource_state = resource_manager->GetResourceState();
        ESP_LOGI(TAG, "Final resource state: %d", final_resource_state);
    }

    bool PerformGracefulCameraEnable() {
        ESP_LOGI(TAG, "Starting graceful camera enable");
        
        if (!camera_system_initialized_) {
            ESP_LOGE(TAG, "Camera system not initialized");
            return false;
        }
        
        // Use the factory system to enable camera with vision
        bool result = CameraSystemHelpers::EnableCameraWithVision(true);
        if (result) {
            camera_enabled_ = true;
            ESP_LOGI(TAG, "Graceful camera enable completed");
        } else {
            ESP_LOGE(TAG, "Failed to enable camera with vision");
        }
        
        return result;
    }

    bool PerformGracefulCameraDisable() {
        ESP_LOGI(TAG, "Starting graceful camera disable");
        
        if (!camera_system_initialized_) {
            ESP_LOGW(TAG, "Camera system not initialized");
            return true;
        }
        
        // Use the factory system to disable camera with vision
        bool result = CameraSystemHelpers::EnableCameraWithVision(false);
        if (result) {
            camera_enabled_ = false;
            ESP_LOGI(TAG, "Graceful camera disable completed");
        } else {
            ESP_LOGW(TAG, "Failed to disable camera with vision");
        }
        
        return result;
    }

public:
    CompactWifiBoardS3Cam() :
        boot_button_(BOOT_BUTTON_GPIO),
        camera_system_initialized_(false),
        camera_enabled_(false) {
        
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeCameraSystem();
        
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        
        // Log initialization status
        ESP_LOGI(TAG, "=== Compact WiFi S3Cam Board Initialization Complete ===");
        ESP_LOGI(TAG, "Camera System: %s", camera_system_initialized_ ? "Initialized" : "Failed");
        ESP_LOGI(TAG, "Camera Available: %s", CameraSystemHelpers::IsCameraAvailable() ? "true" : "false");
        ESP_LOGI(TAG, "Vision Active: %s", CameraSystemHelpers::IsVisionActive() ? "true" : "false");
        ESP_LOGI(TAG, "Camera Enabled: %s", camera_enabled_ ? "true" : "false");
        ESP_LOGI(TAG, "Resource State: %d", CameraSystemHelpers::GetResourceState());
        
        // Log camera configuration
        ESP_LOGI(TAG, "Camera Configuration:");
        ESP_LOGI(TAG, "  Auto-detect: %s", CAMERA_AUTO_DETECT_ENABLED ? "Enabled" : "Disabled");
        ESP_LOGI(TAG, "  Default Model: %s", EnhancedEsp32Camera::GetModelNameStatic(CAMERA_DEFAULT_MODEL));
        ESP_LOGI(TAG, "  Flash Pin: %s", CAMERA_FLASH_PIN != GPIO_NUM_NC ? "Configured" : "Disabled");
        
        // Log supported camera models
        int supported_count = EnhancedEsp32Camera::GetSupportedModelsCount();
        ESP_LOGI(TAG, "  Supported Models (%d):", supported_count);
        camera_model_t supported_models[3];
        EnhancedEsp32Camera::GetSupportedModels(supported_models, 3);
        for (int i = 0; i < supported_count; i++) {
            ESP_LOGI(TAG, "    - %s", EnhancedEsp32Camera::GetModelNameStatic(supported_models[i]));
        }
        
        ESP_LOGI(TAG, "Audio Mode: %s", 
#ifdef AUDIO_I2S_METHOD_SIMPLEX
                 "Simplex (No Pin Conflicts)"
#else
                 "Duplex (Potential Pin Conflicts)"
#endif
        );
        ESP_LOGI(TAG, "========================================================");
    }

    virtual ~CompactWifiBoardS3Cam() {
        DeinitializeCameraSystem();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        // Check for resource conflicts before returning audio codec
        if (camera_system_initialized_) {
            resource_state_t state = CameraSystemHelpers::GetResourceState();
            
            // If camera is active and we're using duplex mode, there are pin conflicts:
            // CAMERA_PIN_VSYNC (GPIO_6) conflicts with AUDIO_I2S_GPIO_DIN (GPIO_6)
            // CAMERA_PIN_HREF (GPIO_7) conflicts with AUDIO_I2S_GPIO_DOUT (GPIO_7)  
            // CAMERA_PIN_SIOC (GPIO_5) conflicts with AUDIO_I2S_GPIO_BCLK (GPIO_5)
#ifndef AUDIO_I2S_METHOD_SIMPLEX
            if (state == RESOURCE_CAMERA_ACTIVE) {
                ESP_LOGW(TAG, "Camera active - audio duplex mode has pin conflicts (GPIO 5,6,7)");
                ESP_LOGW(TAG, "Consider disabling camera or using simplex audio mode");
                // Return null to prevent audio initialization with conflicts
                return nullptr;
            }
#endif
            
            // Try to lock resources for audio if idle
            if (state == RESOURCE_IDLE) {
                auto* resource_manager = CameraComponentFactory::GetResourceManager();
                if (resource_manager && !resource_manager->LockResourceForAudio()) {
                    ESP_LOGW(TAG, "Failed to lock resources for audio");
                }
            }
        }

#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    virtual Camera* GetCamera() override {
        // Only return camera if system is initialized and camera is available
        if (camera_system_initialized_ && CameraSystemHelpers::IsCameraAvailable()) {
            auto* resource_manager = CameraComponentFactory::GetResourceManager();
            if (resource_manager && resource_manager->GetResourceState() == RESOURCE_CAMERA_ACTIVE) {
                return CameraComponentFactory::GetEnhancedCamera();
            }
        }
        return nullptr;
    }

    // Additional methods for resource management
    bool IsCameraEnabled() const {
        return camera_enabled_ && CameraSystemHelpers::IsCameraAvailable();
    }

    resource_state_t GetResourceState() const {
        return CameraSystemHelpers::GetResourceState();
    }

    bool SetCameraEnabled(bool enabled) {
        if (!camera_system_initialized_) {
            return false;
        }
        
        bool result = CameraSystemHelpers::SwitchCameraState(enabled);
        if (result) {
            camera_enabled_ = enabled;
            
            // Handle vision integration state change
            auto* vision_integration = CameraComponentFactory::GetVisionIntegration();
            if (vision_integration) {
                vision_integration->HandleCameraStateChange(enabled);
            }
        }
        
        return result;
    }

    // Camera model management
    bool SetCameraModel(camera_model_t model) {
        if (!camera_system_initialized_) {
            ESP_LOGW(TAG, "Camera system not initialized");
            return false;
        }
        
        auto* enhanced_camera = CameraComponentFactory::GetEnhancedCamera();
        if (!enhanced_camera || !EnhancedEsp32Camera::IsModelSupported(model)) {
            ESP_LOGW(TAG, "Cannot set camera model - camera not available or model not supported");
            return false;
        }
        
        if (camera_enabled_) {
            ESP_LOGI(TAG, "Reinitializing camera with new model: %s", enhanced_camera->GetModelName(model));
            
            // Temporarily disable camera
            bool was_enabled = camera_enabled_;
            if (!PerformGracefulCameraDisable()) {
                ESP_LOGE(TAG, "Failed to disable camera for model change");
                return false;
            }
            
            // Update camera model
            enhanced_camera_config_t config = enhanced_camera->GetEnhancedConfig();
            config.model = model;
            config.auto_detect = false; // Disable auto-detect when manually setting model
            
            if (!enhanced_camera->UpdateEnhancedConfig(config)) {
                ESP_LOGE(TAG, "Failed to update camera config");
                return false;
            }
            
            // Re-enable camera if it was enabled
            if (was_enabled) {
                if (!PerformGracefulCameraEnable()) {
                    ESP_LOGE(TAG, "Failed to re-enable camera with new model");
                    return false;
                }
            }
            
            ESP_LOGI(TAG, "Camera model changed successfully to: %s", enhanced_camera->GetModelName(model));
            return true;
        } else {
            // Camera is disabled, just update the config
            enhanced_camera_config_t config = enhanced_camera->GetEnhancedConfig();
            config.model = model;
            config.auto_detect = false;
            
            if (enhanced_camera->UpdateEnhancedConfig(config)) {
                ESP_LOGI(TAG, "Camera model set to: %s (camera disabled)", enhanced_camera->GetModelName(model));
                return true;
            }
        }
        
        return false;
    }

    camera_model_t GetCurrentCameraModel() const {
        if (camera_system_initialized_) {
            auto* enhanced_camera = CameraComponentFactory::GetEnhancedCamera();
            if (enhanced_camera) {
                return enhanced_camera->GetDetectedModel();
            }
        }
        return CAMERA_NONE;
    }

    // System status for debugging and verification
    std::string GetCameraSystemStatus() const {
        if (!camera_system_initialized_) {
            return "{\"status\":\"not_initialized\"}";
        }
        
        return CameraComponentFactory::GetSystemStatusJson();
    }

    bool IsVisionIntegrationActive() const {
        return CameraSystemHelpers::IsVisionActive();
    }

    // Resource state monitoring
    void OnWheelRun(int interval_ms) {
        static uint32_t last_log_time = 0;
        uint32_t current_time = esp_log_timestamp();
        
        // Log resource state every 30 seconds
        if (current_time - last_log_time > 30000) {
            LogResourceState();
            last_log_time = current_time;
        }
        
        // Check for resource conflicts
        CheckResourceConflicts();
    }

private:
    void LogResourceState() {
        if (!camera_system_initialized_) {
            return;
        }
        
        auto* resource_manager = CameraComponentFactory::GetResourceManager();
        if (!resource_manager) {
            return;
        }
        
        camera_switch_state_t state = resource_manager->GetSwitchState();
        bool vision_active = CameraSystemHelpers::IsVisionActive();
        ESP_LOGI(TAG, "Resource State - Enabled: %s, Initialized: %s, State: %d, Model: %s, Vision: %s",
                 state.enabled ? "true" : "false",
                 state.initialized ? "true" : "false",
                 state.resource_state,
                 state.detected_model,
                 vision_active ? "Active" : "Inactive");
    }

    void CheckResourceConflicts() {
        if (!camera_system_initialized_) {
            return;
        }
        
        auto* resource_manager = CameraComponentFactory::GetResourceManager();
        if (!resource_manager) {
            return;
        }
        
        resource_state_t state = resource_manager->GetResourceState();
        
        // Check if camera is supposed to be active but resources are not locked
        if (camera_enabled_ && state != RESOURCE_CAMERA_ACTIVE) {
            ESP_LOGW(TAG, "Camera enabled but resources not active (state: %d)", state);
            
            // Try to recover by re-locking resources
            if (resource_manager->LockResourceForCamera()) {
                ESP_LOGI(TAG, "Successfully recovered camera resources");
                // Update vision state after resource recovery
                auto* vision_integration = CameraComponentFactory::GetVisionIntegration();
                if (vision_integration) {
                    vision_integration->UpdateVisionState();
                }
            } else {
                ESP_LOGE(TAG, "Failed to recover camera resources");
            }
        }
        
        // Check if camera is disabled but resources are still locked
        if (!camera_enabled_ && state == RESOURCE_CAMERA_ACTIVE) {
            ESP_LOGW(TAG, "Camera disabled but resources still locked");
            resource_manager->ReleaseResource();
            ESP_LOGI(TAG, "Released camera resources");
            // Update vision state after resource release
            auto* vision_integration = CameraComponentFactory::GetVisionIntegration();
            if (vision_integration) {
                vision_integration->UpdateVisionState();
            }
        }
        
        // Update vision state periodically
        auto* vision_integration = CameraComponentFactory::GetVisionIntegration();
        if (vision_integration) {
            vision_integration->UpdateVisionState();
        }
    }
};

DECLARE_BOARD(CompactWifiBoardS3Cam);
