#include "vehicle_content.h"
#include <esp_log.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include <cstring>
#include <algorithm>
#include <string>

#define TAG "VehicleContent"

// Web路由处理函数
static VehicleContent* g_vehicle_content = nullptr;

VehicleContent::VehicleContent(WebServer* server) 
    : WebContent(server), running_(false), server_(server) {
    g_vehicle_content = this;
}

VehicleContent::~VehicleContent() {
    if (running_) {
        Stop();
    }
    g_vehicle_content = nullptr;
}

bool VehicleContent::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Vehicle content already running");
        return true;
    }
    
    if (!server_) {
        ESP_LOGW(TAG, "WebServer not available, cannot start VehicleContent");
        return false;
    }
    
    // 初始化HTTP处理函数
    InitHandlers();
    
    // 启动超声波数据任务
    xTaskCreate(UltrasonicDataTask, "us_data_task", 4096, server_, 5, nullptr);
    
    // 启动舵机数据任务
    xTaskCreate(ServoDataTask, "servo_data_task", 4096, server_, 5, nullptr);
    
    running_ = true;
    ESP_LOGI(TAG, "Vehicle content started");
    return true;
}

void VehicleContent::Stop() {
    running_ = false;
    ESP_LOGI(TAG, "Vehicle content stopped");
}

bool VehicleContent::IsRunning() const {
    return running_;
}

const char* VehicleContent::GetName() const {
    return "VehicleContent";
}

void VehicleContent::HandleWebSocketMessage(int client_index, const PSRAMString& message) {
    if (!running_) {
        return;
    }

    // 尝试解析为JSON
    cJSON* root = cJSON_Parse(message.c_str());
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse WebSocket message: %s", message.c_str());
        return;
    }

    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!type || !cJSON_IsString(type)) {
        cJSON_Delete(root);
        return;
    }

    // 处理移动控制消息
    if (strcmp(type->valuestring, "car_control") == 0 || 
        strcmp(type->valuestring, "joystick") == 0) {
        auto& thing_manager = iot::ThingManager::GetInstance();
        // 摇杆控制消息只控制电机，不控制舵机
        
        // 提取参数
        cJSON* speed = cJSON_GetObjectItem(root, "speed");
        cJSON* dirX = cJSON_GetObjectItem(root, "dirX");
        cJSON* dirY = cJSON_GetObjectItem(root, "dirY");
        
        if (speed && dirX && dirY && 
            cJSON_IsNumber(speed) && cJSON_IsNumber(dirX) && cJSON_IsNumber(dirY)) {
            
            // 创建给Motor的指令
            cJSON* motor_cmd = cJSON_CreateObject();
            cJSON_AddStringToObject(motor_cmd, "name", "Motor");
            cJSON_AddStringToObject(motor_cmd, "method", "Move");
            
            cJSON* params = cJSON_CreateObject();
            cJSON_AddNumberToObject(params, "dirX", dirX->valuedouble);
            cJSON_AddNumberToObject(params, "dirY", dirY->valuedouble);
            cJSON_AddNumberToObject(params, "distance", speed->valuedouble * 100.0);
            
            cJSON_AddItemToObject(motor_cmd, "parameters", params);
            
            // 调用Motor Thing
            thing_manager.Invoke(motor_cmd);
            cJSON_Delete(motor_cmd);
            
            // 发送成功响应
            if (server_) {
                const char* response = "{\"type\":\"joystick_ack\",\"status\":\"ok\"}";
                server_->SendWebSocketMessage(client_index, response);
            }
        }
    }
    // 处理舵机控制消息
    else if (strcmp(type->valuestring, "servo_control") == 0) {
        auto& thing_manager = iot::ThingManager::GetInstance();
        iot::Thing* servo_thing = thing_manager.FindThingByName("Servo");
        
        if (servo_thing) {
            cJSON* index = cJSON_GetObjectItem(root, "index");
            cJSON* angle = cJSON_GetObjectItem(root, "angle");
            
            if (index && angle && cJSON_IsNumber(index) && cJSON_IsNumber(angle)) {
                cJSON* cmd = cJSON_CreateObject();
                cJSON_AddStringToObject(cmd, "name", "Servo");
                cJSON_AddStringToObject(cmd, "method", "SetAngle");
                
                cJSON* params = cJSON_CreateObject();
                cJSON_AddNumberToObject(params, "index", index->valueint);
                cJSON_AddNumberToObject(params, "angle", angle->valueint);
                
                cJSON_AddItemToObject(cmd, "parameters", params);
                
                thing_manager.Invoke(cmd);
                cJSON_Delete(cmd);
                
                // 发送成功响应
                if (server_) {
                    const char* response = "{\"type\":\"servo_ack\",\"status\":\"ok\"}";
                    server_->SendWebSocketMessage(client_index, response);
                }
            }
        }
    }
    
    cJSON_Delete(root);
}

