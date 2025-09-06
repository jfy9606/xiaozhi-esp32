#ifndef ESP32_CAMERA_H
#define ESP32_CAMERA_H

#include <esp_camera.h>
#include <lvgl.h>
#include <thread>
#include <memory>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"

struct JpegChunk {
    uint8_t* data;
    size_t len;
};

class Esp32Camera : public Camera {
private:
    camera_fb_t* fb_ = nullptr;
    std::string explain_url_;
    std::string explain_token_;
    std::thread encoder_thread_;
    bool is_streaming_ = false;
    int brightness_ = 0;
    int contrast_ = 0;
    int saturation_ = 0;
    bool hmirror_ = false;
    bool vflip_ = false;
    int flash_pin_ = -1;
    const char* sensor_name_ = "Unknown";

public:
    Esp32Camera(const camera_config_t& config);
    ~Esp32Camera();

    // 基本方法
    virtual void SetExplainUrl(const std::string& url, const std::string& token) override;
    virtual bool Capture() override;
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual std::string Explain(const std::string& question) override;
    
    // 扩展方法
    virtual const char* GetSensorName() override { return sensor_name_; }
    virtual bool HasFlash() override { return flash_pin_ >= 0; }
    virtual bool SetFlashLevel(int level) override;
    virtual int GetBrightness() override { return brightness_; }
    virtual bool SetBrightness(int brightness) override;
    virtual int GetContrast() override { return contrast_; }
    virtual bool SetContrast(int contrast) override;
    virtual int GetSaturation() override { return saturation_; }
    virtual bool SetSaturation(int saturation) override;
    virtual bool GetHMirror() override { return hmirror_; }
    virtual bool GetVFlip() override { return vflip_; }
    virtual bool StartStreaming() override;
    virtual void StopStreaming() override;
    
    // 帧缓冲区管理
    virtual camera_fb_t* GetFrame() override;
    virtual void ReturnFrame(camera_fb_t* fb) override;
};

#endif // ESP32_CAMERA_H