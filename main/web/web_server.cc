#include "web_server.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <cstring>
#include "sdkconfig.h"
#include "web_content.h"
#include "../motor/motor_content.h"
#include "../ai/ai_content.h"
#include "../vision/vision_content.h"

#define TAG "WebServer"

WebServer::WebServer() 
    : server_(nullptr), running_(false), ws_callback_(nullptr) {
    // 初始化WebSocket客户端数组
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        ws_clients_[i].fd = -1;
        ws_clients_[i].connected = false;
        ws_clients_[i].last_activity = 0;
    }
}

WebServer::~WebServer() {
    Stop();
}

bool WebServer::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Web server already running");
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    // 设置端口号
#if defined(CONFIG_WEB_SERVER_PORT)
    config.server_port = CONFIG_WEB_SERVER_PORT;
#else
    config.server_port = 8080;
#endif

    config.lru_purge_enable = true;
    config.max_uri_handlers = 20;
    config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting web server on port %d", config.server_port);

    esp_err_t ret = httpd_start(&server_, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(ret));
        return false;
    }

    // 注册所有URI处理函数
    for (auto& handler : uri_handlers_) {
        if (!handler.registered) {
            esp_err_t err = httpd_register_uri_handler(server_, &handler.uri_config);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to register URI handler for %s: %s", 
                    handler.uri_config.uri, esp_err_to_name(err));
            } else {
                handler.registered = true;
                ESP_LOGI(TAG, "Registered URI handler: %s", handler.uri_config.uri);
            }
        }
    }

    running_ = true;
    ESP_LOGI(TAG, "Web server started successfully");
    return true;
}

void WebServer::Stop() {
    if (!running_) {
        return;
    }

    CloseAllWebSocketConnections();

    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }

    // 标记所有URI处理程序为未注册
    for (auto& handler : uri_handlers_) {
        handler.registered = false;
    }

    running_ = false;
    ESP_LOGI(TAG, "Web server stopped");
}

bool WebServer::IsRunning() const {
    return running_;
}

const char* WebServer::GetName() const {
    return "WebServer";
}

void WebServer::RegisterUri(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *req), void* user_ctx) {
    UriHandler uri_handler;
    uri_handler.uri_config.uri = uri;
    uri_handler.uri_config.method = method;
    uri_handler.uri_config.handler = handler;
    uri_handler.uri_config.user_ctx = user_ctx;
    uri_handler.registered = false;

    uri_handlers_.push_back(uri_handler);
    
    // 如果服务器已经在运行，直接注册
    if (running_ && server_) {
        esp_err_t err = httpd_register_uri_handler(server_, &uri_handlers_.back().uri_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register URI handler for %s: %s", 
                uri, esp_err_to_name(err));
        } else {
            uri_handlers_.back().registered = true;
            ESP_LOGI(TAG, "Registered URI handler: %s", uri);
        }
    }
}

void WebServer::RegisterWebSocket(const char* uri, WebSocketMessageCallback callback) {
    ws_callback_ = callback;
    
    UriHandler uri_handler;
    uri_handler.uri_config.uri = uri;
    uri_handler.uri_config.method = HTTP_GET;
    uri_handler.uri_config.handler = WebSocketHandler;
    uri_handler.uri_config.user_ctx = this;
    uri_handler.uri_config.is_websocket = true;
    uri_handler.registered = false;

    uri_handlers_.push_back(uri_handler);
    
    // 如果服务器已经在运行，直接注册
    if (running_ && server_) {
        esp_err_t err = httpd_register_uri_handler(server_, &uri_handlers_.back().uri_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register WebSocket handler for %s: %s", 
                uri, esp_err_to_name(err));
        } else {
            uri_handlers_.back().registered = true;
            ESP_LOGI(TAG, "Registered WebSocket handler: %s", uri);
        }
    }
}

bool WebServer::SendWebSocketMessage(int client_index, const std::string& message) {
    if (!running_ || !server_) {
        ESP_LOGW(TAG, "Web server not running, cannot send WebSocket message");
        return false;
    }

    if (client_index < 0 || client_index >= MAX_WS_CLIENTS || !ws_clients_[client_index].connected) {
        ESP_LOGW(TAG, "Invalid WebSocket client index: %d", client_index);
        return false;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)message.c_str();
    ws_pkt.len = message.length();
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_send_frame_async(server_, ws_clients_[client_index].fd, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send WebSocket message: %s", esp_err_to_name(ret));
        RemoveWebSocketClient(client_index);
        return false;
    }

    // 更新最后活动时间
    ws_clients_[client_index].last_activity = esp_timer_get_time() / 1000;
    return true;
}

void WebServer::BroadcastWebSocketMessage(const std::string& message) {
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_clients_[i].connected) {
            SendWebSocketMessage(i, message);
        }
    }
}