void VehicleContent::InitHandlers() {
    if (!server_) {
        ESP_LOGW(TAG, "WebServer not available");
        return;
    }
    
    ESP_LOGI(TAG, "Registering Vehicle HTTP handlers");

    // 注册/vehicle端点处理函数
    server_->RegisterHttpHandler("/vehicle", HTTP_GET, 
        [](httpd_req_t* req) { return HandleVehicle(req); });
    
    // 注册/servo端点处理函数
    server_->RegisterHttpHandler("/servo", HTTP_GET, 
        [](httpd_req_t* req) { return HandleServo(req); });

    ESP_LOGI(TAG, "Handlers registered");
    
    // 注册WebSocket消息处理
    server_->RegisterWebSocketHandler("car_control", 
        [](int client_index, const PSRAMString& message, const PSRAMString& type) {
            if (g_vehicle_content) {
                g_vehicle_content->HandleWebSocketMessage(client_index, message);
            }
        });
    
    server_->RegisterWebSocketHandler("joystick", 
        [](int client_index, const PSRAMString& message, const PSRAMString& type) {
            if (g_vehicle_content) {
                g_vehicle_content->HandleWebSocketMessage(client_index, message);
            }
        });
    
    server_->RegisterWebSocketHandler("servo_control", 
        [](int client_index, const PSRAMString& message, const PSRAMString& type) {
            if (g_vehicle_content) {
                g_vehicle_content->HandleWebSocketMessage(client_index, message);
            }
        });
    
    // 已经不需要再注册HTML内容，web_server.cc会自动处理
    ESP_LOGI(TAG, "Vehicle handlers initialized");
}

esp_err_t VehicleContent::HandleVehicle(httpd_req_t *req) {
    ESP_LOGI(TAG, "Vehicle control request received");
    
    char*  buf;
    size_t buf_len;
    
    // 获取URL查询参数
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*)malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[32];
            
            // 解析操作
            if (httpd_query_key_value(buf, "op", param, sizeof(param)) == ESP_OK) {
                auto& thing_manager = iot::ThingManager::GetInstance();
                iot::Thing* motor_thing = thing_manager.FindThingByName("Motor");
                
                if (motor_thing) {
                    cJSON* cmd = cJSON_CreateObject();
                    cJSON_AddStringToObject(cmd, "name", "Motor");
                    
                    if (strcmp(param, "forward") == 0) {
                        cJSON_AddStringToObject(cmd, "method", "Forward");
                        cJSON* params = cJSON_CreateObject();
                        cJSON_AddNumberToObject(params, "speed", 150);
                        cJSON_AddItemToObject(cmd, "parameters", params);
                    } 
                    else if (strcmp(param, "backward") == 0) {
                        cJSON_AddStringToObject(cmd, "method", "Backward");
                        cJSON* params = cJSON_CreateObject();
                        cJSON_AddNumberToObject(params, "speed", 150);
                        cJSON_AddItemToObject(cmd, "parameters", params);
                    } 
                    else if (strcmp(param, "left") == 0) {
                        cJSON_AddStringToObject(cmd, "method", "TurnLeft");
                        cJSON* params = cJSON_CreateObject();
                        cJSON_AddNumberToObject(params, "speed", 150);
                        cJSON_AddItemToObject(cmd, "parameters", params);
                    } 
                    else if (strcmp(param, "right") == 0) {
                        cJSON_AddStringToObject(cmd, "method", "TurnRight");
                        cJSON* params = cJSON_CreateObject();
                        cJSON_AddNumberToObject(params, "speed", 150);
                        cJSON_AddItemToObject(cmd, "parameters", params);
                    } 
                    else if (strcmp(param, "stop") == 0) {
                        cJSON_AddStringToObject(cmd, "method", "Stop");
                        cJSON* params = cJSON_CreateObject();
                        cJSON_AddBoolToObject(params, "brake", true);
                        cJSON_AddItemToObject(cmd, "parameters", params);
                    }
                    
                    thing_manager.Invoke(cmd);
                    cJSON_Delete(cmd);
                }
            }
        }
        free(buf);
    }

    // 发送响应
    const char *resp_str = "{\"status\":\"ok\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, strlen(resp_str));
    
    return ESP_OK;
}

