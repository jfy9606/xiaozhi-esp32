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
    
    // Register default handlers
    InitDefaultHandlers();
    
    // Initialize API handlers
    InitApiHandlers();
    
    running_ = true;
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
    if (!running_) {
        ESP_LOGW(TAG, "Web component not running, handler registration delayed");
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
    
    // 创建handler key检查是否已注册
    std::string key = std::string(uri) + ":" + std::to_string(static_cast<int>(method));
    if (http_handlers_.find(key) != http_handlers_.end()) {
        ESP_LOGW(TAG, "Handler for %s [%d] already registered, skipping", uri.c_str(), static_cast<int>(method));
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
            ESP_LOGW(TAG, "Handler for %s [%d] already registered by httpd, storing handler anyway", 
                     uri.c_str(), static_cast<int>(method));
            http_handlers_[key] = handler;
            return;
        }
        
        ESP_LOGE(TAG, "Failed to register handler for URI %s: %s", uri.c_str(), esp_err_to_name(ret));
        return;
    }
    
    // Store handler in our map
    http_handlers_[key] = handler;
    
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
    
    // Get embedded file data
    const uint8_t* file_start = nullptr;
    const uint8_t* file_end = nullptr;
    
    // First check HTML files
    if (filename == "/index.html") {
        extern const uint8_t index_html_start[] asm("_binary_index_html_start");
        extern const uint8_t index_html_end[] asm("_binary_index_html_end");
        file_start = index_html_start;
        file_end = index_html_end;
    } else {
        // For other static files, need to map to embedded binary data
        // This would require more detailed handling based on your embedded files
        ESP_LOGW(TAG, "Static file not found: %s", filename.c_str());
        return httpd_resp_send_404(req);
    }
    
    if (!file_start || !file_end || file_start >= file_end) {
        ESP_LOGW(TAG, "Invalid file data for: %s", filename.c_str());
        return httpd_resp_send_404(req);
    }
    
    // Set content type
    const char* content_type = "text/html";
    if (filename.find(".css") != std::string::npos) {
        content_type = "text/css";
    } else if (filename.find(".js") != std::string::npos) {
        content_type = "application/javascript";
    } else if (filename.find(".png") != std::string::npos) {
        content_type = "image/png";
    } else if (filename.find(".jpg") != std::string::npos || filename.find(".jpeg") != std::string::npos) {
        content_type = "image/jpeg";
    }
    
    httpd_resp_set_type(req, content_type);
    
    // Send file data
    return httpd_resp_send(req, (const char*)file_start, file_end - file_start);
}

esp_err_t Web::HandleCssFile(httpd_req_t* req) {
    // Extract CSS filename
    std::string uri(req->uri);
    std::string filename = uri.substr(uri.find_last_of("/") + 1);
    
    // Handle CSS files
    if (filename == "bootstrap.min.css") {
        extern const uint8_t bootstrap_min_css_start[] asm("_binary_bootstrap_min_css_start");
        extern const uint8_t bootstrap_min_css_end[] asm("_binary_bootstrap_min_css_end");
        
        httpd_resp_set_type(req, "text/css");
        return httpd_resp_send(req, (const char*)bootstrap_min_css_start, bootstrap_min_css_end - bootstrap_min_css_start);
    }
    // Add more CSS files as needed
    
    // CSS file not found
    ESP_LOGW(TAG, "CSS file not found: %s", filename.c_str());
    return httpd_resp_send_404(req);
}

esp_err_t Web::HandleJsFile(httpd_req_t* req) {
    // Extract JS filename
    std::string uri(req->uri);
    std::string filename = uri.substr(uri.find_last_of("/") + 1);
    
    // Handle JS files
    if (filename == "common.js") {
        extern const uint8_t common_js_start[] asm("_binary_common_js_start");
        extern const uint8_t common_js_end[] asm("_binary_common_js_end");
        
        httpd_resp_set_type(req, "application/javascript");
        return httpd_resp_send(req, (const char*)common_js_start, common_js_end - common_js_start);
    }
    else if (filename == "bootstrap.bundle.min.js") {
        extern const uint8_t bootstrap_bundle_min_js_start[] asm("_binary_bootstrap_bundle_min_js_start");
        extern const uint8_t bootstrap_bundle_min_js_end[] asm("_binary_bootstrap_bundle_min_js_end");
        
        httpd_resp_set_type(req, "application/javascript");
        return httpd_resp_send(req, (const char*)bootstrap_bundle_min_js_start, bootstrap_bundle_min_js_end - bootstrap_bundle_min_js_start);
    }
    // Add more JS files as needed
    
    // JS file not found
    ESP_LOGW(TAG, "JS file not found: %s", filename.c_str());
    return httpd_resp_send_404(req);
}

