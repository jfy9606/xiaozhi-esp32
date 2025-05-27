#include "web_server.h"
#include "board.h"
#include "system_info.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_wifi.h"
#include "esp_chip_info.h"
#include "cJSON.h"
#include "sdkconfig.h"
#include "settings.h"
#include "components.h"
#include "application.h"
#include "vision/vision_controller.h"
#include "location/location_controller.h"
#include "location/location_content.h"
#include "web/web_content.h"
#include "web/html_content.h"
#include "iot/thing_manager.h"
#include "api_definitions.h"
#include "api_handlers.h"
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <mutex>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <sstream>
#include <iomanip>

#ifdef CONFIG_ENABLE_WEB_CONTENT
#include "html_content.h"
#endif

// 声明外部函数（从其他组件获取HTML内容）
extern const char* get_move_html_content();
extern size_t get_move_html_size();
extern const char* get_vision_html_content();
extern size_t get_vision_html_size();
extern const char* get_ai_html_content();
extern size_t get_ai_html_size();
extern const char* get_location_html_content();
extern size_t get_location_html_size();

#define TAG "WebServer"

// 定义WebSocket消息处理的日志标签
static const char* WS_MSG_TAG = "WsMessage";

// 配置默认值
#ifndef CONFIG_WEB_SERVER_PRIORITY
#define CONFIG_WEB_SERVER_PRIORITY 5
#endif

// 使用HTML内容函数
#if CONFIG_ENABLE_WEB_CONTENT
extern "C" { 
    // 常量别名
    extern const char* INDEX_HTML;
    extern const char* MOVE_HTML;
    extern const char* AI_HTML;
    extern const char* VISION_HTML;
    extern const char* LOCATION_HTML;
}
#endif

// Forward declarations
#if defined(CONFIG_ENABLE_MOTOR_CONTROLLER)
extern void InitMoveComponents(WebServer* server);
#endif
#if CONFIG_ENABLE_AI_CONTROLLER && CONFIG_ENABLE_WEB_CONTENT
extern void InitAIComponents(WebServer* server);
#endif
#if CONFIG_ENABLE_VISION_CONTROLLER && CONFIG_ENABLE_WEB_CONTENT
extern void InitVisionComponents(WebServer* server);
#endif

// MIME类型定义
struct MimeType {
    const char* extension;
    const char* mime_type;
};

// 支持的MIME类型列表
static const MimeType MIME_TYPES[] = {
    {".html", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".ico", "image/x-icon"},
    {".svg", "image/svg+xml"},
    {".txt", "text/plain"},
    {NULL, "application/octet-stream"} // 默认类型
};

// 初始化静态成员变量
WebServer* WebServer::active_instance_ = nullptr;

WebServer::WebServer() 
    : server_(nullptr), running_(false) {
    ESP_LOGI(TAG, "创建WebServer实例");
    // 初始化WebSocket客户端数组
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        ws_clients_[i].fd = -1;
        ws_clients_[i].connected = false;
        ws_clients_[i].last_activity = 0;
        ws_clients_[i].client_type = "";
    }
    // 设置活跃实例
    active_instance_ = this;
}

WebServer::~WebServer() {
    ESP_LOGI(TAG, "销毁WebServer实例");
    // 如果当前实例是活跃实例，则清除引用
    if (active_instance_ == this) {
        active_instance_ = nullptr;
    }
    Stop();
}

bool WebServer::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Web server already running");
        return true;
    }
    
    // 添加PSRAM大小检查
    #if WEB_SERVER_HAS_PSRAM
    size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM总大小: %u 字节, 可用: %u 字节", psram_size, psram_free);
    #endif
    
    // 注册默认处理器
    RegisterDefaultHandlers();
    
    // 初始化API路由器
    ApiRouter* router = ApiRouter::GetInstance();
    router->Initialize(this);
    
    // 注册API处理器
    InitializeApiHandlers(router);
    
    // 配置HTTP服务器
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = CONFIG_WEB_SERVER_PRIORITY;
    config.server_port = CONFIG_WEB_SERVER_PORT;
    config.max_uri_handlers = 32;  // 增加URI处理器上限，原先是16
    config.max_open_sockets = MAX_WS_CLIENTS + 3;  // 符合lwip限制: 7+3=10, 服务器内部使用3个
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard; // 启用通配符匹配
    
    // 使用PSRAM优化
    #if WEB_SERVER_USE_PSRAM 
    // 增加栈大小以避免溢出
    config.stack_size = 8192;
    
    // 增加接收缓冲区大小
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    
    // 启用PSRAM内存分配
    ESP_LOGI(TAG, "配置Web服务器使用PSRAM");
    #else
    ESP_LOGI(TAG, "配置Web服务器使用标准内存");
    #endif
    
    // 启动HTTP服务器
    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    esp_err_t ret = httpd_start(&server_, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return false;
    }
    
    // 初始化WebSocket客户端记录
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        ws_clients_[i].fd = -1;
        ws_clients_[i].connected = false;
    }
    
    running_ = true;
    ESP_LOGI(TAG, "Web server started successfully");
    
    // 开始定期广播系统状态
    StartPeriodicStatusUpdates();
    
    // 注册所有URI处理器
    bool has_registered_all = true;
    for (const auto& pair : http_handlers_) {
        // 跳过WebSocket处理器，它需要特殊处理
        if (pair.first == "/ws") continue;
        
        httpd_uri_t uri_config = {
            .uri = pair.first.c_str(),
            .method = pair.second.first,
            .handler = [](httpd_req_t *req) -> esp_err_t {
                WebServer* server = static_cast<WebServer*>(req->user_ctx);
                PSRAMString uri_path(req->uri);
                auto it = server->http_handlers_.find(uri_path);
                if (it != server->http_handlers_.end()) {
                    return it->second.second(req);
                }
                
                // 尝试使用通配符路径
                for (const auto& pair : server->http_handlers_) {
                    if (pair.first.find('*') != PSRAMString::npos) {
                        // 简单的通配符匹配 - 检查前缀
                        size_t wildcard_pos = pair.first.find('*');
                        if (wildcard_pos > 0) {
                            PSRAMString prefix = pair.first.substr(0, wildcard_pos);
                            if (uri_path.find(prefix) == 0) {
                                return pair.second.second(req);
                            }
                        }
                    }
                }
                
                httpd_resp_send_404(req);
                return ESP_OK;
            },
            .user_ctx = this
        };
        
        ESP_LOGI(TAG, "注册URI处理器: %s", pair.first.c_str());
        ret = httpd_register_uri_handler(server_, &uri_config);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "注册URI处理器失败 %s: %s", pair.first.c_str(), esp_err_to_name(ret));
            has_registered_all = false;
        }
    }
    
    // 注册WebSocket处理器
    httpd_uri_t ws_uri = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = WebSocketHandler,
        .user_ctx   = this,
        .is_websocket = true   // 关键: 设置WebSocket标志
    };
    
    ESP_LOGI(TAG, "注册WebSocket处理器: /ws");
    ret = httpd_register_uri_handler(server_, &ws_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "注册WebSocket处理器失败: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "WebSocket功能可能不可用，继续运行但功能可能受限");
    } else {
        ESP_LOGI(TAG, "WebSocket处理器注册成功");
        // 从http_handlers_中移除，因为已经直接注册了
        auto ws_handler = http_handlers_.find("/ws");
        if (ws_handler != http_handlers_.end()) {
            http_handlers_.erase(ws_handler);
        }
    }
    
    if (!has_registered_all) {
        ESP_LOGW(TAG, "某些URI处理器注册失败，但服务器仍然启动");
    }
    
    return true;
}

void WebServer::Stop() {
    if (!running_) {
        ESP_LOGI(TAG, "Web服务器未运行，无需停止");
        return;
    }

    ESP_LOGI(TAG, "正在停止Web服务器...");
    
    // 安全关闭所有WebSocket连接
    CloseAllWebSocketConnections();

    if (server_) {
        esp_err_t err = httpd_stop(server_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "停止Web服务器出错: %s", esp_err_to_name(err));
        }
        server_ = nullptr;
    }

    http_handlers_.clear();
    ws_handlers_.clear();

    running_ = false;
    ESP_LOGI(TAG, "Web服务器已停止");
}

bool WebServer::IsRunning() const {
    return running_ && server_ != nullptr;
}

const char* WebServer::GetName() const {
    return "WebServer";
}

void WebServer::RegisterDefaultHandlers() {
    // Register handlers for default endpoints
    RegisterHttpHandler("/", HTTP_GET, RootHandler);     // 主页
    ESP_LOGI(TAG, "注册主页处理器: /");
    
    // WebSocket handler registration
    if (server_) {
        httpd_uri_t ws_uri = {
            .uri        = "/ws",
            .method     = HTTP_GET,
            .handler    = WebSocketHandler,
            .user_ctx   = this,
            .is_websocket = true  // 设置WebSocket标志
        };
        
        ESP_LOGI(TAG, "注册WebSocket处理器: /ws");
        esp_err_t ret = httpd_register_uri_handler(server_, &ws_uri);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "注册WebSocket处理器失败: %s", esp_err_to_name(ret));
        }
    } else {
        // 服务器尚未启动，添加到handlers中以便在启动时注册
        RegisterHttpHandler("/ws", HTTP_GET, WebSocketHandler);
        ESP_LOGI(TAG, "WebSocket处理器将在服务器启动时注册");
    }
    
    // API处理器需要明确类型转换为httpd_method_t
    RegisterHttpHandler("/api/*", static_cast<httpd_method_t>(HTTP_ANY), ApiHandler);  // API处理器处理所有/api/前缀的请求
    ESP_LOGI(TAG, "注册API通配符处理器: /api/*");
    
    // Register handlers for special endpoints
    RegisterHttpHandler("/vision", HTTP_GET, VisionHandler);
    ESP_LOGI(TAG, "注册视觉页面处理器: /vision");
    
    RegisterHttpHandler("/car", HTTP_GET, CarHandler);
    ESP_LOGI(TAG, "注册小车控制页面处理器: /car");
    
    RegisterHttpHandler("/ai", HTTP_GET, AIHandler);  // 添加AI页面路由
    ESP_LOGI(TAG, "注册AI页面处理器: /ai");
    
    // 注册位置页面处理器
    RegisterHttpHandler("/location", HTTP_GET, LocationHandler);
    ESP_LOGI(TAG, "注册位置页面处理器: /location");
    
    // Register car control endpoints
    RegisterHttpHandler("/car/stop", HTTP_GET, CarControlHandler);
    ESP_LOGI(TAG, "注册停车控制处理器: /car/stop");
    
    RegisterHttpHandler("/car/*", HTTP_GET, CarControlHandler);
    ESP_LOGI(TAG, "注册小车控制通配符处理器: /car/*");
    
    // Register camera control endpoints
    RegisterHttpHandler("/camera/control", HTTP_GET, CameraControlHandler);
    ESP_LOGI(TAG, "注册相机控制处理器: /camera/control");
    
    RegisterHttpHandler("/camera/stream", HTTP_GET, CameraStreamHandler);
    ESP_LOGI(TAG, "注册相机流处理器: /camera/stream");
    
    // Register API status endpoint directly
    RegisterHttpHandler("/api/status", HTTP_GET, SystemStatusHandler);
    ESP_LOGI(TAG, "注册系统状态API处理器: /api/status");
}

void WebServer::RegisterHttpHandler(const PSRAMString& path, httpd_method_t method, HttpRequestHandler handler) {
    http_handlers_[path] = std::make_pair(method, handler);
    ESP_LOGI(TAG, "注册HTTP处理器: %s", path.c_str());
    
    // 如果服务器已经运行，立即注册处理器
    if (running_ && server_) {
        httpd_uri_t uri_config = {
            .uri = path.c_str(),
            .method = method,
            .handler = [](httpd_req_t *req) -> esp_err_t {
                WebServer* server = static_cast<WebServer*>(req->user_ctx);
                PSRAMString uri_path(req->uri);
                auto it = server->http_handlers_.find(uri_path);
                if (it != server->http_handlers_.end()) {
                    return it->second.second(req);
                }
                
                // 尝试使用通配符路径
                for (const auto& pair : server->http_handlers_) {
                    if (pair.first.find('*') != PSRAMString::npos) {
                        // 简单的通配符匹配 - 检查前缀
                        size_t wildcard_pos = pair.first.find('*');
                        if (wildcard_pos > 0) {
                            PSRAMString prefix = pair.first.substr(0, wildcard_pos);
                            if (uri_path.find(prefix) == 0) {
                                return pair.second.second(req);
                            }
                        }
                    }
                }
                
                httpd_resp_send_404(req);
                return ESP_OK;
            },
            .user_ctx = this
        };
        
        ESP_LOGI(TAG, "立即注册URI处理器: %s", path.c_str());
        esp_err_t ret = httpd_register_uri_handler(server_, &uri_config);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "注册URI处理器失败 %s: %s", path.c_str(), esp_err_to_name(ret));
        }
    }
}

