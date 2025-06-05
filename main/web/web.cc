#include "web.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include <cJSON.h>
#include <algorithm>
#include <sstream>
#include <functional>
#include <string.h>

#define TAG "Web"
#define WEB_DEFAULT_PORT 8080  // 直接定义默认端口

// Static pointer to current instance for callbacks
Web* Web::current_instance_ = nullptr;

// Constructor
Web::Web(int port) 
    : server_(nullptr), 
      port_(port == 0 ? 8080 : port),
      running_(false) {
    ESP_LOGI(TAG, "Web component created, port: %d", port_);
    current_instance_ = this;
}

// Destructor
Web::~Web() {
    if (running_) {
        Stop();
    }
    
    if (current_instance_ == this) {
        current_instance_ = nullptr;
    }
    
    ESP_LOGI(TAG, "Web component destroyed");
}

// Component interface implementation
bool Web::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Web component already running");
        return true;
    }
    
    // 强制使用8080端口
    if (port_ == 0 || port_ == 80) {
        port_ = WEB_DEFAULT_PORT;
    }
    
    ESP_LOGI(TAG, "Starting Web component on port %d", port_);
    
    // HTTP Server Configuration
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = tskIDLE_PRIORITY + 5;
    config.stack_size = 8192;
    config.core_id = 0;
    config.server_port = port_;  // 使用强制设置的端口
    config.ctrl_port = port_;
    config.max_open_sockets = 7;
    config.max_uri_handlers = 24;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    
    // 打印配置信息
    ESP_LOGI(TAG, "Web server config: port=%d, task_priority=%d, stack_size=%d", 
             config.server_port, config.task_priority, config.stack_size);
    
    // Start HTTP server
    esp_err_t ret = httpd_start(&server_, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "HTTP server started successfully on port %d", port_);
    
    // 先标记为已运行，这样处理程序注册才能成功
    running_ = true;
    ESP_LOGI(TAG, "Web component marked as running");
    
    // Register default handlers
    InitDefaultHandlers();
    
    // Initialize API handlers
    InitApiHandlers();
    
    ESP_LOGI(TAG, "Web component started successfully");
    
    // 打印注册的处理程序列表
    ESP_LOGI(TAG, "Registered HTTP handlers:");
    for (const auto& handler : http_handlers_) {
        std::string path = handler.first.substr(0, handler.first.find_last_of(":"));
        std::string method_str = handler.first.substr(handler.first.find_last_of(":") + 1);
        ESP_LOGI(TAG, "  %s [method %s]", path.c_str(), method_str.c_str());
    }
    
    ESP_LOGI(TAG, "Registered API handlers:");
    for (const auto& handler : api_handlers_) {
        std::string path = handler.first.substr(0, handler.first.find_last_of(":"));
        std::string method_str = handler.first.substr(handler.first.find_last_of(":") + 1);
        ESP_LOGI(TAG, "  %s [method %s]", path.c_str(), method_str.c_str());
    }
    
    return true;
}

