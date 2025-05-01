#ifndef _MOTOR_CONTENT_H_
#define _MOTOR_CONTENT_H_

#include <esp_http_server.h>
#include "../components.h"
#include "../web/web_server.h"
#include "motor_controller.h"

// 电机控制的Web内容处理组件
class MotorContent : public Component {
public:
    MotorContent(WebServer* server, MotorController* motor_controller);
    virtual ~MotorContent();

    // Component接口实现
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;

    // 处理小车控制页面
    static esp_err_t HandleCar(httpd_req_t *req);
    
    // 处理WebSocket消息
    void HandleWebSocketMessage(int client_index, const std::string& message);

    // 获取电机控制器实例
    MotorController* GetMotorController() { return motor_controller_; }

private:
    WebServer* server_;
    MotorController* motor_controller_;
    bool running_;
    
    // 初始化URI处理函数
    void InitHandlers();
    
    // 命令处理函数
    static esp_err_t HandleCommand(httpd_req_t *req);
    
    // 状态查询处理函数
    static esp_err_t HandleStatus(httpd_req_t *req);
};

// 初始化电机组件
void InitMotorComponents(WebServer* web_server);

#endif // _MOTOR_CONTENT_H_ 