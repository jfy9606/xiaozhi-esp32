#ifndef CAMERA_H
#define CAMERA_H

#include <string>

class Camera {
public:
    virtual ~Camera() = default;
    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual bool GetHMirror() = 0;
    virtual bool GetVFlip() = 0;
    virtual std::string Explain(const std::string& question) = 0;

    virtual bool Initialize() = 0;
    virtual void Deinitialize() = 0;
    virtual bool IsInitialized() const = 0;
    
    // 增强控制接口
    virtual bool HasFlash() = 0;
    virtual bool SetFlashLevel(int level) = 0;
    virtual int GetFlashLevel() = 0;
    virtual bool SetBrightness(int brightness) = 0;
    virtual int GetBrightness() = 0;
    virtual bool SetContrast(int contrast) = 0;
    virtual int GetContrast() = 0;
    virtual bool SetSaturation(int saturation) = 0;
    virtual int GetSaturation() = 0;
    virtual bool StartStreaming() = 0;
    virtual void StopStreaming() = 0;
    virtual const char* GetSensorName() = 0;
    
    // 帧获取接口 (使用 void* 避免在基类中包含 esp_camera.h)
    virtual void* GetFrame() = 0;
    virtual void ReturnFrame(void* fb) = 0;
};

#endif // CAMERA_H