void Web::Stop() {
    if (!running_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping Web component");
    
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
    
    // Clear handler maps
    http_handlers_.clear();
    api_handlers_.clear();
    ws_callbacks_.clear();
    
    running_ = false;
}

bool Web::IsRunning() const {
    return running_;
}

const char* Web::GetName() const {
    return "Web";
}

// HTTP handler registration
void Web::RegisterHandler(HttpMethod method, const std::string& uri, RequestHandler handler) {
    // 保存所有处理程序，即使组件尚未运行
    std::string key = std::string(uri) + ":" + std::to_string(static_cast<int>(method));
    
    // 保存到处理程序映射表中，这样重启时也能使用
    http_handlers_[key] = handler;
    
    if (!running_ || !server_) {
        ESP_LOGW(TAG, "Web component not fully running, handler for %s saved but not registered with httpd yet", uri.c_str());
        return;
    }
    
    // Map HttpMethod enum to httpd_method_t
    httpd_method_t http_method;
    switch (method) {
        case HttpMethod::HTTP_GET:
            http_method = HTTP_GET;
            break;
        case HttpMethod::HTTP_POST:
            http_method = HTTP_POST;
            break;
        case HttpMethod::HTTP_PUT:
            http_method = HTTP_PUT;
            break;
        case HttpMethod::HTTP_DELETE:
            http_method = HTTP_DELETE;
            break;
        case HttpMethod::HTTP_PATCH:
            http_method = HTTP_PATCH;
            break;
        default:
            ESP_LOGE(TAG, "Unknown HTTP method");
            return;
    }
    
    // 特殊处理根路径
    bool isRootPath = (uri == "/");
    if (isRootPath) {
        ESP_LOGI(TAG, "Registering ROOT path handler");
    }
    
    // Register the handler with HTTPD
    httpd_uri_t uri_handler = {
        .uri = uri.c_str(),
        .method = http_method,
        .handler = InternalRequestHandler,
        .user_ctx = this
    };
    
    esp_err_t ret = httpd_register_uri_handler(server_, &uri_handler);
    if (ret != ESP_OK) {
        // 如果是因为已存在而注册失败，不要当作错误处理
        if (ret == ESP_ERR_HTTPD_HANDLER_EXISTS) {
            ESP_LOGW(TAG, "Handler for %s [%d] already registered by httpd", 
                     uri.c_str(), static_cast<int>(method));
            return;
        }
        
        ESP_LOGE(TAG, "Failed to register handler for URI %s: %s", uri.c_str(), esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Registered handler for %s [%d]", uri.c_str(), static_cast<int>(method));
}

void Web::RegisterApiHandler(HttpMethod method, const std::string& uri, ApiHandler handler) {
    if (!running_) {
        ESP_LOGW(TAG, "Web component not running, API handler registration delayed");
        return;
    }
    
    // 检查API处理器是否已注册
    std::string key = uri + ":" + std::to_string(static_cast<int>(method));
    if (api_handlers_.find(key) != api_handlers_.end()) {
        ESP_LOGW(TAG, "API handler for %s [%d] already registered, skipping", 
                 uri.c_str(), static_cast<int>(method));
        return;
    }
    
    // Register API endpoint with /api prefix
    std::string api_uri = uri.substr(0, 4) == "/api" ? uri : "/api" + uri;
    RegisterHandler(method, api_uri, [this, uri, handler](httpd_req_t* req) -> esp_err_t {
        return HandleApiRequest(req, uri);
    });
    
    // Store handler in our API map
    api_handlers_[key] = handler;
    
    ESP_LOGI(TAG, "Registered API handler for %s [%d]", uri.c_str(), static_cast<int>(method));
}

// WebSocket support
void Web::RegisterWebSocketMessageCallback(WebSocketMessageCallback callback) {
    if (callback) {
        ws_callbacks_.push_back(callback);
        ESP_LOGI(TAG, "Registered WebSocket message callback");
    }
}

void Web::RegisterWebSocketHandler(const std::string& uri, WebSocketClientMessageCallback callback) {
    if (callback) {
        ws_uri_handlers_[uri] = callback;
        
        // Register the WebSocket handler for this URI
        httpd_uri_t ws_uri = {
            .uri = uri.c_str(),
            .method = HTTP_GET,
            .handler = &Web::WebSocketHandler,
            .user_ctx = this
        };
        
        if (running_ && server_) {
            httpd_register_uri_handler(server_, &ws_uri);
        }
        
        ESP_LOGI(TAG, "Registered WebSocket handler for URI: %s", uri.c_str());
    }
}

void Web::BroadcastWebSocketMessage(const std::string& message) {
    if (!running_ || !server_) {
        ESP_LOGW(TAG, "Cannot broadcast message: web server not running");
        return;
    }
    
    // 创建WebSocket帧
    httpd_ws_frame_t ws_frame;
    memset(&ws_frame, 0, sizeof(httpd_ws_frame_t));
    ws_frame.payload = (uint8_t*)message.c_str();
    ws_frame.len = message.length();
    ws_frame.type = HTTPD_WS_TYPE_TEXT;
    ws_frame.final = true;
    
    // ESP-IDF没有提供向所有客户端广播的API，所以我们需要遍历可能的文件描述符
    int clients = 0;
    const int max_clients = 8; // 最大客户端数量，可根据实际需求调整
    
    // 尝试向每个可能的套接字发送消息
    for (int fd = 0; fd < max_clients; fd++) {
        if (httpd_ws_get_fd_info(server_, fd) == HTTPD_WS_CLIENT_WEBSOCKET) {
            esp_err_t send_ret = httpd_ws_send_frame_async(server_, fd, &ws_frame);
            if (send_ret != ESP_OK) {
                ESP_LOGD(TAG, "Failed to send WebSocket message to client %d: %s", 
                        fd, esp_err_to_name(send_ret));
            } else {
                clients++;
            }
        }
    }
    
    ESP_LOGI(TAG, "Broadcast WebSocket message to %d clients", clients);
}

void Web::SendWebSocketMessage(httpd_req_t* req, const std::string& message) {
    if (!req) {
        ESP_LOGW(TAG, "Cannot send message: invalid request");
        return;
    }
    
    httpd_ws_frame_t ws_frame;
    memset(&ws_frame, 0, sizeof(httpd_ws_frame_t));
    ws_frame.payload = (uint8_t*)message.c_str();
    ws_frame.len = message.length();
    ws_frame.type = HTTPD_WS_TYPE_TEXT;
    ws_frame.final = true;
    
    esp_err_t ret = httpd_ws_send_frame(req, &ws_frame);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket send failed: %s", esp_err_to_name(ret));
    }
}

bool Web::SendWebSocketMessage(int client_index, const std::string& message) {
    if (!running_ || !server_) {
        ESP_LOGW(TAG, "Cannot send message: web server not running");
        return false;
    }
    
    // 在ESP-IDF 5.x中，WebSocket API有变化
    // 这里简化实现，不支持按客户端索引发送消息
    ESP_LOGW(TAG, "SendWebSocketMessage by client index not supported in ESP-IDF 5.x");
    return false;
}

// Utility methods
std::string Web::GetPostData(httpd_req_t* req) {
    if (!req || req->content_len == 0) {
        return "";
    }
    
    // Allocate buffer for POST data
    char* buffer = (char*)malloc(req->content_len + 1);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for POST data");
        return "";
    }
    
    // Read POST data
    int received = httpd_req_recv(req, buffer, req->content_len);
    if (received <= 0) {
        free(buffer);
        return "";
    }
    
    // Ensure null termination
    buffer[received] = '\0';
    std::string data(buffer);
    free(buffer);
    
    return data;
}

std::map<std::string, std::string> Web::ParseQueryParams(httpd_req_t* req) {
    std::map<std::string, std::string> params;
    
    // Get query string
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len <= 1) {
        return params;
    }
    
    // Allocate memory for query string
    char* buf = (char*)malloc(buf_len);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for query params");
        return params;
    }
    
    // Get query string
    if (httpd_req_get_url_query_str(req, buf, buf_len) != ESP_OK) {
        free(buf);
        return params;
    }
    
    // Parse query string
    char param_name[32];
    char param_val[128];
    
    char* query_ptr = buf;
    char* next_param = nullptr;
    
    // Split by '&'
    while ((next_param = strchr(query_ptr, '&')) != nullptr) {
        *next_param = '\0';
        
        // Extract name and value
        char* equals_ptr = strchr(query_ptr, '=');
        if (equals_ptr) {
            *equals_ptr = '\0';
            strncpy(param_name, query_ptr, sizeof(param_name) - 1);
            param_name[sizeof(param_name) - 1] = '\0';
            
            strncpy(param_val, equals_ptr + 1, sizeof(param_val) - 1);
            param_val[sizeof(param_val) - 1] = '\0';
            
            // URL decode and add to map
            params[UrlDecode(param_name)] = UrlDecode(param_val);
        }
        
        query_ptr = next_param + 1;
    }
    
    // Handle the last parameter
    char* equals_ptr = strchr(query_ptr, '=');
    if (equals_ptr) {
        *equals_ptr = '\0';
        strncpy(param_name, query_ptr, sizeof(param_name) - 1);
        param_name[sizeof(param_name) - 1] = '\0';
        
        strncpy(param_val, equals_ptr + 1, sizeof(param_val) - 1);
        param_val[sizeof(param_val) - 1] = '\0';
        
        params[UrlDecode(param_name)] = UrlDecode(param_val);
    }
    
    free(buf);
    return params;
}