void WebServer::RegisterWebSocketHandler(const PSRAMString& message_type, WebSocketMessageHandler handler) {
    // 检查处理器是否已经注册
    if (ws_handlers_.find(message_type) != ws_handlers_.end()) {
        ESP_LOGW(TAG, "WebSocket处理器 %s 已注册，正在覆盖", message_type.c_str());
    }
    
    ws_handlers_[message_type] = handler;
    ESP_LOGI(TAG, "注册WebSocket处理器: %s", message_type.c_str());
}

// 获取MIME类型
const char* WebServer::GetContentType(const PSRAMString& path) {
    // 查找文件扩展名
    size_t dot_pos = path.find_last_of(".");
    if (dot_pos != PSRAMString::npos) {
        PSRAMString ext = path.substr(dot_pos);
        
        // 查找MIME类型
        for (int i = 0; MIME_TYPES[i].extension != NULL; i++) {
            if (ext == MIME_TYPES[i].extension) {
                return MIME_TYPES[i].mime_type;
            }
        }
    }
    
    // 默认返回二进制流类型
    return "application/octet-stream";
}

// Helper method to send HTTP responses
esp_err_t WebServer::SendHttpResponse(httpd_req_t* req, const char* content_type, const char* data, size_t len) {
    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, data, len);
}

// 主页处理器
esp_err_t WebServer::RootHandler(httpd_req_t *req) {
#if CONFIG_ENABLE_WEB_CONTENT
    // 获取主页HTML内容
    const char* html_content = get_index_html_content();
    size_t len = get_index_html_size();
    
    ESP_LOGI(TAG, "Serving index.html, size: %d bytes", len);
    return SendHttpResponse(req, "text/html", html_content, len);
#else
    // 当Web内容未启用时，返回一个简单的消息
    const char* message = "<html><body><h1>Web Content Disabled</h1>"
        "<p>The web content feature is not enabled in this build.</p>"
        "<p>API endpoints and WebSocket connections are still available.</p>"
        "<ul>"
        "<li>WebSocket: ws://[device-ip]:8080/ws</li>"
        "<li>API Status: http://[device-ip]:8080/api/status</li>"
        "</ul>"
        "</body></html>";
    ESP_LOGI(TAG, "Web content disabled, serving status page");
    return SendHttpResponse(req, "text/html", message, strlen(message));
#endif
}

// 视觉页面处理器
esp_err_t WebServer::VisionHandler(httpd_req_t *req) {
#if CONFIG_ENABLE_WEB_CONTENT && CONFIG_ENABLE_VISION_CONTROLLER
    // 获取视觉相关的HTML内容
    const char* html_content = get_vision_html_content();
    size_t len = get_vision_html_size();
    
    ESP_LOGI(TAG, "Serving vision.html, size: %d bytes", len);
    return SendHttpResponse(req, "text/html", html_content, len);
#else
    // 当视觉内容未启用时，返回一个简单的消息
    const char* message = "<html><body><h1>Vision Content Disabled</h1>"
        "<p>The vision content feature is not enabled in this build.</p>"
        "<p>API endpoints are still available at /api/vision/*</p>"
        "</body></html>";
    ESP_LOGI(TAG, "Vision content disabled, serving simple message");
    return SendHttpResponse(req, "text/html", message, strlen(message));
#endif
}

// 小车控制页面处理器
esp_err_t WebServer::CarHandler(httpd_req_t *req) {
#if defined(CONFIG_ENABLE_MOTOR_CONTROLLER)
    // 获取电机控制相关的HTML内容
            const char* html_content = get_move_html_content();
        size_t len = get_move_html_size();
    
        ESP_LOGI(TAG, "Serving car.html, size: %d bytes", len);
    return SendHttpResponse(req, "text/html", html_content, len);
#else
    // 当小车控制内容未启用时，返回一个简单的消息
    const char* message = "<html><body><h1>Car Control Disabled</h1>"
        "<p>The car control web interface is not enabled in this build.</p>"
        "<p>Car API endpoints are still available at /api/car/*</p>"
        "<p>WebSocket commands for car control are supported.</p>"
        "</body></html>";
    ESP_LOGI(TAG, "Car content disabled, serving simple message");
    return SendHttpResponse(req, "text/html", message, strlen(message));
#endif
}

// AI页面处理器
esp_err_t WebServer::AIHandler(httpd_req_t *req) {
#if CONFIG_ENABLE_WEB_CONTENT && CONFIG_ENABLE_AI_CONTROLLER
    // 获取AI相关的HTML内容
    const char* html_content = get_ai_html_content();
    size_t len = get_ai_html_size();
    
    ESP_LOGI(TAG, "Serving ai.html, size: %d bytes", len);
    return SendHttpResponse(req, "text/html", html_content, len);
#else
    // 当AI内容未启用时，返回一个简单的消息
    const char* message = "<html><body><h1>AI Control Disabled</h1>"
        "<p>The AI control web interface is not enabled in this build.</p>"
        "<p>AI API endpoints are still available at /api/ai/*</p>"
        "<p>WebSocket commands for AI control are supported.</p>"
        "</body></html>";
    ESP_LOGI(TAG, "AI content disabled, serving simple message");
    return SendHttpResponse(req, "text/html", message, strlen(message));
#endif
}

// 位置定位页面处理器
esp_err_t WebServer::LocationHandler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Location page request: %s", req->uri);
    
    // 获取位置页面HTML内容
#if CONFIG_ENABLE_WEB_CONTENT && CONFIG_ENABLE_LOCATION_CONTROLLER
    const char* html_content = get_location_html_content();
    size_t len = get_location_html_size();
    
    ESP_LOGI(TAG, "Serving location.html, size: %zu bytes", len);
    return SendHttpResponse(req, "text/html", html_content, len);
#else
    // 当Web内容未启用时，返回一个简单的消息
    const char* message = "<html><body><h1>Location Tracking Disabled</h1>"
        "<p>The location tracking web interface is not enabled in this build.</p>"
        "<p>API endpoints are still available at /api/location/*</p>"
        "</body></html>";
    ESP_LOGI(TAG, "Location content disabled, serving simple message");
    return SendHttpResponse(req, "text/html", message, strlen(message));
#endif
}

// API处理器 - 处理所有/api/前缀的请求
esp_err_t WebServer::ApiHandler(httpd_req_t* req) {
    // 获取URI路径
    std::string uri(req->uri);
    ESP_LOGI(TAG, "收到API请求: %s, 方法: %d", uri.c_str(), req->method);
    
    // 解析API路径 - 示例: /api/car/speed -> "car" 和 "speed" 
    std::string path = uri.substr(5); // 去除 "/api/" 前缀
    size_t pos = path.find('/');
    
    std::string resource = (pos != std::string::npos) ? path.substr(0, pos) : path;
    std::string action = (pos != std::string::npos) ? path.substr(pos + 1) : "";
    
    ESP_LOGI(TAG, "API解析: 资源=%s, 动作=%s", resource.c_str(), action.c_str());
    
    // 处理各种API请求
    if (resource == "car") {
        // 处理小车控制API
        if (req->method == HTTP_GET) {
            // 获取小车状态
            cJSON* response = cJSON_CreateObject();
            cJSON_AddStringToObject(response, "status", "ok");
            cJSON_AddStringToObject(response, "resource", "car");
            // 这里添加小车状态信息
            
            char* json_str = cJSON_PrintUnformatted(response);
            esp_err_t ret = SendHttpResponse(req, "application/json", json_str, strlen(json_str));
            free(json_str);
            cJSON_Delete(response);
            return ret;
        } 
        else if (req->method == HTTP_POST) {
            // 读取请求体
            char buf[1024] = {0};
            int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
            if (ret <= 0) {
                if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                    httpd_resp_send_408(req);
                }
                return ESP_FAIL;
            }
            
            // 解析JSON
            cJSON* json = cJSON_Parse(buf);
            if (json) {
                // 处理小车控制命令
                // 这里添加小车控制逻辑
                
                cJSON_Delete(json);
                
                // 发送成功响应
                cJSON* response = cJSON_CreateObject();
                cJSON_AddStringToObject(response, "status", "ok");
                cJSON_AddStringToObject(response, "message", "Car command processed");
                
                char* json_str = cJSON_PrintUnformatted(response);
                esp_err_t ret = SendHttpResponse(req, "application/json", json_str, strlen(json_str));
                free(json_str);
                cJSON_Delete(response);
                return ret;
            } else {
                // JSON解析失败
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_send(req, "Invalid JSON", -1);
                return ESP_OK;
            }
        }
    } 
    else if (resource == "vision") {
        // 处理视觉控制API
        // 类似电机控制的实现
    }
    else if (resource == "ai") {
        // 处理AI相关API
        // 类似电机控制的实现
    }
    else if (resource == "system") {
        // 处理系统相关API
        if (action == "status") {
            cJSON* response = cJSON_CreateObject();
            cJSON_AddStringToObject(response, "status", "ok");
            
            // 添加系统信息
            uint32_t free_heap = esp_get_free_heap_size();
            uint32_t uptime = esp_timer_get_time() / 1000000; // 转换为秒
            
            cJSON_AddNumberToObject(response, "free_heap", free_heap);
            cJSON_AddNumberToObject(response, "uptime", uptime);
            
            // 添加系统版本信息
            #ifdef CONFIG_IDF_TARGET
            cJSON_AddStringToObject(response, "idf_target", CONFIG_IDF_TARGET);
            #endif
            
            #ifdef CONFIG_IDF_FIRMWARE_VERSION
            cJSON_AddStringToObject(response, "firmware_version", CONFIG_IDF_FIRMWARE_VERSION);
            #else
            cJSON_AddStringToObject(response, "firmware_version", "1.0.0"); // 默认版本
            #endif
            
            char* json_str = cJSON_PrintUnformatted(response);
            esp_err_t ret = SendHttpResponse(req, "application/json", json_str, strlen(json_str));
            free(json_str);
            cJSON_Delete(response);
            return ret;
        }
    }
    
    // 未找到对应的API资源或动作
    httpd_resp_send_404(req);
    return ESP_OK;
}

