#include "web_server.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <cstring>
#include <cJSON.h>
#include "sdkconfig.h"
#include "web_content.h"
#if CONFIG_ENABLE_WEB_CONTENT
#include "html_content.h"
#include "../motor/motor_content.h"
#include "../ai/ai_content.h"
#include "../vision/vision_content.h"
#endif
#include "../vision/vision_controller.h"
#include <esp_heap_caps.h>
#if defined(CONFIG_ENABLE_MOTOR_CONTROLLER)
#include "../motor/motor_controller.h"
#include "../motor/motor_content.h"
#endif
#if defined(CONFIG_ENABLE_AI_CONTROLLER)
#include "../ai/ai_controller.h"
#include "../ai/ai_content.h"
#endif
#if defined(CONFIG_ENABLE_VISION_CONTROLLER)
#include "../vision/vision_controller.h"
#include "../vision/vision_content.h"
#endif

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
    const char* get_index_html_content();
    size_t get_index_html_size();
    const char* get_motor_html_content();
    size_t get_motor_html_size();
    const char* get_vision_html_content();
    size_t get_vision_html_size();
    const char* get_ai_html_content();
    size_t get_ai_html_size();
    
    // 常量别名
    extern const char* INDEX_HTML;
    extern const char* MOTOR_HTML;
    extern const char* AI_HTML;
    extern const char* VISION_HTML;
}
#endif

// Forward declarations
#if CONFIG_ENABLE_MOTOR_CONTROLLER && CONFIG_ENABLE_WEB_CONTENT
extern void InitMotorComponents(WebServer* server);
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
}

WebServer::~WebServer() {
    ESP_LOGI(TAG, "销毁WebServer实例");
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
    
    // 配置HTTP服务器
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = CONFIG_WEB_SERVER_PRIORITY;
    config.server_port = CONFIG_WEB_SERVER_PORT;
    config.max_uri_handlers = 16;
    config.max_open_sockets = MAX_WS_CLIENTS + 3;  // 符合lwip限制: 7+3=10, 服务器内部使用3个
    config.lru_purge_enable = true;
    
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
    return running_;
}

const char* WebServer::GetName() const {
    return "WebServer";
}

void WebServer::RegisterDefaultHandlers() {
    // Register handlers for default endpoints
    RegisterHttpHandler("/", HTTP_GET, RootHandler);     // 主页
    RegisterHttpHandler("/ws", HTTP_GET, WebSocketHandler);  // WebSocket端点
    
    // API处理器需要明确类型转换为httpd_method_t
    RegisterHttpHandler("/api/*", static_cast<httpd_method_t>(HTTP_ANY), ApiHandler);  // API处理器处理所有/api/前缀的请求
    
    // Register handlers for special endpoints
    RegisterHttpHandler("/vision", HTTP_GET, VisionHandler);
    RegisterHttpHandler("/motor", HTTP_GET, MotorHandler);
    RegisterHttpHandler("/ai", HTTP_GET, AIHandler);  // 添加AI页面路由
}

