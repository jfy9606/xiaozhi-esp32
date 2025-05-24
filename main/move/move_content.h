#pragma once

#include "../components.h"
#include "move_controller.h"
#include "../iot/thing.h"
#include "../iot/thing_manager.h"
#include "../web/web_server.h"
#include "../web/web_content.h"

// 移动内容类，用于处理Web UI交互
class MoveContent : public WebContent {
public:
    MoveContent(WebServer* server);
    virtual ~MoveContent();

    // WebContent接口实现
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;
    virtual ComponentType GetType() const override { return COMPONENT_TYPE_WEB; }

    // WebSocket消息处理
    void HandleWebSocketMessage(int client_index, const PSRAMString& message);

    // 发送超声波数据
    static void SendUltrasonicData(WebServer* server, iot::Thing* thing);
    
    // 发送舵机状态数据
    static void SendServoData(WebServer* server, iot::Thing* thing);

private:
    bool running_;
    WebServer* server_;

    // 初始化HTTP处理函数
    void InitHandlers();

    // 移动控制HTTP处理
    static esp_err_t HandleMove(httpd_req_t *req);
    
    // 舵机控制HTTP处理
    static esp_err_t HandleServo(httpd_req_t *req);

    // 超声波数据任务
    static void UltrasonicDataTask(void* pvParameters);
    
    // 舵机状态数据任务
    static void ServoDataTask(void* pvParameters);
};

// 全局初始化函数
void InitMoveComponents(WebServer* server); 