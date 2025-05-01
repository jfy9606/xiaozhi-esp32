#include "ai_content.h"
#include <esp_log.h>
#include <ArduinoJson.h>
#include "sdkconfig.h"

#define TAG "AIContent"

// 声明外部HTML内容获取函数（在html_content.cc中定义）
extern const char* AI_HTML;
extern size_t get_ai_html_size();

AIContent::AIContent(WebServer* server, AIController* ai_controller)
    : server_(server), ai_controller_(ai_controller), running_(false) {
}

AIContent::~AIContent() {
    Stop();
}

bool AIContent::Start() {
    if (running_) {
        ESP_LOGW(TAG, "AI content already running");
        return true;
    }

    if (!server_ || !server_->IsRunning()) {
        ESP_LOGE(TAG, "Web server not running, cannot start AI content");
        return false;
    }

    if (!ai_controller_) {
        ESP_LOGE(TAG, "AI controller not available, cannot start AI content");
        return false;
    }

    InitHandlers();
    
    // 设置语音识别完成回调
    ai_controller_->SetVoiceCommandCallback([this](const std::string& text) {
        OnVoiceRecognized(text);
    });
    
    running_ = true;
    ESP_LOGI(TAG, "AI content started");
    return true;
}

void AIContent::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    ESP_LOGI(TAG, "AI content stopped");
}

bool AIContent::IsRunning() const {
    return running_;
}

const char* AIContent::GetName() const {
    return "AIContent";
}

void AIContent::InitHandlers() {
    // 注册URI处理函数
    server_->RegisterUri("/ai", HTTP_GET, HandleAI, this);
    server_->RegisterUri("/ai/speak", HTTP_POST, HandleSpeakText, this);
    server_->RegisterUri("/ai/set_api_key", HTTP_POST, HandleSetApiKey, this);
    server_->RegisterUri("/ai/status", HTTP_GET, HandleStatus, this);
}

esp_err_t AIContent::HandleAI(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, AI_HTML, get_ai_html_size());
    return ESP_OK;
}

esp_err_t AIContent::HandleSpeakText(httpd_req_t *req) {
    AIContent* content = static_cast<AIContent*>(req->user_ctx);
    if (!content || !content->ai_controller_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "AI controller not available");
        return ESP_FAIL;
    }

    char buf[512];
    int ret, remaining = req->content_len;
    
    if (remaining > sizeof(buf) - 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large");
        return ESP_FAIL;
    }
    
    int received = 0;
    while (remaining > 0) {
        ret = httpd_req_recv(req, buf + received, remaining);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        received += ret;
        remaining -= ret;
    }
    buf[received] = '\0';
    
    // 解析JSON
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, buf);
    if (error) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    // 获取文本
    const char* text = doc["text"];
    if (!text) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing text parameter");
        return ESP_FAIL;
    }
    
    bool success = content->ai_controller_->SpeakText(text);
    
    // 创建JSON响应
    DynamicJsonDocument respDoc(128);
    respDoc["success"] = success;
    respDoc["message"] = success ? "Text sent to speech synthesis" : "Failed to speak text";
    
    String response;
    serializeJson(respDoc, response);
    
    // 返回响应
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response.c_str(), response.length());
    
    return ESP_OK;
}

esp_err_t AIContent::HandleSetApiKey(httpd_req_t *req) {
    AIContent* content = static_cast<AIContent*>(req->user_ctx);
    if (!content || !content->ai_controller_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "AI controller not available");
        return ESP_FAIL;
    }

    char buf[512];
    int ret, remaining = req->content_len;
    
    if (remaining > sizeof(buf) - 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large");
        return ESP_FAIL;
    }
    
    int received = 0;
    while (remaining > 0) {
        ret = httpd_req_recv(req, buf + received, remaining);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        received += ret;
        remaining -= ret;
    }
    buf[received] = '\0';
    
    // 解析JSON
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, buf);
    if (error) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    // 获取API密钥
    const char* api_key = doc["api_key"];
    if (!api_key) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing api_key parameter");
        return ESP_FAIL;
    }
    
    // 设置API密钥
    content->ai_controller_->SetApiKey(api_key);
    
    // 如果提供了API端点，也设置它
    const char* api_endpoint = doc["api_endpoint"];
    if (api_endpoint) {
        content->ai_controller_->SetApiEndpoint(api_endpoint);
    }
    
    // 创建JSON响应
    DynamicJsonDocument respDoc(128);
    respDoc["success"] = true;
    respDoc["message"] = "API key set successfully";
    
    String response;
    serializeJson(respDoc, response);
    
    // 返回响应
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response.c_str(), response.length());
    
    return ESP_OK;
}

