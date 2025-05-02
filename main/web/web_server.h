#ifndef _WEB_SERVER_H_
#define _WEB_SERVER_H_

#include <esp_http_server.h>
#include <functional>
#include <string>
#include <map>
#include <vector>
#include "../components.h"
#include <unordered_map>
#include <memory>
#include <inttypes.h>
#include <esp_heap_caps.h>

// 配置选项
#ifndef CONFIG_WEB_SERVER_CORE_ID
#define CONFIG_WEB_SERVER_CORE_ID 0
#endif

// 定义PSRAM使用标志 - 为了保持向后兼容
#ifdef CONFIG_ESP32S3_SPIRAM_SUPPORT
#define WEB_SERVER_HAS_PSRAM 1
#else
#define WEB_SERVER_HAS_PSRAM 0
#endif

// 修改内存分配宏 - 根据CONFIG_WEB_SERVER_USE_PSRAM决定是否使用PSRAM
#if defined(CONFIG_WEB_SERVER_USE_PSRAM) && CONFIG_WEB_SERVER_USE_PSRAM && WEB_SERVER_HAS_PSRAM
#define WEB_SERVER_MALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define WEB_SERVER_CALLOC(n, size) heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define WEB_SERVER_FREE(ptr) heap_caps_free(ptr)
#define WEB_SERVER_USE_PSRAM 1
#else
#define WEB_SERVER_MALLOC(size) malloc(size)
#define WEB_SERVER_CALLOC(n, size) calloc(n, size)
#define WEB_SERVER_FREE(ptr) free(ptr)
#define WEB_SERVER_USE_PSRAM 0
#endif

// PSRAM分配器，用于STL容器
#if WEB_SERVER_USE_PSRAM
template <typename T>
class PSRAMAllocator {
public:
    typedef T value_type;
    typedef T* pointer;
    typedef const T* const_pointer;
    typedef T& reference;
    typedef const T& const_reference;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    template <typename U>
    struct rebind {
        typedef PSRAMAllocator<U> other;
    };

    PSRAMAllocator() = default;
    template <class U> PSRAMAllocator(const PSRAMAllocator<U>&) {}

    T* allocate(std::size_t n) {
        T* ptr = static_cast<T*>(WEB_SERVER_MALLOC(n * sizeof(T)));
        if (!ptr) throw std::bad_alloc();
        return ptr;
    }

    void deallocate(T* p, std::size_t) {
        WEB_SERVER_FREE(p);
    }

    template<typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        new ((void*)p) U(std::forward<Args>(args)...);
    }

    template<typename U>
    void destroy(U* p) {
        p->~U();
    }

    bool operator==(const PSRAMAllocator&) const { return true; }
    bool operator!=(const PSRAMAllocator&) const { return false; }
};

// 使用PSRAM的字符串类型
using PSRAMString = std::basic_string<char, std::char_traits<char>, PSRAMAllocator<char>>;

// 使用PSRAM的容器类型
template <typename Key, typename T>
using PSRAMUnorderedMap = std::unordered_map<Key, T, std::hash<Key>, std::equal_to<Key>, 
                            PSRAMAllocator<std::pair<const Key, T>>>;

template <typename T>
using PSRAMVector = std::vector<T, PSRAMAllocator<T>>;
#else
// 不使用PSRAM时的类型别名
using PSRAMString = std::string;
template <typename Key, typename T>
using PSRAMUnorderedMap = std::unordered_map<Key, T>;
template <typename T>
using PSRAMVector = std::vector<T>;
#endif

// HTTP方法定义 - 确保HTTP_ANY能够正确工作
#ifndef HTTP_ANY
#define HTTP_ANY ((httpd_method_t)(1 << 8))  // 使用一个不与其他方法冲突的值
#endif

// 请求处理器类型定义
using HttpRequestHandler = std::function<esp_err_t(httpd_req_t*)>;

// WebSocket消息处理器定义
using WebSocketMessageHandler = std::function<void(int client_id, const PSRAMString& message, const PSRAMString& type)>;

// 旧版WebSocket消息回调定义(用于向后兼容)
using WebSocketMessageCallback = std::function<void(int, const PSRAMString&)>;

