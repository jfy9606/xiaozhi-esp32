#ifndef _MOTOR_CONTENT_H_
#define _MOTOR_CONTENT_H_

#include <esp_http_server.h>
#include "../components.h"
#include "../web/web_server.h" // Include WebServer to access PSRAMString type

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

private:
    WebServer* server_;
    bool running_;

    // Initialize URI handlers
    void InitHandlers();
    
    // Static handler for car control page
    static esp_err_t HandleCar(httpd_req_t *req);
};

// Initialize motor components
void InitMotorComponents(WebServer* server);

#endif // _MOTOR_CONTENT_H_ 