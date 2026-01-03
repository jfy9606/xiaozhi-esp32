#pragma once
#include "sdkconfig.h"

#if !defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32S2)
#include <lvgl.h>
#include <thread>
#include <memory>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"
#include "jpg/image_to_jpeg.h"
#include "esp_video_init.h"

struct JpegChunk {
    uint8_t* data;
    size_t len;
};

class Esp32Camera : public Camera {
private:
    struct FrameBuffer {
        uint8_t *data = nullptr;
        size_t len = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        v4l2_pix_fmt_t format = 0;
    } frame_;
    v4l2_pix_fmt_t sensor_format_ = 0;
#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    uint16_t sensor_width_ = 0;
    uint16_t sensor_height_ = 0;
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    int video_fd_ = -1;
    bool streaming_on_ = false;
    bool is_streaming_ = false;
    struct MmapBuffer { void *start = nullptr; size_t length = 0; };
    std::vector<MmapBuffer> mmap_buffers_;
    std::string explain_url_;
    std::string explain_token_;
    std::thread encoder_thread_;

    int flash_pin_ = -1;
    int brightness_ = 0;
    int contrast_ = 0;
    int saturation_ = 0;
    void* fb_ = nullptr;

public:
    Esp32Camera(const esp_video_init_config_t& config);
    ~Esp32Camera();

    virtual void SetExplainUrl(const std::string& url, const std::string& token) override;
    virtual bool Capture() override;
    // 翻转控制函数
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual bool GetHMirror() override;
    virtual bool GetVFlip() override;
    virtual std::string Explain(const std::string& question) override;

    // 额外的控制方法
    virtual bool HasFlash() override;
    virtual bool SetFlashLevel(int level) override;
    virtual int GetFlashLevel() override;
    virtual bool SetBrightness(int brightness) override;
    virtual int GetBrightness() override;
    virtual bool SetContrast(int contrast) override;
    virtual int GetContrast() override;
    virtual bool SetSaturation(int saturation) override;
    virtual int GetSaturation() override;
    virtual bool StartStreaming() override;
    virtual void StopStreaming() override;
    virtual const char* GetSensorName() override;
    virtual void* GetFrame() override;
    virtual void ReturnFrame(void* fb) override;

    virtual bool Initialize() override { return true; }
    virtual void Deinitialize() override {}
    virtual bool IsInitialized() const override { return true; }
};

#else // CONFIG_IDF_TARGET_ESP32 or ESP32S3 or ESP32S2 is defined

#include <esp_camera.h>
#include "camera.h"
#include <string>

#include "camera_resource_manager.h"

typedef struct {
    camera_model_t model;
    bool auto_detect;
    bool resource_managed;
    bool vision_enabled;
    gpio_num_t flash_pin;
    int flash_level;
} enhanced_camera_config_t;

class Esp32Camera : public Camera {
private:
    camera_config_t config_;
    enhanced_camera_config_t enhanced_config_;
    camera_model_t detected_model_;
    bool initialized_ = false;
    int flash_pin_ = -1;
    void* fb_ = nullptr;
    std::string explain_url_;
    std::string explain_token_;
    CameraResourceManager* resource_manager_ = nullptr;

    int brightness_ = 0;
    int contrast_ = 0;
    int saturation_ = 0;
    bool is_streaming_ = false;

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

public:
    Esp32Camera(const camera_config_t& config);
    Esp32Camera(const camera_config_t& config, const enhanced_camera_config_t& enhanced_config);
    virtual ~Esp32Camera();

    virtual bool Initialize() override;
    virtual void Deinitialize() override;
    virtual bool IsInitialized() const override { return initialized_; }

    // Enhanced functionality
    bool AutoDetectSensor();
    bool SetCameraModel(camera_model_t model);
    camera_model_t GetDetectedModel() const;
    const char* GetModelName(camera_model_t model) const;
    
    // Resource management integration
    bool EnableWithResourceManagement();
    void DisableWithResourceManagement();
    bool IsResourceManaged() const;

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
    static const char* GetModelNameStatic(camera_model_t model);

    virtual void SetExplainUrl(const std::string& url, const std::string& token) override;
    virtual bool Capture() override;
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual bool GetHMirror() override;
    virtual bool GetVFlip() override;
    virtual std::string Explain(const std::string& question) override;

    virtual bool HasFlash() override;
    virtual bool SetFlashLevel(int level) override;
    virtual int GetFlashLevel() override;
    virtual bool SetBrightness(int brightness) override;
    virtual int GetBrightness() override;
    virtual bool SetContrast(int contrast) override;
    virtual int GetContrast() override;
    virtual bool SetSaturation(int saturation) override;
    virtual int GetSaturation() override;
    virtual bool StartStreaming() override;
    virtual void StopStreaming() override;
    virtual const char* GetSensorName() override;
    virtual void* GetFrame() override;
    virtual void ReturnFrame(void* fb) override;
};

#endif // CONFIG_IDF_TARGET_ESP32 or ESP32S3 or ESP32S2
