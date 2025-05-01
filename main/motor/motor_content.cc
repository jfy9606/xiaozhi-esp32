#include "motor_content.h"
#include <esp_log.h>
#include <ArduinoJson.h>
#include <cmath>
#include "sdkconfig.h"

#define TAG "MotorContent"

// 声明外部HTML内容获取函数（在html_content.cc中定义）
extern const char* CAR_HTML;
extern size_t get_car_html_size();

MotorContent::MotorContent(WebServer* server, MotorController* motor_controller)
    : server_(server), motor_controller_(motor_controller), running_(false) {
}

MotorContent::~MotorContent() {
    Stop();
}

bool MotorContent::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Motor content already running");
        return true;
    }

    if (!server_ || !server_->IsRunning()) {
        ESP_LOGE(TAG, "Web server not running, cannot start motor content");
        return false;
    }

    if (!motor_controller_ || !motor_controller_->IsRunning()) {
        ESP_LOGE(TAG, "Motor controller not running, cannot start motor content");
        return false;
    }

    InitHandlers();
    
    running_ = true;
    ESP_LOGI(TAG, "Motor content started");
    return true;
}

void MotorContent::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    ESP_LOGI(TAG, "Motor content stopped");
}

bool MotorContent::IsRunning() const {
    return running_;
}

const char* MotorContent::GetName() const {
    return "MotorContent";
}

void MotorContent::InitHandlers() {
    // 注册URI处理函数
    server_->RegisterUri("/car", HTTP_GET, HandleCar, this);
    server_->RegisterUri("/motor/command", HTTP_POST, HandleCommand, this);
    server_->RegisterUri("/motor/status", HTTP_GET, HandleStatus, this);
    
    // 注册WebSocket处理
    server_->RegisterWebSocket("/ws", [this](int client_index, const std::string& message) {
        HandleWebSocketMessage(client_index, message);
    });
}

esp_err_t MotorContent::HandleCar(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, CAR_HTML, get_car_html_size());
    return ESP_OK;
}

esp_err_t MotorContent::HandleCommand(httpd_req_t *req) {
    MotorContent* content = static_cast<MotorContent*>(req->user_ctx);
    if (!content || !content->motor_controller_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Motor controller not available");
        return ESP_FAIL;
    }

    char buf[256];
    int ret, remaining = req->content_len;
    
    if (remaining > sizeof(buf) - 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large");
        return ESP_FAIL;
    }
    
    int received = 0;
    while (remaining > 0) {
        ret = httpd_req_recv(req, buf + received, remaining);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        received += ret;
        remaining -= ret;
    }
    buf[received] = '\0';
    
    // 解析JSON
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, buf);
    if (error) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    // 获取命令类型
    const char* cmd = doc["cmd"];
    if (!cmd) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing command");
        return ESP_FAIL;
    }
    
    // 处理不同类型的命令
    if (strcmp(cmd, "joystick") == 0) {
        // 处理摇杆命令
        int x = doc["x"];
        int y = doc["y"];
        float distance = doc["distance"];
        
        content->motor_controller_->SetControlParams(distance, x, y);
        
        // 返回成功响应
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"ok\"}", -1);
    }
    else if (strcmp(cmd, "move") == 0) {
        // 处理移动命令
        const char* direction = doc["direction"];
        int speed = doc["speed"] | DEFAULT_SPEED;
        
        if (!direction) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing direction");
            return ESP_FAIL;
        }
        
        if (strcmp(direction, "forward") == 0) {
            content->motor_controller_->Forward(speed);
        }
        else if (strcmp(direction, "backward") == 0) {
            content->motor_controller_->Backward(speed);
        }
        else if (strcmp(direction, "left") == 0) {
            content->motor_controller_->TurnLeft(speed);
        }
        else if (strcmp(direction, "right") == 0) {
            content->motor_controller_->TurnRight(speed);
        }
        else if (strcmp(direction, "stop") == 0) {
            bool brake = doc["brake"] | false;
            content->motor_controller_->Stop(brake);
        }
        else {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid direction");
            return ESP_FAIL;
        }
        
        // 返回成功响应
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"ok\"}", -1);
    }
    else if (strcmp(cmd, "speed") == 0) {
        // 处理速度命令
        int speed = doc["speed"];
        if (speed < MIN_SPEED || speed > MAX_SPEED) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, 
                               "Speed must be between MIN_SPEED and MAX_SPEED");
            return ESP_FAIL;
        }
        
        content->motor_controller_->SetSpeed(speed);
        
        // 返回成功响应
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"ok\"}", -1);
    }
    else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown command");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t MotorContent::HandleStatus(httpd_req_t *req) {
    MotorContent* content = static_cast<MotorContent*>(req->user_ctx);
    if (!content || !content->motor_controller_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Motor controller not available");
        return ESP_FAIL;
    }
    
    MotorController* motor = content->motor_controller_;
    
    // 创建JSON响应
    DynamicJsonDocument doc(256);
    doc["running"] = motor->IsRunning();
    doc["speed"] = motor->GetCurrentSpeed();
    doc["dirX"] = motor->GetDirectionX();
    doc["dirY"] = motor->GetDirectionY();
    
    String response;
    serializeJson(doc, response);
    
    // 返回响应
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response.c_str(), response.length());
    
    return ESP_OK;
}

