#ifndef CAMERA_H
#define CAMERA_H

#include <string>
#include <esp_camera.h>

class Camera {
public:
    virtual ~Camera() = default;

    // 基本方法
    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual std::string Explain(const std::string& question) = 0;
    
    // 状态检查
    virtual bool IsInitialized() const { return false; }

    // 扩展方法
    virtual const char* GetSensorName() { return "Unknown"; }
    virtual bool HasFlash() { return false; }
    virtual bool SetFlashLevel(int level) { return false; }
    virtual int GetBrightness() { return 0; }
    virtual bool SetBrightness(int brightness) { return false; }
    virtual int GetContrast() { return 0; }
    virtual bool SetContrast(int contrast) { return false; }
    virtual int GetSaturation() { return 0; }
    virtual bool SetSaturation(int saturation) { return false; }
    virtual bool GetHMirror() { return false; }
    virtual bool GetVFlip() { return false; }
    virtual bool StartStreaming() { return false; }
    virtual void StopStreaming() {}
    
    // 帧缓冲区管理
    virtual camera_fb_t* GetFrame() { return nullptr; }
    virtual void ReturnFrame(camera_fb_t* fb) {}
};

#endif // CAMERA_H
