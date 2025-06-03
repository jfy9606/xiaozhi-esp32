#include "ai.h"
#include <esp_log.h>
#include <cJSON.h>
#include <string>
#include <memory>

#define TAG "AI"

// 构造函数
AI::AI(Web* web_server)
    : web_server_(web_server), 
      running_(false),
      listening_(false) {
    ESP_LOGI(TAG, "AI component created");
}

// 析构函数
AI::~AI() {
    if (running_) {
        Stop();
    }
    
    ESP_LOGI(TAG, "AI component destroyed");
}

// Component接口实现
bool AI::Start() {
    if (running_) {
        ESP_LOGW(TAG, "AI already running");
        return true;
    }
    
    ESP_LOGI(TAG, "Starting AI component");
    
    // 如果Web服务器可用，初始化处理器
    if (web_server_ && web_server_->IsRunning()) {
        InitHandlers();
    }
    
    running_ = true;
    return true;
}

void AI::Stop() {
    if (!running_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping AI component");
    
    // 停止语音识别
    if (listening_) {
        StopVoiceRecognition();
    }
    
    running_ = false;
}

bool AI::IsRunning() const {
    return running_;
}

const char* AI::GetName() const {
    return "AI";
}

// AI功能方法
std::string AI::ProcessSpeechQuery(const std::string& speech_input) {
    ESP_LOGI(TAG, "Processing speech query: %s", speech_input.c_str());
    
    // 这里应该实现实际的语音处理逻辑
    // 目前返回一个模拟响应
    return "I heard you say: " + speech_input;
}

std::string AI::GenerateTextResponse(const std::string& query) {
    ESP_LOGI(TAG, "Generating AI response for: %s", query.c_str());
    
    // 这里应该实现实际的AI对话逻辑
    // 目前返回一个模拟响应
    return "AI response to: " + query;
}

// 语音功能
bool AI::StartVoiceRecognition() {
    if (!running_) {
        ESP_LOGW(TAG, "AI component not running");
        return false;
    }
    
    if (listening_) {
        ESP_LOGW(TAG, "Voice recognition already running");
        return true;
    }
    
    ESP_LOGI(TAG, "Starting voice recognition");
    
    // 这里应该实现启动语音识别的逻辑
    
    listening_ = true;
    return true;
}

void AI::StopVoiceRecognition() {
    if (!listening_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping voice recognition");
    
    // 这里应该实现停止语音识别的逻辑
    
    listening_ = false;
}

bool AI::IsListening() const {
    return listening_;
}

// 设置回调
void AI::SetSpeechRecognitionCallback(SpeechRecognitionCallback callback) {
    speech_callback_ = callback;
}

// WebSocket方法
void AI::HandleWebSocketMessage(httpd_req_t* req, const std::string& message) {
    ESP_LOGI(TAG, "Received WebSocket message: %s", message.c_str());
    
    // 解析JSON消息
    cJSON* root = cJSON_Parse(message.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse WebSocket message");
        return;
    }
    
    // 处理不同类型的消息
    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && type->valuestring) {
        std::string msg_type = type->valuestring;
        
        if (msg_type == "startRecognition") {
            // 启动语音识别
            if (StartVoiceRecognition()) {
                // 发送确认消息
                std::string response = "{\"type\":\"recognition\",\"status\":\"started\"}";
                web_server_->SendWebSocketMessage(req, response);
            }
        }
        else if (msg_type == "stopRecognition") {
            // 停止语音识别
            StopVoiceRecognition();
            // 发送确认消息
            std::string response = "{\"type\":\"recognition\",\"status\":\"stopped\"}";
            web_server_->SendWebSocketMessage(req, response);
        }
        else if (msg_type == "audioData") {
            // 处理音频数据
            cJSON* data = cJSON_GetObjectItem(root, "data");
            if (cJSON_IsString(data) && data->valuestring) {
                // 处理音频数据（通常是base64编码）
                std::string audio_data = data->valuestring;
                std::string text = ProcessAudioFile(audio_data);
                
                if (!text.empty() && speech_callback_) {
                    speech_callback_(text);
                }
                
                // 发送识别结果
                char response[512];
                snprintf(response, sizeof(response), 
                         "{\"type\":\"recognitionResult\",\"text\":\"%s\"}", 
                         text.c_str());
                web_server_->SendWebSocketMessage(req, response);
            }
        }
        else if (msg_type == "chatRequest") {
            // 处理对话请求
            cJSON* query = cJSON_GetObjectItem(root, "query");
            if (cJSON_IsString(query) && query->valuestring) {
                std::string user_query = query->valuestring;
                std::string ai_response = GenerateTextResponse(user_query);
                
                // 发送AI响应
                char response[1024];
                snprintf(response, sizeof(response), 
                         "{\"type\":\"chatResponse\",\"text\":\"%s\"}", 
                         ai_response.c_str());
                web_server_->SendWebSocketMessage(req, response);
                
                // 如果启用了语音合成，也可以发送语音数据
                std::string speech_data = SynthesizeSpeech(ai_response);
                if (!speech_data.empty()) {
                    // 发送语音数据（通常是base64编码）
                    char speech_response[1024];
                    snprintf(speech_response, sizeof(speech_response), 
                             "{\"type\":\"speechData\",\"data\":\"%s\"}", 
                             speech_data.c_str());
                    web_server_->SendWebSocketMessage(req, speech_response);
                }
            }
        }
    }
    
    cJSON_Delete(root);
}

// 网页UI处理
void AI::InitHandlers() {
    if (!web_server_) {
        ESP_LOGE(TAG, "Web server not initialized");
        return;
    }
    
    ESP_LOGI(TAG, "Registering AI handlers");
    
    // 注册Web页面处理器
    web_server_->RegisterHandler(HttpMethod::HTTP_GET, "/ai", [this](httpd_req_t* req) {
        ESP_LOGI(TAG, "Processing AI UI request");
        
        // 设置内容类型
        httpd_resp_set_type(req, "text/html");
        
        // 从请求头中获取语言设置
        char lang_buf[64] = {0};
        httpd_req_get_hdr_value_str(req, "Accept-Language", lang_buf, sizeof(lang_buf));
        std::string html = GetAIHtml(lang_buf[0] ? lang_buf : nullptr);
        
        httpd_resp_send(req, html.c_str(), html.length());
        return ESP_OK;
    });
    
    // 注册API处理器
    web_server_->RegisterApiHandler(HttpMethod::HTTP_POST, "/api/speech-to-text", 
                                   [this](httpd_req_t* req) {
                                       return HandleSpeechToText(req);
                                   });
                                   
    web_server_->RegisterApiHandler(HttpMethod::HTTP_POST, "/api/text-to-speech", 
                                   [this](httpd_req_t* req) {
                                       return HandleTextToSpeech(req);
                                   });
                                   
    web_server_->RegisterApiHandler(HttpMethod::HTTP_POST, "/api/chat", 
                                   [this](httpd_req_t* req) {
                                       return HandleAIChat(req);
                                   });
                                   
    // 注册WebSocket消息回调
    web_server_->RegisterWebSocketMessageCallback([this](httpd_req_t* req, const std::string& message) {
        // 检查消息是否与AI相关
        if (message.find("\"type\":\"") != std::string::npos &&
            (message.find("\"recognition") != std::string::npos ||
             message.find("\"audio") != std::string::npos ||
             message.find("\"chat") != std::string::npos)) {
            HandleWebSocketMessage(req, message);
        }
    });
}

std::string AI::GetAIHtml(const char* language) {
    // 这里应该通过资源文件或嵌入式二进制获取HTML内容
    // 实际内容将由Web内容子系统提供
    // 这里只返回一个通用页面框架，具体内容由前端JS填充
    
    return "<html>"
           "<head>"
           "  <title>AI对话</title>"
           "  <meta name='viewport' content='width=device-width, initial-scale=1'>"
           "  <link rel='stylesheet' href='/css/bootstrap.min.css'>"
           "  <link rel='stylesheet' href='/css/ai.css'>"
           "</head>"
           "<body>"
           "  <div class='container'>"
           "    <h1>AI对话</h1>"
           "    <div id='chat-container'></div>"
           "    <div id='voice-controls'></div>"
           "  </div>"
           "  <script src='/js/common.js'></script>"
           "  <script src='/js/bootstrap.bundle.min.js'></script>"
           "  <script src='/js/ai.js'></script>"
           "</body>"
           "</html>";
}

// API响应处理
ApiResponse AI::HandleSpeechToText(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing speech-to-text request");
    
    // 获取POST数据（音频文件）
    std::string post_data = Web::GetPostData(req);
    
    // 处理音频文件
    std::string text = ProcessAudioFile(post_data);
    
    // 构建JSON响应
    char response[256];
    snprintf(response, sizeof(response), "{\"success\":true,\"text\":\"%s\"}", text.c_str());
    
    return ApiResponse(response);
}

ApiResponse AI::HandleTextToSpeech(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing text-to-speech request");
    
    // 获取POST数据（文本）
    std::string post_data = Web::GetPostData(req);
    
    // 解析JSON请求
    cJSON* root = cJSON_Parse(post_data.c_str());
    if (!root) {
        return ApiResponse("{\"success\":false,\"error\":\"Invalid JSON data\"}");
    }
    
    cJSON* text = cJSON_GetObjectItem(root, "text");
    if (!cJSON_IsString(text) || !text->valuestring) {
        cJSON_Delete(root);
        return ApiResponse("{\"success\":false,\"error\":\"Missing text parameter\"}");
    }
    
    std::string text_to_speak = text->valuestring;
    cJSON_Delete(root);
    
    // 合成语音
    std::string speech_data = SynthesizeSpeech(text_to_speak);
    
    // 构建JSON响应
    std::string response = "{\"success\":true,\"data\":\"" + speech_data + "\"}";
    
    return ApiResponse(response);
}

ApiResponse AI::HandleAIChat(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing AI chat request");
    
    // 获取POST数据
    std::string post_data = Web::GetPostData(req);
    
    // 解析JSON请求
    cJSON* root = cJSON_Parse(post_data.c_str());
    if (!root) {
        return ApiResponse("{\"success\":false,\"error\":\"Invalid JSON data\"}");
    }
    
    cJSON* query = cJSON_GetObjectItem(root, "query");
    if (!cJSON_IsString(query) || !query->valuestring) {
        cJSON_Delete(root);
        return ApiResponse("{\"success\":false,\"error\":\"Missing query parameter\"}");
    }
    
    std::string user_query = query->valuestring;
    cJSON_Delete(root);
    
    // 生成AI响应
    std::string response_text = GenerateTextResponse(user_query);
    
    // 构建JSON响应
    std::string response = "{\"success\":true,\"response\":\"" + response_text + "\"}";
    
    return ApiResponse(response);
}

// 实用功能
std::string AI::ProcessAudioFile(const std::string& audio_data) {
    ESP_LOGI(TAG, "Processing audio data (%zu bytes)", audio_data.size());
    
    // 这里应该实现实际的语音识别逻辑
    // 目前返回一个模拟响应
    return "Speech recognition result";
}

std::string AI::SynthesizeSpeech(const std::string& text) {
    ESP_LOGI(TAG, "Synthesizing speech: %s", text.c_str());
    
    // 这里应该实现实际的语音合成逻辑
    // 目前返回一个空字符串
    return "";
} 