std::string Web::UrlDecode(const std::string& encoded) {
    std::string result;
    result.reserve(encoded.length());
    
    for (size_t i = 0; i < encoded.length(); i++) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            // Handle percent-encoded characters
            int hex_val = 0;
            sscanf(encoded.substr(i + 1, 2).c_str(), "%x", &hex_val);
            result += static_cast<char>(hex_val);
            i += 2;
        } else if (encoded[i] == '+') {
            // Handle plus sign
            result += ' ';
        } else {
            // Regular character
            result += encoded[i];
        }
    }
    
    return result;
}

// Static file handling
esp_err_t Web::HandleStaticFile(httpd_req_t* req) {
    // Extract filename from URI
    std::string uri(req->uri);
    std::string filename = uri;
    
    // Check for index.html
    if (uri == "/" || uri.empty()) {
        filename = "/index.html";
    }
    
    ESP_LOGI(TAG, "HandleStaticFile: Trying to serve %s", filename.c_str());
    
    // 前缀处理：移除开头的"/"并检查路径
    if (filename.starts_with("/")) {
        filename = filename.substr(1);
    }
    
    // CSS和JS文件的特殊处理
    bool is_html = (filename.find(".html") != std::string::npos);
    bool is_css = (filename.find(".css") != std::string::npos);
    bool is_js = (filename.find(".js") != std::string::npos);
    
    // 设置适当的内容类型
    const char* content_type = "text/plain";
    if (is_html) {
        content_type = "text/html";
    } else if (is_css) {
        content_type = "text/css";
    } else if (is_js) {
        content_type = "application/javascript";
    } else if (filename.find(".png") != std::string::npos) {
        content_type = "image/png";
    } else if (filename.find(".jpg") != std::string::npos || filename.find(".jpeg") != std::string::npos) {
        content_type = "image/jpeg";
    } else if (filename.find(".ico") != std::string::npos) {
        content_type = "image/x-icon";
    } else if (filename.find(".svg") != std::string::npos) {
        content_type = "image/svg+xml";
    } else if (filename.find(".json") != std::string::npos) {
        content_type = "application/json";
    }
    
    // 如果是CSS或JS文件，确保路径正确
    if (is_css && !filename.starts_with("css/")) {
        // 尝试在css子目录中找到它
        filename = "css/" + filename;
    } else if (is_js && !filename.starts_with("js/")) {
        // 尝试在js子目录中找到它
        filename = "js/" + filename;
    }
    
    // 记录完整路径
    std::string embedded_path = filename;
    ESP_LOGI(TAG, "Looking for embedded file: %s", embedded_path.c_str());
    
    // 获取对应的嵌入式文件
    const uint8_t* file_start = nullptr;
    const uint8_t* file_end = nullptr;
    
    // HTML文件处理
    if (filename == "index.html") {
        extern const uint8_t index_html_start[] asm("_binary_index_html_start");
        extern const uint8_t index_html_end[] asm("_binary_index_html_end");
        file_start = index_html_start;
        file_end = index_html_end;
    } else if (filename == "vehicle.html") {
        extern const uint8_t vehicle_html_start[] asm("_binary_vehicle_html_start");
        extern const uint8_t vehicle_html_end[] asm("_binary_vehicle_html_end");
        file_start = vehicle_html_start;
        file_end = vehicle_html_end;
    } else if (filename == "vision.html") {
        extern const uint8_t vision_html_start[] asm("_binary_vision_html_start");
        extern const uint8_t vision_html_end[] asm("_binary_vision_html_end");
        file_start = vision_html_start;
        file_end = vision_html_end;
    } else if (filename == "ai.html") {
        extern const uint8_t ai_html_start[] asm("_binary_ai_html_start");
        extern const uint8_t ai_html_end[] asm("_binary_ai_html_end");
        file_start = ai_html_start;
        file_end = ai_html_end;
    } else if (filename == "location.html") {
        extern const uint8_t location_html_start[] asm("_binary_location_html_start");
        extern const uint8_t location_html_end[] asm("_binary_location_html_end");
        file_start = location_html_start;
        file_end = location_html_end;
    } else if (filename == "audio_control.html") {
        extern const uint8_t audio_control_html_start[] asm("_binary_audio_control_html_start");
        extern const uint8_t audio_control_html_end[] asm("_binary_audio_control_html_end");
        file_start = audio_control_html_start;
        file_end = audio_control_html_end;
    } else if (filename == "servo_control.html") {
        extern const uint8_t servo_control_html_start[] asm("_binary_servo_control_html_start");
        extern const uint8_t servo_control_html_end[] asm("_binary_servo_control_html_end");
        file_start = servo_control_html_start;
        file_end = servo_control_html_end;
    } else if (filename == "settings.html") {
        extern const uint8_t settings_html_start[] asm("_binary_settings_html_start");
        extern const uint8_t settings_html_end[] asm("_binary_settings_html_end");
        file_start = settings_html_start;
        file_end = settings_html_end;
    }
    // CSS文件处理
    else if (filename == "css/bootstrap.min.css") {
        extern const uint8_t bootstrap_min_css_start[] asm("_binary_bootstrap_min_css_start");
        extern const uint8_t bootstrap_min_css_end[] asm("_binary_bootstrap_min_css_end");
        file_start = bootstrap_min_css_start;
        file_end = bootstrap_min_css_end;
    } else if (filename == "css/common.css") {
        extern const uint8_t common_css_start[] asm("_binary_common_css_start");
        extern const uint8_t common_css_end[] asm("_binary_common_css_end");
        file_start = common_css_start;
        file_end = common_css_end;
    } else if (filename == "css/main.css") {
        extern const uint8_t main_css_start[] asm("_binary_main_css_start");
        extern const uint8_t main_css_end[] asm("_binary_main_css_end");
        file_start = main_css_start;
        file_end = main_css_end;
    } else if (filename == "css/index.css") {
        extern const uint8_t index_css_start[] asm("_binary_index_css_start");
        extern const uint8_t index_css_end[] asm("_binary_index_css_end");
        file_start = index_css_start;
        file_end = index_css_end;
    } else if (filename == "css/vehicle.css") {
        extern const uint8_t vehicle_css_start[] asm("_binary_vehicle_css_start");
        extern const uint8_t vehicle_css_end[] asm("_binary_vehicle_css_end");
        file_start = vehicle_css_start;
        file_end = vehicle_css_end;
    } else if (filename == "css/vision.css") {
        extern const uint8_t vision_css_start[] asm("_binary_vision_css_start");
        extern const uint8_t vision_css_end[] asm("_binary_vision_css_end");
        file_start = vision_css_start;
        file_end = vision_css_end;
    } else if (filename == "css/ai.css") {
        extern const uint8_t ai_css_start[] asm("_binary_ai_css_start");
        extern const uint8_t ai_css_end[] asm("_binary_ai_css_end");
        file_start = ai_css_start;
        file_end = ai_css_end;
    }
    // JS文件处理
    else if (filename == "js/bootstrap.bundle.min.js") {
        extern const uint8_t bootstrap_bundle_min_js_start[] asm("_binary_bootstrap_bundle_min_js_start");
        extern const uint8_t bootstrap_bundle_min_js_end[] asm("_binary_bootstrap_bundle_min_js_end");
        file_start = bootstrap_bundle_min_js_start;
        file_end = bootstrap_bundle_min_js_end;
    } else if (filename == "js/common.js") {
        extern const uint8_t common_js_start[] asm("_binary_common_js_start");
        extern const uint8_t common_js_end[] asm("_binary_common_js_end");
        file_start = common_js_start;
        file_end = common_js_end;
    } else if (filename == "js/vehicle.js") {
        extern const uint8_t vehicle_js_start[] asm("_binary_vehicle_js_start");
        extern const uint8_t vehicle_js_end[] asm("_binary_vehicle_js_end");
        file_start = vehicle_js_start;
        file_end = vehicle_js_end;
    } else if (filename == "js/ai.js") {
        extern const uint8_t ai_js_start[] asm("_binary_ai_js_start");
        extern const uint8_t ai_js_end[] asm("_binary_ai_js_end");
        file_start = ai_js_start;
        file_end = ai_js_end;
    } else if (filename == "js/vision.js") {
        extern const uint8_t vision_js_start[] asm("_binary_vision_js_start");
        extern const uint8_t vision_js_end[] asm("_binary_vision_js_end");
        file_start = vision_js_start;
        file_end = vision_js_end;
    } else if (filename == "js/location.js") {
        extern const uint8_t location_js_start[] asm("_binary_location_js_start");
        extern const uint8_t location_js_end[] asm("_binary_location_js_end");
        file_start = location_js_start;
        file_end = location_js_end;
    } else if (filename == "js/main.js") {
        extern const uint8_t main_js_start[] asm("_binary_main_js_start");
        extern const uint8_t main_js_end[] asm("_binary_main_js_end");
        file_start = main_js_start;
        file_end = main_js_end;
    } else if (filename == "js/index.js") {
        extern const uint8_t index_js_start[] asm("_binary_index_js_start");
        extern const uint8_t index_js_end[] asm("_binary_index_js_end");
        file_start = index_js_start;
        file_end = index_js_end;
    } else if (filename == "js/servo_control.js") {
        extern const uint8_t servo_control_js_start[] asm("_binary_servo_control_js_start");
        extern const uint8_t servo_control_js_end[] asm("_binary_servo_control_js_end");
        file_start = servo_control_js_start;
        file_end = servo_control_js_end;
    } else if (filename == "js/audio_control.js") {
        extern const uint8_t audio_control_js_start[] asm("_binary_audio_control_js_start");
        extern const uint8_t audio_control_js_end[] asm("_binary_audio_control_js_end");
        file_start = audio_control_js_start;
        file_end = audio_control_js_end;
    } else if (filename == "js/ai-chat.js") {
        extern const uint8_t ai_chat_js_start[] asm("_binary_ai_chat_js_start");
        extern const uint8_t ai_chat_js_end[] asm("_binary_ai_chat_js_end");
        file_start = ai_chat_js_start;
        file_end = ai_chat_js_end;
    } else if (filename == "js/api_client.js") {
        extern const uint8_t api_client_js_start[] asm("_binary_api_client_js_start");
        extern const uint8_t api_client_js_end[] asm("_binary_api_client_js_end");
        file_start = api_client_js_start;
        file_end = api_client_js_end;
    } else if (filename == "js/camera-module.js") {
        extern const uint8_t camera_module_js_start[] asm("_binary_camera_module_js_start");
        extern const uint8_t camera_module_js_end[] asm("_binary_camera_module_js_end");
        file_start = camera_module_js_start;
        file_end = camera_module_js_end;
    } else if (filename == "js/location-module.js") {
        extern const uint8_t location_module_js_start[] asm("_binary_location_module_js_start");
        extern const uint8_t location_module_js_end[] asm("_binary_location_module_js_end");
        file_start = location_module_js_start;
        file_end = location_module_js_end;
    } else if (filename == "js/settings-module.js") {
        extern const uint8_t settings_module_js_start[] asm("_binary_settings_module_js_start");
        extern const uint8_t settings_module_js_end[] asm("_binary_settings_module_js_end");
        file_start = settings_module_js_start;
        file_end = settings_module_js_end;
    } else if (filename == "js/device_config.js") {
        extern const uint8_t device_config_js_start[] asm("_binary_device_config_js_start");
        extern const uint8_t device_config_js_end[] asm("_binary_device_config_js_end");
        file_start = device_config_js_start;
        file_end = device_config_js_end;
    }
    
    // 如果没有找到嵌入式文件，尝试使用GetHtml生成
    if (!file_start || !file_end || file_start >= file_end) {
        ESP_LOGW(TAG, "Static file not embedded: %s", filename.c_str());
        
        // 对于HTML文件，尝试使用GetHtml生成页面
        if (is_html) {
            std::string page = filename;
            if (page.ends_with(".html")) {
                page = page.substr(0, page.length() - 5);
            }
            
            ESP_LOGI(TAG, "Trying to generate HTML for: %s", page.c_str());
            std::string html = GetHtml(page);
            
            // 只有当不是404页面时才返回内容
            if (html.find("404 Not Found") == std::string::npos) {
                ESP_LOGI(TAG, "Generated HTML content for %s (%zu bytes)", page.c_str(), html.length());
                httpd_resp_set_type(req, "text/html");
                httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
                httpd_resp_sendstr(req, html.c_str());
                return ESP_OK;
            }
        } 
        // 为缺失的CSS和JS文件提供基本的回退版本
        else if (is_css && filename == "css/bootstrap.min.css") {
            // 提供极简的bootstrap替代版本
            std::string css = "body{font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;line-height:1.5;margin:0}";
            httpd_resp_set_type(req, "text/css");
            httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
            httpd_resp_sendstr(req, css.c_str());
            return ESP_OK;
        }
        else if (is_js && filename == "js/bootstrap.bundle.min.js") {
            // 提供空的JS函数
            std::string js = "/* Bootstrap replacement */";
            httpd_resp_set_type(req, "application/javascript");
            httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
            httpd_resp_sendstr(req, js.c_str());
            return ESP_OK;
        }
        else if (is_js && filename == "js/common.js") {
            // 提供基础共用函数
            std::string js = "function getUrlParam(name){const params=new URLSearchParams(window.location.search);return params.get(name);}";
            httpd_resp_set_type(req, "application/javascript");
            httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
            httpd_resp_sendstr(req, js.c_str());
            return ESP_OK;
        }
        
        // 输出详细的调试信息
        ESP_LOGW(TAG, "Could not find or generate content for %s, returning 404", filename.c_str());
        
        // 返回友好的404页面
        std::string not_found_html = "<!DOCTYPE html><html><head><title>404 Not Found</title></head><body>"
                               "<h1>404 Not Found</h1>"
                               "<p>The requested file '" + filename + "' was not found on this server.</p>"
                               "<p><a href='/'>Return to Home Page</a></p>"
                               "</body></html>";
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        httpd_resp_sendstr(req, not_found_html.c_str());
        return ESP_OK;
    }
    
    // 设置内容类型和缓存控制
    httpd_resp_set_type(req, content_type);
    if (is_css || is_js) {
        // CSS和JS可以长期缓存
        httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    } else if (is_html) {
        // HTML不要缓存
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    }
    
    ESP_LOGI(TAG, "Serving file %s as %s (%ld bytes)", filename.c_str(), content_type, (long)(file_end - file_start));
    
    // 发送文件数据
    return httpd_resp_send(req, (const char*)file_start, file_end - file_start);
}

