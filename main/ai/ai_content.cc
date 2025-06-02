#include "ai_content.h"
#include <esp_log.h>
#include <cJSON.h>
#include <cstring>
#include "sdkconfig.h"
#if CONFIG_ENABLE_WEB_CONTENT
#include "web/web_content.h"
// Define AI_HTML using the web_content function
#define AI_HTML get_ai_html_content()
#endif

#define TAG "AIContent"

// 使用html_content.h中定义的HTML内容函数

AIContent::AIContent(WebServer* server, AIController* ai_controller)
    : server_(server), ai_controller_(ai_controller), running_(false) {
    
    // If ai_controller is not provided, try to get it from ComponentManager
    if (!ai_controller_) {
        auto& manager = ComponentManager::GetInstance();
        Component* component = manager.GetComponent("AIController");
        if (component) {
            ai_controller_ = static_cast<AIController*>(component);
            ESP_LOGI(TAG, "Got AIController from ComponentManager");
        } else {
            ESP_LOGW(TAG, "AIController not found in ComponentManager");
        }
    }
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

    // Check if we need to try getting AIController from component manager
    if (!ai_controller_) {
        auto& manager = ComponentManager::GetInstance();
        Component* component = manager.GetComponent("AIController");
        if (component) {
            ai_controller_ = static_cast<AIController*>(component);
            ESP_LOGI(TAG, "Got AIController from ComponentManager in Start method");
        } else {
            ESP_LOGW(TAG, "AIController not found in ComponentManager, continuing with limited functionality");
        }
    }

    // Initialize handlers even if ai_controller is null (just won't be able to execute AI functions)
    InitHandlers();
    
    // Setup voice recognition callback only if controller exists
    if (ai_controller_) {
        // 设置语音识别完成回调
        ai_controller_->SetVoiceCommandCallback([this](const std::string& text) {
            OnVoiceRecognized(text);
        });
    }
    
    running_ = true;
    ESP_LOGI(TAG, "AI content started %s", ai_controller_ ? "with full functionality" : "with limited functionality (no AI controller)");
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
    if (!server_ || !server_->IsRunning()) {
        ESP_LOGE(TAG, "Server not running, cannot register handlers");
        return;
    }

    // 检查URI是否已经注册，避免重复注册
    if (!server_->IsUriRegistered("/ai")) {
        // 使用lambda捕获this指针，将C风格的静态函数适配为新的HttpRequestHandler类型
        server_->RegisterHttpHandler("/ai", HTTP_GET, 
            [this](httpd_req_t* req) -> esp_err_t {
                return HandleAI(req);
            });
        ESP_LOGI(TAG, "Registered URI handler: /ai");
    } else {
        ESP_LOGI(TAG, "URI /ai already registered, skipping");
    }
    
    if (!server_->IsUriRegistered("/api/speak")) {
        server_->RegisterHttpHandler("/api/speak", HTTP_POST, 
            [this](httpd_req_t* req) -> esp_err_t {
                return HandleSpeakText(req);
            });
        ESP_LOGI(TAG, "Registered URI handler: /api/speak");
    } else {
        ESP_LOGI(TAG, "URI /api/speak already registered, skipping");
    }
    
    if (!server_->IsUriRegistered("/api/set_key")) {
        server_->RegisterHttpHandler("/api/set_key", HTTP_POST, 
            [this](httpd_req_t* req) -> esp_err_t {
                return HandleSetApiKey(req);
            });
        ESP_LOGI(TAG, "Registered URI handler: /api/set_key");
    } else {
        ESP_LOGI(TAG, "URI /api/set_key already registered, skipping");
    }
    
    if (!server_->IsUriRegistered("/api/ai/status")) {
        server_->RegisterHttpHandler("/api/ai/status", HTTP_GET, 
            [this](httpd_req_t* req) -> esp_err_t {
                return HandleStatus(req);
            });
        ESP_LOGI(TAG, "Registered URI handler: /api/ai/status");
    } else {
        ESP_LOGI(TAG, "URI /api/ai/status already registered, skipping");
    }
    
    // 注册WebSocket消息处理器
    server_->RegisterWebSocketHandler("voice_command", 
        [this](int client_index, const PSRAMString& message, const PSRAMString& type) {
            HandleWebSocketMessage(client_index, message);
        });
    
    ESP_LOGI(TAG, "Registered AI WebSocket handler for voice commands");
}

esp_err_t AIContent::HandleAI(httpd_req_t *req) {
#if CONFIG_ENABLE_WEB_CONTENT
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, AI_HTML, get_ai_html_size());
    return ESP_OK;
