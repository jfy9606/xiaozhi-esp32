/**
 * @file api_definitions.h
 * @brief API接口定义
 * 
 * 定义HTTP和WebSocket API接口的结构和常量
 */

#ifndef API_DEFINITIONS_H
#define API_DEFINITIONS_H

#include <string>
#include <functional>
#include <map>
#include "web_server.h"

// API版本常量
#define API_VERSION "v1"
#define API_BASE_PATH "/api/" API_VERSION

// API路径前缀定义
#define HTTP_API_PREFIX API_BASE_PATH "/http"
#define WS_API_PREFIX API_BASE_PATH "/ws"

// HTTP API路径定义
#define HTTP_API_SYSTEM_INFO HTTP_API_PREFIX "/system/info"
#define HTTP_API_SYSTEM_RESTART HTTP_API_PREFIX "/system/restart"
#define HTTP_API_SERVO_STATUS HTTP_API_PREFIX "/servo/status"
#define HTTP_API_SERVO_ANGLE HTTP_API_PREFIX "/servo/angle"
#define HTTP_API_SERVO_FREQUENCY HTTP_API_PREFIX "/servo/frequency"
#define HTTP_API_DEVICE_CONFIG HTTP_API_PREFIX "/device/config"

// WebSocket API路径定义
#define WS_API_SERVO WS_API_PREFIX "/servo"
#define WS_API_SENSOR WS_API_PREFIX "/sensor"
#define WS_API_AUDIO WS_API_PREFIX "/audio"

// WebSocket消息类型
#define WS_MSG_TYPE_SERVO "servo"
#define WS_MSG_TYPE_SENSOR "sensor"
#define WS_MSG_TYPE_AUDIO "audio"

// API响应状态码
enum class ApiStatusCode {
    OK = 200,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    NOT_FOUND = 404,
    INTERNAL_ERROR = 500
};

// API响应结构体
struct ApiResponse {
    ApiStatusCode status_code;
    std::string message;
    cJSON* data;
    
    ApiResponse(ApiStatusCode code = ApiStatusCode::OK, const std::string& msg = "", cJSON* json_data = nullptr)
        : status_code(code), message(msg), data(json_data) {}
        
    ~ApiResponse() {
        if (data) {
            cJSON_Delete(data);
        }
    }
};

// HTTP API处理器函数类型
typedef std::function<ApiResponse(httpd_req_t*, cJSON*)> HttpApiHandler;

// WebSocket API处理器函数类型
typedef std::function<void(int, cJSON*, const std::string&)> WsApiHandler;

// API路由器类 - 管理API请求路由
class ApiRouter {
public:
    ApiRouter();
    ~ApiRouter();
    
    // 初始化API路由
    void Initialize(WebServer* web_server);
    
    // 注册HTTP API处理器
    void RegisterHttpApi(const std::string& path, httpd_method_t method, HttpApiHandler handler);
    
    // 注册WebSocket API处理器
    void RegisterWsApi(const std::string& type, WsApiHandler handler);
    
    // 获取实例
    static ApiRouter* GetInstance() {
        static ApiRouter instance;
        return &instance;
    }
    
    // HTTP请求处理分发
    static esp_err_t HttpApiHandler(httpd_req_t* req);
    
    // WebSocket消息处理分发
    static void WsApiHandler(int client_id, const std::string& message, const std::string& type);
    
    // 创建错误响应
    static ApiResponse CreateErrorResponse(ApiStatusCode code, const std::string& message);
    
    // 创建成功响应
    static ApiResponse CreateSuccessResponse(cJSON* data = nullptr);
    
private:
    // 解析HTTP请求体为JSON
    static cJSON* ParseRequestJson(httpd_req_t* req);
    
    // 发送API响应
    static esp_err_t SendApiResponse(httpd_req_t* req, const ApiResponse& response);
    
    WebServer* web_server_;
    std::map<std::string, std::pair<httpd_method_t, ::HttpApiHandler>> http_handlers_;
    std::map<std::string, ::WsApiHandler> ws_handlers_;
};

#endif // API_DEFINITIONS_H