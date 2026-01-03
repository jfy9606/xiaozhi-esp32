#pragma once

#include "../components.h"
#include "../boards/common/camera.h"
#include "../web/web.h"
#include <esp_camera.h>
#include <memory>
#include <vector>
#include <functional>
#include <string>

// Filter for input values
typedef struct {
    size_t size;   // 用于过滤的值的数量
    size_t index;  // 当前值索引
    size_t count;  // 值计数
    int sum;
    int *values;   // 要填充值的数组
} ra_filter_t;

/**
 * Vision组件：整合视觉控制和内容管理功能
 * 
 * 此类负责:
 * 1. 相机硬件控制
 * 2. 视频流处理
 * 3. 图像捕获
 * 4. Web界面交互
 */
class Vision : public Component {
public:
    // 回调类型定义
    using CaptureCallback = std::function<void(const uint8_t* data, size_t len)>;
    using DetectionCallback = std::function<void(const std::string& result)>;
    
    // 构造函数
    Vision(Web* server = nullptr);
    virtual ~Vision();

    // Component接口实现
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;
    virtual ComponentType GetType() const override { return COMPONENT_TYPE_VISION; }

    // 相机控制功能
    bool StartStreaming();
    void StopStreaming();
    bool IsStreaming() const { return is_streaming_; }
    bool Capture(CaptureCallback callback = nullptr);

    camera_fb_t* GetFrame();
    void ReturnFrame(camera_fb_t* fb);
    
    // 设置相机参数
    bool SetBrightness(int brightness);
    bool SetContrast(int contrast);
    bool SetSaturation(int saturation);
    bool SetHMirror(bool enable);
    bool SetVFlip(bool enable);
    int GetLedIntensity() const { return flash_intensity_; }
    bool SetLedIntensity(int intensity);
    
    // AI视觉检测
    bool RunDetection(const std::string& model_name, DetectionCallback callback = nullptr);

    // 状态信息
    std::string GetStatusJson() const;
    bool HasCamera() const { return camera_ != nullptr; }
    
    // WebSocket消息处理
    void HandleWebSocketMessage(int client_index, const std::string& message);

private:
    // 相机相关
    Camera* camera_;
    bool running_;
    bool is_streaming_;
    int flash_intensity_;
    Web* webserver_;
    std::vector<uint32_t> ws_clients_;
    
    // 内部方法
    void DetectCamera();
    Camera* GetBoardCamera();
    void RegisterWebSocketHandlers(Web* webserver);
    void RegisterHttpHandlers(Web* webserver);
    
    void SendStatusUpdate(uint32_t client_id = 0);
    void SendDetectionResult(const std::string& result, uint32_t client_id = 0);
    
    // HTTP处理器
    static esp_err_t HandleVision(httpd_req_t *req);
    static esp_err_t StreamHandler(httpd_req_t *req);
    static esp_err_t CaptureHandler(httpd_req_t *req);
    static esp_err_t LedHandler(httpd_req_t *req);
    static esp_err_t StatusHandler(httpd_req_t *req);
    static esp_err_t CommandHandler(httpd_req_t *req);
    static esp_err_t BmpHandler(httpd_req_t *req);
    
    // 辅助函数
    ra_filter_t* RaFilterInit(ra_filter_t* filter, size_t sample_size);
    int RaFilterRun(ra_filter_t* filter, int value);
    static size_t JpegEncodeStream(void *arg, size_t index, const void *data, size_t len);
};

// 全局初始化函数
void InitVisionComponent(Web* web_server); 