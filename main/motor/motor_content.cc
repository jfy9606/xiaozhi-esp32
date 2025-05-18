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
#include <memory>
#include <cstdlib>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_config.h"
#include "components.h"
#include "iot/thing_manager.h"
#include "iot/thing.h"

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
    : server_(server), running_(false), us_task_handle_(nullptr) {
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
    
    // 创建超声波数据传输任务
    xTaskCreate(
        UltrasonicDataTask,
        "us_data_task",
        4096,
        this,
        5,
        &us_task_handle_);

    running_ = true;
    ESP_LOGI(TAG, "Motor content started");
    return true;
}

void MotorContent::Stop() {
    if (!running_) {
        return;
    }
    
    // 停止超声波数据任务
    if (us_task_handle_ != nullptr) {
        vTaskDelete(us_task_handle_);
        us_task_handle_ = nullptr;
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
    // 检查URI是否已经注册，避免重复注册
    if (!server_->IsUriRegistered("/car")) {
        // 使用lambda表达式创建HttpRequestHandler类型
        server_->RegisterHttpHandler("/car", HTTP_GET, 
            [this](httpd_req_t* req) -> esp_err_t {
                return HandleCar(req);
            });
        ESP_LOGI(TAG, "Registered URI handler: /car");
    } else {
        ESP_LOGI(TAG, "URI /car already registered, skipping");
    }
    
    if (!server_->IsUriRegistered("/motor/control")) {
        // 对于不需要this指针的静态函数，可以直接使用lambda来适配
        server_->RegisterHttpHandler("/motor/control", HTTP_POST, 
            [](httpd_req_t* req) -> esp_err_t {
                return HandleMotorControl(req);
            });
        ESP_LOGI(TAG, "Registered URI handler: /motor/control");
    } else {
        ESP_LOGI(TAG, "URI /motor/control already registered, skipping");
    }
    
    if (!server_->IsUriRegistered("/motor/status")) {
        server_->RegisterHttpHandler("/motor/status", HTTP_GET, 
            [](httpd_req_t* req) -> esp_err_t {
                return HandleMotorStatus(req);
            });
        ESP_LOGI(TAG, "Registered URI handler: /motor/status");
    } else {
        ESP_LOGI(TAG, "URI /motor/status already registered, skipping");
    }
    
    // 注册各种WebSocket消息类型的处理器
    // 1. 注册摇杆控制
    server_->RegisterWebSocketHandler("joystick", 
        [this](int client_index, const PSRAMString& message, const PSRAMString& type) {
            HandleWebSocketMessage(client_index, message);
        });
    ESP_LOGI(TAG, "Registered motor WebSocket handler for type: joystick");
    
    // 2. 注册hello消息处理器
    server_->RegisterWebSocketHandler("hello", 
        [this](int client_index, const PSRAMString& message, const PSRAMString& type) {
            HandleWebSocketMessage(client_index, message);
        });
    ESP_LOGI(TAG, "Registered motor WebSocket handler for type: hello");
    
    // 3. 注册ping消息处理器
    server_->RegisterWebSocketHandler("ping", 
        [this](int client_index, const PSRAMString& message, const PSRAMString& type) {
            HandleWebSocketMessage(client_index, message);
        });
    ESP_LOGI(TAG, "Registered motor WebSocket handler for type: ping");
    
    // 4. 注册car_control消息处理器
    server_->RegisterWebSocketHandler("car_control", 
        [this](int client_index, const PSRAMString& message, const PSRAMString& type) {
            HandleWebSocketMessage(client_index, message);
        });
    ESP_LOGI(TAG, "Registered motor WebSocket handler for type: car_control");
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
    
    // 处理hello消息（客户端连接时发送）
    if (strcmp(type, "hello") == 0) {
        ESP_LOGI(TAG, "收到WebSocket hello消息");
        
        // 回复欢迎消息
        PSRAMString response = "{\"type\":\"hello_response\",\"status\":\"ok\",\"message\":\"Welcome to ESP32 Car Control\"}";
        server_->SendWebSocketMessage(client_index, response);
    }
    // 处理ping消息（心跳检测）
    else if (strcmp(type, "ping") == 0) {
        ESP_LOGD(TAG, "收到WebSocket ping消息");
        
        // 回复pong消息
        PSRAMString response = "{\"type\":\"pong\",\"timestamp\":" + std::to_string(esp_timer_get_time() / 1000) + "}";
        server_->SendWebSocketMessage(client_index, response);
    }
        // 处理摇杆命令
    else if (strcmp(type, "joystick") == 0) {
        if (!component) {
            ESP_LOGW(TAG, "Motor controller not available");
            cJSON_Delete(doc);
            return;
        }
        
        MotorController* motor_controller = static_cast<MotorController*>(component);
        
        // 解析摇杆参数
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
        
        // 获取超声波传感器状态，检查是否有障碍物
        bool should_block_movement = false;
        std::string block_reason = "";
        
        // 获取ThingManager实例
        iot::ThingManager& thing_manager = iot::ThingManager::GetInstance();
        
        // 获取所有Thing的状态JSON
        std::string states_json;
        if (thing_manager.GetStatesJson(states_json, false)) {
            // 解析JSON数据
            cJSON* states = cJSON_Parse(states_json.c_str());
            if (states) {
                // 查找超声波传感器数据
                for (int i = 0; i < cJSON_GetArraySize(states); i++) {
                    cJSON* thing_state = cJSON_GetArrayItem(states, i);
                    if (!thing_state) continue;
                    
                    cJSON* name_obj = cJSON_GetObjectItem(thing_state, "name");
                    
                    // 找到超声波传感器
                    if (name_obj && cJSON_IsString(name_obj) && strcmp(name_obj->valuestring, "US") == 0) {
                        cJSON* state = cJSON_GetObjectItem(thing_state, "state");
                        if (state) {
                            // 获取各项数据
                            cJSON* front_obstacle_obj = cJSON_GetObjectItem(state, "front_obstacle_detected");
                            cJSON* rear_obstacle_obj = cJSON_GetObjectItem(state, "rear_obstacle_detected");
                            
                            bool front_obstacle = false;
                            bool rear_obstacle = false;
                            
                            if (front_obstacle_obj && cJSON_IsBool(front_obstacle_obj)) {
                                front_obstacle = cJSON_IsTrue(front_obstacle_obj);
                            }
                            
                            if (rear_obstacle_obj && cJSON_IsBool(rear_obstacle_obj)) {
                                rear_obstacle = cJSON_IsTrue(rear_obstacle_obj);
                            }
                            
                            // 检查移动方向和障碍物情况
                            // 如果向前移动(y < 0)且前方有障碍物，阻止移动
                            if (y < 0 && front_obstacle) {
                                should_block_movement = true;
                                block_reason = "前方有障碍物";
                                ESP_LOGW(TAG, "Blocking forward movement due to obstacle");
                            }
                            // 如果向后移动(y > 0)且后方有障碍物，阻止移动
                            else if (y > 0 && rear_obstacle) {
                                should_block_movement = true;
                                block_reason = "后方有障碍物";
                                ESP_LOGW(TAG, "Blocking backward movement due to obstacle");
                            }
                            
                            break;
                        }
                    }
                }
                cJSON_Delete(states);
            }
        }
        
        // 如果需要阻止移动，发送错误消息
        if (should_block_movement) {
            char response[128];
            snprintf(response, sizeof(response), "{\"type\":\"joystick_ack\",\"status\":\"error\",\"message\":\"%s\"}", 
                     block_reason.c_str());
            server_->SendWebSocketMessage(client_index, response);
        } else {
            // 正常设置电机控制参数
            motor_controller->SetControlParams(distance, x, y);
        
        // 发送确认消息
        server_->SendWebSocketMessage(client_index, "{\"type\":\"joystick_ack\",\"status\":\"ok\"}");
        }
    }
    // 处理car_control消息（直接车辆控制）
    else if (strcmp(type, "car_control") == 0) {
        if (!component) {
            ESP_LOGW(TAG, "Motor controller not available");
            cJSON_Delete(doc);
            return;
        }
        
        MotorController* motor_controller = static_cast<MotorController*>(component);
        
        // 获取速度和方向参数
        cJSON *speed_obj = cJSON_GetObjectItem(doc, "speed");
        cJSON *dirX_obj = cJSON_GetObjectItem(doc, "dirX");
        cJSON *dirY_obj = cJSON_GetObjectItem(doc, "dirY");
        
        if (!speed_obj || !dirX_obj || !dirY_obj) {
            ESP_LOGW(TAG, "Missing car_control parameters");
            cJSON_Delete(doc);
            return;
        }
        
        float speed = speed_obj->valuedouble;
        int dirX = dirX_obj->valueint;
        int dirY = dirY_obj->valueint;
        
        ESP_LOGI(TAG, "Car control: speed=%.2f, dirX=%d, dirY=%d", speed, dirX, dirY);
        
        // 获取超声波传感器状态，检查是否有障碍物
        bool should_block_movement = false;
        std::string block_reason = "";
        
        // 获取ThingManager实例
        iot::ThingManager& thing_manager = iot::ThingManager::GetInstance();
        
        // 获取所有Thing的状态JSON
        std::string states_json;
        if (thing_manager.GetStatesJson(states_json, false)) {
            // 解析JSON数据
            cJSON* states = cJSON_Parse(states_json.c_str());
            if (states) {
                // 查找超声波传感器数据
                for (int i = 0; i < cJSON_GetArraySize(states); i++) {
                    cJSON* thing_state = cJSON_GetArrayItem(states, i);
                    if (!thing_state) continue;
                    
                    cJSON* name_obj = cJSON_GetObjectItem(thing_state, "name");
                    
                    // 找到超声波传感器
                    if (name_obj && cJSON_IsString(name_obj) && strcmp(name_obj->valuestring, "US") == 0) {
                        cJSON* state = cJSON_GetObjectItem(thing_state, "state");
                        if (state) {
                            // 获取各项数据
                            cJSON* front_obstacle_obj = cJSON_GetObjectItem(state, "front_obstacle_detected");
                            cJSON* rear_obstacle_obj = cJSON_GetObjectItem(state, "rear_obstacle_detected");
                            
                            bool front_obstacle = false;
                            bool rear_obstacle = false;
                            
                            if (front_obstacle_obj && cJSON_IsBool(front_obstacle_obj)) {
                                front_obstacle = cJSON_IsTrue(front_obstacle_obj);
                            }
                            
                            if (rear_obstacle_obj && cJSON_IsBool(rear_obstacle_obj)) {
                                rear_obstacle = cJSON_IsTrue(rear_obstacle_obj);
                            }
                            
                            // 检查移动方向和障碍物情况
                            // 如果向前移动(dirY < 0)且前方有障碍物，阻止移动
                            if (dirY < 0 && front_obstacle) {
                                should_block_movement = true;
                                block_reason = "前方有障碍物";
                                ESP_LOGW(TAG, "Blocking forward movement due to obstacle");
                            }
                            // 如果向后移动(dirY > 0)且后方有障碍物，阻止移动
                            else if (dirY > 0 && rear_obstacle) {
                                should_block_movement = true;
                                block_reason = "后方有障碍物";
                                ESP_LOGW(TAG, "Blocking backward movement due to obstacle");
                            }
                            
                            break;
                        }
                    }
                }
                cJSON_Delete(states);
            }
        }
        
        // 如果需要阻止移动，发送错误消息
        if (should_block_movement) {
            char response[128];
            snprintf(response, sizeof(response), "{\"type\":\"car_control_ack\",\"status\":\"error\",\"message\":\"%s\"}", 
                     block_reason.c_str());
            server_->SendWebSocketMessage(client_index, response);
        } else {
            // 正常设置电机控制参数
            motor_controller->SetControlParams(speed, dirX, dirY);
            
            // 发送确认消息
            server_->SendWebSocketMessage(client_index, "{\"type\":\"car_control_ack\",\"status\":\"ok\"}");
        }
    }
    else {
        ESP_LOGW(TAG, "未知的WebSocket消息类型: %s", type);
    }
    
    cJSON_Delete(doc);
}

// 超声波数据传输任务
void MotorContent::UltrasonicDataTask(void* pvParameters) {
    MotorContent* content = static_cast<MotorContent*>(pvParameters);
    WebServer* server = content->server_;
    
    if (!server) {
        ESP_LOGE(TAG, "WebServer指针为空，超声波数据任务退出");
        vTaskDelete(NULL);
        return;
    }
    
    // 获取ThingManager实例
    iot::ThingManager& thing_manager = iot::ThingManager::GetInstance();
    
    // 超声波传感器数据刷新频率 (毫秒)
    const int US_DATA_REFRESH_MS = 200;
    
    // 上次发送数据的时间，用于避免发送重复数据
    int64_t last_send_time = 0;
    float last_front_distance = -1;
    float last_rear_distance = -1;
    bool last_front_obstacle = false;
    bool last_rear_obstacle = false;
    
    ESP_LOGI(TAG, "超声波数据传输任务已启动");
    
    while (content->running_) {
        // 获取当前时间
        int64_t current_time = esp_timer_get_time() / 1000;  // 转换为毫秒
        
        // 获取所有Thing的状态JSON
        std::string states_json;
        bool got_states = thing_manager.GetStatesJson(states_json, false);
        
        if (!got_states || states_json.empty()) {
            ESP_LOGW(TAG, "无法获取Thing状态或状态为空");
            vTaskDelay(pdMS_TO_TICKS(US_DATA_REFRESH_MS));
            continue;
        }
        
        // 解析JSON数据
        cJSON* states = cJSON_Parse(states_json.c_str());
        if (!states) {
            ESP_LOGW(TAG, "解析Thing状态JSON失败");
            vTaskDelay(pdMS_TO_TICKS(US_DATA_REFRESH_MS));
            continue;
        }
        
        // 查找变量保存数据
        float front_distance = -1;
        float rear_distance = -1;
        float front_safe_distance = 10; // 前方默认安全距离
        float rear_safe_distance = 15;  // 后方默认安全距离
        bool front_obstacle = false;
        bool rear_obstacle = false;
        bool found_us = false;
        
        // 遍历所有Thing的状态
        for (int i = 0; i < cJSON_GetArraySize(states); i++) {
            cJSON* thing_state = cJSON_GetArrayItem(states, i);
            if (!thing_state) continue;
            
            cJSON* name_obj = cJSON_GetObjectItem(thing_state, "name");
            
            // 找到超声波传感器
            if (name_obj && cJSON_IsString(name_obj) && strcmp(name_obj->valuestring, "US") == 0) {
                // 获取state对象
                cJSON* state = cJSON_GetObjectItem(thing_state, "state");
                if (state) {
                    found_us = true;
                    
                    // 获取各项数据
                    cJSON* front_distance_obj = cJSON_GetObjectItem(state, "front_distance");
                    if (front_distance_obj && cJSON_IsNumber(front_distance_obj)) {
                        front_distance = front_distance_obj->valuedouble;
                    }
                    
                    cJSON* rear_distance_obj = cJSON_GetObjectItem(state, "rear_distance");
                    if (rear_distance_obj && cJSON_IsNumber(rear_distance_obj)) {
                        rear_distance = rear_distance_obj->valuedouble;
                    }
                    
                    // 获取前方安全距离
                    cJSON* front_safe_distance_obj = cJSON_GetObjectItem(state, "front_safe_distance");
                    if (front_safe_distance_obj && cJSON_IsNumber(front_safe_distance_obj)) {
                        front_safe_distance = front_safe_distance_obj->valuedouble;
                    }
                    
                    // 获取后方安全距离
                    cJSON* rear_safe_distance_obj = cJSON_GetObjectItem(state, "rear_safe_distance");
                    if (rear_safe_distance_obj && cJSON_IsNumber(rear_safe_distance_obj)) {
                        rear_safe_distance = rear_safe_distance_obj->valuedouble;
                    }
                    
                    // 根据安全距离判断是否存在障碍物
                    if (front_distance >= 0 && front_distance < front_safe_distance) {
                        front_obstacle = true;
                    } else {
                        front_obstacle = false;
                    }
                    
                    if (rear_distance >= 0 && rear_distance < rear_safe_distance) {
                        rear_obstacle = true;
                    } else {
                        rear_obstacle = false;
                    }
                    
                    break;
                }
            }
        }
        
        // 检查是否需要发送更新
        
        // 如果找到超声波传感器数据
        if (found_us) {
            // 检查数据是否有变化或者距离上次发送已经过去了较长时间
            if (front_distance != last_front_distance || 
                rear_distance != last_rear_distance || 
                front_obstacle != last_front_obstacle || 
                rear_obstacle != last_rear_obstacle || 
                (current_time - last_send_time) >= 1000) { // 至少每秒发送一次，即使没有变化
                
                // 创建要发送的超声波数据JSON
                cJSON* us_data = cJSON_CreateObject();
                cJSON_AddStringToObject(us_data, "type", "ultrasonic_data");
                
                // 添加数据到发送对象
                cJSON_AddNumberToObject(us_data, "front_distance", front_distance >= 0 ? front_distance : 0);
                cJSON_AddNumberToObject(us_data, "rear_distance", rear_distance >= 0 ? rear_distance : 0);
                cJSON_AddNumberToObject(us_data, "front_safe_distance", front_safe_distance);
                cJSON_AddNumberToObject(us_data, "rear_safe_distance", rear_safe_distance);
                cJSON_AddBoolToObject(us_data, "front_obstacle", front_obstacle);
                cJSON_AddBoolToObject(us_data, "rear_obstacle", rear_obstacle);
                
                // 将JSON转换为字符串并发送
                char* json_str = cJSON_PrintUnformatted(us_data);
                if (json_str) {
                    // 使用WebSocket广播消息
                    if (server->IsRunning() && server->GetActiveWebSocketClientCount() > 0) {
                        server->BroadcastWebSocketMessage(json_str);
                        
                        // 记录日志，但为避免日志过多，只在首次或数据变化较大时记录
                        if (last_front_distance < 0 || last_rear_distance < 0 || 
                            std::abs(front_distance - last_front_distance) > 5 || 
                            std::abs(rear_distance - last_rear_distance) > 5 || 
                            front_obstacle != last_front_obstacle || 
                            rear_obstacle != last_rear_obstacle) {
                            ESP_LOGI(TAG, "发送超声波数据: 前方=%.1fcm, 后方=%.1fcm, 前方安全距离=%.1fcm, 后方安全距离=%.1fcm", 
                                   front_distance, rear_distance, front_safe_distance, rear_safe_distance);
                        }
                        
                        // 更新上次发送时间和数据
                        last_send_time = current_time;
                        last_front_distance = front_distance;
                        last_rear_distance = rear_distance;
                        last_front_obstacle = front_obstacle;
                        last_rear_obstacle = rear_obstacle;
                    }
                    free(json_str);
                }
                
                cJSON_Delete(us_data);
            }
        } else {
            ESP_LOGD(TAG, "未找到超声波传感器Thing");
        }
        
        cJSON_Delete(states);
        
        // 等待下次刷新
        vTaskDelay(pdMS_TO_TICKS(US_DATA_REFRESH_MS));
    }
    
    ESP_LOGI(TAG, "超声波数据传输任务已结束");
    vTaskDelete(NULL);
}

// 初始化电机组件
void InitMotorComponents(WebServer* server) {
#if defined(CONFIG_ENABLE_MOTOR_CONTROLLER)
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
#else
    ESP_LOGI(TAG, "Motor controller disabled in config (CONFIG_ENABLE_MOTOR_CONTROLLER not defined)");
#endif
}

void MotorContent::SendUltrasonicData(WebServer* server, iot::Thing* thing) {
    if (!thing) {
        ESP_LOGW(TAG, "US (ultrasonic) thing not available");
        return;
    }
    
    // 直接从thing中获取状态JSON
    std::string state_json = thing->GetStateJson();
    if (state_json.empty()) {
        ESP_LOGW(TAG, "Failed to get ultrasonic state json");
        return;
    }
    
    float front_distance = 0.0;
    float rear_distance = 0.0;
    bool front_obstacle_detected = false;
    bool rear_obstacle_detected = false;
    float front_safe_distance = 0.0;
    float rear_safe_distance = 0.0;
    
    // 解析JSON中的属性
    cJSON* state = cJSON_Parse(state_json.c_str());
    if (state) {
        cJSON* front_distance_obj = cJSON_GetObjectItem(state, "front_distance");
        if (front_distance_obj && cJSON_IsNumber(front_distance_obj)) {
            front_distance = front_distance_obj->valuedouble;
        }
        
        cJSON* rear_distance_obj = cJSON_GetObjectItem(state, "rear_distance");
        if (rear_distance_obj && cJSON_IsNumber(rear_distance_obj)) {
            rear_distance = rear_distance_obj->valuedouble;
        }
        
        cJSON* front_obstacle_obj = cJSON_GetObjectItem(state, "front_obstacle_detected");
        if (front_obstacle_obj && cJSON_IsBool(front_obstacle_obj)) {
            front_obstacle_detected = cJSON_IsTrue(front_obstacle_obj);
        }
        
        cJSON* rear_obstacle_obj = cJSON_GetObjectItem(state, "rear_obstacle_detected");
        if (rear_obstacle_obj && cJSON_IsBool(rear_obstacle_obj)) {
            rear_obstacle_detected = cJSON_IsTrue(rear_obstacle_obj);
        }
        
        cJSON* front_safe_distance_obj = cJSON_GetObjectItem(state, "front_safe_distance");
        if (front_safe_distance_obj && cJSON_IsNumber(front_safe_distance_obj)) {
            front_safe_distance = front_safe_distance_obj->valuedouble;
        }
        
        cJSON* rear_safe_distance_obj = cJSON_GetObjectItem(state, "rear_safe_distance");
        if (rear_safe_distance_obj && cJSON_IsNumber(rear_safe_distance_obj)) {
            rear_safe_distance = rear_safe_distance_obj->valuedouble;
        }
        
        cJSON_Delete(state);
    } else {
        ESP_LOGW(TAG, "Failed to parse ultrasonic state json");
    }
    
    // 创建发送的JSON消息
    cJSON* ultrasonicDoc = cJSON_CreateObject();
    cJSON_AddStringToObject(ultrasonicDoc, "type", "ultrasonic_data");
    cJSON_AddNumberToObject(ultrasonicDoc, "front_distance", front_distance);
    cJSON_AddNumberToObject(ultrasonicDoc, "rear_distance", rear_distance);
    cJSON_AddBoolToObject(ultrasonicDoc, "front_obstacle_detected", front_obstacle_detected);
    cJSON_AddBoolToObject(ultrasonicDoc, "rear_obstacle_detected", rear_obstacle_detected);
    cJSON_AddNumberToObject(ultrasonicDoc, "front_safe_distance", front_safe_distance);
    cJSON_AddNumberToObject(ultrasonicDoc, "rear_safe_distance", rear_safe_distance);
    
    char* json_str = cJSON_PrintUnformatted(ultrasonicDoc);
    if (json_str) {
        // 发送到WebSocket客户端，使用BroadcastWebSocketMessage代替SendWebSocketMessageToAll
        if (server->IsRunning() && server->GetActiveWebSocketClientCount() > 0) {
            server->BroadcastWebSocketMessage(json_str);
        }
        
        free(json_str);
    }
    
    cJSON_Delete(ultrasonicDoc);
}