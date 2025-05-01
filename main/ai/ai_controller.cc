#include "ai_controller.h"
#include <esp_log.h>

#define TAG "AIController"

AIController::AIController()
    : running_(false),
      recognition_state_(IDLE),
      api_key_(""),
      api_endpoint_("https://api.openai.com/v1/audio/transcriptions") {
}

AIController::~AIController() {
    Stop();
}

bool AIController::Start() {
    if (running_) {
        ESP_LOGW(TAG, "AI controller already running");
        return true;
    }

    recognition_state_ = IDLE;
    running_ = true;
    ESP_LOGI(TAG, "AI controller started");
    return true;
}

void AIController::Stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    ESP_LOGI(TAG, "AI controller stopped");
}

bool AIController::IsRunning() const {
    return running_;
}

const char* AIController::GetName() const {
    return "AIController";
}

bool AIController::IsRecording() const {
    // 简化实现，始终返回false
    return false;
}

AIController::RecognitionState AIController::GetRecognitionState() const {
    return recognition_state_;
}

const std::string& AIController::GetLastRecognizedText() const {
    return last_recognized_text_;
}

void AIController::SetApiKey(const std::string& key) {
    api_key_ = key;
    ESP_LOGI(TAG, "API key set");
}

void AIController::SetApiEndpoint(const std::string& endpoint) {
    api_endpoint_ = endpoint;
    ESP_LOGI(TAG, "API endpoint set to: %s", endpoint.c_str());
}

bool AIController::SpeakText(const std::string& text) {
    if (!running_) {
        ESP_LOGW(TAG, "AI controller not running");
        return false;
    }
    
    // 简化的TTS实现
    ESP_LOGI(TAG, "Text to speech (simulated): %s", text.c_str());
    
    // 处理文本，如果有回调函数则调用它
    if (voice_command_callback_) {
        last_recognized_text_ = text;
        voice_command_callback_(text);
    }
    
    return true;
}

void AIController::SetVoiceCommandCallback(VoiceCommandCallback callback) {
    voice_command_callback_ = callback;
} 