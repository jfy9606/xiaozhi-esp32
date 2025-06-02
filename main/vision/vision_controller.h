#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include "components.h"
#include "../boards/common/camera.h"

// Forward declarations
class WebServer;
class Board;

/**
 * 视觉控制器类
 * 提供摄像头视频流、拍照、AI视觉分析等功能
 * 设计为与开发板无关，可适配任何支持的摄像头
 */
class VisionController : public Component {
public:
    // 回调函数类型定义
    using CaptureCallback = std::function<void(const uint8_t* data, size_t len)>;
    using DetectionCallback = std::function<void(const std::string& result)>;

    /**
     * 构造函数
     * 自动检测并使用可用的摄像头
     */
    VisionController();
    
    /**
     * 析构函数
     */
    virtual ~VisionController();

    /**
     * 获取组件名称
     * @return 组件名称
     */
    virtual const char* GetName() const override { return "VisionController"; }

    /**
     * 组件启动
     * @return 是否成功启动
     */
    virtual bool Start() override;
    
    /**
     * 组件停止
     */
    virtual void Stop() override;
    
    /**
     * 检查组件是否运行中
     * @return 组件是否运行中
     */
    virtual bool IsRunning() const override;

    /**
     * 初始化视觉控制器
     * @param webserver 网络服务器指针，用于注册API接口
     * @return 初始化是否成功
     */
    bool Initialize(WebServer* webserver);

    /**
     * 启动视频流
     * @return 是否成功启动
     */
    bool StartStreaming();

    /**
     * 停止视频流
     */
    void StopStreaming();

    /**
     * 检查视频流是否活跃
     * @return 视频流是否活跃
     */
    bool IsStreaming() const { return is_streaming_; }

    /**
     * 拍照
     * @param callback 拍照完成回调函数
     * @return 是否成功拍照
     */
    bool Capture(CaptureCallback callback = nullptr);

    /**
     * 获取单帧图像
     * @return 图像帧或nullptr
     */
    camera_fb_t* GetFrame();
    
    /**
     * 返回图像帧缓冲区
     * @param fb 图像帧
     */
    void ReturnFrame(camera_fb_t* fb);

    /**
     * 设置摄像头亮度
     * @param brightness 亮度值，范围-2到2
     * @return 是否设置成功
     */
    bool SetBrightness(int brightness);

    /**
     * 设置摄像头对比度
     * @param contrast 对比度值，范围-2到2
     * @return 是否设置成功
     */
    bool SetContrast(int contrast);

    /**
     * 设置摄像头饱和度
     * @param saturation 饱和度值，范围-2到2
     * @return 是否设置成功
     */
    bool SetSaturation(int saturation);

    /**
     * 设置摄像头水平镜像
     * @param enable 是否启用
     * @return 是否设置成功
     */
    bool SetHMirror(bool enable);

    /**
     * 设置摄像头垂直翻转
     * @param enable 是否启用
     * @return 是否设置成功
     */
    bool SetVFlip(bool enable);
    
    /**
     * 获取LED闪光灯亮度
     * @return 闪光灯亮度（0-255）
     */
    int GetLedIntensity() const { return flash_intensity_; }

    /**
     * 设置闪光灯强度
     * @param intensity 强度值，范围0-255
     * @return 是否设置成功
     */
    bool SetLedIntensity(int intensity);

    /**
     * 运行AI视觉识别
     * @param model_name 模型名称
     * @param callback 识别结果回调函数
     * @return 是否成功启动识别
     */
    bool RunDetection(const std::string& model_name, DetectionCallback callback = nullptr);

    /**
     * 获取摄像头状态JSON
     * @return 摄像头状态JSON字符串
     */
    std::string GetStatusJson() const;

    /**
     * 获取摄像头对象
     * @return 摄像头对象指针
     */
    Camera* GetCamera() const { return camera_; }

    /**
     * 检查是否有可用摄像头
     * @return 是否有可用摄像头
     */
    bool HasCamera() const { return camera_ != nullptr; }

private:
    // 自动检测并获取可用摄像头
    void DetectCamera();
    
    // 从Board获取摄像头
    Camera* GetBoardCamera();

    // 注册WebSocket处理程序
    void RegisterWebSocketHandlers(WebServer* webserver);

    // 注册HTTP处理程序
    void RegisterHttpHandlers(WebServer* webserver);

    // 处理来自WebSocket的消息
    void HandleWebSocketMessage(const std::string& message, uint32_t client_id);

    // 发送状态更新到WebSocket客户端
    void SendStatusUpdate(uint32_t client_id = 0);

    // 发送AI检测结果到WebSocket客户端
    void SendDetectionResult(const std::string& result, uint32_t client_id = 0);

    Camera* camera_ = nullptr;              // 摄像头对象指针
    bool running_ = false;                  // 组件是否运行中
    bool is_streaming_ = false;             // 是否正在进行视频流
    int flash_intensity_ = 0;               // 闪光灯强度
    WebServer* webserver_ = nullptr;        // WebServer对象指针
    std::vector<uint32_t> ws_clients_;      // WebSocket客户端列表
};

/**
 * 初始化视觉组件
 * 创建并初始化VisionController组件
 * 
 * @param webserver WebServer指针
 */
void InitVisionComponents(WebServer* webserver); 