// Internal handlers
void Web::InitDefaultHandlers() {
    // ROOT: 确保根路径处理程序是第一个注册的，并增加其优先级
    RegisterHandler(HttpMethod::HTTP_GET, "/", RootHandler);
    
    // 添加对favicon.ico的处理
    RegisterHandler(HttpMethod::HTTP_GET, "/favicon.ico", [](httpd_req_t* req) -> esp_err_t {
        // 如果没有图标，返回204 No Content
        httpd_resp_set_status(req, "204 No Content");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    });
    
    // 添加用于调试的主路由
    RegisterHandler(HttpMethod::HTTP_GET, "/index.html", RootHandler);
    
    // 为常用URL路径添加处理器
    RegisterHandler(HttpMethod::HTTP_GET, "/cam", VisionHandler);
    RegisterHandler(HttpMethod::HTTP_GET, "/vision", VisionHandler);
    RegisterHandler(HttpMethod::HTTP_GET, "/motor", CarHandler);
    RegisterHandler(HttpMethod::HTTP_GET, "/vehicle", CarHandler);
    RegisterHandler(HttpMethod::HTTP_GET, "/car", CarHandler);
    RegisterHandler(HttpMethod::HTTP_GET, "/ai", AIHandler);
    RegisterHandler(HttpMethod::HTTP_GET, "/location", LocationHandler);
    
    // 添加可能的拼写错误处理
    RegisterHandler(HttpMethod::HTTP_GET, "/vechicle", CarHandler);
    
    // 添加WebSocket支持
    RegisterHandler(HttpMethod::HTTP_GET, "/ws/*", WebSocketHandler);
    
    // 添加静态文件支持 - 使用lambda包装非静态成员函数
    RegisterHandler(HttpMethod::HTTP_GET, "/css/*", [this](httpd_req_t* req) -> esp_err_t {
        return this->HandleStaticFile(req);
    });
    RegisterHandler(HttpMethod::HTTP_GET, "/js/*", [this](httpd_req_t* req) -> esp_err_t {
        return this->HandleStaticFile(req);
    });
    RegisterHandler(HttpMethod::HTTP_GET, "/img/*", [this](httpd_req_t* req) -> esp_err_t {
        return this->HandleStaticFile(req);
    });
    RegisterHandler(HttpMethod::HTTP_GET, "/fonts/*", [this](httpd_req_t* req) -> esp_err_t {
        return this->HandleStaticFile(req);
    });
    
    // 添加全局捕获处理程序来处理没有明确注册的路径
    RegisterHandler(HttpMethod::HTTP_GET, "/*", [this](httpd_req_t* req) -> esp_err_t {
        return this->HandleStaticFile(req);
    });
    
    ESP_LOGI(TAG, "Registered default HTTP handlers");
}