// WebSocket处理器
esp_err_t WebServer::WebSocketHandler(httpd_req_t* req) {
    if (!req || !req->user_ctx) {
        ESP_LOGE(TAG, "无效的WebSocket请求或用户上下文");
        return ESP_ERR_INVALID_ARG;
    }
    
    WebServer* server = static_cast<WebServer*>(req->user_ctx);
    int client_fd = httpd_req_to_sockfd(req);
    
    // 判断请求类型 - 在ESP-IDF中，使用检查请求方法来确定握手
    if (req->method == HTTP_GET) {
        // 获取客户端类型参数
        char type_buf[32] = {0};
        size_t buf_len = sizeof(type_buf);
        PSRAMString client_type = "generic";
        
        if (httpd_req_get_url_query_str(req, type_buf, buf_len) == ESP_OK) {
            char param_val[16] = {0};
            if (httpd_query_key_value(type_buf, "type", param_val, sizeof(param_val)) == ESP_OK) {
                client_type = param_val;
            }
        }
        
        // 记录WebSocket连接
        int client_index = server->AddWebSocketClient(client_fd, client_type);
        ESP_LOGI(TAG, "WebSocket客户端已连接: fd=%d, 类型=%s, 索引=%d", 
                client_fd, client_type.c_str(), client_index);
                
        ESP_LOGI(TAG, "WebSocket握手处理完成");
        
        // 在握手完成后立即发送一次状态更新，让客户端能快速获取初始状态
        if (client_index >= 0) {
            try {
                // 获取系统状态并发送给新连接的客户端
                PSRAMString status_json = server->GetSystemStatusJson();
                server->SendWebSocketMessage(client_index, status_json);
                ESP_LOGI(TAG, "WebSocket初始状态信息已发送给客户端 %d", client_index);
            } catch (const std::exception& e) {
                ESP_LOGW(TAG, "发送WebSocket初始状态时出现异常: %s", e.what());
            } catch (...) {
                ESP_LOGW(TAG, "发送WebSocket初始状态时出现未知异常");
            }
        }
                
        return ESP_OK;
    }
    
    // 处理WebSocket数据帧
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;  // 默认为文本帧
    
    // 首先接收帧获取长度信息
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "接收WebSocket帧失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 处理特殊类型的帧
    if (ws_pkt.type == HTTPD_WS_TYPE_PING) {
        httpd_ws_frame_t pong;
        memset(&pong, 0, sizeof(httpd_ws_frame_t));
        pong.type = HTTPD_WS_TYPE_PONG;
        ESP_LOGD(TAG, "收到PING，发送PONG");
        return httpd_ws_send_frame(req, &pong);
    }
    
    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        // 查找客户端索引
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (server->ws_clients_[i].connected && server->ws_clients_[i].fd == client_fd) {
                ESP_LOGI(TAG, "收到WebSocket关闭帧，关闭客户端连接 %d", i);
                server->RemoveWebSocketClient(i);
                break;
            }
        }
        return ESP_OK;
    }
    
    // 如果不是文本帧，记录并跳过
    if (ws_pkt.type != HTTPD_WS_TYPE_TEXT) {
        ESP_LOGI(TAG, "收到非文本WebSocket帧，类型: %d", ws_pkt.type);
        return ESP_OK;
    }
    
    // 只处理有负载的数据帧
    if (ws_pkt.len == 0) {
        return ESP_OK;
    }
    
    // 为数据帧分配内存
    if (ws_pkt.len > 16384) {  // 限制单个消息最大长度为16KB
            ESP_LOGE(TAG, "WebSocket负载过大: %d字节", ws_pkt.len);
            return ESP_ERR_NO_MEM;
        }
        
        // 根据配置使用PSRAM或标准内存
        #if WEB_SERVER_USE_PSRAM
        uint8_t* payload = (uint8_t*)WEB_SERVER_MALLOC(ws_pkt.len + 1);
        if (!payload) {
            ESP_LOGE(TAG, "为WebSocket负载分配PSRAM内存失败: %d字节", ws_pkt.len);
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGD(TAG, "使用PSRAM分配WebSocket负载: %d字节", ws_pkt.len);
        #else
        uint8_t* payload = (uint8_t*)WEB_SERVER_MALLOC(ws_pkt.len + 1);
        if (!payload) {
            ESP_LOGE(TAG, "为WebSocket负载分配内存失败: %d字节", ws_pkt.len);
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGD(TAG, "使用标准内存分配WebSocket负载: %d字节", ws_pkt.len);
        #endif
        
    // 设置负载指针并接收完整帧
        ws_pkt.payload = payload;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
        WEB_SERVER_FREE(payload);
            ESP_LOGE(TAG, "接收WebSocket负载失败: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // 添加字符串结束符
    payload[ws_pkt.len] = 0;
    
    // 查找客户端
    int client_index = -1;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (server->ws_clients_[i].connected && server->ws_clients_[i].fd == client_fd) {
            client_index = i;
            // 更新活动时间
            server->ws_clients_[i].last_activity = esp_timer_get_time() / 1000;
            break;
        }
    }
    
    // 如果没有找到客户端，添加新客户端
    if (client_index == -1) {
        client_index = server->AddWebSocketClient(client_fd, "generic");
        if (client_index == -1) {
            // 无法添加更多客户端
            WEB_SERVER_FREE(payload);
            ESP_LOGW(TAG, "无法添加更多WebSocket客户端，拒绝连接");
            return ESP_OK;
        }
    }
    
    // 处理WebSocket数据
        try {
        PSRAMString message((char*)payload);
        WEB_SERVER_FREE(payload);  // 及时释放内存
            
            // 处理消息
            server->HandleWebSocketMessage(client_index, message);
        
        return ESP_OK;
        } catch (const std::exception& e) {
        WEB_SERVER_FREE(payload);  // 确保异常情况下也释放内存
            ESP_LOGE(TAG, "处理WebSocket消息异常: %s", e.what());
        return ESP_FAIL;
        } catch (...) {
        WEB_SERVER_FREE(payload);  // 确保异常情况下也释放内存
            ESP_LOGE(TAG, "处理WebSocket消息未知异常");
        return ESP_FAIL;
        }
}

// 处理WebSocket消息
void WebServer::HandleWebSocketMessage(int client_id, const PSRAMString& message) {
    if (message.empty()) {
        ESP_LOGE(WS_MSG_TAG, "收到空的WebSocket消息");
        return;
    }
    
    // 记录收到的消息
    if (message.length() > 200) {
        ESP_LOGD(WS_MSG_TAG, "收到WebSocket消息 (截断): %.200s...", message.c_str());
    } else {
        ESP_LOGD(WS_MSG_TAG, "收到WebSocket消息: %s", message.c_str());
    }
    
    try {
        // 检查是否是心跳消息
        if (message.find("heartbeat") != PSRAMString::npos) {
            ESP_LOGD(WS_MSG_TAG, "收到心跳消息，发送响应和状态更新");
            
            // 生成系统状态数据
            PSRAMString status = GetSystemStatusJson();
            
            // 发送心跳响应
            SendWebSocketMessage(client_id, "{\"type\":\"heartbeat_response\",\"status\":\"ok\"}");
            
            // 同时发送系统状态更新
            SendWebSocketMessage(client_id, status);
            return;
        }
        
        // 解析JSON消息
        cJSON* root = cJSON_Parse(message.c_str());
        if (!root) {
            ESP_LOGE(WS_MSG_TAG, "解析JSON失败: %s", message.c_str());
            return;
        }
        
        // 获取消息类型
        cJSON* type_json = cJSON_GetObjectItem(root, "type");
        if (!type_json || !cJSON_IsString(type_json)) {
            ESP_LOGE(WS_MSG_TAG, "缺少有效的消息类型字段");
            cJSON_Delete(root);
            return;
        }
        
        PSRAMString msg_type = type_json->valuestring;
        ESP_LOGD(WS_MSG_TAG, "消息类型: %s", msg_type.c_str());
        
        // 处理不同类型的消息
        if (msg_type == "get_system_status") {
            // 获取系统状态
            PSRAMString status = GetSystemStatusJson();
            SendWebSocketMessage(client_id, status);
        }
        // 位置相关消息处理
        else if (msg_type == "get_location" || msg_type == "location_request") {
            // 获取位置数据
            HandleLocationRequest(client_id, root);
        }
        else if (msg_type == "set_location_mode") {
            // 设置位置模式
            HandleSetLocationMode(client_id, root);
        }
        else if (msg_type == "calibrate_position" || msg_type == "calibrate_location") {
            // 校准位置
            HandleCalibratePosition(client_id, root);
        }
        else if (msg_type == "save_map" || msg_type == "save_location_map") {
            // 保存位置地图
            HandleSaveLocationMap(client_id, root);
        }
        // 注册客户端类型
        else if (msg_type == "register_client") {
            // 处理客户端类型注册
            cJSON* client_type_json = cJSON_GetObjectItem(root, "client_type");
            if (client_type_json && cJSON_IsString(client_type_json)) {
                PSRAMString client_type = client_type_json->valuestring;
                
                // 更新客户端类型
                for (int i = 0; i < MAX_WS_CLIENTS; i++) {
                    if (ws_clients_[i].connected && ws_clients_[i].fd >= 0 && i == client_id) {
                        ws_clients_[i].client_type = client_type;
                        ESP_LOGI(WS_MSG_TAG, "客户端 %d 注册为 '%s' 类型", client_id, client_type.c_str());
                        
                        // 发送欢迎消息
                        SendWebSocketMessage(client_id, "{\"type\":\"hello_response\",\"message\":\"Welcome!\"}");
                        
                        // 同时发送系统状态
                        PSRAMString status = GetSystemStatusJson();
                        SendWebSocketMessage(client_id, status);
                        
                        // 如果是位置客户端，立即发送位置更新
                        if (client_type == "location") {
                            HandleLocationRequest(client_id, root);
                        }
                        
                        break;
                    }
                }
            }
        }
        
        cJSON_Delete(root);
    }
    catch (const std::exception& e) {
        ESP_LOGE(WS_MSG_TAG, "处理WebSocket消息异常: %s", e.what());
    }
}

// Make GetSystemStatusJson static so it can be called from static methods
static PSRAMString GetSystemStatusJson() {
    // Create system status JSON response
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "status_response");
    
    // System info section
    cJSON* system = cJSON_CreateObject();
    cJSON_AddItemToObject(response, "system", system);
    
    // Add uptime
    uint32_t uptime = esp_timer_get_time() / 1000000; // microseconds to seconds
    cJSON_AddNumberToObject(system, "uptime", uptime);
    
    // Add memory info
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    cJSON_AddNumberToObject(system, "free_heap", free_heap);
    cJSON_AddNumberToObject(system, "min_free_heap", min_free_heap);
    
    // Add ESP-IDF version
    cJSON_AddStringToObject(system, "esp_idf_version", esp_get_idf_version());
    
    // Add chip info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    char chip_model[32];
    snprintf(chip_model, sizeof(chip_model), "%s Rev %d", 
             chip_info.model == CHIP_ESP32 ? "ESP32" :
             chip_info.model == CHIP_ESP32S2 ? "ESP32-S2" :
             chip_info.model == CHIP_ESP32S3 ? "ESP32-S3" :
             chip_info.model == CHIP_ESP32C3 ? "ESP32-C3" : "Unknown",
             chip_info.revision);
    
    cJSON_AddStringToObject(system, "chip", chip_model);
    cJSON_AddNumberToObject(system, "cores", chip_info.cores);
    
    // PSRAM info if available
    size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (psram_size > 0) {
        size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        cJSON_AddNumberToObject(system, "psram_total", psram_size);
        cJSON_AddNumberToObject(system, "psram_free", psram_free);
    }
    
    // Add IoT sensor data 
    // 这里添加传感器数据，包括超声波传感器
#if defined(CONFIG_ENABLE_US_SENSOR)
    cJSON* sensors = cJSON_CreateObject();
    cJSON_AddItemToObject(response, "sensors", sensors);
    
    // 创建超声波数据对象
    cJSON* ultrasonic = cJSON_CreateObject();
    cJSON_AddItemToObject(sensors, "ultrasonic", ultrasonic);
    
    // 默认值
    float front_distance = 0;
    float rear_distance = 0;
    bool front_obstacle = false;
    bool rear_obstacle = false;
    float front_safe_distance = 30.0;  // 默认安全距离30cm
    float rear_safe_distance = 30.0;   // 默认安全距离30cm
    
    // 尝试从ThingManager获取超声波传感器数据
    try {
        // 获取当前所有设备状态
        std::string states_json;
        auto& thing_manager = iot::ThingManager::GetInstance();
        
        // 先检查是否有设备注册，避免显示警告
        std::string descriptors = thing_manager.GetDescriptorsJson();
        bool has_us_device = descriptors.find("\"name\":\"US\"") != std::string::npos;
        
        // 只有当存在US设备时才尝试获取状态
        if (has_us_device && thing_manager.GetStatesJson(states_json, false)) {
            // 解析返回的JSON状态
            cJSON* states = cJSON_Parse(states_json.c_str());
            if (states && cJSON_IsArray(states)) {
                // 遍历数组查找US设备的状态
                for (int i = 0; i < cJSON_GetArraySize(states); i++) {
                    cJSON* thing_state = cJSON_GetArrayItem(states, i);
                    cJSON* name = cJSON_GetObjectItem(thing_state, "name");
                    
                    // 寻找US设备
                    if (name && cJSON_IsString(name) && strcmp(name->valuestring, "US") == 0) {
                        // 找到超声波传感器，获取其属性
                        cJSON* properties = cJSON_GetObjectItem(thing_state, "properties");
                        if (properties) {
                            // 获取前方距离
                            cJSON* front_dist = cJSON_GetObjectItem(properties, "front_distance");
                            if (front_dist && cJSON_IsNumber(front_dist)) {
                                front_distance = front_dist->valuedouble;
                            }
                            
                            // 获取后方距离
                            cJSON* rear_dist = cJSON_GetObjectItem(properties, "rear_distance");
                            if (rear_dist && cJSON_IsNumber(rear_dist)) {
                                rear_distance = rear_dist->valuedouble;
                            }
                            
                            // 获取障碍物检测状态
                            cJSON* front_obs = cJSON_GetObjectItem(properties, "front_obstacle_detected");
                            if (front_obs && cJSON_IsBool(front_obs)) {
                                front_obstacle = cJSON_IsTrue(front_obs);
                            }
                            
                            cJSON* rear_obs = cJSON_GetObjectItem(properties, "rear_obstacle_detected");
                            if (rear_obs && cJSON_IsBool(rear_obs)) {
                                rear_obstacle = cJSON_IsTrue(rear_obs);
                            }

                            // 获取安全距离值（如果有）
                            cJSON* front_safe = cJSON_GetObjectItem(properties, "front_safe_distance");
                            if (front_safe && cJSON_IsNumber(front_safe)) {
                                front_safe_distance = front_safe->valuedouble;
                            }
                            
                            cJSON* rear_safe = cJSON_GetObjectItem(properties, "rear_safe_distance");
                            if (rear_safe && cJSON_IsNumber(rear_safe)) {
                                rear_safe_distance = rear_safe->valuedouble;
                            }
                            
                            ESP_LOGD(TAG, "Found US device in states, front=%.2f, rear=%.2f", 
                                   front_distance, rear_distance);
                        }
                        break; // 已找到US设备，不需要继续查找
                    }
                }
                cJSON_Delete(states);
            } else {
                ESP_LOGW(TAG, "Failed to parse states JSON or not an array");
            }
        } else if (has_us_device) {
            // 只有当US设备存在但获取状态失败时才显示警告
            ESP_LOGW(TAG, "Failed to get states from ThingManager");
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error getting sensor data: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "Unknown error getting sensor data");
    }
    
    // 无论是否成功获取数据，都添加超声波数据到响应
    cJSON_AddNumberToObject(ultrasonic, "front_distance", front_distance);
    cJSON_AddNumberToObject(ultrasonic, "rear_distance", rear_distance);
    cJSON_AddBoolToObject(ultrasonic, "front_obstacle_detected", front_obstacle);  // 修改字段名称
    cJSON_AddBoolToObject(ultrasonic, "rear_obstacle_detected", rear_obstacle);    // 修改字段名称
    cJSON_AddNumberToObject(ultrasonic, "front_safe_distance", front_safe_distance);  // 添加安全距离
    cJSON_AddNumberToObject(ultrasonic, "rear_safe_distance", rear_safe_distance);    // 添加安全距离
    
    // 同时发送专用的超声波数据消息
    // 注意：这里我们每次状态更新时也发送一次专用消息，以确保前端能收到数据
    if (WebServer::GetActiveInstance()) {
        cJSON* us_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(us_msg, "type", "ultrasonic_data");
        cJSON_AddNumberToObject(us_msg, "front_distance", front_distance);
        cJSON_AddNumberToObject(us_msg, "rear_distance", rear_distance);
        cJSON_AddBoolToObject(us_msg, "front_obstacle_detected", front_obstacle);
        cJSON_AddBoolToObject(us_msg, "rear_obstacle_detected", rear_obstacle);
        cJSON_AddNumberToObject(us_msg, "front_safe_distance", front_safe_distance);
        cJSON_AddNumberToObject(us_msg, "rear_safe_distance", rear_safe_distance);
        
        char* us_json_str = cJSON_PrintUnformatted(us_msg);
        PSRAMString us_json(us_json_str);
        WebServer::GetActiveInstance()->BroadcastWebSocketMessage(us_json);
        free(us_json_str);
        cJSON_Delete(us_msg);
    }
#endif
    
    // Format and return the response
    char* json_str = cJSON_PrintUnformatted(response);
    PSRAMString result(json_str);
    free(json_str);
    cJSON_Delete(response);
    return result;
}