#else
    // 当Web内容未启用时，返回一个简单的消息
    const char* message = "<html><body><h1>AI Content Disabled</h1><p>The web content feature is not enabled in this build.</p></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, message, strlen(message));
    return ESP_OK;
#endif
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
    cJSON *doc = cJSON_Parse(buf);
    if (!doc) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    // 获取文本
    cJSON *text_obj = cJSON_GetObjectItem(doc, "text");
    if (!text_obj || !text_obj->valuestring) {
        cJSON_Delete(doc);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing text parameter");
        return ESP_FAIL;
    }
    
    bool success = content->ai_controller_->SpeakText(text_obj->valuestring);
    
    // 创建JSON响应
    cJSON *respDoc = cJSON_CreateObject();
    cJSON_AddBoolToObject(respDoc, "success", success);
    cJSON_AddStringToObject(respDoc, "message", success ? "Text sent to speech synthesis" : "Failed to speak text");
    
    char *response = cJSON_Print(respDoc);
    
    // 返回响应
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    
    cJSON_Delete(doc);
    cJSON_Delete(respDoc);
    free(response);
    
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
    cJSON *doc = cJSON_Parse(buf);
    if (!doc) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    // 获取API密钥
    cJSON *api_key_obj = cJSON_GetObjectItem(doc, "api_key");
    if (!api_key_obj || !api_key_obj->valuestring) {
        cJSON_Delete(doc);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing api_key parameter");
        return ESP_FAIL;
    }
    
    // 设置API密钥
    content->ai_controller_->SetApiKey(api_key_obj->valuestring);
    
    // 如果提供了API端点，也设置它
    cJSON *api_endpoint_obj = cJSON_GetObjectItem(doc, "api_endpoint");
    if (api_endpoint_obj && api_endpoint_obj->valuestring) {
        content->ai_controller_->SetApiEndpoint(api_endpoint_obj->valuestring);
    }
    
    // 创建JSON响应
    cJSON *respDoc = cJSON_CreateObject();
    cJSON_AddBoolToObject(respDoc, "success", true);
    cJSON_AddStringToObject(respDoc, "message", "API key set successfully");
    
    char *response = cJSON_Print(respDoc);
    
    // 返回响应
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    
    cJSON_Delete(doc);
    cJSON_Delete(respDoc);
    free(response);
    
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
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddBoolToObject(doc, "running", ai->IsRunning());
    cJSON_AddBoolToObject(doc, "recording", ai->IsRecording());
    cJSON_AddNumberToObject(doc, "recognition_state", static_cast<int>(ai->GetRecognitionState()));
    cJSON_AddStringToObject(doc, "last_recognized_text", ai->GetLastRecognizedText().c_str());
    
    char *response = cJSON_Print(doc);
    
    // 返回响应
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    
    cJSON_Delete(doc);
    free(response);
    
    return ESP_OK;
}

