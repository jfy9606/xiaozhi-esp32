#ifndef _WEB_SERVER_H_
#define _WEB_SERVER_H_

#include <esp_http_server.h>
#include <functional>
#include <string>
#include <map>
#include <vector>
#include "../components.h"

// WebSocket客户端定义
struct WebSocketClient {
    int fd;
    bool connected;
    int64_t last_activity;
};

// WebSocket消息处理回调
using WebSocketMessageCallback = std::function<void(int client_index, const std::string& message)>;

// Web服务器组件
class WebServer : public Component {
public:
    WebServer();
    virtual ~WebServer();

    // Component接口实现
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;

    // 注册URI处理函数
    void RegisterUri(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *req), void* user_ctx = nullptr);
    
    // 注册WebSocket URI
    void RegisterWebSocket(const char* uri, WebSocketMessageCallback callback);
    
    // 发送WebSocket消息给指定客户端
    bool SendWebSocketMessage(int client_index, const std::string& message);
    
    // 广播WebSocket消息给所有客户端
    void BroadcastWebSocketMessage(const std::string& message);
    
    // 获取服务器句柄
    httpd_handle_t GetServer() const { return server_; }
    
    // 初始化Web组件并注册到ComponentManager
    static void InitWebComponents();

private:
    httpd_handle_t server_;
    bool running_;
    
    static constexpr int MAX_WS_CLIENTS = 4;
    static constexpr int64_t WS_TIMEOUT_MS = 30000; // 30秒超时
    
    // WebSocket客户端数组
    WebSocketClient ws_clients_[MAX_WS_CLIENTS];
    
    // WebSocket处理回调
    WebSocketMessageCallback ws_callback_;
    
    // WebSocket URI处理函数
    static esp_err_t WebSocketHandler(httpd_req_t *req);
    
    // 添加WebSocket客户端
    int AddWebSocketClient(int fd);
    
    // 移除WebSocket客户端
    void RemoveWebSocketClient(int index);
    
    // 关闭所有WebSocket连接
    void CloseAllWebSocketConnections();
    
    // 检查WebSocket客户端超时
    void CheckWebSocketTimeouts();
    
    // URI处理回调集合
    struct UriHandler {
        httpd_uri_t uri_config;
        bool registered;
    };
    std::vector<UriHandler> uri_handlers_;
};

#endif // _WEB_SERVER_H_ 