void Web::InitApiHandlers() {
    // Define basic API endpoints
    
    // System info
    RegisterApiHandler(HttpMethod::HTTP_GET, "/system/info", [](httpd_req_t* req) -> ApiResponse {
        cJSON* data = cJSON_CreateObject();
        
        // System info
        cJSON_AddStringToObject(data, "version", "1.0.0");
        cJSON_AddNumberToObject(data, "uptime_ms", esp_timer_get_time() / 1000);
        cJSON_AddNumberToObject(data, "free_heap", esp_get_free_heap_size());
        
        ApiResponse response;
        response.content = cJSON_PrintUnformatted(data);
        cJSON_Delete(data);
        
        return response;
    });
    
    // Additional API endpoints would be registered here
    // Test API endpoint
    RegisterApiHandler(HttpMethod::HTTP_GET, "/test", [](httpd_req_t* req) -> ApiResponse {
        ApiResponse response;
        response.content = "{\"status\":\"ok\",\"message\":\"API is working!\"}";
        return response;
    });
    ESP_LOGI(TAG, "Registered API handlers");
}

// Static HTTP handlers
esp_err_t Web::InternalRequestHandler(httpd_req_t* req) {
    if (!req || !req->user_ctx) {
        ESP_LOGE(TAG, "Invalid request or user context");
        return httpd_resp_send_500(req);
    }
    
    const char* method_str = "";
    switch (req->method) {
        case HTTP_GET:    method_str = "GET"; break;
        case HTTP_POST:   method_str = "POST"; break;
        case HTTP_PUT:    method_str = "PUT"; break;
        case HTTP_DELETE: method_str = "DELETE"; break;
        case HTTP_PATCH:  method_str = "PATCH"; break;
        default: method_str = "UNKNOWN"; break;
    }
    
    ESP_LOGI(TAG, "Request received: %s %s", method_str, req->uri);
    Web* web = static_cast<Web*>(req->user_ctx);
    
    // 优先处理根路径
    if (strcmp(req->uri, "/") == 0 && req->method == HTTP_GET) {
        return web->RootHandler(req);
    }
    
    // 尝试使用注册的URL处理程序
    std::string uri = req->uri;
    std::string key = uri + ":" + std::to_string(req->method);
    
    // 特殊处理 API 请求
    if (uri.starts_with("/api/")) {
        return web->HandleApiRequest(req, uri);
    }
    
    // 特殊处理 WebSocket 请求
    if (uri.starts_with("/ws/")) {
        return web->WebSocketHandler(req);
    }
    
    // 检查是否有处理程序
    auto it = web->http_handlers_.find(key);
    if (it != web->http_handlers_.end()) {
        ESP_LOGI(TAG, "Found handler for %s", uri.c_str());
        return it->second(req);
    }
    
    // 检查是否有.html扩展名
    if (uri.find(".html") != std::string::npos && req->method == HTTP_GET) {
        // 尝试使用没有.html的路径查找处理程序
        std::string base_uri = uri.substr(0, uri.length() - 5);
        std::string base_key = base_uri + ":" + std::to_string(req->method);
        
        auto base_it = web->http_handlers_.find(base_key);
        if (base_it != web->http_handlers_.end()) {
            ESP_LOGI(TAG, "Using handler for %s instead of %s", base_uri.c_str(), uri.c_str());
            return base_it->second(req);
        }
    }
    
    // 对于GET请求，尝试作为静态文件处理
    if (req->method == HTTP_GET) {
        return web->HandleStaticFile(req);
    }
    
    // 没有找到处理程序，返回友好的404页面
    ESP_LOGW(TAG, "No handler found for %s %s", method_str, req->uri);
    std::string not_found_html = "<!DOCTYPE html><html><head><title>404 Not Found</title></head><body>"
                               "<h1>404 Not Found</h1>"
                               "<p>The requested URL " + std::string(req->uri) + " was not found on this server.</p>"
                               "<p><a href='/'>返回主页</a></p>"
                               "</body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, not_found_html.c_str());
    return ESP_OK;
}