// Content generation
std::string Web::GetHtml(const std::string& page, const char* accept_language) {
    // Map page name to HTML content
    if (page == "index" || page.empty()) {
        return "<!DOCTYPE html>\n"
               "<html>\n"
               "<head>\n"
               "  <meta charset=\"UTF-8\">\n"
               "  <title>小智机器人</title>\n"
               "  <meta name='viewport' content='width=device-width, initial-scale=1'>\n"
               "  <style>\n"
               "    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }\n"
               "    h1 { color: #333; text-align: center; }\n"
               "    .container { max-width: 800px; margin: 0 auto; }\n"
               "    .card { border: 1px solid #ddd; border-radius: 8px; padding: 15px; margin-bottom: 20px; }\n"
               "    .card-title { font-size: 20px; margin-top: 0; }\n"
               "    .btn { display: inline-block; background: #2196F3; color: white; padding: 10px 15px; text-decoration: none; border-radius: 4px; margin-top: 10px; }\n"
               "    .row { display: flex; flex-wrap: wrap; margin: 0 -10px; }\n"
               "    .col { flex: 1; padding: 0 10px; min-width: 250px; margin-bottom: 20px; }\n"
               "  </style>\n"
               "</head>\n"
               "<body>\n"
               "  <div class='container'>\n"
               "    <h1>小智机器人控制系统</h1>\n"
               "    <div class='row'>\n"
               "      <div class='col'>\n"
               "        <div class='card'>\n"
               "          <h3 class='card-title'>车辆控制</h3>\n"
               "          <p>控制小智机器人的移动和转向。</p>\n"
               "          <a href='/vehicle' class='btn'>车辆控制</a>\n"
               "        </div>\n"
               "      </div>\n"
               "      <div class='col'>\n"
               "        <div class='card'>\n"
               "          <h3 class='card-title'>视觉</h3>\n"
               "          <p>相机流和图像处理。</p>\n"
               "          <a href='/vision' class='btn'>视觉控制</a>\n"
               "          <a href='/cam' class='btn'>相机预览</a>\n"
               "        </div>\n"
               "      </div>\n"
               "    </div>\n"
               "    <div class='row'>\n"
               "      <div class='col'>\n"
               "        <div class='card'>\n"
               "          <h3 class='card-title'>AI对话</h3>\n"
               "          <p>与机器人AI进行对话。</p>\n"
               "          <a href='/ai' class='btn'>AI对话</a>\n"
               "        </div>\n"
               "      </div>\n"
               "      <div class='col'>\n"
               "        <div class='card'>\n"
               "          <h3 class='card-title'>位置服务</h3>\n"
               "          <p>GPS和导航功能。</p>\n"
               "          <a href='/location' class='btn'>位置服务</a>\n"
               "        </div>\n"
               "      </div>\n"
               "    </div>\n"
               "    <div class='card'>\n"
               "      <h3 class='card-title'>系统信息</h3>\n"
               "      <p>当前Web服务器运行在端口: " + std::to_string(port_) + "</p>\n"
               "      <a href='/api/system/info' class='btn'>查看系统信息</a>\n"
               "    </div>\n"
               "  </div>\n"
               "  <script>\n"
               "    // 简单的页面加载脚本\n"
               "    document.addEventListener('DOMContentLoaded', function() {\n"
               "      console.log('Page loaded');\n"
               "    });\n"
               "  </script>\n"
               "</body>\n"
               "</html>";
    } else if (page == "vehicle" || page == "car") {
        return "<!DOCTYPE html>\n"
               "<html>\n"
               "<head>\n"
               "  <meta charset=\"UTF-8\">\n"
               "  <title>车辆控制</title>\n"
               "  <meta name='viewport' content='width=device-width, initial-scale=1'>\n"
               "  <style>\n"
               "    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }\n"
               "    h1 { color: #333; text-align: center; }\n"
               "    .container { max-width: 800px; margin: 0 auto; }\n"
               "    .control-pad { display: flex; flex-direction: column; align-items: center; margin: 20px 0; }\n"
               "    .btn-row { display: flex; margin: 5px 0; }\n"
               "    .control-btn { width: 80px; height: 80px; margin: 5px; font-size: 20px; background: #2196F3; color: white; border: none; border-radius: 8px; cursor: pointer; }\n"
               "    .control-btn:active { background: #0b7dda; }\n"
               "    .home-link { display: block; margin-top: 20px; text-align: center; }\n"
               "  </style>\n"
               "</head>\n"
               "<body>\n"
               "  <div class='container'>\n"
               "    <h1>车辆控制</h1>\n"
               "    <div class='control-pad'>\n"
               "      <div class='btn-row'>\n"
               "        <button class='control-btn' id='forward'>前进</button>\n"
               "      </div>\n"
               "      <div class='btn-row'>\n"
               "        <button class='control-btn' id='left'>左转</button>\n"
               "        <button class='control-btn' id='stop'>停止</button>\n"
               "        <button class='control-btn' id='right'>右转</button>\n"
               "      </div>\n"
               "      <div class='btn-row'>\n"
               "        <button class='control-btn' id='backward'>后退</button>\n"
               "      </div>\n"
               "    </div>\n"
               "    <a href='/' class='home-link'>返回首页</a>\n"
               "  </div>\n"
               "  <script>\n"
               "    document.addEventListener('DOMContentLoaded', function() {\n"
               "      // 控制按钮事件\n"
               "      document.getElementById('forward').addEventListener('click', function() {\n"
               "        sendCommand('forward');\n"
               "      });\n"
               "      document.getElementById('backward').addEventListener('click', function() {\n"
               "        sendCommand('backward');\n"
               "      });\n"
               "      document.getElementById('left').addEventListener('click', function() {\n"
               "        sendCommand('left');\n"
               "      });\n"
               "      document.getElementById('right').addEventListener('click', function() {\n"
               "        sendCommand('right');\n"
               "      });\n"
               "      \n"
               "      function sendCommand(cmd) {\n"
               "        fetch('/api/vehicle/control', {\n"
               "          method: 'POST',\n"
               "          headers: {\n"
               "            'Content-Type': 'application/json',\n"
               "          },\n"
               "          body: JSON.stringify({ command: cmd }),\n"
               "        })\n"
               "        .then(response => response.json())\n"
               "        .then(data => console.log('Success:', data))\n"
               "        .catch(error => console.error('Error:', error));\n"
               "      }\n"
               "    });\n"
               "  </script>\n"
               "</body>\n"
               "</html>";
    } else if (page == "vision" || page == "cam") {
        return "<!DOCTYPE html>\n"
               "<html>\n"
               "<head>\n"
               "  <meta charset=\"UTF-8\">\n"
               "  <title>视觉控制</title>\n"
               "  <meta name='viewport' content='width=device-width, initial-scale=1'>\n"
               "  <style>\n"
               "    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }\n"
               "    h1 { color: #333; text-align: center; }\n"
               "    .container { max-width: 800px; margin: 0 auto; }\n"
               "    .video-container { width: 100%; max-width: 640px; margin: 20px auto; text-align: center; }\n"
               "    .camera-feed { width: 100%; border: 1px solid #ddd; }\n"
               "    .control-panel { margin-top: 20px; }\n"
               "    .control-btn { padding: 10px 15px; margin: 5px; background: #2196F3; color: white; border: none; border-radius: 4px; cursor: pointer; }\n"
               "    .home-link { display: block; margin-top: 20px; text-align: center; }\n"
               "  </style>\n"
               "</head>\n"
               "<body>\n"
               "  <div class='container'>\n"
               "    <h1>视觉控制</h1>\n"
               "    <div class='video-container'>\n"
               "      <img id='camera-feed' class='camera-feed' src='/cam/stream' alt='相机未连接'>\n"
               "    </div>\n"
               "    <div class='control-panel'>\n"
               "      <button class='control-btn' id='start-stream'>启动视频流</button>\n"
               "      <button class='control-btn' id='stop-stream'>停止视频流</button>\n"
               "      <button class='control-btn' id='capture'>拍照</button>\n"
               "    </div>\n"
               "    <a href='/' class='home-link'>返回首页</a>\n"
               "  </div>\n"
               "  <script>\n"
               "    document.addEventListener('DOMContentLoaded', function() {\n"
               "      document.getElementById('start-stream').addEventListener('click', function() {\n"
               "        document.getElementById('camera-feed').src = '/cam/stream';\n"
               "      });\n"
               "      \n"
               "      document.getElementById('stop-stream').addEventListener('click', function() {\n"
               "        document.getElementById('camera-feed').src = '';\n"
               "      });\n"
               "      \n"
               "      document.getElementById('capture').addEventListener('click', function() {\n"
               "        fetch('/api/vision/capture', { method: 'POST' })\n"
               "          .then(response => response.json())\n"
               "          .then(data => {\n"
               "            if (data.success) {\n"
               "              alert('照片已保存');\n"
               "            }\n"
               "          })\n"
               "          .catch(error => console.error('Error:', error));\n"
               "      });\n"
               "    });\n"
               "  </script>\n"
               "</body>\n"
               "</html>";
    }
    
    // For other pages, return a generic template
    return "<!DOCTYPE html>\n"
           "<html>\n"
           "<head>\n"
           "  <meta charset=\"UTF-8\">\n"
           "  <title>页面不存在</title>\n"
           "  <style>\n"
           "    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; text-align: center; }\n"
           "    h1 { color: #d9534f; }\n"
           "    .container { max-width: 600px; margin: 50px auto; }\n"
           "    .home-link { display: block; margin-top: 30px; }\n"
           "  </style>\n"
           "</head>\n"
           "<body>\n"
           "  <div class='container'>\n"
           "    <h1>404 - 页面不存在</h1>\n"
           "    <p>您请求的页面 \"" + page + "\" 不存在。</p>\n"
           "    <a href='/' class='home-link'>返回首页</a>\n"
           "  </div>\n"
           "</body>\n"
           "</html>";
}

