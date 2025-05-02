#include "motor_content.h"
#include <esp_log.h>
#include <cJSON.h>
#include <cstring>
#include <cmath>
#include "sdkconfig.h"
#include "motor_controller.h"
#include "web/web_server.h"
#if CONFIG_ENABLE_WEB_CONTENT
#include "web/html_content.h"
#endif

#define TAG "MotorContent"

// 使用html_content.h中定义的HTML内容函数

// Handle motor control requests
static esp_err_t HandleMotorControl(httpd_req_t* req) {
    // Read the request content
    char content[100];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[ret] = '\0';

    // Parse the request
    int dirX = 0, dirY = 0;
    float distance = 0.0;
    
    char *token = strtok(content, "&");
    while (token != NULL) {
        if (strstr(token, "dirX=") == token) {
            dirX = atoi(token + 5);
        } else if (strstr(token, "dirY=") == token) {
            dirY = atoi(token + 5);
        } else if (strstr(token, "distance=") == token) {
            distance = atof(token + 9);
        }
        token = strtok(NULL, "&");
    }

    // Get the motor controller
    auto& manager = ComponentManager::GetInstance();
    Component* component = manager.GetComponent("MotorController");
    if (!component) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    MotorController* motor = static_cast<MotorController*>(component);
    motor->SetControlParams(distance, dirX, dirY);

    // Send response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", -1);
    
    return ESP_OK;
}

// Motor status endpoint to get the current state of the motor
static esp_err_t HandleMotorStatus(httpd_req_t* req) {
    auto& manager = ComponentManager::GetInstance();
    Component* component = manager.GetComponent("MotorController");
    
    char response[100];
    if (component && component->IsRunning()) {
        snprintf(response, sizeof(response), 
            "{\"status\":\"ok\",\"running\":true}");
    } else {
        snprintf(response, sizeof(response), 
            "{\"status\":\"ok\",\"running\":false}");
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, -1);
    
    return ESP_OK;
}

MotorContent::MotorContent(WebServer* server)
    : server_(server), running_(false) {
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

    // Initialize handlers when starting
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
    // 检查URI是否已注册，避免重复注册
    if (!server_->IsUriRegistered("/car")) {
        server_->RegisterUri("/car", HTTP_GET, HandleCar, this);
        ESP_LOGI(TAG, "Registered URI handler: /car");
    } else {
        ESP_LOGW(TAG, "URI already registered, skipping: /car");
    }
    
    if (!server_->IsUriRegistered("/motor/control")) {
        server_->RegisterUri("/motor/control", HTTP_POST, HandleMotorControl);
        ESP_LOGI(TAG, "Registered URI handler: /motor/control");
    } else {
        ESP_LOGW(TAG, "URI already registered, skipping: /motor/control");
    }
    
    if (!server_->IsUriRegistered("/motor/status")) {
        server_->RegisterUri("/motor/status", HTTP_GET, HandleMotorStatus);
        ESP_LOGI(TAG, "Registered URI handler: /motor/status");
    } else {
        ESP_LOGW(TAG, "URI already registered, skipping: /motor/status");
    }
    
    // WebSocket可能已被其他组件注册，检查后再注册
    if (!server_->IsUriRegistered("/ws")) {
        // 使用匹配WebSocketMessageCallback参数类型的lambda
        server_->RegisterWebSocket("/ws", [this](int client_index, const PSRAMString& message) {
            HandleWebSocketMessage(client_index, message);
        });
        ESP_LOGI(TAG, "Registered WebSocket handler: /ws");
    } else {
        ESP_LOGW(TAG, "WebSocket handler already registered, skipping: /ws");
    }
}

esp_err_t MotorContent::HandleCar(httpd_req_t *req) {
#if CONFIG_ENABLE_WEB_CONTENT
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, MOTOR_HTML, get_motor_html_size());
    return ESP_OK;
#else
    // 当Web内容未启用时，返回一个简单的消息
    const char* message = "<html><body><h1>Motor Content Disabled</h1><p>The web content feature is not enabled in this build.</p></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, message, strlen(message));
    return ESP_OK;
#endif
}

void MotorContent::HandleWebSocketMessage(int client_index, const PSRAMString& message) {
    // Get the motor controller from ComponentManager
    auto& manager = ComponentManager::GetInstance();
    Component* component = manager.GetComponent("MotorController");
    if (!component) {
        ESP_LOGW(TAG, "Motor controller not available");
        return;
    }
    
    MotorController* motor_controller = static_cast<MotorController*>(component);
    
    // 解析JSON消息
    cJSON *doc = cJSON_Parse(message.c_str());
    if (!doc) {
        ESP_LOGW(TAG, "Invalid JSON in WebSocket message");
        return;
    }
    
    // 处理不同类型的消息
    cJSON *type_obj = cJSON_GetObjectItem(doc, "type");
    if (!type_obj || !type_obj->valuestring) {
        ESP_LOGW(TAG, "Missing message type");
        cJSON_Delete(doc);
        return;
    }
    
    const char* type = type_obj->valuestring;
    
    if (strcmp(type, "joystick") == 0) {
        // 处理摇杆命令
        cJSON *x_obj = cJSON_GetObjectItem(doc, "x");
        cJSON *y_obj = cJSON_GetObjectItem(doc, "y");
        cJSON *distance_obj = cJSON_GetObjectItem(doc, "distance");
        
        if (!x_obj || !y_obj || !distance_obj) {
            ESP_LOGW(TAG, "Missing joystick parameters");
            cJSON_Delete(doc);
            return;
        }
        
        int x = x_obj->valueint;
        int y = y_obj->valueint;
        float distance = distance_obj->valuedouble;
        
        motor_controller->SetControlParams(distance, x, y);
        
        // 发送确认消息
        server_->SendWebSocketMessage(client_index, "{\"type\":\"joystick_ack\",\"status\":\"ok\"}");
    }
    
    cJSON_Delete(doc);
}

// 初始化电机组件
void InitMotorComponents(WebServer* server) {
    ESP_LOGI(TAG, "Initializing motor components");
    
    auto& manager = ComponentManager::GetInstance();
    
    // 检查MotorController是否已经存在
    MotorController* motor_controller = nullptr;
    Component* existing_controller = manager.GetComponent("MotorController");
    if (existing_controller) {
        ESP_LOGI(TAG, "MotorController already exists, using existing instance");
        motor_controller = static_cast<MotorController*>(existing_controller);
    } else {
        // 创建新的电机控制器组件
        motor_controller = new MotorController();
        manager.RegisterComponent(motor_controller);
        ESP_LOGI(TAG, "Created new MotorController instance");
    }
    
    // 检查MotorContent是否已经存在
    if (manager.GetComponent("MotorContent")) {
        ESP_LOGI(TAG, "MotorContent already exists, skipping creation");
    } else {
        // 创建电机内容处理组件
        MotorContent* motor_content = new MotorContent(server);
        manager.RegisterComponent(motor_content);
        ESP_LOGI(TAG, "Created new MotorContent instance");
    }
    
    ESP_LOGI(TAG, "Motor components initialized");
}