#pragma once

#include "../components.h"
#include <esp_http_server.h>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <memory>

// HTTP方法枚举
enum class HttpMethod {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_PATCH
};

// API响应类型
enum class ApiResponseType {
    JSON,
    TEXT,
    BINARY,
    HTML
};

// API响应结构
struct ApiResponse {
    ApiResponseType type;
    int status_code;
    std::string content;
    std::map<std::string, std::string> headers;

    ApiResponse() : type(ApiResponseType::JSON), status_code(200) {}
    ApiResponse(const std::string& json_content) : type(ApiResponseType::JSON), status_code(200), content(json_content) {}
};

/**
 * @brief Web组件 - 提供HTTP服务器、API路由和内容服务功能
 * 
 * 此类整合了之前的WebServer、WebContent、ApiHandler和ApiRouter功能
 * 主要负责:
 * 1. HTTP服务器管理
 * 2. URL路由
 * 3. 静态文件服务
 * 4. API处理
 * 5. WebSocket支持
 */
class Web : public Component {
public:
    // 处理器类型定义
    using RequestHandler = std::function<esp_err_t(httpd_req_t*)>;
    using ApiHandler = std::function<ApiResponse(httpd_req_t*)>;
    using WebSocketMessageCallback = std::function<void(httpd_req_t*, const std::string&)>;
    using WebSocketClientMessageCallback = std::function<void(int, const std::string&)>;

    // 构造函数和析构函数
    Web(int port = 80);
    virtual ~Web();

    // Component接口实现
    bool Start() override;
    void Stop() override;
    bool IsRunning() const override;
    const char* GetName() const override;

    // HTTP处理器注册
    void RegisterHandler(HttpMethod method, const std::string& uri, RequestHandler handler);
    void RegisterApiHandler(HttpMethod method, const std::string& uri, ApiHandler handler);
    
    // WebSocket支持
    void RegisterWebSocketMessageCallback(WebSocketMessageCallback callback);
    void RegisterWebSocketHandler(const std::string& uri, WebSocketClientMessageCallback callback);
    void BroadcastWebSocketMessage(const std::string& message);
    void SendWebSocketMessage(httpd_req_t* req, const std::string& message);
    bool SendWebSocketMessage(int client_index, const std::string& message);

    // 实用工具方法
    static std::string GetPostData(httpd_req_t* req);
    static std::map<std::string, std::string> ParseQueryParams(httpd_req_t* req);
    static std::string UrlDecode(const std::string& encoded);
    
    // 静态文件处理
    esp_err_t HandleStaticFile(httpd_req_t* req);
    esp_err_t HandleCssFile(httpd_req_t* req);
    esp_err_t HandleJsFile(httpd_req_t* req);
    
    // 内容生成方法
    std::string GetHtml(const std::string& page, const char* accept_language = nullptr);
    
    // 路径标准化
    static std::string NormalizeWebSocketPath(const std::string& uri);

protected:
    // HTTP服务器实例和配置
    httpd_handle_t server_;
    int port_;
    bool running_;
    
    // 处理器映射
    std::map<std::string, RequestHandler> http_handlers_;
    std::map<std::string, ApiHandler> api_handlers_;
    std::map<std::string, WebSocketClientMessageCallback> ws_uri_handlers_;
    
    // WebSocket回调
    std::vector<WebSocketMessageCallback> ws_callbacks_;
    
    // 内部方法
    void InitDefaultHandlers();
    void InitApiHandlers();
    void InitVehicleWebSocketHandlers();
    void InitSensorHandlers();
    
    // 内部静态HTTP处理器
    static esp_err_t InternalRequestHandler(httpd_req_t* req);
    static esp_err_t WebSocketHandler(httpd_req_t* req);
    static esp_err_t RootHandler(httpd_req_t* req);
    static esp_err_t CarHandler(httpd_req_t* req);
    static esp_err_t VisionHandler(httpd_req_t* req);
    static esp_err_t AIHandler(httpd_req_t* req);
    static esp_err_t LocationHandler(httpd_req_t* req);
    
    // 内部静态WebSocket处理方法
    static void WebSocketRecvCb(httpd_handle_t handle, httpd_req_t* req, httpd_ws_frame_t* frame, httpd_ws_type_t type, void* user_ctx);
    
    // API处理方法
    esp_err_t HandleApiRequest(httpd_req_t* req, const std::string& uri);
    
    // 启用静态成员指针，用于回调
    static Web* current_instance_;
    
private:
    // 禁止复制和赋值
    Web(const Web&) = delete;
    Web& operator=(const Web&) = delete;
}; 