void WebServer::RegisterHttpHandler(const PSRAMString& path, httpd_method_t method, HttpRequestHandler handler) {
    // 检查处理器是否已经注册
    if (http_handlers_.find(path) != http_handlers_.end()) {
        ESP_LOGW(TAG, "HTTP处理器 %s 已注册，正在覆盖", path.c_str());
    }
    
    http_handlers_[path] = std::make_pair(method, handler);
    ESP_LOGI(TAG, "注册HTTP处理器: %s", path.c_str());
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

// 发送HTTP响应
esp_err_t WebServer::SendHttpResponse(httpd_req_t* req, const char* content_type, const char* data, size_t len) {
    httpd_resp_set_type(req, content_type);
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

// 电机控制页面处理器
esp_err_t WebServer::MotorHandler(httpd_req_t *req) {
#if CONFIG_ENABLE_WEB_CONTENT && CONFIG_ENABLE_MOTOR_CONTROLLER
    // 获取电机控制相关的HTML内容
    const char* html_content = get_motor_html_content();
    size_t len = get_motor_html_size();
    
    ESP_LOGI(TAG, "Serving motor.html, size: %d bytes", len);
    return SendHttpResponse(req, "text/html", html_content, len);
#else
    // 当电机内容未启用时，返回一个简单的消息
    const char* message = "<html><body><h1>Motor Control Disabled</h1>"
        "<p>The motor control web interface is not enabled in this build.</p>"
        "<p>Motor API endpoints are still available at /api/motor/*</p>"
        "<p>WebSocket commands for motor control are supported.</p>"
        "</body></html>";
    ESP_LOGI(TAG, "Motor content disabled, serving simple message");
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

// API处理器 - 处理所有/api/前缀的请求
esp_err_t WebServer::ApiHandler(httpd_req_t* req) {
    // 获取URI路径
    std::string uri(req->uri);
    ESP_LOGI(TAG, "收到API请求: %s, 方法: %d", uri.c_str(), req->method);
    
    // 解析API路径 - 示例: /api/motor/speed -> "motor" 和 "speed" 
    std::string path = uri.substr(5); // 去除 "/api/" 前缀
    size_t pos = path.find('/');
    
    std::string resource = (pos != std::string::npos) ? path.substr(0, pos) : path;
    std::string action = (pos != std::string::npos) ? path.substr(pos + 1) : "";
    
    ESP_LOGI(TAG, "API解析: 资源=%s, 动作=%s", resource.c_str(), action.c_str());
    
    // 处理各种API请求
    if (resource == "motor") {
        // 处理电机控制API
        if (req->method == HTTP_GET) {
            // 获取电机状态
            cJSON* response = cJSON_CreateObject();
            cJSON_AddStringToObject(response, "status", "ok");
            cJSON_AddStringToObject(response, "resource", "motor");
            // 这里添加电机状态信息
            
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
                // 处理电机控制命令
                // 这里添加电机控制逻辑
                
                cJSON_Delete(json);
                
                // 发送成功响应
                cJSON* response = cJSON_CreateObject();
                cJSON_AddStringToObject(response, "status", "ok");
                cJSON_AddStringToObject(response, "message", "Motor command processed");
                
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
            uint32_t uptime = esp_timer_get_time() / 1000000;
            
            cJSON_AddNumberToObject(response, "free_heap", free_heap);
            cJSON_AddNumberToObject(response, "uptime", uptime);
            
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
    
    if (req->method == HTTP_GET) {
        // WebSocket握手请求
        // 获取客户端类型参数，如 /ws?type=motor
        char type_buf[32] = {0};
        size_t buf_len = sizeof(type_buf);
        
        esp_err_t ret = httpd_req_get_url_query_str(req, type_buf, buf_len);
        PSRAMString client_type = "generic";
        
        if (ret == ESP_OK) {
            char param_val[16] = {0};
            ret = httpd_query_key_value(type_buf, "type", param_val, sizeof(param_val));
            if (ret == ESP_OK) {
                client_type = param_val;
            }
        }
        
        ESP_LOGI(TAG, "WebSocket握手请求，客户端类型: %s, fd: %d", 
                 client_type.c_str(), httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    // 获取WebSocket帧
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "接收WebSocket帧失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 如果帧有负载，分配内存接收数据
    if (ws_pkt.len) {
        // 限制单个消息的最大长度
        if (ws_pkt.len > 16384) {  // 16KB限制
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
        
        ws_pkt.payload = payload;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            WEB_SERVER_FREE(ws_pkt.payload);
            ESP_LOGE(TAG, "接收WebSocket负载失败: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // 添加字符串结束符
        ws_pkt.payload[ws_pkt.len] = 0;
    } else {
        ESP_LOGW(TAG, "接收到空的WebSocket帧");
        return ESP_OK;
    }
    
    // 查找客户端
    int client_index = -1;
    int client_fd = httpd_req_to_sockfd(req);
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (server->ws_clients_[i].connected && server->ws_clients_[i].fd == client_fd) {
            client_index = i;
            break;
        }
    }
    
    // 获取查询参数中的类型
    char type_buf[32] = {0};
    size_t buf_len = sizeof(type_buf);
    PSRAMString client_type = "generic";
    
    if (httpd_req_get_url_query_str(req, type_buf, buf_len) == ESP_OK) {
        char param_val[16] = {0};
        if (httpd_query_key_value(type_buf, "type", param_val, sizeof(param_val)) == ESP_OK) {
            client_type = param_val;
        }
    }
    
    // 如果没有找到客户端，添加新客户端
    if (client_index == -1) {
        client_index = server->AddWebSocketClient(client_fd, client_type);
        if (client_index == -1) {
            // 无法添加更多客户端
            if (ws_pkt.payload) {
                WEB_SERVER_FREE(ws_pkt.payload);
            }
            ESP_LOGW(TAG, "无法添加更多WebSocket客户端，拒绝连接");
            return ESP_OK;
        }
    } else {
        // 更新活动时间
        server->ws_clients_[client_index].last_activity = esp_timer_get_time() / 1000;
        // 更新客户端类型
        if (!client_type.empty() && client_type != "generic") {
            server->ws_clients_[client_index].client_type = client_type;
        }
    }
    
    // 处理WebSocket数据
    if (ws_pkt.payload) {
        try {
            PSRAMString message((char*)ws_pkt.payload);
            WEB_SERVER_FREE(ws_pkt.payload);  // 及时释放内存
            
            // 处理消息
            server->HandleWebSocketMessage(client_index, message);
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "处理WebSocket消息异常: %s", e.what());
        } catch (...) {
            ESP_LOGE(TAG, "处理WebSocket消息未知异常");
        }
    }
    
    return ESP_OK;
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
            // 响应心跳
            BroadcastWebSocketMessage("{\"type\":\"heartbeat_response\",\"status\":\"ok\"}", 
                                      ws_clients_[client_id].client_type);
            return;
        }
        
        // 解析JSON消息
        cJSON* json = cJSON_Parse(message.c_str());
        if (!json) {
            ESP_LOGW(WS_MSG_TAG, "解析WebSocket消息为JSON失败");
            return;
        }
        
        // 使用智能指针管理JSON对象
        struct JsonGuard {
            cJSON* json;
            JsonGuard(cJSON* j) : json(j) {}
            ~JsonGuard() { if (json) cJSON_Delete(json); }
        } json_guard(json);
        
        // 提取消息类型
        cJSON* type_obj = cJSON_GetObjectItem(json, "type");
        if (!type_obj || !cJSON_IsString(type_obj)) {
            ESP_LOGW(WS_MSG_TAG, "WebSocket消息没有类型字段");
            return;
        }
        
        PSRAMString type = type_obj->valuestring;
        
        // 查找对应的处理器
        auto it = ws_handlers_.find(type);
        if (it != ws_handlers_.end() && it->second) {
            // 调用对应的处理函数
            it->second(client_id, message, type);
        } else {
            ESP_LOGW(WS_MSG_TAG, "未找到类型为 %s 的WebSocket处理器", type.c_str());
        }
    } catch (const std::exception& e) {
        ESP_LOGE(WS_MSG_TAG, "处理WebSocket消息异常: %s", e.what());
    } catch (...) {
        ESP_LOGE(WS_MSG_TAG, "处理WebSocket消息未知异常");
    }
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

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)message.c_str();
    ws_pkt.len = message.length();
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.final = true;  // 标记为最终帧

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

// 初始化Web组件
void WebServer::InitWebComponents() {
#if defined(CONFIG_ENABLE_WEB_SERVER)
    ESP_LOGI(TAG, "初始化Web组件");
    
    // 检查WebServer是否已经存在
    auto& manager = ComponentManager::GetInstance();
    Component* existing_web_server = manager.GetComponent("WebServer");
    
    WebServer* web_server = nullptr;
    if (existing_web_server) {
        ESP_LOGI(TAG, "WebServer已存在，使用现有实例");
        web_server = static_cast<WebServer*>(existing_web_server);
    } else {
        // 创建Web服务器组件
        web_server = new WebServer();
        manager.RegisterComponent(web_server);
        ESP_LOGI(TAG, "已创建新的WebServer实例");
    }
    
    // 注册WebContent组件
#if defined(CONFIG_ENABLE_WEB_CONTENT)
    Component* existing_web_content = manager.GetComponent("WebContent");
    if (existing_web_content) {
        ESP_LOGI(TAG, "WebContent已存在，跳过创建");
    } else {
        WebContent* web_content = new WebContent(web_server);
        manager.RegisterComponent(web_content);
        ESP_LOGI(TAG, "已创建新的WebContent实例");
    }
#else
    ESP_LOGI(TAG, "WebContent在配置中已禁用");
#endif
    
    // 注册但不启动电机组件
#if defined(CONFIG_ENABLE_MOTOR_CONTROLLER)
#if defined(CONFIG_ENABLE_WEB_CONTENT)
    InitMotorComponents(web_server);
#else
    // 即使禁用了WebContent，也要初始化电机组件
    if (!manager.GetComponent("MotorController")) {
        MotorController* motor_controller = new MotorController();
        manager.RegisterComponent(motor_controller);
        ESP_LOGI(TAG, "注册电机控制器 (WebContent已禁用)");
    }
#endif
    ESP_LOGI(TAG, "注册电机组件");
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
#endif

    ESP_LOGI(TAG, "Web组件初始化完成 (组件将在网络初始化后启动)");
#else
    ESP_LOGI(TAG, "Web服务器在配置中已禁用");
#endif // CONFIG_ENABLE_WEB_SERVER
}

// 启动Web组件
bool WebServer::StartWebComponents() {
#if defined(CONFIG_ENABLE_WEB_SERVER)
    auto& manager = ComponentManager::GetInstance();
    
    // 获取Web服务器组件
    Component* web_server_comp = manager.GetComponent("WebServer");
    WebServer* web_server = static_cast<WebServer*>(web_server_comp);
    
    if (!web_server) {
        ESP_LOGE(TAG, "未找到WebServer组件，无法启动web组件");
        return false;
    }
    
    try {
        // 启动Web服务器
        if (!web_server->IsRunning()) {
            if (!web_server->Start()) {
                ESP_LOGE(TAG, "启动WebServer失败");
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
                return false;
            }
            ESP_LOGI(TAG, "WebContent启动成功");
        }
#else
        ESP_LOGI(TAG, "WebContent已禁用，但WebServer启动成功，API和WebSocket可用");
#endif // CONFIG_ENABLE_WEB_CONTENT

        // 启动电机相关组件
#if defined(CONFIG_ENABLE_MOTOR_CONTROLLER)
        Component* motor_controller = manager.GetComponent("MotorController");
        
        if (motor_controller && !motor_controller->IsRunning()) {
            if (!motor_controller->Start()) {
                ESP_LOGE(TAG, "启动MotorController失败");
                // 继续尝试启动其他组件
            } else {
                ESP_LOGI(TAG, "MotorController启动成功");
            }
        }
        
#if defined(CONFIG_ENABLE_WEB_CONTENT)
        Component* motor_content = manager.GetComponent("MotorContent");
        if (motor_content && !motor_content->IsRunning()) {
            if (!motor_content->Start()) {
                ESP_LOGE(TAG, "启动MotorContent失败");
                // 继续尝试启动其他组件
            } else {
                ESP_LOGI(TAG, "MotorContent启动成功");
            }
        } else if (motor_content) {
            ESP_LOGI(TAG, "MotorContent已经在运行");
        }
#endif // CONFIG_ENABLE_WEB_CONTENT
#endif // CONFIG_ENABLE_MOTOR_CONTROLLER
        
        // 启动AI相关组件
#if defined(CONFIG_ENABLE_AI_CONTROLLER)
        Component* ai_controller = manager.GetComponent("AIController");
        
        if (ai_controller && !ai_controller->IsRunning()) {
            if (!ai_controller->Start()) {
                ESP_LOGE(TAG, "启动AIController失败");
                // 继续尝试启动其他组件
            } else {
                ESP_LOGI(TAG, "AIController启动成功");
            }
        }
        
#if defined(CONFIG_ENABLE_WEB_CONTENT)
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
#endif // CONFIG_ENABLE_WEB_CONTENT
#endif // CONFIG_ENABLE_AI_CONTROLLER
        
        // 启动视觉相关组件
#if defined(CONFIG_ENABLE_VISION_CONTROLLER)
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
#endif // CONFIG_ENABLE_VISION_CONTROLLER
        
        ESP_LOGI(TAG, "所有Web组件启动成功");
        return true;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "StartWebComponents异常: %s", e.what());
        return false;
    } catch (...) {
        ESP_LOGE(TAG, "StartWebComponents未知异常");
        return false;
    }
#else
    ESP_LOGI(TAG, "Web服务器在配置中已禁用");
    return false;
#endif // CONFIG_ENABLE_WEB_SERVER
}

// ================ 向后兼容方法实现 ================

// 判断URI是否已注册 (向后兼容)
bool WebServer::IsUriRegistered(const char* uri) {
    PSRAMString uri_str(uri);
    return http_handlers_.find(uri_str) != http_handlers_.end();
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