esp_err_t Web::WebSocketHandler(httpd_req_t* req) {
    if (!current_instance_) {
        ESP_LOGE(TAG, "No active Web instance for WebSocket handler");
        return httpd_resp_send_500(req);
    }
    
    if (req->method == HTTP_GET) {
        // WebSocket handshake
        ESP_LOGI(TAG, "WebSocket handshake for URI: %s", req->uri);
        
        // 在ESP-IDF 5.x中，WebSocket API有变化
        // 不需要显式调用httpd_ws_respond_server_handshake
        
        // 设置接收回调
        // 在ESP-IDF 5.x中，WebSocket API有变化
        // 不再需要显式设置接收回调
        
        ESP_LOGI(TAG, "WebSocket connection established");
        return ESP_OK;
    }
    
    // 处理WebSocket帧
    httpd_ws_frame_t ws_frame;
    memset(&ws_frame, 0, sizeof(httpd_ws_frame_t));
    ws_frame.type = HTTPD_WS_TYPE_TEXT;
    
    // 获取帧长度
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WebSocket frame length: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 分配内存并接收完整帧
    uint8_t* payload = (uint8_t*)malloc(ws_frame.len + 1);
    if (!payload) {
        ESP_LOGE(TAG, "Failed to allocate memory for WebSocket frame");
        return ESP_ERR_NO_MEM;
    }
    
    ws_frame.payload = payload;
    ret = httpd_ws_recv_frame(req, &ws_frame, ws_frame.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to receive WebSocket frame: %s", esp_err_to_name(ret));
        free(payload);
        return ret;
    }
    
    // 确保字符串以null结尾
    payload[ws_frame.len] = 0;
    
    // 处理消息
    std::string message((char*)payload, ws_frame.len);
    free(payload);
    
    // 获取URI
    std::string uri = req->uri;
    
    // 检查是否有URI特定处理器
    auto it = current_instance_->ws_uri_handlers_.find(uri);
    if (it != current_instance_->ws_uri_handlers_.end()) {
        // 获取客户端索引
        int client_index = httpd_req_to_sockfd(req);
        it->second(client_index, message);
    }
    
    // 调用通用回调
    for (auto& callback : current_instance_->ws_callbacks_) {
        callback(req, message);
    }
    
    return ESP_OK;
}

