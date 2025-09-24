#include "web.h"
#include "api.h"
#include <cmath>   // 添加数学函数头文件，包含NAN和isnan
#include <algorithm>
#include <sstream>
#include <functional>
#include <cstring>
#include <cstdlib>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_http_server.h>
#include <esp_wifi.h>
#include <cJSON.h>

#include "../iot/thing.h"
#include "../iot/thing_manager.h"

// 使用命名空间
using namespace std;  // 使用标准命名空间
using iot::Thing;
using iot::ThingManager;

// 添加安全获取属性值的辅助函数
float SafeGetValue(Thing* thing, const std::string& property_name) {
    if (!thing) {
        return NAN;
    }
    
    // 检查属性是否存在于values映射中（避免在日志中产生警告）
    const auto& values = thing->GetValues();
    auto it = values.find(property_name);
    if (it != values.end()) {
        return it->second;
    }
    
    // 如果不在map中，不直接使用GetValue（这会产生警告日志）
    // 而是直接返回NAN表示不可用
    return NAN;
}

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
    
    // Initialize API handlers - 仅用于获取信息
    InitApiHandlers();
    
    // 初始化车辆WebSocket处理程序 - 用于实时控制
    InitVehicleWebSocketHandlers();
    
    // 初始化传感器WebSocket处理程序 - 用于实时数据
    InitSensorHandlers();
    
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
    
    ESP_LOGI(TAG, "Registered WebSocket handlers:");
    for (const auto& handler : ws_uri_handlers_) {
        ESP_LOGI(TAG, "  %s", handler.first.c_str());
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
    
    // 确保API URI格式正确，以/api/开头
    std::string api_uri = uri;
    if (api_uri.substr(0, 5) != "/api/") {
        if (api_uri.substr(0, 4) == "/api") {
            api_uri = "/api/" + api_uri.substr(4);
        } else {
            api_uri = "/api/" + (api_uri.front() == '/' ? api_uri.substr(1) : api_uri);
        }
    }
    
    // 确保键格式一致：/api/xxx:HTTP_METHOD_NUM
    std::string key = api_uri + ":" + std::to_string(static_cast<int>(method));
    
    // 检查API处理器是否已注册
    if (api_handlers_.find(key) != api_handlers_.end()) {
        ESP_LOGW(TAG, "API handler for %s [%d] already registered, skipping", 
                 api_uri.c_str(), static_cast<int>(method));
        return;
    }
    
    // 保存处理程序到 API 映射，确保键和实际 URI 匹配
    api_handlers_[key] = handler;
    
    // 注册处理程序
    RegisterHandler(method, api_uri, [this, api_uri, handler](httpd_req_t* req) -> esp_err_t {
        return HandleApiRequest(req, api_uri);
    });
    
    ESP_LOGI(TAG, "Registered API handler for %s [%d] with key %s", 
             api_uri.c_str(), static_cast<int>(method), key.c_str());
}

// WebSocket support
void Web::RegisterWebSocketMessageCallback(WebSocketMessageCallback callback) {
    if (callback) {
        ws_callbacks_.push_back(callback);
        ESP_LOGI(TAG, "Registered WebSocket message callback");
    }
}

// 标准化WebSocket路径 - 确保所有WebSocket路径遵循统一格式
std::string Web::NormalizeWebSocketPath(const std::string& uri) {
    std::string normalized = uri;
    
    // 确保路径以/ws开头
    if (normalized.find("/ws") != 0) {
        if (normalized[0] == '/') {
            normalized = "/ws" + normalized;
        } else {
            normalized = "/ws/" + normalized;
        }
    }
    
    // 确保/ws后面有斜杠（除非只是/ws）
    if (normalized == "/ws") {
        return normalized;
    }
    
    if (normalized.length() > 3 && normalized[3] != '/') {
        normalized.insert(3, "/");
    }
    
    // 移除末尾的斜杠（如果存在）
    if (normalized.length() > 4 && normalized.back() == '/') {
        normalized.pop_back();
    }
    
    return normalized;
}

