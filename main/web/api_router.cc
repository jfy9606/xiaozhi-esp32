/**
 * @file api_router.cc
 * @brief API路由器实现
 */

#include "api_definitions.h"
#include <esp_log.h>
#include <string.h>

static const char* TAG = "ApiRouter";

ApiRouter::ApiRouter() : web_server_(nullptr) {
    ESP_LOGI(TAG, "Initializing API Router");
}

ApiRouter::~ApiRouter() {
    // 清理资源
    http_handlers_.clear();
    ws_handlers_.clear();
}

void ApiRouter::Initialize(WebServer* web_server) {
    if (!web_server) {
        ESP_LOGE(TAG, "Web server is null, cannot initialize API router");
        return;
    }
    
    web_server_ = web_server;
    
    // 注册HTTP API处理器
    web_server_->RegisterHttpHandler(HTTP_API_PREFIX "/*", HTTP_GET, HttpApiHandler);
    web_server_->RegisterHttpHandler(HTTP_API_PREFIX "/*", HTTP_POST, HttpApiHandler);
    web_server_->RegisterHttpHandler(HTTP_API_PREFIX "/*", HTTP_PUT, HttpApiHandler);
    web_server_->RegisterHttpHandler(HTTP_API_PREFIX "/*", HTTP_DELETE, HttpApiHandler);
    
    // 注册WebSocket处理器
    web_server_->RegisterWebSocketHandler(WS_MSG_TYPE_SERVO, 
        [this](int client_id, const PSRAMString& message, const PSRAMString& type) {
            WsApiHandler(client_id, message, type);
        });
    
    web_server_->RegisterWebSocketHandler(WS_MSG_TYPE_SENSOR, 
        [this](int client_id, const PSRAMString& message, const PSRAMString& type) {
            WsApiHandler(client_id, message, type);
        });
    
    web_server_->RegisterWebSocketHandler(WS_MSG_TYPE_AUDIO, 
        [this](int client_id, const PSRAMString& message, const PSRAMString& type) {
            WsApiHandler(client_id, message, type);
        });
    
    // 注册WebSocket API端点
    web_server_->RegisterHttpHandler(WS_API_SERVO, HTTP_GET, [](httpd_req_t* req) {
        return WebServer::WebSocketHandler(req);
    });
    
    web_server_->RegisterHttpHandler(WS_API_SENSOR, HTTP_GET, [](httpd_req_t* req) {
        return WebServer::WebSocketHandler(req);
    });
    
    web_server_->RegisterHttpHandler(WS_API_AUDIO, HTTP_GET, [](httpd_req_t* req) {
        return WebServer::WebSocketHandler(req);
    });
    
    ESP_LOGI(TAG, "API Router initialized successfully");
}

void ApiRouter::RegisterHttpApi(const std::string& path, httpd_method_t method, HttpApiHandler handler) {
    std::string full_path = path;
    if (path.find(HTTP_API_PREFIX) != 0) {
        // 自动添加前缀
        full_path = HTTP_API_PREFIX + path;
    }
    
    http_handlers_[full_path] = std::make_pair(method, handler);
    ESP_LOGI(TAG, "Registered HTTP API handler for %s", full_path.c_str());
}

void ApiRouter::RegisterWsApi(const std::string& type, WsApiHandler handler) {
    ws_handlers_[type] = handler;
    ESP_LOGI(TAG, "Registered WebSocket API handler for type '%s'", type.c_str());
}

esp_err_t ApiRouter::HttpApiHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing HTTP API request: %s", req->uri);
    
    ApiRouter* router = ApiRouter::GetInstance();
    if (!router) {
        ESP_LOGE(TAG, "API Router instance not available");
        return ESP_FAIL;
    }
    
    // 查找处理器
    auto it = router->http_handlers_.find(req->uri);
    if (it == router->http_handlers_.end()) {
        // 尝试匹配通配符路径
        std::string uri(req->uri);
        std::string best_match;
        
        for (const auto& handler : router->http_handlers_) {
            const std::string& pattern = handler.first;
            if (pattern.back() == '*' && uri.find(pattern.substr(0, pattern.length() - 1)) == 0) {
                if (best_match.empty() || pattern.length() > best_match.length()) {
                    best_match = pattern;
                }
            }
        }
        
        if (!best_match.empty()) {
            it = router->http_handlers_.find(best_match);
        }
    }
    
    if (it == router->http_handlers_.end()) {
        ESP_LOGW(TAG, "No handler found for %s", req->uri);
        ApiResponse response = CreateErrorResponse(ApiStatusCode::NOT_FOUND, "API endpoint not found");
        return SendApiResponse(req, response);
    }
    
    // 检查HTTP方法
    if (it->second.first != req->method) {
        ESP_LOGW(TAG, "Method not allowed: %d for %s", req->method, req->uri);
        ApiResponse response = CreateErrorResponse(ApiStatusCode::BAD_REQUEST, "Method not allowed");
        return SendApiResponse(req, response);
    }
    
    // 解析请求体
    cJSON* request_json = nullptr;
    if (req->method == HTTP_POST || req->method == HTTP_PUT) {
        request_json = ParseRequestJson(req);
        if (!request_json && req->content_len > 0) {
            ESP_LOGW(TAG, "Failed to parse request JSON");
            ApiResponse response = CreateErrorResponse(ApiStatusCode::BAD_REQUEST, "Invalid JSON");
            return SendApiResponse(req, response);
        }
    }
    
    // 调用处理器
    try {
        ApiResponse response = it->second.second(req, request_json);
        
        // 如果请求JSON已被处理器使用，我们不应该在这里释放它
        if (request_json && response.data != request_json) {
            cJSON_Delete(request_json);
        }
        
        return SendApiResponse(req, response);
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in API handler: %s", e.what());
        if (request_json) {
            cJSON_Delete(request_json);
        }
        ApiResponse response = CreateErrorResponse(ApiStatusCode::INTERNAL_ERROR, "Internal server error");
        return SendApiResponse(req, response);
    }
}