void MotorContent::HandleWebSocketMessage(int client_index, const std::string& message) {
    if (!motor_controller_) {
        ESP_LOGW(TAG, "Motor controller not available");
        return;
    }
    
    // 解析JSON消息
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, message.c_str());
    if (error) {
        ESP_LOGW(TAG, "Invalid JSON in WebSocket message: %s", error.c_str());
        return;
    }
    
    // 处理不同类型的消息
    const char* type = doc["type"];
    if (!type) {
        ESP_LOGW(TAG, "Missing message type");
        return;
    }
    
    if (strcmp(type, "joystick") == 0) {
        // 处理摇杆命令
        int x = doc["x"];
        int y = doc["y"];
        float distance = doc["distance"];
        
        motor_controller_->SetControlParams(distance, x, y);
        
        // 发送确认消息
        String response = "{\"type\":\"joystick_ack\",\"status\":\"ok\"}";
        server_->SendWebSocketMessage(client_index, response.c_str());
    }
    else if (strcmp(type, "motor_command") == 0) {
        // 处理电机命令
        const char* cmd = doc["cmd"];
        
        if (!cmd) {
            ESP_LOGW(TAG, "Missing motor command");
            return;
        }
        
        if (strcmp(cmd, "forward") == 0) {
            int speed = doc["speed"] | DEFAULT_SPEED;
            motor_controller_->Forward(speed);
        }
        else if (strcmp(cmd, "backward") == 0) {
            int speed = doc["speed"] | DEFAULT_SPEED;
            motor_controller_->Backward(speed);
        }
        else if (strcmp(cmd, "left") == 0) {
            int speed = doc["speed"] | DEFAULT_SPEED;
            motor_controller_->TurnLeft(speed);
        }
        else if (strcmp(cmd, "right") == 0) {
            int speed = doc["speed"] | DEFAULT_SPEED;
            motor_controller_->TurnRight(speed);
        }
        else if (strcmp(cmd, "stop") == 0) {
            bool brake = doc["brake"] | false;
            motor_controller_->Stop(brake);
        }
        
        // 发送确认消息
        String response = "{\"type\":\"motor_ack\",\"status\":\"ok\"}";
        server_->SendWebSocketMessage(client_index, response.c_str());
    }
    else if (strcmp(type, "status_request") == 0) {
        // 发送状态信息
        DynamicJsonDocument statusDoc(256);
        statusDoc["type"] = "motor_status";
        statusDoc["running"] = motor_controller_->IsRunning();
        statusDoc["speed"] = motor_controller_->GetCurrentSpeed();
        statusDoc["dirX"] = motor_controller_->GetDirectionX();
        statusDoc["dirY"] = motor_controller_->GetDirectionY();
        
        String response;
        serializeJson(statusDoc, response);
        
        server_->SendWebSocketMessage(client_index, response.c_str());
    }
}

// 初始化电机组件
void InitMotorComponents(WebServer* web_server) {
#if defined(CONFIG_ENABLE_MOTOR_CONTROLLER)
    // 创建电机控制器组件 - 使用默认参数，引脚定义在motor_controller.cc中
    MotorController* motor_controller = new MotorController();
    ComponentManager::GetInstance().RegisterComponent(motor_controller);
    
    // 创建电机内容处理组件
    MotorContent* motor_content = new MotorContent(web_server, motor_controller);
    ComponentManager::GetInstance().RegisterComponent(motor_content);
    
    ESP_LOGI(TAG, "Motor components initialized");
#else
    ESP_LOGI(TAG, "Motor controller disabled in configuration");
#endif
} 