void Web::RegisterWebSocketHandler(const std::string& uri, WebSocketClientMessageCallback callback) {
    if (callback) {
        // 使用标准化方法处理URI
        std::string normalized_uri = NormalizeWebSocketPath(uri);
        if (uri != normalized_uri) {
            ESP_LOGI(TAG, "Normalizing WebSocket URI from %s to %s", uri.c_str(), normalized_uri.c_str());
        }
        
        ws_uri_handlers_[normalized_uri] = callback;
        
        // 注册WebSocket处理程序
        httpd_uri_t ws_uri = {
            .uri = normalized_uri.c_str(),
            .method = HTTP_GET,
            .handler = &Web::WebSocketHandler,
            .user_ctx = this
        };
        
        if (running_ && server_) {
            esp_err_t ret = httpd_register_uri_handler(server_, &ws_uri);
            if (ret != ESP_OK) {
                if (ret == ESP_ERR_HTTPD_HANDLER_EXISTS) {
                    ESP_LOGW(TAG, "WebSocket handler for %s already exists", normalized_uri.c_str());
                } else {
                    ESP_LOGE(TAG, "Failed to register WebSocket handler for %s: %s", 
                             normalized_uri.c_str(), esp_err_to_name(ret));
                }
            } else {
                ESP_LOGI(TAG, "Registered WebSocket handler for URI: %s", normalized_uri.c_str());
            }
        } else {
            ESP_LOGW(TAG, "Server not running, WebSocket registration for %s delayed", normalized_uri.c_str());
        }
    }
}