// API request handling
esp_err_t Web::HandleApiRequest(httpd_req_t* req, const std::string& uri) {
    // Find appropriate API handler
    std::string key = uri + ":" + std::to_string(req->method);
    auto it = api_handlers_.find(key);
    
    if (it == api_handlers_.end()) {
        ESP_LOGW(TAG, "API handler not found for %s [method %d]", uri.c_str(), req->method);
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":404,\"message\":\"API endpoint not found\"}");
        return ESP_OK;
    }
    
    // Call API handler
    ApiResponse response = it->second(req);
    
    // Set response headers
    httpd_resp_set_type(req, 
        response.type == ApiResponseType::JSON ? "application/json" :
        response.type == ApiResponseType::TEXT ? "text/plain" :
        response.type == ApiResponseType::HTML ? "text/html" : 
        "application/octet-stream");
    
    // Add custom headers
    for (const auto& header : response.headers) {
        httpd_resp_set_hdr(req, header.first.c_str(), header.second.c_str());
    }
    
    // Set status code
    char status_str[16];
    sprintf(status_str, "%d", response.status_code);
    httpd_resp_set_status(req, status_str);
    
    // Send response
    httpd_resp_send(req, response.content.c_str(), response.content.length());
    return ESP_OK;
}

// Root handler implementation
esp_err_t Web::RootHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "Handling root request: %s", req->uri);
    
    if (!current_instance_) {
        ESP_LOGE(TAG, "No Web instance for ROOT handler");
        return httpd_resp_send_500(req);
    }
    
    std::string html = current_instance_->GetHtml("index");
    
    if (html.empty() || html.find("页面不存在") != std::string::npos) {
        ESP_LOGW(TAG, "Failed to load index.html content or received 404 template");
        // 如果无法获取正确的index内容，尝试使用默认的简单HTML
        html = "<!DOCTYPE html><html><head><title>Vehicle Control</title>"
               "<meta charset='UTF-8'>"
               "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
               "<style>body{font-family:Arial;text-align:center;margin:40px}</style>"
               "</head><body>"
               "<h1>ESP32 Vehicle Control</h1>"
               "<p>Welcome to the ESP32 Vehicle Control System</p>"
               "<ul style='list-style:none;padding:0'>"
               "<li><a href='/vehicle'>Vehicle Control</a></li>"
               "<li><a href='/cam'>Camera</a></li>"
               "</ul></body></html>";
    }
    
    ESP_LOGI(TAG, "Serving index.html (%zu bytes)", html.length());
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, html.c_str());
    
    return ESP_OK;
}