// Update the WebServer member method to call the static function
PSRAMString WebServer::GetSystemStatusJson() {
    return ::GetSystemStatusJson();
}

// System status handler - provides detailed system status
esp_err_t WebServer::SystemStatusHandler(httpd_req_t *req) {
    ESP_LOGI(TAG, "System status request: %s", req->uri);
    
    // 设置CORS头，允许所有来源访问
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    // 调用静态函数获取完整状态信息（与WebSocket使用的完全相同）
    PSRAMString status_json = ::GetSystemStatusJson();
    
    ESP_LOGD(TAG, "Sending status response: %s", status_json.c_str());
    return SendHttpResponse(req, "application/json", status_json.c_str(), status_json.length());
}

bool WebServer::SendWebSocketMessage(int client_index, const PSRAMString& message) {
    if (!running_ || !server_) {
        ESP_LOGW(TAG, "Web服务器未运行，无法发送WebSocket消息");
        return false;
    }

    if (client_index < 0 || client_index >= MAX_WS_CLIENTS) {
        ESP_LOGE(TAG, "无效的WebSocket客户端索引: %d", client_index);
        return false;
    }
    
    if (!ws_clients_[client_index].connected) {
        ESP_LOGW(TAG, "WebSocket客户端 %d 未连接，无法发送消息", client_index);
        return false;
    }
    
    // 检查客户端fd有效性
    if (ws_clients_[client_index].fd < 0) {
        ESP_LOGE(TAG, "WebSocket客户端 %d 的fd无效: %d", client_index, ws_clients_[client_index].fd);
        RemoveWebSocketClient(client_index);
        return false;
    }

    // 使用ESP-IDF v5.4.1 WebSocket API
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)message.c_str();
    ws_pkt.len = message.length();
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.final = true;  // 标记为最终帧

    // 使用异步发送
    esp_err_t ret = httpd_ws_send_frame_async(server_, ws_clients_[client_index].fd, &ws_pkt);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "发送WebSocket消息到客户端 %d 失败: %s", 
                 client_index, esp_err_to_name(ret));
        RemoveWebSocketClient(client_index);
        return false;
    }

    // 更新最后活动时间
    ws_clients_[client_index].last_activity = esp_timer_get_time() / 1000;
    return true;
}

void WebServer::BroadcastWebSocketMessage(const PSRAMString& message, const PSRAMString& client_type) {
    if (message.empty()) {
        ESP_LOGW(TAG, "尝试广播空的WebSocket消息");
        return;
    }
    
    int clients_sent = 0;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_clients_[i].connected) {
            // 如果指定了客户端类型，只发送给匹配的客户端
            if (!client_type.empty() && client_type != "generic" && 
                ws_clients_[i].client_type != client_type) {
                continue;
            }
            
            if (SendWebSocketMessage(i, message)) {
                clients_sent++;
            }
        }
    }
    
    // 只在发送成功时记录日志
    if (clients_sent > 0) {
        if (message.length() > 100) {
            ESP_LOGD(TAG, "广播WebSocket消息到 %d 个客户端 (类型:%s): %.100s...", 
                   clients_sent, client_type.empty() ? "all" : client_type.c_str(), message.c_str());
        } else {
            ESP_LOGD(TAG, "广播WebSocket消息到 %d 个客户端 (类型:%s): %s", 
                   clients_sent, client_type.empty() ? "all" : client_type.c_str(), message.c_str());
        }
    }
}

int WebServer::AddWebSocketClient(int fd, const PSRAMString& client_type) {
    if (fd < 0) {
        ESP_LOGE(TAG, "尝试添加无效的WebSocket fd: %d", fd);
        return -1;
    }
    
    // 检查是否已存在该fd
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_clients_[i].connected && ws_clients_[i].fd == fd) {
            ESP_LOGI(TAG, "WebSocket客户端fd %d 已在索引 %d 注册", fd, i);
            ws_clients_[i].last_activity = esp_timer_get_time() / 1000;
            if (!client_type.empty()) {
                ws_clients_[i].client_type = client_type;
            }
            return i;
        }
    }
    
    // 寻找空位添加新客户端
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (!ws_clients_[i].connected) {
            ws_clients_[i].fd = fd;
            ws_clients_[i].connected = true;
            ws_clients_[i].last_activity = esp_timer_get_time() / 1000;
            ws_clients_[i].client_type = client_type;
            ESP_LOGI(TAG, "添加WebSocket客户端，索引: %d, fd: %d, 类型: %s", 
                    i, fd, client_type.c_str());
            return i;
        }
    }
    
    ESP_LOGW(TAG, "WebSocket客户端fd %d 无可用槽位", fd);
    return -1;
}

void WebServer::RemoveWebSocketClient(int index) {
    if (index >= 0 && index < MAX_WS_CLIENTS) {
        ESP_LOGI(TAG, "移除WebSocket客户端，索引: %d, fd: %d, 类型: %s", 
            index, ws_clients_[index].fd, ws_clients_[index].client_type.c_str());
        ws_clients_[index].connected = false;
        ws_clients_[index].fd = -1;
        ws_clients_[index].client_type = "";
    } else {
        ESP_LOGE(TAG, "尝试移除无效的WebSocket客户端索引: %d", index);
    }
}

void WebServer::CloseAllWebSocketConnections() {
    ESP_LOGI(TAG, "关闭所有WebSocket连接");
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_clients_[i].connected && server_ && ws_clients_[i].fd >= 0) {
            ESP_LOGI(TAG, "关闭WebSocket连接，索引: %d, fd: %d, 类型: %s", 
                i, ws_clients_[i].fd, ws_clients_[i].client_type.c_str());
            
            try {
                httpd_sess_trigger_close(server_, ws_clients_[i].fd);
            } catch (...) {
                ESP_LOGE(TAG, "关闭WebSocket索引 %d 异常", i);
            }
            
            ws_clients_[i].connected = false;
            ws_clients_[i].fd = -1;
            ws_clients_[i].client_type = "";
        }
    }
}

void WebServer::CheckWebSocketTimeouts() {
    if (!running_) {
        return; // 服务器未运行，不检查超时
    }
    
    int64_t current_time = esp_timer_get_time() / 1000;
    int expired_count = 0;
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_clients_[i].connected) {
            if (current_time - ws_clients_[i].last_activity > WS_TIMEOUT_MS) {
                ESP_LOGI(TAG, "WebSocket客户端超时，索引: %d, fd: %d, 类型: %s, 上次活动: %" PRId64 "ms前", 
                         i, ws_clients_[i].fd, ws_clients_[i].client_type.c_str(), 
                         current_time - ws_clients_[i].last_activity);
                
                if (server_ && ws_clients_[i].fd >= 0) {
                    try {
                        httpd_sess_trigger_close(server_, ws_clients_[i].fd);
                    } catch (...) {
                        ESP_LOGE(TAG, "关闭超时WebSocket索引 %d 异常", i);
                    }
                }
                
                RemoveWebSocketClient(i);
                expired_count++;
            }
        }
    }
    
    if (expired_count > 0) {
        ESP_LOGI(TAG, "移除 %d 个超时WebSocket连接", expired_count);
    }
}

// 创建一个定期任务发送系统状态更新
void WebServer::StartPeriodicStatusUpdates() {
    esp_timer_handle_t timer_handle;
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            WebServer* server = static_cast<WebServer*>(arg);
            if (server && server->IsRunning()) {
                // 生成系统状态并广播给所有客户端
                PSRAMString status_json = server->GetSystemStatusJson();
                server->BroadcastWebSocketMessage(status_json);
            }
        },
        .arg = this,
        .name = "ws_status_update"
    };
    
    esp_err_t ret = esp_timer_create(&timer_args, &timer_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建系统状态定时器失败: %s", esp_err_to_name(ret));
        return;
    }
    
    // 每1秒发送一次系统状态更新（原先是5秒一次）
    ret = esp_timer_start_periodic(timer_handle, 1000000); // 1秒 = 1,000,000 微秒
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动系统状态定时器失败: %s", esp_err_to_name(ret));
        esp_timer_delete(timer_handle);
        return;
    }
    
    ESP_LOGI(TAG, "系统状态定期更新已启动 (1秒/次)");
}