void AIContent::HandleWebSocketMessage(int client_index, const PSRAMString& message) {
    if (!ai_controller_) {
        ESP_LOGW(TAG, "AI controller not available");
        return;
    }
    
    // 解析JSON消息
    cJSON *doc = cJSON_Parse(message.c_str());
    if (!doc) {
        ESP_LOGW(TAG, "Invalid JSON in WebSocket message");
        return;
    }
    
    // 处理不同类型的消息
    cJSON *type_obj = cJSON_GetObjectItem(doc, "type");
    if (!type_obj || !type_obj->valuestring) {
        ESP_LOGW(TAG, "Missing message type");
        cJSON_Delete(doc);
        return;
    }
    
    const char* type = type_obj->valuestring;
    
    if (strcmp(type, "speak_text") == 0) {
        cJSON *text_obj = cJSON_GetObjectItem(doc, "text");
        if (!text_obj || !text_obj->valuestring) {
            ESP_LOGW(TAG, "Missing text parameter");
            cJSON_Delete(doc);
            return;
        }
        
        bool success = ai_controller_->SpeakText(text_obj->valuestring);
        
        // 发送确认消息
        cJSON *respDoc = cJSON_CreateObject();
        cJSON_AddStringToObject(respDoc, "type", "speak_status");
        cJSON_AddStringToObject(respDoc, "status", success ? "success" : "failed");
        
        char *response = cJSON_Print(respDoc);
        
        server_->SendWebSocketMessage(client_index, response);
        
        cJSON_Delete(respDoc);
        free(response);
    }
    else if (strcmp(type, "set_api_key") == 0) {
        cJSON *api_key_obj = cJSON_GetObjectItem(doc, "api_key");
        if (!api_key_obj || !api_key_obj->valuestring) {
            ESP_LOGW(TAG, "Missing api_key parameter");
            cJSON_Delete(doc);
            return;
        }
        
        ai_controller_->SetApiKey(api_key_obj->valuestring);
        
        // 如果提供了API端点，也设置它
        cJSON *api_endpoint_obj = cJSON_GetObjectItem(doc, "api_endpoint");
        if (api_endpoint_obj && api_endpoint_obj->valuestring) {
            ai_controller_->SetApiEndpoint(api_endpoint_obj->valuestring);
        }
        
        // 发送确认消息
        server_->SendWebSocketMessage(client_index, "{\"type\":\"api_key_status\",\"status\":\"set\"}");
    }
    else if (strcmp(type, "status_request") == 0) {
        // 发送状态信息
        cJSON *statusDoc = cJSON_CreateObject();
        cJSON_AddStringToObject(statusDoc, "type", "ai_status");
        cJSON_AddBoolToObject(statusDoc, "running", ai_controller_->IsRunning());
        cJSON_AddBoolToObject(statusDoc, "recording", ai_controller_->IsRecording());
        cJSON_AddNumberToObject(statusDoc, "recognition_state", static_cast<int>(ai_controller_->GetRecognitionState()));
        cJSON_AddStringToObject(statusDoc, "last_recognized_text", ai_controller_->GetLastRecognizedText().c_str());
        
        char *response = cJSON_Print(statusDoc);
        
        server_->SendWebSocketMessage(client_index, response);
        
        cJSON_Delete(statusDoc);
        free(response);
    }
    else if (strcmp(type, "test_tts") == 0) {
        // 测试TTS功能，用于网页端的语音合成
        cJSON *text_obj = cJSON_GetObjectItem(doc, "text");
        if (text_obj && text_obj->valuestring) {
            ai_controller_->SpeakText(text_obj->valuestring);
        }
    }
    else if (strcmp(type, "enable_wake_word") == 0 || 
             strcmp(type, "disable_wake_word") == 0 ||
             strcmp(type, "start_listening") == 0 ||
             strcmp(type, "stop_listening") == 0) {
        // 处理网页端的语音控制命令
        // 注意：这些命令只影响网页端JavaScript中的状态，不会影响ESP32S3设备端的语音唤醒和识别功能
        ESP_LOGI(TAG, "Processing web client voice command: %s (does not affect device voice functions)", type);
        
        // 这里可以添加实际处理逻辑，但要确保只影响网页端，不干扰设备端功能
    }
    
    cJSON_Delete(doc);
}

void AIContent::OnVoiceRecognized(const std::string& text) {
    if (!server_ || !server_->IsRunning()) {
        return;
    }
    
    // 广播语音识别结果到所有WebSocket客户端
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "type", "voice_recognized");
    cJSON_AddStringToObject(doc, "text", text.c_str());
    
    char *message = cJSON_Print(doc);
    
    server_->BroadcastWebSocketMessage(message);
    
    cJSON_Delete(doc);
    free(message);
}

// 初始化AI组件
void InitAIComponents(WebServer* web_server) {
#if defined(CONFIG_ENABLE_AI_CONTROLLER)
    auto& manager = ComponentManager::GetInstance();
    
    // 检查AIController是否已经存在
    AIController* ai_controller = nullptr;
    Component* existing_controller = manager.GetComponent("AIController");
    if (existing_controller) {
        ESP_LOGI(TAG, "AIController already exists, using existing instance");
        ai_controller = static_cast<AIController*>(existing_controller);
    } else {
        // 创建新的AI控制器组件
        ai_controller = new AIController();
        manager.RegisterComponent(ai_controller);
        ESP_LOGI(TAG, "Created new AIController instance");
    }
    
    // 检查AIContent是否已经存在
    if (manager.GetComponent("AIContent")) {
        ESP_LOGI(TAG, "AIContent already exists, skipping creation");
    } else {
        // 创建AI内容处理组件
        AIContent* ai_content = new AIContent(web_server, ai_controller);
        manager.RegisterComponent(ai_content);
        ESP_LOGI(TAG, "Created new AIContent instance");
    }
    
    ESP_LOGI(TAG, "AI components initialized");
#else
    ESP_LOGI(TAG, "AI controller disabled in configuration");
#endif
}