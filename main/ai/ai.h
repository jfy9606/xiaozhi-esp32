#pragma once

#include "../components.h"
#include "../web/web.h"
#include "../hardware/hardware_manager.h"
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
    
    // 硬件管理器接口
    void SetHardwareManager(HardwareManager* hardware_manager);
    std::string GetSensorDataJson();
    std::string GetFilteredSensorDataJson(const std::vector<std::string>& sensor_ids);
    bool ExecuteHardwareCommand(const std::string& command_json);
    
    // 传感器数据回调
    using SensorDataCallback = std::function<void(const std::string&)>;
    void RegisterSensorDataCallback(SensorDataCallback callback);
    void StartSensorDataPush(int interval_ms = 1000);
    void StopSensorDataPush();
    
    // 执行器控制接口
    bool ExecuteMotorCommand(int motor_id, int speed);
    bool ExecuteServoCommand(int servo_id, int angle);
    std::string GetControlHistory(int limit = 10);
    void ClearControlHistory();
    
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
    HardwareManager* hardware_manager_;
    bool running_;
    bool listening_;
    SpeechRecognitionCallback speech_callback_;
    
    // 传感器数据推送
    SensorDataCallback sensor_data_callback_;
    bool sensor_push_active_;
    int sensor_push_interval_;
    
    // 控制历史记录
    struct ControlHistoryEntry {
        uint64_t timestamp;
        std::string command_type;
        int device_id;
        int value;
        bool success;
    };
    std::vector<ControlHistoryEntry> control_history_;
    
    // 实用功能
    std::string ProcessAudioFile(const std::string& audio_data);
    std::string SynthesizeSpeech(const std::string& text);
    
    // 控制历史记录辅助方法
    void RecordControlHistory(const std::string& command_type, int device_id, int value, bool success);
    
    // 禁止复制和赋值
    AI(const AI&) = delete;
    AI& operator=(const AI&) = delete;
}; 