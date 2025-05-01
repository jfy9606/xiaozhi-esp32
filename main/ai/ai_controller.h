#ifndef _AI_CONTROLLER_H_
#define _AI_CONTROLLER_H_

#include <string>
#include <functional>
#include "../components.h"

// AI控制器组件 - 简化版本，仅保留前端交互功能
class AIController : public Component {
public:
    enum RecognitionState {
        IDLE,
        COMPLETED
    };
    
    AIController();
    virtual ~AIController();

    // Component接口实现
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;

    // 设置API密钥和端点
    void SetApiKey(const std::string& key);
    void SetApiEndpoint(const std::string& endpoint);
    
    // 文本到语音 - 模拟实现
    bool SpeakText(const std::string& text);
    
    // 设置回调
    using VoiceCommandCallback = std::function<void(const std::string&)>;
    void SetVoiceCommandCallback(VoiceCommandCallback callback);
    
    // 获取当前状态
    RecognitionState GetRecognitionState() const;
    const std::string& GetLastRecognizedText() const;
    bool IsRecording() const;

private:
    // 状态标志
    bool running_;
    RecognitionState recognition_state_;
    std::string last_recognized_text_;
    
    // API配置
    std::string api_key_;
    std::string api_endpoint_;
    
    // 回调函数
    VoiceCommandCallback voice_command_callback_;
};

#endif // _AI_CONTROLLER_H_ 