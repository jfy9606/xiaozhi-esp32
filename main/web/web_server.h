#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <esp_http_server.h>
#include <string>
#include <map>
#include <functional>
#include <cinttypes>
#include "sdkconfig.h"
#include "../components.h"
#include <cJSON.h>

// 添加服务器相关宏定义
// 无论是否启用WEB_CONTENT，WebServer都应该可以工作
#if defined(CONFIG_ENABLE_WEB_SERVER)
#define WEB_SERVER_ENABLED 1
#else
#define WEB_SERVER_ENABLED 0
#endif

#if defined(CONFIG_ENABLE_WEB_CONTENT)
#define WEB_CONTENT_ENABLED 1
#else
#define WEB_CONTENT_ENABLED 0
#endif

// 检查是否支持PSRAM
#if defined(CONFIG_SPIRAM) && defined(CONFIG_SPIRAM_USE_MALLOC)
#define WEB_SERVER_HAS_PSRAM 1
#else
#define WEB_SERVER_HAS_PSRAM 0
#endif

// 配置内存分配
#if defined(CONFIG_WEB_SERVER_USE_PSRAM) && WEB_SERVER_HAS_PSRAM
#define WEB_SERVER_USE_PSRAM 1
#include <esp_heap_caps.h>
#define WEB_SERVER_MALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define WEB_SERVER_FREE(ptr) heap_caps_free(ptr)
#else
#define WEB_SERVER_USE_PSRAM 0
#define WEB_SERVER_MALLOC(size) malloc(size)
#define WEB_SERVER_FREE(ptr) free(ptr)
#endif

// ESP-IDF WebSocket相关函数声明（如果没有包含在esp_http_server.h中）
#ifndef ESP_IDF_VERSION_VAL
#define ESP_IDF_VERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | (patch))
#endif

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(4, 4, 0)
// 为旧版本ESP-IDF添加WebSocket函数声明
#ifndef httpd_ws_get_fd_session_ctx
extern "C" httpd_req_t *httpd_ws_get_fd_session_ctx(httpd_handle_t hd, int sockfd);
#endif

#ifndef httpd_sess_is_async
extern "C" bool httpd_sess_is_async(httpd_handle_t handle, int sockfd);
#endif

#ifndef httpd_ws_get_frame_type
extern "C" httpd_ws_type_t httpd_ws_get_frame_type(httpd_req_t *req);
#endif
#endif

// 使用PSRAM的字符串类型
#if WEB_SERVER_USE_PSRAM
class PSRAMString : public std::string {
public:
    PSRAMString() : std::string() {}
    PSRAMString(const char* s) : std::string(s) {}
    PSRAMString(const std::string& s) : std::string(s) {}
    
    void* operator new(size_t size) {
        return WEB_SERVER_MALLOC(size);
    }
    
    void operator delete(void* ptr) {
        WEB_SERVER_FREE(ptr);
    }
};
#else
// 如果不使用PSRAM，则使用标准std::string
typedef std::string PSRAMString;
#endif

// WebSocket超时设置
#define MAX_WS_CLIENTS 7
#define WS_TIMEOUT_MS 60000  // 60秒超时

// WebSocket客户端结构
struct WebSocketClient {
    int fd;
    bool connected;
    int64_t last_activity;
    PSRAMString client_type;
};

// HTTP请求处理器类型
typedef std::function<esp_err_t(httpd_req_t*)> HttpRequestHandler;

// WebSocket消息处理器类型
typedef std::function<void(int client_index, const PSRAMString& message, const PSRAMString& type)> WebSocketMessageHandler;

// WebSocket消息回调函数（向后兼容旧接口）
typedef void (*WebSocketMessageCallback)(int client_index, const PSRAMString& message);

class WebServer : public Component {
public:
    WebServer();
    virtual ~WebServer();

    bool Start() override;
    void Stop() override;
    bool IsRunning() const override;
    const char* GetName() const override;

    // 初始化状态追踪
    bool IsInitialized() const { return is_initialized_; }
    void SetInitialized(bool initialized) { is_initialized_ = initialized; }

    // 注册HTTP处理器
    void RegisterHttpHandler(const PSRAMString& path, httpd_method_t method, HttpRequestHandler handler);

    // 注册WebSocket消息处理器
    void RegisterWebSocketHandler(const PSRAMString& message_type, WebSocketMessageHandler handler);

    // 发送WebSocket消息
    bool SendWebSocketMessage(int client_index, const PSRAMString& message);

    // 广播WebSocket消息
    void BroadcastWebSocketMessage(const PSRAMString& message, const PSRAMString& client_type = "");

    // 检查WebSocket超时连接
    void CheckWebSocketTimeouts();

    // 添加WebSocket客户端
    int AddWebSocketClient(int fd, const PSRAMString& client_type = "");

    // 移除WebSocket客户端
    void RemoveWebSocketClient(int index);

    // 关闭所有WebSocket连接
    void CloseAllWebSocketConnections();

    // 获取当前活动的WebSocket客户端数量
    int GetActiveWebSocketClientCount() const;

    // 向后兼容方法
    bool IsUriRegistered(const char* uri);
    void RegisterUri(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *req), void* user_ctx = nullptr);
    void RegisterWebSocket(const char* uri, WebSocketMessageCallback callback);
    bool HasWebSocketCallback() const;
    void CallWebSocketCallback(int client_index, const PSRAMString& message);

    // 静态HTTP处理器函数
    static esp_err_t RootHandler(httpd_req_t *req);
    static esp_err_t WebSocketHandler(httpd_req_t *req);
    static esp_err_t ApiHandler(httpd_req_t *req);
    static esp_err_t VisionHandler(httpd_req_t *req);
    static esp_err_t MotorHandler(httpd_req_t *req);
    static esp_err_t AIHandler(httpd_req_t *req);
    static esp_err_t CarControlHandler(httpd_req_t *req);
    static esp_err_t CameraControlHandler(httpd_req_t *req);
    static esp_err_t CameraStreamHandler(httpd_req_t *req);
    static esp_err_t SystemStatusHandler(httpd_req_t *req);
    static esp_err_t SendHttpResponse(httpd_req_t* req, const char* content_type, const char* data, size_t len);
    static const char* GetContentType(const PSRAMString& path);

    // 初始化和启动Web组件的静态方法
    static void InitWebComponents();
    static bool StartWebComponents();

private:
    void RegisterDefaultHandlers();
    void HandleWebSocketMessage(int client_id, const PSRAMString& message);
    PSRAMString GetSystemStatusJson();
    void StartPeriodicStatusUpdates();
    
    // 辅助方法：检查是否可以安全地调用命令（修改为静态方法）
    static bool SafeToInvokeCommand(const cJSON* cmd);

    httpd_handle_t server_;
    bool running_;
    WebSocketClient ws_clients_[MAX_WS_CLIENTS];
    std::map<PSRAMString, std::pair<httpd_method_t, HttpRequestHandler>> http_handlers_;
    std::map<PSRAMString, WebSocketMessageHandler> ws_handlers_;
    WebSocketMessageCallback legacy_ws_callback_;
    bool is_initialized_;
};

#endif // WEB_SERVER_H 