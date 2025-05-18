#ifndef _MOTOR_CONTENT_H_
#define _MOTOR_CONTENT_H_

#include <esp_http_server.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../components.h"
#include "../web/web_server.h" // Include WebServer to access PSRAMString type
#include "../iot/thing.h"

class WebServer;
class MotorController;

// Motor web content handler component
class MotorContent : public Component {
public:
    MotorContent(WebServer* server);
    virtual ~MotorContent();

    // Component interface implementation
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;

    // Handle WebSocket messages - updated parameter type to match WebSocketMessageCallback
    void HandleWebSocketMessage(int client_index, const PSRAMString& message);
    
    // Send ultrasonic data to clients
    static void SendUltrasonicData(WebServer* server, iot::Thing* thing);

private:
    WebServer* server_;
    bool running_;
    TaskHandle_t us_task_handle_; // 超声波数据传输任务的句柄

    // Initialize URI handlers
    void InitHandlers();
    
    // Static handler for car control page
    static esp_err_t HandleCar(httpd_req_t *req);
    
    // 超声波数据传输任务
    static void UltrasonicDataTask(void* pvParameters);
};

// Initialize motor components
void InitMotorComponents(WebServer* server);

#endif // _MOTOR_CONTENT_H_ 