void ApiRouter::WsApiHandler(int client_id, const std::string& message, const std::string& type) {
    ESP_LOGI(TAG, "Processing WebSocket API message type '%s' from client %d", type.c_str(), client_id);
    
    ApiRouter* router = ApiRouter::GetInstance();
    if (!router) {
        ESP_LOGE(TAG, "API Router instance not available");
        return;
    }
    
    // 解析消息JSON
    cJSON* json = cJSON_Parse(message.c_str());
    if (!json) {
        ESP_LOGW(TAG, "Failed to parse WebSocket message JSON");
        // 发送错误响应
        if (router->web_server_) {
            cJSON* error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "status", "error");
            cJSON_AddStringToObject(error, "message", "Invalid JSON");
            
            char* error_str = cJSON_PrintUnformatted(error);
            router->web_server_->SendWebSocketMessage(client_id, error_str);
            free(error_str);
            cJSON_Delete(error);
        }
        return;
    }
    
    // 查找处理器
    auto it = router->ws_handlers_.find(type);
    if (it != router->ws_handlers_.end()) {
        try {
            it->second(client_id, json, type);
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Exception in WebSocket API handler: %s", e.what());
        }
    } else {
        ESP_LOGW(TAG, "No WebSocket handler found for type '%s'", type.c_str());
    }
    
    cJSON_Delete(json);
}

cJSON* ApiRouter::ParseRequestJson(httpd_req_t* req) {
    if (req->content_len <= 0) {
        return nullptr;
    }
    
    // 分配内存接收请求体
    char* buf = (char*)malloc(req->content_len + 1);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for request body");
        return nullptr;
    }
    
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Failed to receive request body: %d", ret);
        free(buf);
        return nullptr;
    }
    
    buf[req->content_len] = '\0';
    
    // 解析JSON
    cJSON* json = cJSON_Parse(buf);
    free(buf);
    
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON: %s", cJSON_GetErrorPtr());
        return nullptr;
    }
    
    return json;
}

esp_err_t ApiRouter::SendApiResponse(httpd_req_t* req, const ApiResponse& response) {
    // 创建响应JSON
    cJSON* resp_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp_json, "success", response.status_code == ApiStatusCode::OK);
    
    if (!response.message.empty()) {
        cJSON_AddStringToObject(resp_json, "message", response.message.c_str());
    }
    
    if (response.data) {
        cJSON_AddItemToObject(resp_json, "data", response.data);
        // 注意：data现在由resp_json拥有，不会在此函数中被释放
    }
    
    // 转换为字符串
    char* resp_str = cJSON_PrintUnformatted(resp_json);
    if (!resp_str) {
        ESP_LOGE(TAG, "Failed to generate response JSON string");
        cJSON_Delete(resp_json);
        return ESP_FAIL;
    }
    
    // 设置响应头
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, response.status_code == ApiStatusCode::OK ? "200 OK" : 
                               response.status_code == ApiStatusCode::BAD_REQUEST ? "400 Bad Request" :
                               response.status_code == ApiStatusCode::UNAUTHORIZED ? "401 Unauthorized" :
                               response.status_code == ApiStatusCode::NOT_FOUND ? "404 Not Found" :
                               "500 Internal Server Error");
    
    // 发送响应
    esp_err_t ret = httpd_resp_send(req, resp_str, strlen(resp_str));
    
    // 清理资源
    free(resp_str);
    cJSON_Delete(resp_json);
    
    return ret;
}

ApiResponse ApiRouter::CreateErrorResponse(ApiStatusCode code, const std::string& message) {
    return ApiResponse(code, message);
}

ApiResponse ApiRouter::CreateSuccessResponse(cJSON* data) {
    return ApiResponse(ApiStatusCode::OK, "", data);
} 