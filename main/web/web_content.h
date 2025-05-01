#ifndef _WEB_CONTENT_H_
#define _WEB_CONTENT_H_

#include <esp_http_server.h>
#include "../components.h"
#include "web_server.h"

// Web内容处理组件
class WebContent : public Component {
public:
    WebContent(WebServer* server);
    virtual ~WebContent();

    // Component接口实现
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;

    // 处理首页
    static esp_err_t HandleRoot(httpd_req_t *req);
    
    // 处理小车控制页面
    static esp_err_t HandleCar(httpd_req_t *req);
    
    // 处理AI页面
    static esp_err_t HandleAI(httpd_req_t *req);
    
    // 处理摄像头页面
    static esp_err_t HandleCamera(httpd_req_t *req);
    
    // 处理WebSocket消息
    void HandleWebSocketMessage(int client_index, const std::string& message);

private:
    WebServer* server_;
    bool running_;
    
    // 初始化URI处理函数
    void InitHandlers();
    
    // 状态查询处理
    static esp_err_t HandleStatus(httpd_req_t *req);
    
    // WiFi配置处理
    static esp_err_t HandleWifiConfig(httpd_req_t *req);
    static esp_err_t HandleWifiSettings(httpd_req_t *req);
};

#endif // _WEB_CONTENT_H_ 