esp_err_t VehicleContent::HandleServo(httpd_req_t *req) {
    ESP_LOGI(TAG, "Servo control request received");
    
    char*  buf;
    size_t buf_len;
    
    // 获取URL查询参数
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*)malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char index_str[8];
            char angle_str[8];
            
            // 解析舵机索引和角度
            if (httpd_query_key_value(buf, "index", index_str, sizeof(index_str)) == ESP_OK &&
                httpd_query_key_value(buf, "angle", angle_str, sizeof(angle_str)) == ESP_OK) {
                
                int index = atoi(index_str);
                int angle = atoi(angle_str);
                
                auto& thing_manager = iot::ThingManager::GetInstance();
                iot::Thing* servo_thing = thing_manager.FindThingByName("Servo");
                
                if (servo_thing) {
                    cJSON* cmd = cJSON_CreateObject();
                    cJSON_AddStringToObject(cmd, "name", "Servo");
                    cJSON_AddStringToObject(cmd, "method", "SetAngle");
                    
                    cJSON* params = cJSON_CreateObject();
                    cJSON_AddNumberToObject(params, "index", index);
                    cJSON_AddNumberToObject(params, "angle", angle);
                    cJSON_AddItemToObject(cmd, "parameters", params);
                    
                    thing_manager.Invoke(cmd);
                    cJSON_Delete(cmd);
                }
            }
        }
        free(buf);
    }

    // 发送响应
    const char *resp_str = "{\"status\":\"ok\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, strlen(resp_str));
    
    return ESP_OK;
}

void VehicleContent::SendUltrasonicData(WebServer* server, iot::Thing* thing) {
    if (!server || !thing) {
        return;
    }
    
    // 创建超声波数据JSON
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "ultrasonic_data");
    
    int front_distance = 0;
    int rear_distance = 0;
    int front_safe_distance = 0;
    int rear_safe_distance = 0;
    
    // 尝试获取超声波参数
    auto& thing_manager = iot::ThingManager::GetInstance();
    iot::Thing* us_thing = thing_manager.FindThingByName("US");
    
    if (us_thing) {
        try {
            // 通过Thing的属性访问机制获取值
            cJSON* state = cJSON_Parse(us_thing->GetStateJson().c_str());
            if (state) {
                cJSON* state_obj = cJSON_GetObjectItem(state, "state");
                if (state_obj) {
                    // 获取前方距离
                    cJSON* front_dist = cJSON_GetObjectItem(state_obj, "front_distance");
                    if (front_dist && cJSON_IsNumber(front_dist)) {
                        front_distance = front_dist->valueint;
                    }
                    
                    // 获取后方距离
                    cJSON* rear_dist = cJSON_GetObjectItem(state_obj, "rear_distance");
                    if (rear_dist && cJSON_IsNumber(rear_dist)) {
                        rear_distance = rear_dist->valueint;
                    }
                    
                    // 获取安全距离
                    cJSON* front_safe = cJSON_GetObjectItem(state_obj, "front_safe_distance");
                    if (front_safe && cJSON_IsNumber(front_safe)) {
                        front_safe_distance = front_safe->valueint;
                    }
                    
                    cJSON* rear_safe = cJSON_GetObjectItem(state_obj, "rear_safe_distance");
                    if (rear_safe && cJSON_IsNumber(rear_safe)) {
                        rear_safe_distance = rear_safe->valueint;
                    }
                }
                cJSON_Delete(state);
            }
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Error retrieving ultrasonic data: %s", e.what());
        }
    }
    
    // 添加超声波数据
    cJSON_AddNumberToObject(root, "front_distance", front_distance);
    cJSON_AddNumberToObject(root, "rear_distance", rear_distance);
    cJSON_AddNumberToObject(root, "front_safe_distance", front_safe_distance);
    cJSON_AddNumberToObject(root, "rear_safe_distance", rear_safe_distance);
    cJSON_AddBoolToObject(root, "front_obstacle_detected", front_distance < front_safe_distance && front_distance > 0);
    cJSON_AddBoolToObject(root, "rear_obstacle_detected", rear_distance < rear_safe_distance && rear_distance > 0);
    
    // 转换为字符串并发送
    char* json_str = cJSON_Print(root);
    if (json_str) {
        server->BroadcastWebSocketMessage(json_str);
        free(json_str);
    }
    
    cJSON_Delete(root);
}