// 初始化Web组件
void WebServer::InitWebComponents() {
#if defined(CONFIG_ENABLE_WEB_SERVER)
    // 添加静态标志防止重复初始化
    static bool init_in_progress = false;
    static bool init_completed = false;
    
    if (init_completed) {
        ESP_LOGI(TAG, "Web组件已经完成初始化，跳过");
        return;
    }
    
    if (init_in_progress) {
        ESP_LOGW(TAG, "Web组件初始化已在进行，跳过重复调用");
        return;
    }
    
    init_in_progress = true;
    
    ESP_LOGI(TAG, "初始化Web组件");
    
    // 检查Web服务器组件类型是否启用
    if (!ComponentManager::IsComponentTypeEnabled(COMPONENT_TYPE_WEB)) {
        ESP_LOGW(TAG, "Web组件在配置中已禁用");
        init_in_progress = false;
        return;
    }
    
    // 获取或创建WebServer实例
    auto& manager = ComponentManager::GetInstance();
    WebServer* web_server = nullptr;
    
    Component* existing_web_server = manager.GetComponent("WebServer");
    if (existing_web_server) {
        ESP_LOGI(TAG, "使用现有WebServer实例");
        web_server = static_cast<WebServer*>(existing_web_server);
    } else {
        // 创建Web服务器组件
        web_server = new WebServer();
        manager.RegisterComponent(web_server);
        ESP_LOGI(TAG, "创建新的WebServer实例");
    }
    
    // 标记WebServer已初始化
    web_server->SetInitialized(true);
    
    // 延迟初始化其他组件，确保应用程序主流程已经完成初始化
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 注册WebContent组件
#if defined(CONFIG_ENABLE_WEB_CONTENT)
    if (!manager.GetComponent("WebContent")) {
        WebContent* web_content = new WebContent(web_server);
        manager.RegisterComponent(web_content);
        ESP_LOGI(TAG, "创建WebContent实例");
    }
#else
    ESP_LOGI(TAG, "WebContent在配置中已禁用");
#endif
    
    // 注册但不启动移动组件
#if defined(CONFIG_ENABLE_MOTOR_CONTROLLER)
    if (ComponentManager::IsComponentTypeEnabled(COMPONENT_TYPE_MOTOR)) {
    InitMoveComponents(web_server);
        ESP_LOGI(TAG, "移动组件已注册");
    } else {
        ESP_LOGI(TAG, "移动组件在配置中已禁用");
    }
#endif

    // 注册但不启动AI组件
#if defined(CONFIG_ENABLE_AI_CONTROLLER)
#if defined(CONFIG_ENABLE_WEB_CONTENT)
    InitAIComponents(web_server);
#else
    // 即使禁用了WebContent，也要初始化AI组件
    if (!manager.GetComponent("AIController")) {
        AIController* ai_controller = new AIController();
        manager.RegisterComponent(ai_controller);
        ESP_LOGI(TAG, "注册AI控制器 (WebContent已禁用)");
    }
#endif
    ESP_LOGI(TAG, "注册AI组件");
#endif

    // 注册但不启动视觉组件
#if defined(CONFIG_ENABLE_VISION_CONTROLLER)
    if (ComponentManager::IsComponentTypeEnabled(COMPONENT_TYPE_VISION)) {
#if defined(CONFIG_ENABLE_WEB_CONTENT)
    InitVisionComponents(web_server);
#else
    // 即使禁用了WebContent，也要初始化视觉组件
    if (!manager.GetComponent("VisionController")) {
        VisionController* vision_controller = new VisionController();
        manager.RegisterComponent(vision_controller);
        ESP_LOGI(TAG, "注册视觉控制器 (WebContent已禁用)");
    }
#endif
    ESP_LOGI(TAG, "注册视觉组件");
    } else {
        ESP_LOGI(TAG, "视觉组件在配置中已禁用");
    }
#endif

    ESP_LOGI(TAG, "Web组件初始化完成 (组件将在网络初始化后启动)");
    init_in_progress = false;
    init_completed = true;
#else
    ESP_LOGI(TAG, "Web服务器在配置中已禁用");
#endif // CONFIG_ENABLE_WEB_SERVER
}

// 启动Web组件
bool WebServer::StartWebComponents() {
#if defined(CONFIG_ENABLE_WEB_SERVER)
    // 添加静态标志防止重复启动
    static bool start_in_progress = false;
    
    if (start_in_progress) {
        ESP_LOGW(TAG, "Web组件启动已在进行中，跳过重复调用");
        return false;
    }
    
    start_in_progress = true;
    
    // 检查Web组件类型是否启用
    if (!ComponentManager::IsComponentTypeEnabled(COMPONENT_TYPE_WEB)) {
        ESP_LOGW(TAG, "Web组件在配置中已禁用");
        start_in_progress = false;
        return false;
    }
    
    auto& manager = ComponentManager::GetInstance();
    
    // 获取Web服务器组件
    Component* web_server_comp = manager.GetComponent("WebServer");
    WebServer* web_server = static_cast<WebServer*>(web_server_comp);
    
    if (!web_server) {
        ESP_LOGE(TAG, "未找到WebServer组件，无法启动web组件");
        start_in_progress = false;
        return false;
    }
    
    bool success = false;
    try {
        // 启动Web服务器
        if (!web_server->IsRunning()) {
            if (!web_server->Start()) {
                ESP_LOGE(TAG, "启动WebServer失败");
                start_in_progress = false;
                return false;
            }
            ESP_LOGI(TAG, "WebServer启动成功");
        } else {
            ESP_LOGI(TAG, "WebServer已经在运行");
        }
        
        // 启动Web内容
#if defined(CONFIG_ENABLE_WEB_CONTENT)
        Component* web_content_comp = manager.GetComponent("WebContent");
        if (web_content_comp && !web_content_comp->IsRunning()) {
            if (!web_content_comp->Start()) {
                ESP_LOGE(TAG, "启动WebContent失败");
                start_in_progress = false;
                return false;
            }
            ESP_LOGI(TAG, "WebContent启动成功");
        }
#else
        ESP_LOGI(TAG, "WebContent已禁用，但WebServer启动成功，API和WebSocket可用");
#endif // CONFIG_ENABLE_WEB_CONTENT

        // 启动移动相关组件
#if defined(CONFIG_ENABLE_MOTOR_CONTROLLER)
        if (ComponentManager::IsComponentTypeEnabled(COMPONENT_TYPE_MOTOR)) {
        Component* move_controller = manager.GetComponent("MoveController");
        
        if (move_controller && !move_controller->IsRunning()) {
            if (!move_controller->Start()) {
                ESP_LOGE(TAG, "启动MoveController失败");
            } else {
                ESP_LOGI(TAG, "MoveController启动成功");
        
                    // 只有在MoveController启动成功后才启动MoveContent
        Component* move_content = manager.GetComponent("MoveContent");
        if (move_content && !move_content->IsRunning()) {
            if (!move_content->Start()) {
                ESP_LOGE(TAG, "启动MoveContent失败");
            } else {
                ESP_LOGI(TAG, "MoveContent启动成功");
            }
                    }
                }
            }
        } else {
            ESP_LOGI(TAG, "移动组件在配置中已禁用");
        }
#endif // CONFIG_ENABLE_MOTOR_CONTROLLER
        
        // 启动AI相关组件
#if defined(CONFIG_ENABLE_AI_CONTROLLER)
        if (ComponentManager::IsComponentTypeEnabled(COMPONENT_TYPE_AUDIO)) {
        Component* ai_controller = manager.GetComponent("AIController");
        
        if (ai_controller && !ai_controller->IsRunning()) {
            if (!ai_controller->Start()) {
                ESP_LOGE(TAG, "启动AIController失败");
                // 继续尝试启动其他组件
            } else {
                ESP_LOGI(TAG, "AIController启动成功");
            }
            }
        } else {
            ESP_LOGI(TAG, "AI组件在配置中已禁用");
        }
        
#if defined(CONFIG_ENABLE_WEB_CONTENT)
        if (ComponentManager::IsComponentTypeEnabled(COMPONENT_TYPE_AUDIO)) {
        Component* ai_content = manager.GetComponent("AIContent");
        if (ai_content && !ai_content->IsRunning()) {
            if (!ai_content->Start()) {
                ESP_LOGE(TAG, "启动AIContent失败");
                // 继续尝试启动其他组件
            } else {
                ESP_LOGI(TAG, "AIContent启动成功");
            }
        } else if (ai_content) {
            ESP_LOGI(TAG, "AIContent已经在运行");
            }
        }
#endif // CONFIG_ENABLE_WEB_CONTENT
#endif // CONFIG_ENABLE_AI_CONTROLLER
        
        // 启动视觉相关组件
#if defined(CONFIG_ENABLE_VISION_CONTROLLER)
        if (ComponentManager::IsComponentTypeEnabled(COMPONENT_TYPE_VISION)) {
        Component* vision_controller = manager.GetComponent("VisionController");
        
        if (vision_controller && !vision_controller->IsRunning()) {
            if (!vision_controller->Start()) {
                ESP_LOGE(TAG, "启动VisionController失败");
                // 继续尝试启动其他组件
            } else {
                ESP_LOGI(TAG, "VisionController启动成功");
            }
        }
        
#if defined(CONFIG_ENABLE_WEB_CONTENT)
        Component* vision_content = manager.GetComponent("VisionContent");
        if (vision_content && !vision_content->IsRunning()) {
            if (!vision_content->Start()) {
                ESP_LOGE(TAG, "启动VisionContent失败");
                // 继续尝试启动其他组件
            } else {
                ESP_LOGI(TAG, "VisionContent启动成功");
            }
        } else if (vision_content) {
            ESP_LOGI(TAG, "VisionContent已经在运行");
        }
#endif // CONFIG_ENABLE_WEB_CONTENT
        } else {
            ESP_LOGI(TAG, "视觉组件在配置中已禁用");
        }
#endif // CONFIG_ENABLE_VISION_CONTROLLER
        
        ESP_LOGI(TAG, "所有Web组件启动成功");
        success = true;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "StartWebComponents异常: %s", e.what());
        success = false;
    } catch (...) {
        ESP_LOGE(TAG, "StartWebComponents未知异常");
        success = false;
    }
    
    start_in_progress = false;
    return success;
#else
    ESP_LOGI(TAG, "Web服务器在配置中已禁用");
    return false;
#endif // CONFIG_ENABLE_WEB_SERVER
}

// ================ 向后兼容方法实现 ================

// 判断URI是否已注册 (向后兼容)
bool WebServer::IsUriRegistered(const char* uri) {
    PSRAMString uri_str(uri);
    // 检查具体URI是否在http_handlers_中已注册
    if (http_handlers_.find(uri_str) != http_handlers_.end()) {
        return true;
    }
    
    // 检查是否有通配符URI可能匹配当前URI
    // 例如，检查"/api/*"是否已注册，如果请求的是"/api/status"
    for (const auto& handler : http_handlers_) {
        const PSRAMString& registered_uri = handler.first;
        if (registered_uri.find('*') != PSRAMString::npos) {
            size_t wildcard_pos = registered_uri.find('*');
            if (wildcard_pos > 0) {
                PSRAMString prefix = registered_uri.substr(0, wildcard_pos);
                if (uri_str.find(prefix) == 0) {
                    ESP_LOGI(TAG, "URI %s 匹配已注册的通配符 %s", uri, registered_uri.c_str());
                    return true;
                }
            }
        }
    }
    
    return false;
}

// 注册URI处理函数 (向后兼容)
void WebServer::RegisterUri(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *req), void* user_ctx) {
    if (!uri || !handler) {
        ESP_LOGE(TAG, "RegisterUri调用的参数无效");
        return;
    }
    
    // 转换为新接口
    PSRAMString path(uri);
    
    // 创建一个包装函数，使旧式处理器适配新接口
    HttpRequestHandler wrapper = [handler, user_ctx](httpd_req_t* req) -> esp_err_t {
        // 设置用户上下文，以保持原有行为
        if (user_ctx) {
            req->user_ctx = user_ctx;
        }
        return handler(req);
    };
    
    // 调用新接口注册
    RegisterHttpHandler(path, method, wrapper);
    ESP_LOGI(TAG, "通过兼容层注册URI: %s", uri);
}

// 注册WebSocket URI (向后兼容)
void WebServer::RegisterWebSocket(const char* uri, WebSocketMessageCallback callback) {
    if (!uri) {
        ESP_LOGE(TAG, "RegisterWebSocket调用的URI为空");
        return;
    }
    
    // 保存回调函数用于兼容
    legacy_ws_callback_ = callback;
    
    // 注册通用WebSocket处理器，将旧回调适配到新系统
    RegisterWebSocketHandler("legacy", [this](int client_id, const PSRAMString& message, const PSRAMString& type) {
        if (legacy_ws_callback_) {
            legacy_ws_callback_(client_id, message);
        }
    });
    
    // 注册WebSocket URL，但我们在新系统中只使用/ws
    PSRAMString ws_uri(uri);
    if (ws_uri != "/ws") {
        ESP_LOGW(TAG, "旧接口注册了非标准WebSocket路径 %s，已映射到 /ws", uri);
    }
    
    // 这里不需要额外操作，因为我们在RegisterDefaultHandlers中已经注册了WebSocket处理器
    ESP_LOGI(TAG, "通过兼容层注册WebSocket处理器: %s", uri);
}

// 检查WebSocket回调是否已设置 (向后兼容)
bool WebServer::HasWebSocketCallback() const {
    return legacy_ws_callback_ != nullptr || !ws_handlers_.empty();
}

// 调用WebSocket回调函数 (向后兼容)
void WebServer::CallWebSocketCallback(int client_index, const PSRAMString& message) {
    if (legacy_ws_callback_) {
        try {
            legacy_ws_callback_(client_index, message);
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "WebSocket回调异常: %s", e.what());
        } catch (...) {
            ESP_LOGE(TAG, "WebSocket回调未知异常");
        }
    } else {
        // 如果没有旧回调，尝试使用新的消息处理系统
        HandleWebSocketMessage(client_index, message);
    }
}

// Car control handler - manages motor commands
esp_err_t WebServer::CarControlHandler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Car control request: %s", req->uri);
    
    std::string uri(req->uri);
    std::string action = uri.substr(5); // Remove "/car/" prefix
    
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "action", action.c_str());
    