void Web::BroadcastWebSocketMessage(const std::string& message) {
    if (!running_ || !server_) {
        ESP_LOGD(TAG, "Cannot broadcast message: web server not running");
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
    const int max_clients = 32; // 增加最大客户端数量
    
    // 打印要广播的消息内容（仅用于调试）
    ESP_LOGI(TAG, "Broadcasting WebSocket message: %s", message.c_str());
    
    // 尝试向每个可能的套接字发送消息
    for (int fd = 0; fd < max_clients; fd++) {
        int client_info = httpd_ws_get_fd_info(server_, fd);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
            esp_err_t send_ret = httpd_ws_send_frame_async(server_, fd, &ws_frame);
            if (send_ret == ESP_OK) {
                clients++;
            } else {
                ESP_LOGW(TAG, "Failed to send WebSocket message to client %d: %s", fd, esp_err_to_name(send_ret));
            }
        }
    }
    
    ESP_LOGI(TAG, "WebSocket message broadcasted to %d clients", clients);
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
    if (is_css) {
        // 确保CSS文件在正确的路径
        if (!filename.starts_with("css/")) {
        filename = "css/" + filename;
        }
        ESP_LOGI(TAG, "CSS file path adjusted to: %s", filename.c_str());
    } else if (is_js) {
        // 确保JS文件在正确的路径
        if (!filename.starts_with("js/")) {
        filename = "js/" + filename;
        }
        ESP_LOGI(TAG, "JS file path adjusted to: %s", filename.c_str());
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
    } else if (filename == "js/jquery-3.6.0.min.js") {
        // 提供改进的 jQuery 替代品，处理更多边缘情况
        std::string js = "/* Improved jQuery replacement */\n"
                       "window.$ = function(selector) {\n"
                       "  if (!selector) return createWrapper([]);\n"
                       "  if (selector === document) return createWrapper([document]);\n"
                       "  if (typeof selector === 'object' && selector.nodeType) return createWrapper([selector]);\n"
                       "  let elements = [];\n"
                       "  try {\n"
                       "    elements = Array.from(document.querySelectorAll(selector));\n"
                       "  } catch(e) {\n"
                       "    console.warn('Invalid selector:', selector);\n"
                       "  }\n"
                       "  return createWrapper(elements);\n"
                       "};\n\n"
                       "function createWrapper(elements) {\n"
                       "  return {\n"
                       "    elements: elements,\n"
                       "    length: elements.length,\n"
                       "    on: function(event, callback) {\n"
                       "      elements.forEach(el => el.addEventListener(event, callback));\n"
                       "      return this;\n"
                       "    },\n"
                       "    val: function(value) {\n"
                       "      if (value === undefined) return elements[0] ? elements[0].value : '';\n"
                       "      elements.forEach(el => el.value = value);\n"
                       "      return this;\n"
                       "    },\n"
                       "    text: function(value) {\n"
                       "      if (value === undefined) return elements[0] ? elements[0].textContent : '';\n"
                       "      elements.forEach(el => el.textContent = value);\n"
                       "      return this;\n"
                       "    },\n"
                       "    html: function(value) {\n"
                       "      if (value === undefined) return elements[0] ? elements[0].innerHTML : '';\n"
                       "      elements.forEach(el => el.innerHTML = value);\n"
                       "      return this;\n"
                       "    },\n"
                       "    hide: function() { elements.forEach(el => el.style.display = 'none'); return this; },\n"
                       "    show: function() { elements.forEach(el => el.style.display = ''); return this; },\n"
                       "    addClass: function(cls) { elements.forEach(el => el.classList.add(cls)); return this; },\n"
                       "    removeClass: function(cls) { elements.forEach(el => el.classList.remove(cls)); return this; },\n"
                       "    ready: function(fn) { if (document.readyState !== 'loading') fn(); else document.addEventListener('DOMContentLoaded', fn); return this; },\n"
                       "  };\n"
                       "}\n\n"
                       "$.ajax = function(options) {\n"
                       "  const xhr = new XMLHttpRequest();\n"
                       "  xhr.open(options.type || 'GET', options.url);\n"
                       "  if (options.contentType) xhr.setRequestHeader('Content-Type', options.contentType);\n"
                       "  xhr.onload = function() {\n"
                       "    if (xhr.status >= 200 && xhr.status < 300) {\n"
                       "      let data = xhr.responseText;\n"
                       "      if (options.dataType === 'json') {\n"
                       "        try { data = JSON.parse(data); } catch(e) { console.error('Error parsing JSON:', e); }\n"
                       "      }\n"
                       "      if (options.success) options.success(data);\n"
                       "    } else if (options.error) {\n"
                       "      options.error(xhr);\n"
                       "    }\n"
                       "  };\n"
                       "  xhr.onerror = function() { if (options.error) options.error(xhr); };\n"
                       "  xhr.send(options.data);\n"
                       "};\n"
                       "$.get = function(url, success) { $.ajax({url: url, success: success}); };\n"
                       "$.post = function(url, data, success) { $.ajax({url: url, type: 'POST', data: data, success: success}); };\n"
                       "$.ready = function(fn) { $(document).ready(fn); };";
        httpd_resp_set_type(req, "application/javascript");
        httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
        httpd_resp_sendstr(req, js.c_str());
        return ESP_OK;
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
    
    // 先添加WebSocket支持 - 确保这些处理程序有最高优先级
    // 注意：先注册具体路径，再注册通配符路径
    RegisterHandler(HttpMethod::HTTP_GET, "/ws", WebSocketHandler);  // 处理纯/ws路径
    RegisterHandler(HttpMethod::HTTP_GET, "/ws/*", WebSocketHandler); // 处理/ws/子路径
    
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
    ESP_LOGI(TAG, "Initializing API handlers");
    
    // 系统信息API
    RegisterApiHandler(HttpMethod::HTTP_GET, "/api/system/info", [](httpd_req_t* req) -> ApiResponse {
        ESP_LOGI(TAG, "Handling /api/system/info request");
        
        cJSON* data = cJSON_CreateObject();
        
        // System info
        cJSON_AddStringToObject(data, "version", "1.0.0");
        cJSON_AddNumberToObject(data, "uptime_ms", esp_timer_get_time() / 1000);
        cJSON_AddNumberToObject(data, "free_heap", esp_get_free_heap_size());
        cJSON_AddStringToObject(data, "build_time", __DATE__ " " __TIME__);
        
        ApiResponse response;
        response.content = cJSON_PrintUnformatted(data);
        cJSON_Delete(data);
        
        ESP_LOGI(TAG, "Sending system info response: %s", response.content.c_str());
        return response;
    });
    
    // 摄像头API现在由api.cc中的HandleCameraStream函数处理
    
    // 添加位置API
    RegisterApiHandler(HttpMethod::HTTP_GET, "/api/location", [](httpd_req_t* req) -> ApiResponse {
        ESP_LOGI(TAG, "Handling /api/location request");
        ApiResponse response;
        response.content = "{\"status\":\"ok\",\"latitude\":30.2825,\"longitude\":120.1253,\"accuracy\":10.5}";
        return response;
    });
    
    // 测试API - 用于测试API系统是否正常工作
    RegisterApiHandler(HttpMethod::HTTP_GET, "/api/test", [](httpd_req_t* req) -> ApiResponse {
        ESP_LOGI(TAG, "Handling /api/test request");
        ApiResponse response;
        response.content = "{\"status\":\"ok\",\"message\":\"API is working!\"}";
        return response;
    });
    
    // 注意：所有用于控制电机和舵机的API端点已被移除
    // 这些功能现在通过WebSocket连接实现，以获得更好的实时性能
    
    ESP_LOGI(TAG, "Registered API handlers");
    
    // 摄像头流处理程序 - 添加在InitApiHandlers方法中其他API处理程序后面
    RegisterApiHandler(HttpMethod::HTTP_GET, "/api/camera/stream", [](httpd_req_t* req) -> ApiResponse {
        ESP_LOGI(TAG, "Handling camera stream request");
        
        // 这里只返回一个占位信息，真正的摄像头流可能需要单独的处理器来支持MJPEG或其他流格式
        ApiResponse response;
        response.status_code = 200;
        response.type = ApiResponseType::JSON;
        
        // 创建JSON响应
        cJSON* json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "status", "Camera stream not implemented yet");
        cJSON_AddStringToObject(json, "message", "This is a placeholder for camera stream API");
        
        // 转换为字符串
        char* json_str = cJSON_PrintUnformatted(json);
        response.content = json_str;
        cJSON_Delete(json);
        free(json_str);
        
        return response;
    });
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
    if (uri.starts_with("/api")) {
        return web->HandleApiRequest(req, uri);
    }
    
    // 特殊处理 WebSocket 请求 - 同时处理 /ws 和 /ws/* 路径
    if (uri == "/ws" || uri.starts_with("/ws/")) {
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
    
    // 获取并标准化URI
    std::string uri = req->uri;
    std::string normalized_uri = NormalizeWebSocketPath(uri);
    ESP_LOGI(TAG, "WebSocket message received on URI: %s (normalized: %s), message: %s", 
             uri.c_str(), normalized_uri.c_str(), message.c_str());
    
    // 检查是否有URI特定处理器
    bool handled = false;
    
    // 获取客户端索引
    int client_index = httpd_req_to_sockfd(req);
    
    // 针对根WebSocket路径，发送给所有注册的回调
    if (normalized_uri == "/ws") {
        for (auto& callback : current_instance_->ws_callbacks_) {
            callback(req, message);
            handled = true;
        }
    }
    
    // 检查特定路径处理程序 - 使用精确匹配
    auto it = current_instance_->ws_uri_handlers_.find(normalized_uri);
    if (it != current_instance_->ws_uri_handlers_.end()) {
        ESP_LOGI(TAG, "Found exact handler for WebSocket path: %s", normalized_uri.c_str());
        it->second(client_index, message);
        handled = true;
    } 
    // 如果没有精确匹配，尝试前缀匹配
    else {
        for (auto& handler_pair : current_instance_->ws_uri_handlers_) {
            // 检查是否是路径前缀
            if (normalized_uri.find(handler_pair.first + "/") == 0 || 
                normalized_uri == handler_pair.first) {
                ESP_LOGI(TAG, "Found prefix handler for WebSocket path: %s (prefix: %s)", 
                         normalized_uri.c_str(), handler_pair.first.c_str());
                handler_pair.second(client_index, message);
                handled = true;
                break;
            }
        }
    }
    
    // 如果没有任何处理程序处理消息，记录警告
    if (!handled) {
        ESP_LOGW(TAG, "No handler found for WebSocket message on %s (normalized: %s)", 
                 uri.c_str(), normalized_uri.c_str());
        
        // 列出所有已注册的处理程序供调试
        ESP_LOGW(TAG, "Registered WebSocket handlers:");
        for (const auto& h : current_instance_->ws_uri_handlers_) {
            ESP_LOGW(TAG, "  - %s", h.first.c_str());
        }
    }
    
    return ESP_OK;
}

// API request handling
esp_err_t Web::HandleApiRequest(httpd_req_t* req, const std::string& uri) {
    ESP_LOGI(TAG, "API Request received: %s", uri.c_str());
    
    // 确保URI格式正确 - 必须以/api/开头
    std::string normalized_uri = uri;
    
    // 直接使用请求的URI作为键，不做任何修改
    std::string key = uri + ":" + std::to_string(req->method);
    ESP_LOGI(TAG, "Looking for API handler with key: %s", key.c_str());
    
    auto it = api_handlers_.find(key);
    
    // 如果找不到，尝试检查是否存在路径问题
    if (it == api_handlers_.end()) {
        // 检查路径是否有/api前缀，如果没有尝试添加
        if (normalized_uri.substr(0, 5) != "/api/") {
            if (normalized_uri.substr(0, 4) == "/api") {
                normalized_uri = "/api/" + normalized_uri.substr(4);
            } else {
                normalized_uri = "/api/" + (normalized_uri.front() == '/' ? normalized_uri.substr(1) : normalized_uri);
            }
            key = normalized_uri + ":" + std::to_string(req->method);
            ESP_LOGI(TAG, "Trying with normalized URI: %s", normalized_uri.c_str());
            it = api_handlers_.find(key);
        }
        
        // 如果还是找不到，尝试处理末尾的斜杠问题
        if (it == api_handlers_.end()) {
            std::string alt_uri = normalized_uri;
            if (alt_uri.back() == '/') {
                alt_uri.pop_back();
            } else {
                alt_uri += "/";
            }
            std::string alt_key = alt_uri + ":" + std::to_string(req->method);
            ESP_LOGI(TAG, "Trying alternative key: %s", alt_key.c_str());
            it = api_handlers_.find(alt_key);
        }
        
        // 记录所有已注册的 API 处理程序以便调试
        if (it == api_handlers_.end()) {
            ESP_LOGW(TAG, "API handler still not found, registered handlers:");
            for (const auto& h : api_handlers_) {
                ESP_LOGW(TAG, "  - %s", h.first.c_str());
            }
            
            // 再尝试一次最后的修复 - 查找不带方法的匹配项
            for (const auto& h : api_handlers_) {
                std::string handler_path = h.first.substr(0, h.first.find_last_of(":"));
                if (handler_path == normalized_uri || handler_path == uri) {
                    ESP_LOGI(TAG, "Found handler with matching path but different method: %s", h.first.c_str());
                }
            }
        }
    }
    
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

// 车辆控制WebSocket处理程序
void Web::InitVehicleWebSocketHandlers() {
    ESP_LOGI(TAG, "Initializing vehicle WebSocket handlers");
    
    // 初始化随机数种子
    srand(esp_timer_get_time());
    
    // 小车实时控制 WebSocket 处理程序
    RegisterWebSocketHandler("/ws/motor", [](int client_index, const std::string& message) {
        ESP_LOGI(TAG, "Motor control WebSocket message from client %d: %s", client_index, message.c_str());
        
        // 解析JSON控制命令
        cJSON* root = cJSON_Parse(message.c_str());
        if (!root) {
            ESP_LOGW(TAG, "Failed to parse motor control message as JSON");
            return;
        }
        
        // 提取控制参数
        cJSON* cmd = cJSON_GetObjectItem(root, "cmd");
        if (cmd && cJSON_IsString(cmd)) {
            std::string cmd_str = cmd->valuestring;
            
            if (cmd_str == "move") {
                // 移动命令处理
                int speed = 0, direction = 0;
                cJSON* speed_obj = cJSON_GetObjectItem(root, "speed");
                cJSON* dir_obj = cJSON_GetObjectItem(root, "direction");
                
                if (speed_obj && cJSON_IsNumber(speed_obj)) {
                    speed = speed_obj->valueint;
                }
                if (dir_obj && cJSON_IsNumber(dir_obj)) {
                    direction = dir_obj->valueint;
                }
                
                ESP_LOGI(TAG, "Motor move command: speed=%d, direction=%d", speed, direction);
                
                // Note: Motor control is now handled through hardware manager API
                // Use /api/motors/control endpoint for motor control
            } 
            else if (cmd_str == "stop") {
                ESP_LOGI(TAG, "Motor stop command");
                // Note: Motor control is now handled through hardware manager API
                // Use /api/motors/control endpoint for motor control
            }
        }
        
        cJSON_Delete(root);
    });
    
    // 舵机实时控制 WebSocket 处理程序
    RegisterWebSocketHandler("/ws/servo", [](int client_index, const std::string& message) {
        ESP_LOGI(TAG, "Servo control WebSocket message from client %d: %s", client_index, message.c_str());
        
        // 解析JSON控制命令
        cJSON* root = cJSON_Parse(message.c_str());
        if (!root) {
            ESP_LOGW(TAG, "Failed to parse servo control message as JSON");
            return;
        }
        
        // 提取舵机控制参数
        cJSON* cmd = cJSON_GetObjectItem(root, "cmd");
        if (cmd && cJSON_IsString(cmd)) {
            std::string cmd_str = cmd->valuestring;
            
            if (cmd_str == "set") {
                // 设置舵机角度
                int servo_id = 0, angle = 0, speed = 100;
                cJSON* id_obj = cJSON_GetObjectItem(root, "id");
                cJSON* angle_obj = cJSON_GetObjectItem(root, "angle");
                cJSON* speed_obj = cJSON_GetObjectItem(root, "speed");
                
                if (id_obj && cJSON_IsNumber(id_obj)) {
                    servo_id = id_obj->valueint;
                }
                if (angle_obj && cJSON_IsNumber(angle_obj)) {
                    angle = angle_obj->valueint;
                }
                if (speed_obj && cJSON_IsNumber(speed_obj)) {
                    speed = speed_obj->valueint;
                }
                
                ESP_LOGI(TAG, "Servo command: id=%d, angle=%d, speed=%d", servo_id, angle, speed);
                
                // Note: Servo control is now handled through hardware manager API
                // Use /api/servos/control endpoint for servo control
            }
        }
        
        cJSON_Delete(root);
    });
    
    // 车辆一般状态 WebSocket 处理程序 - 可用于发送车辆状态更新
    RegisterWebSocketHandler("/ws/vehicle", [](int client_index, const std::string& message) {
        ESP_LOGI(TAG, "Vehicle status WebSocket message from client %d: %s", client_index, message.c_str());
        
        // 解析JSON命令
        cJSON* root = cJSON_Parse(message.c_str());
        if (!root) {
            ESP_LOGW(TAG, "Failed to parse vehicle status message as JSON");
            return;
        }
        
        // 提取命令
        cJSON* cmd = cJSON_GetObjectItem(root, "cmd");
        if (cmd && cJSON_IsString(cmd)) {
            std::string cmd_str = cmd->valuestring;
            
            if (cmd_str == "getStatus" || cmd_str == "register") {
                // 返回车辆状态
                ESP_LOGI(TAG, "Vehicle status request received");
                
                // 获取实时数据
                static int battery_level = 75;
                static int speed = 0;
                static int front_distance = 25;
                static int rear_distance = 40;
                
                // 创建状态响应JSON
                cJSON* status = cJSON_CreateObject();
                cJSON_AddStringToObject(status, "type", "vehicle_status");
                cJSON_AddStringToObject(status, "status", "ok");
                cJSON_AddBoolToObject(status, "connected", true);
                cJSON_AddNumberToObject(status, "batteryLevel", battery_level);
                cJSON_AddNumberToObject(status, "speed", speed);
                
                // 添加障碍物传感器数据
                cJSON* distances = cJSON_CreateObject();
                cJSON_AddNumberToObject(distances, "front", front_distance);
                cJSON_AddNumberToObject(distances, "rear", rear_distance);
                cJSON_AddItemToObject(status, "distances", distances);
                
                // 添加更多状态信息
                cJSON_AddStringToObject(status, "mode", "Manual Control");
                cJSON_AddStringToObject(status, "signal", "Excellent");
                cJSON_AddStringToObject(status, "readyState", "Ready");
                
                // 发送状态响应
                char* status_str = cJSON_PrintUnformatted(status);
                if (status_str) {
                    if (Web::current_instance_) {
                        Web::current_instance_->SendWebSocketMessage(client_index, status_str);
                    }
                    free(status_str);
                }
                
                cJSON_Delete(status);
                
                // 模拟数据变化
                battery_level = (battery_level + 1) % 100;
                speed = (speed + 3) % 40;
                front_distance = 15 + (rand() % 40);
                rear_distance = 20 + (rand() % 40);
            }
        }
        
        cJSON_Delete(root);
    });
    
    ESP_LOGI(TAG, "Vehicle WebSocket handlers initialized");
    
    // 启动定期状态更新任务
    esp_timer_handle_t status_timer;
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            if (!Web::current_instance_ || !Web::current_instance_->IsRunning()) {
                return;
            }
            
            // 创建状态消息
            cJSON* status = cJSON_CreateObject();
            cJSON_AddStringToObject(status, "type", "vehicle_status");
            cJSON_AddStringToObject(status, "status", "ok");
            cJSON_AddBoolToObject(status, "connected", true);
            
            // 从电池组件获取数据
            Thing* battery_thing = ThingManager::GetInstance().FindThingByName("Battery");
            if (battery_thing) {
                float percentage = SafeGetValue(battery_thing, "percentage");
                if (!std::isnan(percentage)) {
                    cJSON_AddNumberToObject(status, "batteryLevel", (int)percentage);
                }
                
                float voltage = SafeGetValue(battery_thing, "voltage");
                if (!std::isnan(voltage)) {
                    cJSON_AddNumberToObject(status, "batteryVoltage", voltage);
                }
            }
            
            // 从电机组件获取速度信息
            Thing* motor_thing = ThingManager::GetInstance().FindThingByName("Motor");
            if (motor_thing) {
                float speed = SafeGetValue(motor_thing, "speed");
                if (!std::isnan(speed)) {
                    cJSON_AddNumberToObject(status, "speed", speed);
                    // 根据速度设置状态
                    cJSON_AddStringToObject(status, "readyState", speed > 0.5f ? "Moving" : "Ready");
                } else {
                    cJSON_AddStringToObject(status, "readyState", "Ready");
                }
            } else {
                cJSON_AddStringToObject(status, "readyState", "Ready");
            }
            
            // 从超声波传感器获取距离数据
            Thing* us_thing = ThingManager::GetInstance().FindThingByName("UltrasonicSensor");
            if (us_thing) {
                float front_distance = SafeGetValue(us_thing, "front_distance");
                float rear_distance = SafeGetValue(us_thing, "rear_distance");
                
                if (!std::isnan(front_distance) || !std::isnan(rear_distance)) {
                    cJSON* distances = cJSON_CreateObject();
                    
                    if (!std::isnan(front_distance)) {
                        cJSON_AddNumberToObject(distances, "front", front_distance);
                    }
                    
                    if (!std::isnan(rear_distance)) {
                        cJSON_AddNumberToObject(distances, "rear", rear_distance);
                    }
                    
                    cJSON_AddItemToObject(status, "distances", distances);
                }
            }
            
            // 添加基本信息
            cJSON_AddStringToObject(status, "mode", "Manual Control");
            cJSON_AddStringToObject(status, "signal", "Excellent");
            
            // 转换为字符串并广播
            char* status_str = cJSON_PrintUnformatted(status);
            if (status_str) {
                Web::current_instance_->BroadcastWebSocketMessage(status_str);
                free(status_str);
            }
            
            cJSON_Delete(status);
        },
        .arg = nullptr,
        .name = "vehicle_status"
    };
    
    // 创建并启动计时器，每2秒发送一次状态更新
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &status_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(status_timer, 2000000)); // 2秒 = 2000000微秒
}