// Pre-defined content type handlers
esp_err_t Web::VisionHandler(httpd_req_t* req) {
    if (!current_instance_) {
        return httpd_resp_send_500(req);
    }
    
    std::string html = current_instance_->GetHtml("vision");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

esp_err_t Web::CarHandler(httpd_req_t* req) {
    if (!current_instance_) {
        return httpd_resp_send_500(req);
    }
    
    std::string html = current_instance_->GetHtml("vehicle");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

esp_err_t Web::AIHandler(httpd_req_t* req) {
    if (!current_instance_) {
        return httpd_resp_send_500(req);
    }
    
    std::string html = current_instance_->GetHtml("ai");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

esp_err_t Web::LocationHandler(httpd_req_t* req) {
    if (!current_instance_) {
        return httpd_resp_send_500(req);
    }
    
    std::string html = current_instance_->GetHtml("location");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

// Content generation - 改为直接读取HTML文件
std::string Web::GetHtml(const std::string& page, const char* accept_language) {
    ESP_LOGI(TAG, "GetHtml: Looking for HTML file %s.html", page.c_str());
    
    // 构建正确的文件名
    std::string filename = page;
    if (filename.ends_with(".html")) {
        filename = filename.substr(0, filename.length() - 5);
    }
    
    // 尝试从嵌入的二进制文件中获取HTML内容
    const uint8_t* file_start = nullptr;
    const uint8_t* file_end = nullptr;
    bool found = false;
    
    // 查找HTML文件
    if (filename == "index") {
        extern const uint8_t index_html_start[] asm("_binary_index_html_start");
        extern const uint8_t index_html_end[] asm("_binary_index_html_end");
        file_start = index_html_start;
        file_end = index_html_end;
        found = true;
    } else if (filename == "vehicle") {
        extern const uint8_t vehicle_html_start[] asm("_binary_vehicle_html_start");
        extern const uint8_t vehicle_html_end[] asm("_binary_vehicle_html_end");
        file_start = vehicle_html_start;
        file_end = vehicle_html_end;
        found = true;
    } else if (filename == "vision") {
        extern const uint8_t vision_html_start[] asm("_binary_vision_html_start");
        extern const uint8_t vision_html_end[] asm("_binary_vision_html_end");
        file_start = vision_html_start;
        file_end = vision_html_end;
        found = true;
    } else if (filename == "ai") {
        extern const uint8_t ai_html_start[] asm("_binary_ai_html_start");
        extern const uint8_t ai_html_end[] asm("_binary_ai_html_end");
        file_start = ai_html_start;
        file_end = ai_html_end;
        found = true;
    } else if (filename == "location") {
        extern const uint8_t location_html_start[] asm("_binary_location_html_start");
        extern const uint8_t location_html_end[] asm("_binary_location_html_end");
        file_start = location_html_start;
        file_end = location_html_end;
        found = true;
    } else if (filename == "settings") {
        extern const uint8_t settings_html_start[] asm("_binary_settings_html_start");
        extern const uint8_t settings_html_end[] asm("_binary_settings_html_end");
        file_start = settings_html_start;
        file_end = settings_html_end;
        found = true;
    } else if (filename == "servo_control") {
        extern const uint8_t servo_control_html_start[] asm("_binary_servo_control_html_start");
        extern const uint8_t servo_control_html_end[] asm("_binary_servo_control_html_end");
        file_start = servo_control_html_start;
        file_end = servo_control_html_end;
        found = true;
    } else if (filename == "audio_control") {
        extern const uint8_t audio_control_html_start[] asm("_binary_audio_control_html_start");
        extern const uint8_t audio_control_html_end[] asm("_binary_audio_control_html_end");
        file_start = audio_control_html_start;
        file_end = audio_control_html_end;
        found = true;
    }
    
    // 如果找到了文件，返回其内容
    if (found && file_start && file_end && file_start < file_end) {
        ESP_LOGI(TAG, "Found embedded HTML file %s.html (%ld bytes)", 
                 filename.c_str(), (long)(file_end - file_start));
        return std::string((const char*)file_start, file_end - file_start);
    }
    
    ESP_LOGW(TAG, "HTML file %s.html not found in embedded files", filename.c_str());
    
    // 如果找不到嵌入的HTML文件，返回简单的404页面
    if (page != "index" && page != "") {
        return "<!DOCTYPE html><html><head><title>404 Not Found</title>"
               "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
               "<style>body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:20px;text-align:center;}"
               "h1{color:#dc3545;}</style></head><body>"
               "<h1>404 Not Found</h1>"
               "<p>The requested page \"" + page + "\" was not found.</p>"
               "<p><a href='/'>Back to Home</a></p>"
               "</body></html>";
    }
    
    // 对于根页面，提供一个简单但功能完整的替代首页
    return "<!DOCTYPE html><html><head><title>ESP32 Web Server</title>"
           "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
           "<style>body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:20px;text-align:center;}"
           "a{color:#0d6efd;text-decoration:none;}a:hover{text-decoration:underline;}"
           ".card{border:1px solid #ddd;border-radius:8px;padding:15px;margin:15px auto;max-width:300px;}"
           "</style></head><body>"
           "<h1>ESP32 Web Server</h1>"
           "<div class='card'><h2>Vehicle Control</h2><p><a href='/vehicle'>Open Vehicle Control</a></p></div>"
           "<div class='card'><h2>Camera</h2><p><a href='/vision'>View Camera</a></p></div>"
           "<p>Server running on port " + std::to_string(port_) + "</p>"
           "</body></html>";
} 