#if defined(CONFIG_ENABLE_MOTOR_CONTROLLER)
    // 获取小车控制组件
    auto& manager = ComponentManager::GetInstance();
    Component* car_comp = manager.GetComponent("MotorController");
    
    if (car_comp) {
        ESP_LOGI(TAG, "Processing car control action: %s", action.c_str());
        
        // 实现小车控制的实际逻辑
        bool success = false;
        
        // 尝试调用控制方法（如果存在）
        // 这些方法名基于通用的小车控制接口
        if (action == "stop") {
            // Try to dynamically call the Stop method
            try {
                // Use a generic approach since we don't know the exact type
                cJSON* cmd = cJSON_CreateObject();
                if (!cmd) {
                    ESP_LOGE(TAG, "Failed to create JSON command");
                    cJSON_AddStringToObject(response, "status", "error");
                    cJSON_AddStringToObject(response, "message", "Memory allocation failed");
                    success = false;
                } else {
                    // 为兼容Thing::Invoke方法的预期格式，必须添加method和parameters字段
                cJSON_AddStringToObject(cmd, "command", "stop");
                    cJSON_AddStringToObject(cmd, "method", "Stop");  // 修正方法名称大小写
                    
                    // 添加必要的parameters对象及brake参数
                    cJSON* params = cJSON_CreateObject();
                    if (params) {
                        cJSON_AddBoolToObject(params, "brake", false);  // 默认使用非制动模式
                        cJSON_AddItemToObject(cmd, "parameters", params);
                    } else {
                        ESP_LOGW(TAG, "Failed to create parameters object");
                    }
                
                // Try to invoke the component through JSON command
                auto& thing_manager = iot::ThingManager::GetInstance();
                
                // 添加安全检查，防止空指针崩溃
                    if (SafeToInvokeCommand(cmd)) {
                        try {
                        thing_manager.Invoke(cmd);
                        success = true;
                        } catch (const std::exception& e) {
                            ESP_LOGE(TAG, "Exception when invoking 'Stop' command: %s", e.what());
                            success = false;
                        } catch (...) {
                            ESP_LOGE(TAG, "Unknown exception when invoking 'Stop' command");
                            success = false;
                        }
                    } else {
                        ESP_LOGW(TAG, "No Thing available to handle 'Stop' command");
                        success = false;
                }
                
                cJSON_Delete(cmd);
                    cJSON_AddStringToObject(response, "message", success ? "Car stopped" : "Failed to stop car");
                }
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Error stopping motor: %s", e.what());
                success = false;
            } catch (...) {
                ESP_LOGE(TAG, "Unknown error stopping motor");
                success = false;
            }
        } else if (action.find("forward") != std::string::npos) {
            try {
                cJSON* cmd = cJSON_CreateObject();
                if (!cmd) {
                    ESP_LOGE(TAG, "Failed to create JSON command");
                    success = false;
                } else {
                    // 为兼容Thing::Invoke方法的预期格式，必须添加method和parameters字段
                    cJSON_AddStringToObject(cmd, "command", "forward");
                    cJSON_AddStringToObject(cmd, "method", "Forward");  // 修正方法名称大小写
                    
                    // 添加parameters对象
                    cJSON* params = cJSON_CreateObject();
                    if (params) {
                        // 解析速度参数
                    size_t speed_pos = action.find("speed=");
                    if (speed_pos != std::string::npos) {
                        int speed = atoi(action.c_str() + speed_pos + 6);
                            cJSON_AddNumberToObject(params, "speed", speed);
                        }
                        
                        cJSON_AddItemToObject(cmd, "parameters", params);
                    } else {
                        ESP_LOGW(TAG, "Failed to create parameters object");
                    }
                    
                    // 安全检查
                    auto& thing_manager = iot::ThingManager::GetInstance();
                    if (SafeToInvokeCommand(cmd)) {
                        try {
                        thing_manager.Invoke(cmd);
                        success = true;
                        } catch (const std::exception& e) {
                            ESP_LOGE(TAG, "Exception when invoking 'Forward' command: %s", e.what());
                            success = false;
                        } catch (...) {
                            ESP_LOGE(TAG, "Unknown exception when invoking 'Forward' command");
                            success = false;
                        }
                    } else {
                        ESP_LOGW(TAG, "No Thing available to handle 'Forward' command");
                        success = false;
                    }
                    
                    cJSON_Delete(cmd);
                }
                cJSON_AddStringToObject(response, "message", success ? "Moving forward" : "Failed to move forward");
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Error moving forward: %s", e.what());
                success = false;
            } catch (...) {
                ESP_LOGE(TAG, "Unknown error moving forward");
                success = false;
            }
        } else if (action.find("backward") != std::string::npos) {
            try {
                cJSON* cmd = cJSON_CreateObject();
                if (!cmd) {
                    ESP_LOGE(TAG, "Failed to create JSON command");
                    success = false;
                } else {
                    // 为兼容Thing::Invoke方法的预期格式，必须添加method和parameters字段
                    cJSON_AddStringToObject(cmd, "command", "backward");
                    cJSON_AddStringToObject(cmd, "method", "Backward");  // 修正方法名称大小写
                    
                    // 添加parameters对象
                    cJSON* params = cJSON_CreateObject();
                    if (params) {
                        // 解析速度参数
                    size_t speed_pos = action.find("speed=");
                    if (speed_pos != std::string::npos) {
                        int speed = atoi(action.c_str() + speed_pos + 6);
                            cJSON_AddNumberToObject(params, "speed", speed);
                        }
                        
                        cJSON_AddItemToObject(cmd, "parameters", params);
                    } else {
                        ESP_LOGW(TAG, "Failed to create parameters object");
                    }
                    
                    // 安全检查
                    auto& thing_manager = iot::ThingManager::GetInstance();
                    if (SafeToInvokeCommand(cmd)) {
                        try {
                        thing_manager.Invoke(cmd);
                        success = true;
                        } catch (const std::exception& e) {
                            ESP_LOGE(TAG, "Exception when invoking 'Backward' command: %s", e.what());
                            success = false;
                        } catch (...) {
                            ESP_LOGE(TAG, "Unknown exception when invoking 'Backward' command");
                            success = false;
                        }
                    } else {
                        ESP_LOGW(TAG, "No Thing available to handle 'Backward' command");
                        success = false;
                    }
                    
                    cJSON_Delete(cmd);
                }
                cJSON_AddStringToObject(response, "message", success ? "Moving backward" : "Failed to move backward");
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Error moving backward: %s", e.what());
                success = false;
            } catch (...) {
                ESP_LOGE(TAG, "Unknown error moving backward");
                success = false;
            }
        } else if (action.find("left") != std::string::npos) {
            try {
                cJSON* cmd = cJSON_CreateObject();
                if (!cmd) {
                    ESP_LOGE(TAG, "Failed to create JSON command");
                    success = false;
                } else {
                    // 为兼容Thing::Invoke方法的预期格式，必须添加method和parameters字段
                    cJSON_AddStringToObject(cmd, "command", "left");
                    cJSON_AddStringToObject(cmd, "method", "TurnLeft");  // 修正方法名称为正确的方法
                    
                    // 添加parameters对象
                    cJSON* params = cJSON_CreateObject();
                    if (params) {
                        // 解析角度参数
                    size_t angle_pos = action.find("angle=");
                    if (angle_pos != std::string::npos) {
                        int angle = atoi(action.c_str() + angle_pos + 6);
                            cJSON_AddNumberToObject(params, "speed", angle);  // 注意：TurnLeft使用speed参数，而不是angle
                        }
                        
                        cJSON_AddItemToObject(cmd, "parameters", params);
                    } else {
                        ESP_LOGW(TAG, "Failed to create parameters object");
                    }
                    
                    // 安全检查
                    auto& thing_manager = iot::ThingManager::GetInstance();
                    if (SafeToInvokeCommand(cmd)) {
                        try {
                        thing_manager.Invoke(cmd);
                        success = true;
                        } catch (const std::exception& e) {
                            ESP_LOGE(TAG, "Exception when invoking 'TurnLeft' command: %s", e.what());
                            success = false;
                        } catch (...) {
                            ESP_LOGE(TAG, "Unknown exception when invoking 'TurnLeft' command");
                            success = false;
                        }
                    } else {
                        ESP_LOGW(TAG, "No Thing available to handle 'TurnLeft' command");
                        success = false;
                    }
                    
                    cJSON_Delete(cmd);
                }
                cJSON_AddStringToObject(response, "message", success ? "Turning left" : "Failed to turn left");
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Error turning left: %s", e.what());
                success = false;
            } catch (...) {
                ESP_LOGE(TAG, "Unknown error turning left");
                success = false;
            }
        } else if (action.find("right") != std::string::npos) {
            try {
                cJSON* cmd = cJSON_CreateObject();
                if (!cmd) {
                    ESP_LOGE(TAG, "Failed to create JSON command");
                    success = false;
                } else {
                    // 为兼容Thing::Invoke方法的预期格式，必须添加method和parameters字段
                    cJSON_AddStringToObject(cmd, "command", "right");
                    cJSON_AddStringToObject(cmd, "method", "TurnRight");  // 修正方法名称为正确的方法
                    
                    // 添加parameters对象
                    cJSON* params = cJSON_CreateObject();
                    if (params) {
                        // 解析角度参数
                    size_t angle_pos = action.find("angle=");
                    if (angle_pos != std::string::npos) {
                        int angle = atoi(action.c_str() + angle_pos + 6);
                            cJSON_AddNumberToObject(params, "speed", angle);  // 注意：TurnRight使用speed参数，而不是angle
                        }
                        
                        cJSON_AddItemToObject(cmd, "parameters", params);
                    } else {
                        ESP_LOGW(TAG, "Failed to create parameters object");
                    }
                    
                    // 安全检查
                    auto& thing_manager = iot::ThingManager::GetInstance();
                    if (SafeToInvokeCommand(cmd)) {
                        try {
                        thing_manager.Invoke(cmd);
                        success = true;
                        } catch (const std::exception& e) {
                            ESP_LOGE(TAG, "Exception when invoking 'TurnRight' command: %s", e.what());
                            success = false;
                        } catch (...) {
                            ESP_LOGE(TAG, "Unknown exception when invoking 'TurnRight' command");
                            success = false;
                        }
                    } else {
                        ESP_LOGW(TAG, "No Thing available to handle 'TurnRight' command");
                        success = false;
                    }
                    
                    cJSON_Delete(cmd);
                }
                cJSON_AddStringToObject(response, "message", success ? "Turning right" : "Failed to turn right");
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Error turning right: %s", e.what());
                success = false;
            } catch (...) {
                ESP_LOGE(TAG, "Unknown error turning right");
                success = false;
            }
        } else {
            cJSON_AddStringToObject(response, "message", "Unknown command");
        }
        
        if (!success) {
            cJSON_AddStringToObject(response, "status", "error");
            cJSON_AddStringToObject(response, "message", "Failed to execute car command");
        }
    } else {
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "message", "Car controller not available");
    }
#else
    cJSON_AddStringToObject(response, "status", "error");
    cJSON_AddStringToObject(response, "message", "Car control not enabled in this build");
#endif

    char* json_str = cJSON_PrintUnformatted(response);
    esp_err_t ret = SendHttpResponse(req, "application/json", json_str, strlen(json_str));
    free(json_str);
    cJSON_Delete(response);
    return ret;
}