void VehicleContent::SendServoData(WebServer* server, iot::Thing* thing) {
    if (!server || !thing) {
        return;
    }
    
    // 创建舵机状态数据JSON
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "servo_data");
    
    int steering_angle = 90;
    int throttle_position = 90;
    int servo_count = 0;
    
    // 尝试获取舵机参数
    auto& thing_manager = iot::ThingManager::GetInstance();
    iot::Thing* servo_thing = thing_manager.FindThingByName("Servo");
    
    if (servo_thing) {
        try {
            // 通过GetStateJson获取舵机状态
            cJSON* state = cJSON_Parse(servo_thing->GetStateJson().c_str());
            if (state) {
                cJSON* state_obj = cJSON_GetObjectItem(state, "state");
                if (state_obj) {
                    // 获取舵机数量
                    cJSON* count = cJSON_GetObjectItem(state_obj, "servoCount");
                    if (count && cJSON_IsNumber(count)) {
                        servo_count = count->valueint;
                    }
                    
                    // 获取转向舵机角度（通常是索引0）
                    cJSON* servo0 = cJSON_GetObjectItem(state_obj, "servo0Angle");
                    if (servo0 && cJSON_IsNumber(servo0)) {
                        steering_angle = servo0->valueint;
                    }
                    
                    // 获取油门舵机位置（通常是索引1）
                    cJSON* servo1 = cJSON_GetObjectItem(state_obj, "servo1Angle");
                    if (servo1 && cJSON_IsNumber(servo1)) {
                        throttle_position = servo1->valueint;
                    }
                }
                cJSON_Delete(state);
            }
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Error retrieving servo data: %s", e.what());
        }
    }
    
    // 添加舵机数据
    cJSON_AddNumberToObject(root, "steering_angle", steering_angle);
    cJSON_AddNumberToObject(root, "throttle_position", throttle_position);
    cJSON_AddNumberToObject(root, "servo_count", servo_count);
    
    // 转换为字符串并发送
    char* json_str = cJSON_Print(root);
    if (json_str) {
        server->BroadcastWebSocketMessage(json_str);
        free(json_str);
    }
    
    cJSON_Delete(root);
}

void VehicleContent::UltrasonicDataTask(void* pvParameters) {
    WebServer* server = (WebServer*)pvParameters;
    auto& thing_manager = iot::ThingManager::GetInstance();
    
    while (true) {
        // 等待一段时间，避免频繁发送
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // 如果存在US Thing，发送超声波数据
        iot::Thing* us_thing = thing_manager.FindThingByName("US");
        if (us_thing) {
            SendUltrasonicData(server, us_thing);
        }
    }
}

void VehicleContent::ServoDataTask(void* pvParameters) {
    WebServer* server = (WebServer*)pvParameters;
    auto& thing_manager = iot::ThingManager::GetInstance();
    
    while (true) {
        // 等待一段时间，避免频繁发送
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // 如果存在Servo Thing，发送舵机数据
        iot::Thing* servo_thing = thing_manager.FindThingByName("Servo");
        if (servo_thing) {
            SendServoData(server, servo_thing);
        }
    }
}

void InitVehicleComponents(WebServer* server) {
    ESP_LOGI(TAG, "Initializing Vehicle components");
    
    static VehicleContent* content = new VehicleContent(server);
    
    if (!content->Start()) {
        ESP_LOGE(TAG, "Failed to start Vehicle content");
        delete content;
        return;
    }
    
    ESP_LOGI(TAG, "Vehicle components initialized");
} 