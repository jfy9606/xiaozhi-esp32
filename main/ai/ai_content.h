#ifndef _AI_CONTENT_H_
#define _AI_CONTENT_H_

#include <esp_http_server.h>
#include "../components.h"
#include "../web/web_server.h"
#include "ai_controller.h"

// AI的Web内容处理组件
class AIContent : public Component {
public:
    AIContent(WebServer* server, AIController* ai_controller);
    virtual ~AIContent();

    // Component接口实现
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;

    // 处理AI页面
    static esp_err_t HandleAI(httpd_req_t *req);
    
    // 处理WebSocket消息 - 更新为使用PSRAMString
    void HandleWebSocketMessage(int client_index, const PSRAMString& message);
    
    // 处理语音识别完成
    void OnVoiceRecognized(const std::string& text);
    
    // 获取AI控制器实例
    AIController* GetAIController() { return ai_controller_; }

private:
    WebServer* server_;
    AIController* ai_controller_;
    bool running_;
    
    // 初始化URI处理函数
    void InitHandlers();
    
    // API接口处理函数
    static esp_err_t HandleSpeakText(httpd_req_t *req);
    static esp_err_t HandleSetApiKey(httpd_req_t *req);
    static esp_err_t HandleStatus(httpd_req_t *req);
};

// 初始化AI组件
void InitAIComponents(WebServer* web_server);

#endif // _AI_CONTENT_H_ 