// Internal handlers
void Web::InitDefaultHandlers() {
    // ROOT: 确保根路径处理程序是第一个注册的
    RegisterHandler(HttpMethod::HTTP_GET, "/", [this](httpd_req_t* req) -> esp_err_t {
        ESP_LOGI(TAG, "Handling ROOT request for: %s", req->uri);
        std::string html = GetHtml("index");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req, html.c_str());
        return ESP_OK;
    });
    
    // 添加对favicon.ico的处理
    RegisterHandler(HttpMethod::HTTP_GET, "/favicon.ico", [](httpd_req_t* req) -> esp_err_t {
        // 如果没有图标，返回204 No Content
        httpd_resp_set_status(req, "204 No Content");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    });
    
    // 添加用于调试的主路由
    RegisterHandler(HttpMethod::HTTP_GET, "/index.html", [this](httpd_req_t* req) -> esp_err_t {
        return RootHandler(req);
    });
    
    // 为常用URL路径添加处理器
    RegisterHandler(HttpMethod::HTTP_GET, "/cam", VisionHandler);
    RegisterHandler(HttpMethod::HTTP_GET, "/motor", CarHandler);
    RegisterHandler(HttpMethod::HTTP_GET, "/vehicle", CarHandler);
    
    // CSS files
    RegisterHandler(HttpMethod::HTTP_GET, "/css/*", [this](httpd_req_t* req) -> esp_err_t {
        return HandleCssFile(req);
    });
    
    // JS files
    RegisterHandler(HttpMethod::HTTP_GET, "/js/*", [this](httpd_req_t* req) -> esp_err_t {
        return HandleJsFile(req);
    });
    
    // WebSocket handler
    RegisterHandler(HttpMethod::HTTP_GET, "/ws", [](httpd_req_t* req) -> esp_err_t {
        return WebSocketHandler(req);
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
    
    ESP_LOGI(TAG, "Request received for URI: %s, method: %d", req->uri, req->method);
    Web* web = static_cast<Web*>(req->user_ctx);
    
    // Find handler for this URI and method
    std::string key = std::string(req->uri) + ":" + std::to_string(req->method);
    auto it = web->http_handlers_.find(key);
    
    if (it != web->http_handlers_.end()) {
        ESP_LOGI(TAG, "Handler found for URI: %s", req->uri);
        return it->second(req);
    }
    
    // Try wildcard handlers
    for (const auto& handler_pair : web->http_handlers_) {
        // Extract URI part from key (uri:method format)
        std::string handler_uri = handler_pair.first.substr(0, handler_pair.first.find_last_of(":"));
        
        // Check if this is a wildcard handler that matches
        if (handler_uri.back() == '*') {
            std::string prefix = handler_uri.substr(0, handler_uri.size() - 1);
            if (strncmp(req->uri, prefix.c_str(), prefix.length()) == 0) {
                int method_code = std::stoi(handler_pair.first.substr(handler_pair.first.find_last_of(":") + 1));
                if (method_code == req->method) {
                    ESP_LOGI(TAG, "Wildcard handler found for URI: %s, prefix: %s", req->uri, prefix.c_str());
                    return handler_pair.second(req);
                }
            }
        }
    }
    
    // Handle special case for root path
    if (strcmp(req->uri, "/") == 0 && req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Special handling for root path: %s", req->uri);
        return web->RootHandler(req);
    }
    
    // No handler found
    ESP_LOGW(TAG, "No handler for %s [method %d], URI: %s", req->uri, req->method, req->uri);
    return httpd_resp_send_404(req);
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
    if (!current_instance_) {
        ESP_LOGE(TAG, "No Web instance for ROOT handler");
        return httpd_resp_send_500(req);
    }
    
    ESP_LOGI(TAG, "Serving ROOT path via RootHandler");
    
    std::string html = current_instance_->GetHtml("index");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "identity");
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