// WebSocket客户端定义
struct WebSocketClient {
    int fd;                 // 文件描述符
    bool connected;         // 连接状态
    int64_t last_activity;  // 上次活动时间 (毫秒)
    PSRAMString client_type; // 客户端类型 (如 "motor", "vision", "ai" 等)
};

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

    // 注册处理指定类型的WebSocket消息的处理器
    void RegisterWebSocketHandler(const PSRAMString& message_type, WebSocketMessageHandler handler);
    
    // 注册HTTP路径处理器
    void RegisterHttpHandler(const PSRAMString& path, httpd_method_t method, HttpRequestHandler handler);
    
    // 发送WebSocket消息给指定客户端
    bool SendWebSocketMessage(int client_index, const PSRAMString& message);
    
    // 广播WebSocket消息给所有客户端
    void BroadcastWebSocketMessage(const PSRAMString& message, const PSRAMString& client_type = "");
    
    // 获取服务器句柄
    httpd_handle_t GetServer() const { return server_; }
    
    // 初始化Web组件并注册到ComponentManager
    static void InitWebComponents();
    
    // 启动Web组件（在网络和AI初始化后调用）
    static bool StartWebComponents();
    
    // 定期调用来清理过期的WebSocket连接
    void CheckWebSocketTimeouts();
    
    //================= 向后兼容接口 =================
    
    // 判断URI是否已注册 (向后兼容)
    bool IsUriRegistered(const char* uri);
    
    // 注册URI处理函数 (向后兼容)
    void RegisterUri(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *req), void* user_ctx = nullptr);
    
    // 注册WebSocket URI (向后兼容)
    void RegisterWebSocket(const char* uri, WebSocketMessageCallback callback);
    
    // 检查WebSocket回调是否已设置 (向后兼容)
    bool HasWebSocketCallback() const;
    
    // 调用WebSocket回调函数 (向后兼容)
    void CallWebSocketCallback(int client_index, const PSRAMString& message);

private:
    // HTTP路径处理器
    static esp_err_t RootHandler(httpd_req_t* req);     // 处理主页请求 "/"
    static esp_err_t VisionHandler(httpd_req_t* req);   // 处理视觉页请求 "/vision"
    static esp_err_t MotorHandler(httpd_req_t* req);    // 处理电机页请求 "/motor"
    static esp_err_t ApiHandler(httpd_req_t* req);      // 处理API请求 "/api"
    static esp_err_t WebSocketHandler(httpd_req_t* req); // 处理WebSocket请求 "/ws"
    
    // 处理WebSocket消息
    void HandleWebSocketMessage(int client_id, const PSRAMString& message);
    
    // 添加WebSocket客户端
    int AddWebSocketClient(int fd, const PSRAMString& client_type = "");
    
    // 移除WebSocket客户端
    void RemoveWebSocketClient(int index);
    
    // 关闭所有WebSocket连接
    void CloseAllWebSocketConnections();
    
    // 注册默认的HTTP处理器
    void RegisterDefaultHandlers();
    
    // 发送HTTP响应
    static esp_err_t SendHttpResponse(httpd_req_t* req, const char* content_type, const char* data, size_t len);
    
    // 判断参数类型，返回mime类型
    static const char* GetContentType(const PSRAMString& path);
    
    // 服务器实例与状态
    httpd_handle_t server_;
    bool running_;
    
    // 常量定义
    static constexpr int MAX_WS_CLIENTS = 8;         // 最大WebSocket客户端数量
    static constexpr int64_t WS_TIMEOUT_MS = 30000;  // WebSocket超时时间 (毫秒)
    
    // WebSocket客户端数组
    WebSocketClient ws_clients_[MAX_WS_CLIENTS];
    
    // 注册的HTTP处理器
    PSRAMUnorderedMap<PSRAMString, std::pair<httpd_method_t, HttpRequestHandler>> http_handlers_;
    
    // 注册的WebSocket消息处理器
    PSRAMUnorderedMap<PSRAMString, WebSocketMessageHandler> ws_handlers_;
    
    // 保存旧版WebSocket回调 (向后兼容)
    WebSocketMessageCallback legacy_ws_callback_;
};

#endif // _WEB_SERVER_H_ 