esp_err_t AIContent::HandleStatus(httpd_req_t *req) {
    AIContent* content = static_cast<AIContent*>(req->user_ctx);
    if (!content || !content->ai_controller_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "AI controller not available");
        return ESP_FAIL;
    }
    
    AIController* ai = content->ai_controller_;
    
    // 创建JSON响应
    DynamicJsonDocument doc(256);
    doc["running"] = ai->IsRunning();
    doc["recording"] = ai->IsRecording();
    doc["recognition_state"] = static_cast<int>(ai->GetRecognitionState());
    doc["last_recognized_text"] = ai->GetLastRecognizedText();
    
    String response;
    serializeJson(doc, response);
    
    // 返回响应
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response.c_str(), response.length());
    
    return ESP_OK;
}

void AIContent::HandleWebSocketMessage(int client_index, const std::string& message) {
    if (!ai_controller_) {
        ESP_LOGW(TAG, "AI controller not available");
        return;
    }
    
    // 解析JSON消息
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, message.c_str());
    if (error) {
        ESP_LOGW(TAG, "Invalid JSON in WebSocket message: %s", error.c_str());
        return;
    }
    
    // 处理不同类型的消息
    const char* type = doc["type"];
    if (!type) {
        ESP_LOGW(TAG, "Missing message type");
        return;
    }
    
    if (strcmp(type, "speak_text") == 0) {
        const char* text = doc["text"];
        if (!text) {
            ESP_LOGW(TAG, "Missing text parameter");
            return;
        }
        
        bool success = ai_controller_->SpeakText(text);
        
        // 发送确认消息
        DynamicJsonDocument respDoc(128);
        respDoc["type"] = "speak_status";
        respDoc["status"] = success ? "success" : "failed";
        
        String response;
        serializeJson(respDoc, response);
        
        server_->SendWebSocketMessage(client_index, response.c_str());
    }
    else if (strcmp(type, "set_api_key") == 0) {
        const char* api_key = doc["api_key"];
        if (!api_key) {
            ESP_LOGW(TAG, "Missing api_key parameter");
            return;
        }
        
        ai_controller_->SetApiKey(api_key);
        
        // 如果提供了API端点，也设置它
        const char* api_endpoint = doc["api_endpoint"];
        if (api_endpoint) {
            ai_controller_->SetApiEndpoint(api_endpoint);
        }
        
        // 发送确认消息
        server_->SendWebSocketMessage(client_index, "{\"type\":\"api_key_status\",\"status\":\"set\"}");
    }
    else if (strcmp(type, "status_request") == 0) {
        // 发送状态信息
        DynamicJsonDocument statusDoc(256);
        statusDoc["type"] = "ai_status";
        statusDoc["running"] = ai_controller_->IsRunning();
        statusDoc["recording"] = ai_controller_->IsRecording();
        statusDoc["recognition_state"] = static_cast<int>(ai_controller_->GetRecognitionState());
        statusDoc["last_recognized_text"] = ai_controller_->GetLastRecognizedText();
        
        String response;
        serializeJson(statusDoc, response);
        
        server_->SendWebSocketMessage(client_index, response.c_str());
    }
}

void AIContent::OnVoiceRecognized(const std::string& text) {
    if (!server_ || !server_->IsRunning()) {
        return;
    }
    
    // 广播语音识别结果到所有WebSocket客户端
    DynamicJsonDocument doc(256);
    doc["type"] = "voice_recognized";
    doc["text"] = text;
    
    String message;
    serializeJson(doc, message);
    
    server_->BroadcastWebSocketMessage(message.c_str());
}

// 初始化AI组件
void InitAIComponents(WebServer* web_server) {
#if defined(CONFIG_ENABLE_AI_CONTROLLER)
    // 创建简化版AI控制器组件
    AIController* ai_controller = new AIController();
    ComponentManager::GetInstance().RegisterComponent(ai_controller);
    
    // 创建AI内容处理组件
    AIContent* ai_content = new AIContent(web_server, ai_controller);
    ComponentManager::GetInstance().RegisterComponent(ai_content);
    
    ESP_LOGI(TAG, "AI components initialized");
#else
    ESP_LOGI(TAG, "AI controller disabled in configuration");
#endif
} 