int WebServer::AddWebSocketClient(int fd) {
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (!ws_clients_[i].connected) {
            ws_clients_[i].fd = fd;
            ws_clients_[i].connected = true;
            ws_clients_[i].last_activity = esp_timer_get_time() / 1000;
            ESP_LOGI(TAG, "WebSocket client added, index: %d, fd: %d", i, fd);
            return i;
        }
    }
    ESP_LOGW(TAG, "No free WebSocket client slots");
    return -1;
}

void WebServer::RemoveWebSocketClient(int index) {
    if (index >= 0 && index < MAX_WS_CLIENTS) {
        ESP_LOGI(TAG, "Removing WebSocket client, index: %d, fd: %d", 
            index, ws_clients_[index].fd);
        ws_clients_[index].connected = false;
        ws_clients_[index].fd = -1;
    }
}

void WebServer::CloseAllWebSocketConnections() {
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_clients_[i].connected && server_) {
            ESP_LOGI(TAG, "Closing WebSocket connection, index: %d, fd: %d", 
                i, ws_clients_[i].fd);
            httpd_sess_trigger_close(server_, ws_clients_[i].fd);
            ws_clients_[i].connected = false;
            ws_clients_[i].fd = -1;
        }
    }
}

void WebServer::CheckWebSocketTimeouts() {
    int64_t current_time = esp_timer_get_time() / 1000;
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_clients_[i].connected && 
            (current_time - ws_clients_[i].last_activity > WS_TIMEOUT_MS)) {
            ESP_LOGI(TAG, "WebSocket client timed out, index: %d, fd: %d", 
                i, ws_clients_[i].fd);
            if (server_) {
                httpd_sess_trigger_close(server_, ws_clients_[i].fd);
            }
            RemoveWebSocketClient(i);
        }
    }
}

esp_err_t WebServer::WebSocketHandler(httpd_req_t *req) {
    WebServer* server = static_cast<WebServer*>(req->user_ctx);
    
    if (req->method == HTTP_GET) {
        // WebSocket握手请求
        ESP_LOGI(TAG, "WebSocket handshake request");
        return ESP_OK;
    }
    
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    // 获取WebSocket帧
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to receive WebSocket frame: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 如果帧有负载，分配内存接收数据
    if (ws_pkt.len) {
        ws_pkt.payload = (uint8_t*)malloc(ws_pkt.len + 1);
        if (!ws_pkt.payload) {
            ESP_LOGE(TAG, "Failed to allocate memory for WebSocket payload");
            return ESP_ERR_NO_MEM;
        }
        
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            free(ws_pkt.payload);
            ESP_LOGE(TAG, "Failed to receive WebSocket payload: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // 添加字符串结束符
        ws_pkt.payload[ws_pkt.len] = 0;
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
    
    // 如果没有找到客户端，添加新客户端
    if (client_index == -1) {
        client_index = server->AddWebSocketClient(client_fd);
        if (client_index == -1) {
            // 无法添加更多客户端
            if (ws_pkt.payload) {
                free(ws_pkt.payload);
            }
            return ESP_OK;
        }
    } else {
        // 更新活动时间
        server->ws_clients_[client_index].last_activity = esp_timer_get_time() / 1000;
    }
    
    // 处理WebSocket数据
    if (ws_pkt.payload && server->ws_callback_) {
        std::string message((char*)ws_pkt.payload);
        free(ws_pkt.payload);
        
        // 调用回调处理消息
        server->ws_callback_(client_index, message);
    } else if (ws_pkt.payload) {
        free(ws_pkt.payload);
    }
    
    return ESP_OK;
}

// 初始化Web组件
void WebServer::InitWebComponents() {
#if defined(CONFIG_ENABLE_WEB_SERVER)
    // 创建Web服务器组件
    WebServer* web_server = new WebServer();
    ComponentManager::GetInstance().RegisterComponent(web_server);

#if defined(CONFIG_ENABLE_WEB_CONTENT)
    // 创建Web内容组件
    WebContent* web_content = new WebContent(web_server);
    ComponentManager::GetInstance().RegisterComponent(web_content);
#endif // CONFIG_ENABLE_WEB_CONTENT

    // 初始化电机组件
#if defined(CONFIG_ENABLE_MOTOR_CONTROLLER)
    InitMotorComponents(web_server);
#endif // CONFIG_ENABLE_MOTOR_CONTROLLER

    // 初始化AI组件
#if defined(CONFIG_ENABLE_AI_CONTROLLER)
    InitAIComponents(web_server);
#endif // CONFIG_ENABLE_AI_CONTROLLER

    // 初始化视觉组件
#if defined(CONFIG_ENABLE_VISION_CONTROLLER)
    InitVisionComponents(web_server);
#endif // CONFIG_ENABLE_VISION_CONTROLLER

    ESP_LOGI(TAG, "Web components initialized");
#else
    ESP_LOGI(TAG, "Web server disabled in configuration");
#endif // CONFIG_ENABLE_WEB_SERVER
} 