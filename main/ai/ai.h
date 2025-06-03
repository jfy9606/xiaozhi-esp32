#pragma once

#include "../components.h"
#include "../web/web.h"
#include <functional>
#include <string>
#include <memory>
#include <vector>

/**
 * @brief AI组件 - 提供语音识别和AI对话功能
 * 
 * 此类整合了之前的AIController和AIContent功能
 * 主要负责:
 * 1. 语音识别处理
 * 2. AI对话交互
 * 3. Web界面交互
 */
class AI : public Component {
public:
    // 构造函数和析构函数
    explicit AI(Web* web_server = nullptr);
    virtual ~AI();
    
    // Component接口实现
    bool Start() override;
    void Stop() override;
    bool IsRunning() const override;
    const char* GetName() const override;
    
    // AI功能方法
    std::string ProcessSpeechQuery(const std::string& speech_input);
    std::string GenerateTextResponse(const std::string& query);
    
    // 语音功能
    bool StartVoiceRecognition();
    void StopVoiceRecognition();
    bool IsListening() const;
    
    // 设置回调
    using SpeechRecognitionCallback = std::function<void(const std::string&)>;
    void SetSpeechRecognitionCallback(SpeechRecognitionCallback callback);
    
    // WebSocket方法
    void HandleWebSocketMessage(httpd_req_t* req, const std::string& message);
    
protected:
    // 网页UI处理
    void InitHandlers();
    std::string GetAIHtml(const char* language = nullptr);
    
    // API响应处理
    ApiResponse HandleSpeechToText(httpd_req_t* req);
    ApiResponse HandleTextToSpeech(httpd_req_t* req);
    ApiResponse HandleAIChat(httpd_req_t* req);
    
private:
    // 成员变量
    Web* web_server_;
    bool running_;
    bool listening_;
    SpeechRecognitionCallback speech_callback_;
    
    // 实用功能
    std::string ProcessAudioFile(const std::string& audio_data);
    std::string SynthesizeSpeech(const std::string& text);
    
    // 禁止复制和赋值
    AI(const AI&) = delete;
    AI& operator=(const AI&) = delete;
}; 