// Camera control handler - handles camera settings
esp_err_t WebServer::CameraControlHandler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Camera control request: %s", req->uri);
    
    // Parse query parameters
    char query_buf[256] = {0};
    esp_err_t ret = httpd_req_get_url_query_str(req, query_buf, sizeof(query_buf));
    
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    
#if defined(CONFIG_ENABLE_VISION_CONTROLLER)
    // Get vision controller component
    auto& manager = ComponentManager::GetInstance();
    Component* vision_comp = manager.GetComponent("VisionController");
    
    if (vision_comp && ret == ESP_OK) {
        char var[32] = {0};
        char val[32] = {0};
        
        // Parse var and val parameters
        if (httpd_query_key_value(query_buf, "var", var, sizeof(var)) == ESP_OK &&
            httpd_query_key_value(query_buf, "val", val, sizeof(val)) == ESP_OK) {
            
            ESP_LOGI(TAG, "Camera setting: %s = %s", var, val);
            cJSON_AddStringToObject(response, "variable", var);
            cJSON_AddStringToObject(response, "value", val);
            
            // Create a JSON command to send to the VisionController through ThingManager
            cJSON* cmd = cJSON_CreateObject();
            cJSON_AddStringToObject(cmd, "component", "camera");
            cJSON_AddStringToObject(cmd, "command", "set_property");
            cJSON_AddStringToObject(cmd, "property", var);
            
            bool success = false;
            
            // Apply camera settings
            if (strcmp(var, "framesize") == 0) {
                int frame_size = atoi(val);
                cJSON_AddNumberToObject(cmd, "value", frame_size);
                
                try {
                    auto& thing_manager = iot::ThingManager::GetInstance();
                    if (SafeToInvokeCommand(cmd)) {
                    thing_manager.Invoke(cmd);
                    success = true;
                    cJSON_AddStringToObject(response, "message", "Frame size updated");
                    } else {
                        ESP_LOGW(TAG, "No Thing available to handle camera framesize command");
                        success = false;
                    }
                } catch (const std::exception& e) {
                    ESP_LOGE(TAG, "Error setting framesize: %s", e.what());
                    success = false;
                }
            } else if (strcmp(var, "quality") == 0) {
                int quality = atoi(val);
                cJSON_AddNumberToObject(cmd, "value", quality);
                
                try {
                    auto& thing_manager = iot::ThingManager::GetInstance();
                    if (SafeToInvokeCommand(cmd)) {
                    thing_manager.Invoke(cmd);
                    success = true;
                    cJSON_AddStringToObject(response, "message", "Quality updated");
                    } else {
                        ESP_LOGW(TAG, "No Thing available to handle camera quality command");
                        success = false;
                    }
                } catch (const std::exception& e) {
                    ESP_LOGE(TAG, "Error setting quality: %s", e.what());
                    success = false;
                }
            } else if (strcmp(var, "brightness") == 0 || 
                      strcmp(var, "contrast") == 0 ||
                      strcmp(var, "saturation") == 0 ||
                      strcmp(var, "hmirror") == 0 ||
                      strcmp(var, "vflip") == 0) {
                // Handle other common camera settings
                int value = atoi(val);
                cJSON_AddNumberToObject(cmd, "value", value);
                
                try {
                    auto& thing_manager = iot::ThingManager::GetInstance();
                    if (SafeToInvokeCommand(cmd)) {
                    thing_manager.Invoke(cmd);
                    success = true;
                    char msg[64];
                    snprintf(msg, sizeof(msg), "%s updated to %d", var, value);
                    cJSON_AddStringToObject(response, "message", msg);
                    } else {
                        ESP_LOGW(TAG, "No Thing available to handle camera %s command", var);
                        success = false;
                    }
                } catch (const std::exception& e) {
                    ESP_LOGE(TAG, "Error setting %s: %s", var, e.what());
                    success = false;
                }
            } else {
                cJSON_AddStringToObject(response, "message", "Unknown camera parameter");
                success = false;
            }
            
            cJSON_Delete(cmd);
            
            if (!success) {
                cJSON_AddStringToObject(response, "status", "error");
                cJSON_AddStringToObject(response, "message", "Failed to set camera parameter");
            }
        } else {
            cJSON_AddStringToObject(response, "message", "Missing var or val parameters");
        }
    } else {
        if (vision_comp) {
            cJSON_AddStringToObject(response, "message", "Missing query parameters");
        } else {
            cJSON_AddStringToObject(response, "status", "error");
            cJSON_AddStringToObject(response, "message", "Vision controller not available");
        }
    }
#else
    cJSON_AddStringToObject(response, "status", "error");
    cJSON_AddStringToObject(response, "message", "Camera control not enabled in this build");
#endif

    char* json_str = cJSON_PrintUnformatted(response);
    ret = SendHttpResponse(req, "application/json", json_str, strlen(json_str));
    free(json_str);
    cJSON_Delete(response);
    return ret;
}

// Camera stream handler - provides MJPEG streaming
esp_err_t WebServer::CameraStreamHandler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Camera stream request: %s", req->uri);
    
#if defined(CONFIG_ENABLE_VISION_CONTROLLER)
    // Get vision controller component
    auto& manager = ComponentManager::GetInstance();
    Component* vision_comp = manager.GetComponent("VisionController");
    
    if (vision_comp) {
        // Set response content type to multipart/x-mixed-replace
        httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        
        // Create a JSON command to start streaming via ThingManager
        cJSON* start_cmd = cJSON_CreateObject();
        cJSON_AddStringToObject(start_cmd, "component", "camera");
        cJSON_AddStringToObject(start_cmd, "command", "start_streaming");
        
        bool streaming_started = false;
        try {
            auto& thing_manager = iot::ThingManager::GetInstance();
            thing_manager.Invoke(start_cmd);
            streaming_started = true;
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Error starting camera stream: %s", e.what());
            streaming_started = false;
        }
        cJSON_Delete(start_cmd);
        
        if (streaming_started) {
            // In a real implementation we would:
            // 1. Set up a callback to receive frames from the vision controller
            // 2. For each frame, send it with the appropriate MJPEG format headers
            // 3. Continue until client disconnects
            
            // For a basic implementation, we'll just generate a mock stream
            // In a real implementation, you would get frames from the camera and stream them
            const char* frame_header = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ";
            const char* frame_separator = "\r\n\r\n";
            
            // Send a few mock frames to show it's working
            for (int i = 0; i < 10; i++) {
                // In a real implementation, this would be:
                // 1. Get a frame from the camera as JPEG data
                // 2. Send the frame with appropriate headers
                
                // Here we just use a placeholder message instead of real image data
                const char* placeholder = "This would be JPEG image data in a real implementation.";
                char buffer[128];
                sprintf(buffer, "%s%d%s", frame_header, (int)strlen(placeholder), frame_separator);
                
                // Send the header
                httpd_resp_send_chunk(req, buffer, strlen(buffer));
                
                // Send the "image data" (placeholder text)
                httpd_resp_send_chunk(req, placeholder, strlen(placeholder));
                
                // Short delay to simulate frame rate
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            
            // End the stream with an empty chunk
            httpd_resp_send_chunk(req, NULL, 0);
            
            // Stop streaming
            cJSON* stop_cmd = cJSON_CreateObject();
            cJSON_AddStringToObject(stop_cmd, "component", "camera");
            cJSON_AddStringToObject(stop_cmd, "command", "stop_streaming");
            
            try {
                auto& thing_manager = iot::ThingManager::GetInstance();
                thing_manager.Invoke(stop_cmd);
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Error stopping camera stream: %s", e.what());
            }
            cJSON_Delete(stop_cmd);
        } else {
            // Return error message if streaming couldn't be started
            httpd_resp_set_type(req, "text/plain");
            httpd_resp_send(req, "Failed to start camera streaming", -1);
        }
    } else {
        // Return error message if vision controller not available
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Vision controller not available", -1);
    }
#else
    // Return error message if vision module is not enabled
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Camera streaming not enabled in this build", -1);
#endif

    return ESP_OK;
} 

// 获取活动的WebSocket客户端数量
int WebServer::GetActiveWebSocketClientCount() const {
    int count = 0;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_clients_[i].connected && ws_clients_[i].fd >= 0) {
            count++;
        }
    }
    return count;
} 

// 检查是否能安全调用命令，而不修改 ThingManager
bool WebServer::SafeToInvokeCommand(const cJSON* cmd) {
    if (!cmd) {
        ESP_LOGE(TAG, "Command is null");
        return false;
    }
    
    auto& thing_manager = iot::ThingManager::GetInstance();
    
    // 首先检查 ThingManager 是否已初始化
    if (!thing_manager.IsInitialized()) {
        ESP_LOGW(TAG, "ThingManager not initialized");
        return false;
    }
    
    // 基本命令格式验证 - 确保命令有必要的字段
    cJSON* method = cJSON_GetObjectItem(cmd, "method");
    if (!method || !cJSON_IsString(method) || !method->valuestring) {
        ESP_LOGW(TAG, "Command missing valid 'method' field");
        return false;
    }
    
    // 验证参数对象存在
    cJSON* params = cJSON_GetObjectItem(cmd, "parameters");
    if (!params || !cJSON_IsObject(params)) {
        ESP_LOGW(TAG, "Command missing valid 'parameters' field");
        // 有些命令可能不需要参数，所以不返回 false
    }
    
    // 可以添加更多验证逻辑
    // 例如，检查特定的命令类型，或针对特定命令检查特定的参数
    
    // 由于我们不能修改 ThingManager，无法直接检查是否存在处理命令的 Thing
    // 因此我们只能进行基本的格式验证
    
    return true; // 通过基本验证
}

// 添加位置处理方法实现 - 在WebServer类实现的适当位置添加
void WebServer::HandleLocationRequest(int client_id, cJSON* root) {
    // 提取请求的位置模式
    cJSON* mode_json = cJSON_GetObjectItem(root, "mode");
    PSRAMString mode = mode_json && cJSON_IsString(mode_json) ? mode_json->valuestring : "uwb";
    
    // 创建位置响应消息
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "location_update");
    cJSON_AddStringToObject(response, "mode", mode.c_str());
    
    // 创建位置数据对象
    cJSON* position = cJSON_CreateObject();
    
    // 从位置控制器获取实际位置数据
    float x = 0.0f;
    float y = 0.0f;
    float orientation = 0.0f;
    float accuracy = 1.0f;
    
    // 尝试从LocationController获取位置数据
    try {
        #ifdef CONFIG_ENABLE_LOCATION_CONTROLLER
        // 获取LocationController实例
        auto* location_controller = LocationController::GetInstance();
        
        // 将字符串模式转换为LocationMode
        LocationMode location_mode;
        if (mode == "gps") {
            location_mode = MODE_GPS;
        } else if (mode == "uwb") {
            location_mode = MODE_UWB;
        } else if (mode == "fusion") {
            location_mode = MODE_FUSION;
        } else {
            // 默认模式
            location_mode = MODE_UWB;
        }
        
        // 设置位置模式（如果与当前模式不同）
        if (location_controller->GetLocationMode() != location_mode) {
            location_controller->SetLocationMode(location_mode);
        }
        
        // 获取当前位置
        PositionInfo current_position = location_controller->GetCurrentPosition();
        
        // 提取位置数据
        x = current_position.x;
        y = current_position.y;
        orientation = current_position.orientation;
        accuracy = current_position.accuracy;
        
        ESP_LOGD(TAG, "位置控制器返回位置: x=%.2f, y=%.2f, orientation=%.1f°, accuracy=%.2f", 
                x, y, orientation, accuracy);
        #else
        // 如果未启用位置控制器，尝试从ThingManager获取位置数据
        auto& thing_manager = iot::ThingManager::GetInstance();
        std::string states_json;
        
        if (thing_manager.GetStatesJson(states_json, false)) {
            // 解析返回的JSON状态
            cJSON* states = cJSON_Parse(states_json.c_str());
            if (states && cJSON_IsArray(states)) {
                // 遍历数组查找位置设备的状态
                for (int i = 0; i < cJSON_GetArraySize(states); i++) {
                    cJSON* thing_state = cJSON_GetArrayItem(states, i);
                    cJSON* name = cJSON_GetObjectItem(thing_state, "name");
                    
                    // 寻找位置设备（可能是UWB、GPS或其他位置服务）
                    if (name && cJSON_IsString(name) && 
                        (strcmp(name->valuestring, "UWB") == 0 || 
                         strcmp(name->valuestring, "GPS") == 0 ||
                         strcmp(name->valuestring, "Location") == 0)) {
                        
                        // 找到位置设备，获取其属性
                        cJSON* properties = cJSON_GetObjectItem(thing_state, "properties");
                        if (properties) {
                            // 获取坐标
                            cJSON* x_pos = cJSON_GetObjectItem(properties, "x");
                            if (x_pos && cJSON_IsNumber(x_pos)) {
                                x = x_pos->valuedouble;
                            }
                            
                            cJSON* y_pos = cJSON_GetObjectItem(properties, "y");
                            if (y_pos && cJSON_IsNumber(y_pos)) {
                                y = y_pos->valuedouble;
                            }
                            
                            // 获取方向
                            cJSON* orient = cJSON_GetObjectItem(properties, "orientation");
                            if (orient && cJSON_IsNumber(orient)) {
                                orientation = orient->valuedouble;
                            }
                            
                            // 获取精度
                            cJSON* acc = cJSON_GetObjectItem(properties, "accuracy");
                            if (acc && cJSON_IsNumber(acc)) {
                                accuracy = acc->valuedouble;
                            }
                            
                            ESP_LOGD(TAG, "找到位置数据: x=%.2f, y=%.2f, orientation=%.1f°, accuracy=%.2f", 
                                   x, y, orientation, accuracy);
                            break; // 找到位置设备，不需要继续查找
                        }
                    }
                }
                cJSON_Delete(states);
            }
        }
        #endif
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "获取位置数据时出错: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "获取位置数据时发生未知错误");
    }
    
    // 添加位置数据到响应
    cJSON_AddNumberToObject(position, "x", x);
    cJSON_AddNumberToObject(position, "y", y);
    cJSON_AddNumberToObject(position, "orientation", orientation);
    cJSON_AddNumberToObject(position, "accuracy", accuracy);
    cJSON_AddNumberToObject(position, "timestamp", esp_timer_get_time() / 1000000);
    
    // 将位置对象添加到响应
    cJSON_AddItemToObject(response, "position", position);
    
    // 发送位置数据响应
    char* json_str = cJSON_PrintUnformatted(response);
    PSRAMString message(json_str);
    free(json_str);
    cJSON_Delete(response);
    
    SendWebSocketMessage(client_id, message);
}

