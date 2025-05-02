#ifndef _VISION_CONTENT_H_
#define _VISION_CONTENT_H_

#include <esp_http_server.h>
#include "../components.h"
#include "../web/web_server.h"
#include "vision_controller.h"

// 滑动平均过滤器
typedef struct {
    size_t size;   // 用于过滤的值的数量
    size_t index;  // 当前值索引
    size_t count;  // 值计数
    int sum;
    int *values;   // 要填充值的数组
} ra_filter_t;

// 视觉的Web内容处理组件，集成了摄像头功能
class VisionContent : public Component {
public:
    // 修改构造函数只需要WebServer，VisionController会从ComponentManager获取
    VisionContent(WebServer* server);
    virtual ~VisionContent();

    // Component接口实现
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;

    // 处理摄像头页面 - 作为视觉子系统的一部分
    static esp_err_t HandleCamera(httpd_req_t *req);
    
    // 处理WebSocket消息 - 更新为使用PSRAMString
    void HandleWebSocketMessage(int client_index, const PSRAMString& message);

private:
    WebServer* server_;
    VisionController* vision_controller_;  // 将从ComponentManager获取
    bool running_;
    ra_filter_t ra_filter_;
    
    // 初始化URI处理函数
    void InitHandlers();
    
    // 获取视觉控制器引用
    VisionController* GetVisionController();
    
    // MJPEG流处理函数
    static esp_err_t StreamHandler(httpd_req_t *req);
    
    // 捕获单张图像
    static esp_err_t CaptureHandler(httpd_req_t *req);
    
    // 设置LED强度
    static esp_err_t LedHandler(httpd_req_t *req);
    
    // 状态查询
    static esp_err_t StatusHandler(httpd_req_t *req);
    
    // 分析命令
    static esp_err_t CommandHandler(httpd_req_t *req);
    
    // BMP格式处理
    static esp_err_t BmpHandler(httpd_req_t *req);
    
    // 滑动平均过滤器函数
    static ra_filter_t* RaFilterInit(ra_filter_t* filter, size_t sample_size);
    static int RaFilterRun(ra_filter_t* filter, int value);
    
    // JPEG块编码函数
    static size_t JpegEncodeStream(void *arg, size_t index, const void *data, size_t len);
};

// 初始化视觉组件（包含摄像头功能）
void InitVisionComponents(WebServer* web_server);

#endif // _VISION_CONTENT_H_ 