// 传感器数据WebSocket处理程序
void Web::InitSensorHandlers() {
    ESP_LOGI(TAG, "Initializing sensor WebSocket handlers");
    
    // 设置定期广播传感器数据的计时器
    esp_timer_handle_t sensor_timer;
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            if (!Web::current_instance_ || !Web::current_instance_->IsRunning()) {
                return;
            }
            
            // 创建传感器数据JSON
            cJSON* sensor_data = cJSON_CreateObject();
            cJSON_AddStringToObject(sensor_data, "type", "sensor_data");
            
            // 从IMU传感器读取数据
            Thing* imu_thing = ThingManager::GetInstance().FindThingByName("IMU");
            if (imu_thing) {
                // 获取IMU数据
                float accel_x = SafeGetValue(imu_thing, "accel_x");
                if (!std::isnan(accel_x)) cJSON_AddNumberToObject(sensor_data, "accelX", accel_x);
                
                float accel_y = SafeGetValue(imu_thing, "accel_y");
                if (!std::isnan(accel_y)) cJSON_AddNumberToObject(sensor_data, "accelY", accel_y);
                
                float accel_z = SafeGetValue(imu_thing, "accel_z");
                if (!std::isnan(accel_z)) cJSON_AddNumberToObject(sensor_data, "accelZ", accel_z);
                
                float gyro_x = SafeGetValue(imu_thing, "gyro_x");
                if (!std::isnan(gyro_x)) cJSON_AddNumberToObject(sensor_data, "gyroX", gyro_x);
                
                float gyro_y = SafeGetValue(imu_thing, "gyro_y");
                if (!std::isnan(gyro_y)) cJSON_AddNumberToObject(sensor_data, "gyroY", gyro_y);
                
                float gyro_z = SafeGetValue(imu_thing, "gyro_z");
                if (!std::isnan(gyro_z)) cJSON_AddNumberToObject(sensor_data, "gyroZ", gyro_z);
                
                float mag_x = SafeGetValue(imu_thing, "mag_x");
                if (!std::isnan(mag_x)) cJSON_AddNumberToObject(sensor_data, "magX", mag_x);
                
                float mag_y = SafeGetValue(imu_thing, "mag_y");
                if (!std::isnan(mag_y)) cJSON_AddNumberToObject(sensor_data, "magY", mag_y);
                
                float mag_z = SafeGetValue(imu_thing, "mag_z");
                if (!std::isnan(mag_z)) cJSON_AddNumberToObject(sensor_data, "magZ", mag_z);
                
                float temperature = SafeGetValue(imu_thing, "temperature");
                if (!std::isnan(temperature)) cJSON_AddNumberToObject(sensor_data, "temperature", temperature);
                
                float pressure = SafeGetValue(imu_thing, "pressure");
                if (!std::isnan(pressure)) cJSON_AddNumberToObject(sensor_data, "pressure", pressure);
                
                float altitude = SafeGetValue(imu_thing, "altitude");
                if (!std::isnan(altitude)) cJSON_AddNumberToObject(sensor_data, "altitude", altitude);
            }
            
            // 从超声波传感器读取数据
            Thing* us_thing = ThingManager::GetInstance().FindThingByName("UltrasonicSensor");
            if (us_thing) {
                // 获取超声波数据
                float front_distance = SafeGetValue(us_thing, "front_distance");
                float rear_distance = SafeGetValue(us_thing, "rear_distance");
                
                if (!std::isnan(front_distance) || !std::isnan(rear_distance)) {
                    // 如果至少有一个距离值可用，创建距离对象
                    cJSON* distances = cJSON_CreateObject();
                    
                    if (!std::isnan(front_distance)) {
                        cJSON_AddNumberToObject(distances, "front", front_distance);
                    }
                    
                    if (!std::isnan(rear_distance)) {
                        cJSON_AddNumberToObject(distances, "rear", rear_distance);
                    }
                    
                    cJSON_AddItemToObject(sensor_data, "distances", distances);
                    
                    // 计算平均距离（如果两个传感器都有值）
                    if (!std::isnan(front_distance) && !std::isnan(rear_distance)) {
                        cJSON_AddNumberToObject(sensor_data, "distance", (front_distance + rear_distance) / 2);
                    } else if (!std::isnan(front_distance)) {
                        cJSON_AddNumberToObject(sensor_data, "distance", front_distance);
                    } else if (!std::isnan(rear_distance)) {
                        cJSON_AddNumberToObject(sensor_data, "distance", rear_distance);
                    }
                    
                    // 添加安全距离（固定值）
                    cJSON_AddNumberToObject(sensor_data, "safeDistance", 20.0f);
                    
                    // 障碍物检测 - 假设超声波Thing有这些布尔值属性
                    // 如果没有，可以根据距离计算
                    bool front_obstacle = (front_distance < 20.0f) && !std::isnan(front_distance);
                    bool rear_obstacle = (rear_distance < 20.0f) && !std::isnan(rear_distance);
                    
                    cJSON_AddBoolToObject(sensor_data, "frontObstacle", front_obstacle);
                    cJSON_AddBoolToObject(sensor_data, "rearObstacle", rear_obstacle);
                }
            }
            
            // 从光线传感器读取数据
            Thing* light_thing = ThingManager::GetInstance().FindThingByName("Light");
            if (light_thing) {
                float light = SafeGetValue(light_thing, "light");
                if (!std::isnan(light)) {
                    cJSON_AddNumberToObject(sensor_data, "light", light);
                }
            }
            
            // 转换为字符串并广播
            char* sensor_str = cJSON_PrintUnformatted(sensor_data);
            if (sensor_str) {
                Web::current_instance_->BroadcastWebSocketMessage(sensor_str);
                free(sensor_str);
            }
            
            cJSON_Delete(sensor_data);
        },
        .arg = nullptr,
        .name = "sensor_data"
    };
    
    // 创建并启动计时器，每秒发送一次传感器数据
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &sensor_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(sensor_timer, 1000000)); // 1秒 = 1000000微秒
    
    ESP_LOGI(TAG, "Sensor WebSocket handlers initialized");
}