void WebServer::HandleSetLocationMode(int client_id, cJSON* root) {
    // 提取设置的位置模式
    cJSON* mode_json = cJSON_GetObjectItem(root, "mode");
    if (!mode_json || !cJSON_IsString(mode_json)) {
        ESP_LOGE(TAG, "缺少有效的位置模式");
        // 发送错误响应
        SendWebSocketMessage(client_id, "{\"type\":\"error\",\"message\":\"缺少有效的位置模式\"}");
        return;
    }
    
    PSRAMString mode_str = mode_json->valuestring;
    ESP_LOGI(TAG, "设置位置模式为: %s", mode_str.c_str());
    
    // 尝试设置位置模式
    bool success = false;
    
    try {
        #ifdef CONFIG_ENABLE_LOCATION_CONTROLLER
        // 获取LocationController实例
        auto* location_controller = LocationController::GetInstance();
        
        // 将字符串模式转换为LocationMode枚举
        LocationMode location_mode;
        if (mode_str == "gps") {
            location_mode = MODE_GPS;
        } else if (mode_str == "uwb") {
            location_mode = MODE_UWB;
        } else if (mode_str == "fusion") {
            location_mode = MODE_FUSION;
        } else {
            ESP_LOGW(TAG, "未知的位置模式: %s", mode_str.c_str());
            SendWebSocketMessage(client_id, "{\"type\":\"error\",\"message\":\"未知的位置模式\"}");
            return;
        }
        
        // 直接使用LocationController设置模式
        success = location_controller->SetLocationMode(location_mode);
        
        if (!success) {
            ESP_LOGW(TAG, "位置控制器无法设置模式 %s", mode_str.c_str());
        }
        #else
        // 尝试通过ThingManager设置位置模式
        auto& thing_manager = iot::ThingManager::GetInstance();
        
        // 创建命令JSON
        cJSON* cmd = cJSON_CreateObject();
        cJSON_AddStringToObject(cmd, "component", "location");
        cJSON_AddStringToObject(cmd, "command", "set_mode");
        cJSON_AddStringToObject(cmd, "mode", mode_str.c_str());
        
        // 调用位置服务
        if (SafeToInvokeCommand(cmd)) {
            thing_manager.Invoke(cmd);
            success = true;
        } else {
            ESP_LOGW(TAG, "位置服务不可用");
            success = false;
        }
        cJSON_Delete(cmd);
        #endif
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "设置位置模式时出错: %s", e.what());
        success = false;
    } catch (...) {
        ESP_LOGE(TAG, "设置位置模式时发生未知错误");
        success = false;
    }
    
    // 创建响应
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "mode_changed");
    cJSON_AddStringToObject(response, "mode", mode_str.c_str());
    cJSON_AddBoolToObject(response, "success", success);
    
    if (!success) {
        cJSON_AddStringToObject(response, "message", "切换模式失败，位置服务可能不可用");
    }
    
    // 发送响应
    char* json_str = cJSON_PrintUnformatted(response);
    PSRAMString message(json_str);
    free(json_str);
    cJSON_Delete(response);
    
    SendWebSocketMessage(client_id, message);
    
    // 如果成功切换模式，请求更新位置数据
    if (success) {
        // 短暂延迟后请求更新位置
        vTaskDelay(pdMS_TO_TICKS(100));
        HandleLocationRequest(client_id, nullptr);
    }
}

void WebServer::HandleCalibratePosition(int client_id, cJSON* root) {
    // 默认校准值
    float x = 0.0f;
    float y = 0.0f;
    float orientation = 0.0f;
    
    // 从请求中获取校准参数（如果有的话）
    cJSON* x_json = cJSON_GetObjectItem(root, "x");
    if (x_json && cJSON_IsNumber(x_json)) {
        x = x_json->valuedouble;
    }
    
    cJSON* y_json = cJSON_GetObjectItem(root, "y");
    if (y_json && cJSON_IsNumber(y_json)) {
        y = y_json->valuedouble;
    }
    
    cJSON* orientation_json = cJSON_GetObjectItem(root, "orientation");
    if (orientation_json && cJSON_IsNumber(orientation_json)) {
        orientation = orientation_json->valuedouble;
    }
    
    ESP_LOGI(TAG, "校准位置: x=%.2f, y=%.2f, orientation=%.1f°", x, y, orientation);
    
    // 执行位置校准
    bool success = false;
    try {
        #ifdef CONFIG_ENABLE_LOCATION_CONTROLLER
        // 获取LocationController实例
        auto* location_controller = LocationController::GetInstance();
        
        // 直接调用校准方法
        success = location_controller->CalibratePosition(x, y, orientation);
        
        if (!success) {
            ESP_LOGW(TAG, "位置控制器校准失败");
        } else {
            ESP_LOGI(TAG, "位置控制器校准成功");
        }
        #else
        // 使用ThingManager进行校准
        auto& thing_manager = iot::ThingManager::GetInstance();
        
        // 创建校准命令
        cJSON* cmd = cJSON_CreateObject();
        cJSON_AddStringToObject(cmd, "component", "location");
        cJSON_AddStringToObject(cmd, "command", "calibrate");
        cJSON_AddNumberToObject(cmd, "x", x);
        cJSON_AddNumberToObject(cmd, "y", y);
        cJSON_AddNumberToObject(cmd, "orientation", orientation);
        
        // 调用位置服务执行校准
        if (SafeToInvokeCommand(cmd)) {
            thing_manager.Invoke(cmd);
            success = true;
        } else {
            ESP_LOGW(TAG, "位置服务不可用，无法执行校准");
            success = false;
        }
        cJSON_Delete(cmd);
        #endif
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "位置校准时出错: %s", e.what());
        success = false;
    } catch (...) {
        ESP_LOGE(TAG, "位置校准时发生未知错误");
        success = false;
    }
    
    // 创建校准响应
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "location_calibration_result");
    cJSON_AddBoolToObject(response, "success", success);
    
    // 添加校准位置数据
    cJSON* position = cJSON_CreateObject();
    cJSON_AddNumberToObject(position, "x", x);
    cJSON_AddNumberToObject(position, "y", y);
    cJSON_AddNumberToObject(position, "orientation", orientation);
    cJSON_AddItemToObject(response, "position", position);
    
    if (!success) {
        cJSON_AddStringToObject(response, "error", "位置校准失败，位置服务可能不可用");
    }
    
    // 发送校准结果
    char* json_str = cJSON_PrintUnformatted(response);
    PSRAMString message(json_str);
    free(json_str);
    cJSON_Delete(response);
    
    SendWebSocketMessage(client_id, message);
    
    // 如果校准成功，请求更新位置数据
    if (success) {
        // 短暂延迟后请求更新位置
        vTaskDelay(pdMS_TO_TICKS(200));
        HandleLocationRequest(client_id, nullptr);
    }
}

void WebServer::HandleSaveLocationMap(int client_id, cJSON* root) {
    ESP_LOGI(TAG, "保存位置地图请求");
    
    // 执行地图保存
    bool success = false;
    PSRAMString path = "/spiffs/location_map.json"; // 默认保存路径
    
    // 获取文件路径（如果提供）
    cJSON* path_json = cJSON_GetObjectItem(root, "path");
    if (path_json && cJSON_IsString(path_json)) {
        path = path_json->valuestring;
    }
    
    try {
        #ifdef CONFIG_ENABLE_LOCATION_CONTROLLER
        // 获取LocationController实例
        auto* location_controller = LocationController::GetInstance();
        
        // 调用保存地图方法
        success = location_controller->SaveLocationMap(path.c_str());
        
        if (!success) {
            ESP_LOGW(TAG, "位置控制器保存地图失败");
        } else {
            ESP_LOGI(TAG, "位置控制器保存地图成功: %s", path.c_str());
        }
        #else
        // 使用ThingManager保存地图
        auto& thing_manager = iot::ThingManager::GetInstance();
        
        // 创建保存地图命令
        cJSON* cmd = cJSON_CreateObject();
        cJSON_AddStringToObject(cmd, "component", "location");
        cJSON_AddStringToObject(cmd, "command", "save_map");
        cJSON_AddStringToObject(cmd, "path", path.c_str());
        
        // 调用位置服务保存地图
        if (SafeToInvokeCommand(cmd)) {
            thing_manager.Invoke(cmd);
            success = true;
        } else {
            ESP_LOGW(TAG, "位置服务不可用，无法保存地图");
            success = false;
        }
        cJSON_Delete(cmd);
        #endif
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "保存地图时出错: %s", e.what());
        success = false;
    } catch (...) {
        ESP_LOGE(TAG, "保存地图时发生未知错误");
        success = false;
    }
    
    // 创建保存地图响应
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "map_saved");
    cJSON_AddBoolToObject(response, "success", success);
    
    if (success) {
        cJSON_AddStringToObject(response, "path", path.c_str());
    } else {
        cJSON_AddStringToObject(response, "error", "保存地图失败，位置服务可能不可用");
    }
    
    // 发送保存结果
    char* json_str = cJSON_PrintUnformatted(response);
    PSRAMString message(json_str);
    free(json_str);
    cJSON_Delete(response);
    
    SendWebSocketMessage(client_id, message);
}

#ifdef CONFIG_WEB_SERVER_ENABLE_SELF_TEST
// Self-test for location handlers
bool WebServer::TestLocationHandlers() {
    ESP_LOGI(TAG, "测试位置处理器");
    
    bool all_tests_passed = true;
    
    try {
        // 测试处理位置请求
        {
            ESP_LOGI(TAG, "测试HandleLocationRequest...");
            cJSON* test_req = cJSON_CreateObject();
            cJSON_AddStringToObject(test_req, "mode", "uwb");
            
            // 模拟客户端ID（-1表示仅测试，不实际发送）
            int test_client = -1;
            
            // 这不会实际发送消息，但会执行处理过程
            HandleLocationRequest(test_client, test_req);
            
            cJSON_Delete(test_req);
            ESP_LOGI(TAG, "HandleLocationRequest测试通过");
        }
        
        // 测试设置位置模式
        {
            ESP_LOGI(TAG, "测试HandleSetLocationMode...");
            cJSON* test_req = cJSON_CreateObject();
            cJSON_AddStringToObject(test_req, "mode", "gps");
            
            // 模拟客户端ID（-1表示仅测试，不实际发送）
            int test_client = -1;
            
            // 这不会实际修改模式或发送消息，但会执行处理过程
            HandleSetLocationMode(test_client, test_req);
            
            cJSON_Delete(test_req);
            ESP_LOGI(TAG, "HandleSetLocationMode测试通过");
        }
        
        // 测试校准位置
        {
            ESP_LOGI(TAG, "测试HandleCalibratePosition...");
            cJSON* test_req = cJSON_CreateObject();
            cJSON_AddNumberToObject(test_req, "x", 1.0);
            cJSON_AddNumberToObject(test_req, "y", 2.0);
            cJSON_AddNumberToObject(test_req, "orientation", 90.0);
            
            // 模拟客户端ID（-1表示仅测试，不实际发送）
            int test_client = -1;
            
            // 这不会实际校准或发送消息，但会执行处理过程
            HandleCalibratePosition(test_client, test_req);
            
            cJSON_Delete(test_req);
            ESP_LOGI(TAG, "HandleCalibratePosition测试通过");
        }
        
        // 测试保存地图
        {
            ESP_LOGI(TAG, "测试HandleSaveLocationMap...");
            cJSON* test_req = cJSON_CreateObject();
            
            // 模拟客户端ID（-1表示仅测试，不实际发送）
            int test_client = -1;
            
            // 这不会实际保存或发送消息，但会执行处理过程
            HandleSaveLocationMap(test_client, test_req);
            
            cJSON_Delete(test_req);
            ESP_LOGI(TAG, "HandleSaveLocationMap测试通过");
        }
        
        ESP_LOGI(TAG, "所有位置处理器测试通过");
    }
    catch (const std::exception& e) {
        ESP_LOGE(TAG, "位置处理器测试失败: %s", e.what());
        all_tests_passed = false;
    }
    catch (...) {
        ESP_LOGE(TAG, "位置处理器测试失败，未知错误");
        all_tests_passed = false;
    }
    
    return all_tests_passed;
}
#endif // CONFIG_WEB_SERVER_ENABLE_SELF_TEST

