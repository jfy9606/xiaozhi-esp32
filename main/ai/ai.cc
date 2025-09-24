#include "ai.h"
#include <esp_log.h>
#include <cJSON.h>
#include <string>
#include <memory>

#define TAG "AI"

// 构造函数
AI::AI(Web* web_server)
    : web_server_(web_server), 
      hardware_manager_(nullptr),
      running_(false),
      listening_(false),
      sensor_push_active_(false),
      sensor_push_interval_(1000) {
    ESP_LOGI(TAG, "AI component created");
}

// 析构函数
AI::~AI() {
    if (running_) {
        Stop();
    }
    
    ESP_LOGI(TAG, "AI component destroyed");
}

// Component接口实现
bool AI::Start() {
    if (running_) {
        ESP_LOGW(TAG, "AI already running");
        return true;
    }
    
    ESP_LOGI(TAG, "Starting AI component");
    
    // 如果Web服务器可用，初始化处理器
    if (web_server_ && web_server_->IsRunning()) {
        InitHandlers();
    }
    
    running_ = true;
    return true;
}

void AI::Stop() {
    if (!running_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping AI component");
    
    // 停止语音识别
    if (listening_) {
        StopVoiceRecognition();
    }
    
    // 停止传感器数据推送
    if (sensor_push_active_) {
        StopSensorDataPush();
    }
    
    running_ = false;
}

bool AI::IsRunning() const {
    return running_;
}

const char* AI::GetName() const {
    return "AI";
}

// AI功能方法
std::string AI::ProcessSpeechQuery(const std::string& speech_input) {
    ESP_LOGI(TAG, "Processing speech query: %s", speech_input.c_str());
    
    // 这里应该实现实际的语音处理逻辑
    // 目前返回一个模拟响应
    return "I heard you say: " + speech_input;
}

std::string AI::GenerateTextResponse(const std::string& query) {
    ESP_LOGI(TAG, "Generating AI response for: %s", query.c_str());
    
    // 这里应该实现实际的AI对话逻辑
    // 目前返回一个模拟响应
    return "AI response to: " + query;
}

// 语音功能
bool AI::StartVoiceRecognition() {
    if (!running_) {
        ESP_LOGW(TAG, "AI component not running");
        return false;
    }
    
    if (listening_) {
        ESP_LOGW(TAG, "Voice recognition already running");
        return true;
    }
    
    ESP_LOGI(TAG, "Starting voice recognition");
    
    // 这里应该实现启动语音识别的逻辑
    
    listening_ = true;
    return true;
}

void AI::StopVoiceRecognition() {
    if (!listening_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping voice recognition");
    
    // 这里应该实现停止语音识别的逻辑
    
    listening_ = false;
}

bool AI::IsListening() const {
    return listening_;
}

// 设置回调
void AI::SetSpeechRecognitionCallback(SpeechRecognitionCallback callback) {
    speech_callback_ = callback;
}

// 硬件管理器接口
void AI::SetHardwareManager(HardwareManager* hardware_manager) {
    hardware_manager_ = hardware_manager;
    ESP_LOGI(TAG, "Hardware manager set: %s", hardware_manager ? "enabled" : "disabled");
}

std::string AI::GetSensorDataJson() {
    if (!hardware_manager_ || !hardware_manager_->IsInitialized()) {
        ESP_LOGW(TAG, "Hardware manager not available");
        return "{\"success\":false,\"error\":\"Hardware manager not available\"}";
    }
    
    ESP_LOGI(TAG, "Getting sensor data for AI");
    
    // 读取所有传感器数据
    std::vector<sensor_reading_t> readings = hardware_manager_->ReadAllSensors();
    
    // 构建JSON响应
    cJSON* root = cJSON_CreateObject();
    cJSON* success = cJSON_CreateBool(true);
    cJSON* timestamp = cJSON_CreateNumber(esp_timer_get_time() / 1000);
    cJSON* sensors = cJSON_CreateArray();
    
    cJSON_AddItemToObject(root, "success", success);
    cJSON_AddItemToObject(root, "timestamp", timestamp);
    cJSON_AddItemToObject(root, "sensors", sensors);
    
    // 添加传感器数据
    for (const auto& reading : readings) {
        cJSON* sensor = cJSON_CreateObject();
        cJSON_AddStringToObject(sensor, "id", reading.sensor_id.c_str());
        cJSON_AddStringToObject(sensor, "name", reading.name.c_str());
        cJSON_AddStringToObject(sensor, "type", reading.type.c_str());
        cJSON_AddNumberToObject(sensor, "value", reading.value);
        cJSON_AddStringToObject(sensor, "unit", reading.unit.c_str());
        cJSON_AddNumberToObject(sensor, "timestamp", reading.timestamp);
        cJSON_AddBoolToObject(sensor, "valid", reading.valid);
        
        cJSON_AddItemToArray(sensors, sensor);
    }
    
    char* json_string = cJSON_Print(root);
    std::string result(json_string);
    
    free(json_string);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Sensor data JSON generated (%zu sensors)", readings.size());
    return result;
}

bool AI::ExecuteHardwareCommand(const std::string& command_json) {
    if (!hardware_manager_ || !hardware_manager_->IsInitialized()) {
        ESP_LOGW(TAG, "Hardware manager not available");
        return false;
    }
    
    ESP_LOGI(TAG, "Executing hardware command: %s", command_json.c_str());
    
    // 解析JSON命令
    cJSON* root = cJSON_Parse(command_json.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse hardware command JSON");
        return false;
    }
    
    bool success = false;
    
    // 获取命令类型
    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && type->valuestring) {
        std::string cmd_type = type->valuestring;
        
        if (cmd_type == "motor") {
            // 电机控制命令
            cJSON* motor_id = cJSON_GetObjectItem(root, "motor_id");
            cJSON* speed = cJSON_GetObjectItem(root, "speed");
            
            if (cJSON_IsNumber(motor_id) && cJSON_IsNumber(speed)) {
                int id = motor_id->valueint;
                int spd = speed->valueint;
                success = ExecuteMotorCommand(id, spd);
            }
        }
        else if (cmd_type == "servo") {
            // 舵机控制命令
            cJSON* servo_id = cJSON_GetObjectItem(root, "servo_id");
            cJSON* angle = cJSON_GetObjectItem(root, "angle");
            
            if (cJSON_IsNumber(servo_id) && cJSON_IsNumber(angle)) {
                int id = servo_id->valueint;
                int ang = angle->valueint;
                success = ExecuteServoCommand(id, ang);
            }
        }
        else if (cmd_type == "stop_motor") {
            // 停止电机命令
            cJSON* motor_id = cJSON_GetObjectItem(root, "motor_id");
            
            if (cJSON_IsNumber(motor_id)) {
                int id = motor_id->valueint;
                esp_err_t result = hardware_manager_->StopMotor(id);
                success = (result == ESP_OK);
                RecordControlHistory("stop_motor", id, 0, success);
                ESP_LOGI(TAG, "Motor %d stopped: %s", id, success ? "success" : "failed");
            }
        }
        else if (cmd_type == "stop_all_motors") {
            // 停止所有电机命令
            esp_err_t result = hardware_manager_->StopAllMotors();
            success = (result == ESP_OK);
            RecordControlHistory("stop_all_motors", -1, 0, success);
            ESP_LOGI(TAG, "All motors stopped: %s", success ? "success" : "failed");
        }
        else if (cmd_type == "center_servo") {
            // 舵机居中命令
            cJSON* servo_id = cJSON_GetObjectItem(root, "servo_id");
            
            if (cJSON_IsNumber(servo_id)) {
                int id = servo_id->valueint;
                esp_err_t result = hardware_manager_->CenterServo(id);
                success = (result == ESP_OK);
                RecordControlHistory("center_servo", id, 90, success);  // 假设居中角度为90度
                ESP_LOGI(TAG, "Servo %d centered: %s", id, success ? "success" : "failed");
            }
        }
        else {
            ESP_LOGW(TAG, "Unknown hardware command type: %s", cmd_type.c_str());
        }
    }
    
    cJSON_Delete(root);
    return success;
}

std::string AI::GetFilteredSensorDataJson(const std::vector<std::string>& sensor_ids) {
    if (!hardware_manager_ || !hardware_manager_->IsInitialized()) {
        ESP_LOGW(TAG, "Hardware manager not available");
        return "{\"success\":false,\"error\":\"Hardware manager not available\"}";
    }
    
    ESP_LOGI(TAG, "Getting filtered sensor data for AI (%zu sensors)", sensor_ids.size());
    
    // 构建JSON响应
    cJSON* root = cJSON_CreateObject();
    cJSON* success = cJSON_CreateBool(true);
    cJSON* timestamp = cJSON_CreateNumber(esp_timer_get_time() / 1000);
    cJSON* sensors = cJSON_CreateArray();
    
    cJSON_AddItemToObject(root, "success", success);
    cJSON_AddItemToObject(root, "timestamp", timestamp);
    cJSON_AddItemToObject(root, "sensors", sensors);
    
    // 读取指定的传感器数据
    for (const std::string& sensor_id : sensor_ids) {
        sensor_reading_t reading = hardware_manager_->ReadSensor(sensor_id);
        
        if (!reading.sensor_id.empty()) {  // 传感器存在
            cJSON* sensor = cJSON_CreateObject();
            cJSON_AddStringToObject(sensor, "id", reading.sensor_id.c_str());
            cJSON_AddStringToObject(sensor, "name", reading.name.c_str());
            cJSON_AddStringToObject(sensor, "type", reading.type.c_str());
            cJSON_AddNumberToObject(sensor, "value", reading.value);
            cJSON_AddStringToObject(sensor, "unit", reading.unit.c_str());
            cJSON_AddNumberToObject(sensor, "timestamp", reading.timestamp);
            cJSON_AddBoolToObject(sensor, "valid", reading.valid);
            
            cJSON_AddItemToArray(sensors, sensor);
        } else {
            ESP_LOGW(TAG, "Sensor not found: %s", sensor_id.c_str());
        }
    }
    
    char* json_string = cJSON_Print(root);
    std::string result(json_string);
    
    free(json_string);
    cJSON_Delete(root);
    
    return result;
}

void AI::RegisterSensorDataCallback(SensorDataCallback callback) {
    sensor_data_callback_ = callback;
    ESP_LOGI(TAG, "Sensor data callback registered");
}

void AI::StartSensorDataPush(int interval_ms) {
    if (!hardware_manager_ || !hardware_manager_->IsInitialized()) {
        ESP_LOGW(TAG, "Cannot start sensor push: hardware manager not available");
        return;
    }
    
    if (sensor_push_active_) {
        ESP_LOGW(TAG, "Sensor data push already active");
        return;
    }
    
    sensor_push_interval_ = interval_ms;
    sensor_push_active_ = true;
    
    ESP_LOGI(TAG, "Starting sensor data push (interval: %d ms)", interval_ms);
    
    // 创建定时器任务来推送传感器数据
    // 注意：这里使用简单的实现，实际项目中可能需要使用FreeRTOS定时器
    xTaskCreate([](void* param) {
        AI* ai = static_cast<AI*>(param);
        
        while (ai->sensor_push_active_) {
            if (ai->sensor_data_callback_) {
                std::string sensor_data = ai->GetSensorDataJson();
                ai->sensor_data_callback_(sensor_data);
            }
            
            vTaskDelay(pdMS_TO_TICKS(ai->sensor_push_interval_));
        }
        
        ESP_LOGI(TAG, "Sensor data push task ended");
        vTaskDelete(NULL);
    }, "sensor_push", 4096, this, 5, NULL);
}

void AI::StopSensorDataPush() {
    if (!sensor_push_active_) {
        return;
    }
    
    sensor_push_active_ = false;
    ESP_LOGI(TAG, "Sensor data push stopped");
}

bool AI::ExecuteMotorCommand(int motor_id, int speed) {
    if (!hardware_manager_ || !hardware_manager_->IsInitialized()) {
        ESP_LOGW(TAG, "Hardware manager not available");
        return false;
    }
    
    ESP_LOGI(TAG, "AI executing motor command: motor %d, speed %d", motor_id, speed);
    
    // 验证参数
    if (speed < -255 || speed > 255) {
        ESP_LOGW(TAG, "Invalid motor speed: %d (must be -255 to 255)", speed);
        RecordControlHistory("motor", motor_id, speed, false);
        return false;
    }
    
    // 执行命令
    esp_err_t result = hardware_manager_->SetMotorSpeed(motor_id, speed);
    bool success = (result == ESP_OK);
    
    // 记录历史
    RecordControlHistory("motor", motor_id, speed, success);
    
    ESP_LOGI(TAG, "Motor command result: %s", success ? "success" : "failed");
    return success;
}

bool AI::ExecuteServoCommand(int servo_id, int angle) {
    if (!hardware_manager_ || !hardware_manager_->IsInitialized()) {
        ESP_LOGW(TAG, "Hardware manager not available");
        return false;
    }
    
    ESP_LOGI(TAG, "AI executing servo command: servo %d, angle %d", servo_id, angle);
    
    // 验证参数
    if (angle < 0 || angle > 180) {
        ESP_LOGW(TAG, "Invalid servo angle: %d (must be 0 to 180)", angle);
        RecordControlHistory("servo", servo_id, angle, false);
        return false;
    }
    
    // 执行命令
    esp_err_t result = hardware_manager_->SetServoAngle(servo_id, angle);
    bool success = (result == ESP_OK);
    
    // 记录历史
    RecordControlHistory("servo", servo_id, angle, success);
    
    ESP_LOGI(TAG, "Servo command result: %s", success ? "success" : "failed");
    return success;
}

std::string AI::GetControlHistory(int limit) {
    ESP_LOGI(TAG, "Getting control history (limit: %d)", limit);
    
    // 构建JSON响应
    cJSON* root = cJSON_CreateObject();
    cJSON* success = cJSON_CreateBool(true);
    cJSON* timestamp = cJSON_CreateNumber(esp_timer_get_time() / 1000);
    cJSON* history = cJSON_CreateArray();
    
    cJSON_AddItemToObject(root, "success", success);
    cJSON_AddItemToObject(root, "timestamp", timestamp);
    cJSON_AddItemToObject(root, "history", history);
    
    // 添加历史记录（最新的在前）
    int count = 0;
    for (auto it = control_history_.rbegin(); 
         it != control_history_.rend() && count < limit; 
         ++it, ++count) {
        
        cJSON* entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(entry, "timestamp", it->timestamp);
        cJSON_AddStringToObject(entry, "command_type", it->command_type.c_str());
        cJSON_AddNumberToObject(entry, "device_id", it->device_id);
        cJSON_AddNumberToObject(entry, "value", it->value);
        cJSON_AddBoolToObject(entry, "success", it->success);
        
        cJSON_AddItemToArray(history, entry);
    }
    
    char* json_string = cJSON_Print(root);
    std::string result(json_string);
    
    free(json_string);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Control history JSON generated (%d entries)", count);
    return result;
}

void AI::ClearControlHistory() {
    control_history_.clear();
    ESP_LOGI(TAG, "Control history cleared");
}

// WebSocket方法
void AI::HandleWebSocketMessage(httpd_req_t* req, const std::string& message) {
    ESP_LOGI(TAG, "Received WebSocket message: %s", message.c_str());
    
    // 解析JSON消息
    cJSON* root = cJSON_Parse(message.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse WebSocket message");
        return;
    }
    
    // 处理不同类型的消息
    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && type->valuestring) {
        std::string msg_type = type->valuestring;
        
        if (msg_type == "startRecognition") {
            // 启动语音识别
            if (StartVoiceRecognition()) {
                // 发送确认消息
                std::string response = "{\"type\":\"recognition\",\"status\":\"started\"}";
                web_server_->SendWebSocketMessage(req, response);
            }
        }
        else if (msg_type == "stopRecognition") {
            // 停止语音识别
            StopVoiceRecognition();
            // 发送确认消息
            std::string response = "{\"type\":\"recognition\",\"status\":\"stopped\"}";
            web_server_->SendWebSocketMessage(req, response);
        }
        else if (msg_type == "audioData") {
            // 处理音频数据
            cJSON* data = cJSON_GetObjectItem(root, "data");
            if (cJSON_IsString(data) && data->valuestring) {
                // 处理音频数据（通常是base64编码）
                std::string audio_data = data->valuestring;
                std::string text = ProcessAudioFile(audio_data);
                
                if (!text.empty() && speech_callback_) {
                    speech_callback_(text);
                }
                
                // 发送识别结果
                char response[512];
                snprintf(response, sizeof(response), 
                         "{\"type\":\"recognitionResult\",\"text\":\"%s\"}", 
                         text.c_str());
                web_server_->SendWebSocketMessage(req, response);
            }
        }
        else if (msg_type == "chatRequest") {
            // 处理对话请求
            cJSON* query = cJSON_GetObjectItem(root, "query");
            if (cJSON_IsString(query) && query->valuestring) {
                std::string user_query = query->valuestring;
                std::string ai_response = GenerateTextResponse(user_query);
                
                // 发送AI响应
                char response[1024];
                snprintf(response, sizeof(response), 
                         "{\"type\":\"chatResponse\",\"text\":\"%s\"}", 
                         ai_response.c_str());
                web_server_->SendWebSocketMessage(req, response);
                
                // 如果启用了语音合成，也可以发送语音数据
                std::string speech_data = SynthesizeSpeech(ai_response);
                if (!speech_data.empty()) {
                    // 发送语音数据（通常是base64编码）
                    char speech_response[1024];
                    snprintf(speech_response, sizeof(speech_response), 
                             "{\"type\":\"speechData\",\"data\":\"%s\"}", 
                             speech_data.c_str());
                    web_server_->SendWebSocketMessage(req, speech_response);
                }
            }
        }
        else if (msg_type == "getSensorData") {
            // 获取传感器数据请求
            ESP_LOGI(TAG, "Processing getSensorData WebSocket request");
            
            cJSON* sensor_ids = cJSON_GetObjectItem(root, "sensor_ids");
            std::string sensor_data;
            
            if (cJSON_IsArray(sensor_ids)) {
                // 获取指定传感器的数据
                std::vector<std::string> ids;
                int array_size = cJSON_GetArraySize(sensor_ids);
                
                for (int i = 0; i < array_size; i++) {
                    cJSON* item = cJSON_GetArrayItem(sensor_ids, i);
                    if (cJSON_IsString(item) && item->valuestring) {
                        ids.push_back(item->valuestring);
                    }
                }
                
                sensor_data = GetFilteredSensorDataJson(ids);
            } else {
                // 获取所有传感器数据
                sensor_data = GetSensorDataJson();
            }
            
            // 发送传感器数据响应
            std::string response = "{\"type\":\"sensorDataResponse\",\"data\":" + sensor_data + "}";
            web_server_->SendWebSocketMessage(req, response);
        }
        else if (msg_type == "controlMotor") {
            // 电机控制请求
            ESP_LOGI(TAG, "Processing controlMotor WebSocket request");
            
            cJSON* motor_id = cJSON_GetObjectItem(root, "motor_id");
            cJSON* speed = cJSON_GetObjectItem(root, "speed");
            
            bool success = false;
            if (cJSON_IsNumber(motor_id) && cJSON_IsNumber(speed)) {
                int id = motor_id->valueint;
                int spd = speed->valueint;
                success = ExecuteMotorCommand(id, spd);
            }
            
            // 发送控制结果响应
            char response[256];
            snprintf(response, sizeof(response), 
                     "{\"type\":\"motorControlResponse\",\"success\":%s,\"motor_id\":%d,\"speed\":%d}", 
                     success ? "true" : "false",
                     cJSON_IsNumber(motor_id) ? motor_id->valueint : -1,
                     cJSON_IsNumber(speed) ? speed->valueint : 0);
            web_server_->SendWebSocketMessage(req, response);
        }
        else if (msg_type == "controlServo") {
            // 舵机控制请求
            ESP_LOGI(TAG, "Processing controlServo WebSocket request");
            
            cJSON* servo_id = cJSON_GetObjectItem(root, "servo_id");
            cJSON* angle = cJSON_GetObjectItem(root, "angle");
            
            bool success = false;
            if (cJSON_IsNumber(servo_id) && cJSON_IsNumber(angle)) {
                int id = servo_id->valueint;
                int ang = angle->valueint;
                success = ExecuteServoCommand(id, ang);
            }
            
            // 发送控制结果响应
            char response[256];
            snprintf(response, sizeof(response), 
                     "{\"type\":\"servoControlResponse\",\"success\":%s,\"servo_id\":%d,\"angle\":%d}", 
                     success ? "true" : "false",
                     cJSON_IsNumber(servo_id) ? servo_id->valueint : -1,
                     cJSON_IsNumber(angle) ? angle->valueint : 0);
            web_server_->SendWebSocketMessage(req, response);
        }
        else if (msg_type == "aiHardwareCommand") {
            // AI硬件控制命令
            ESP_LOGI(TAG, "Processing aiHardwareCommand WebSocket request");
            
            cJSON* command = cJSON_GetObjectItem(root, "command");
            bool success = false;
            
            if (cJSON_IsObject(command)) {
                char* command_str = cJSON_Print(command);
                if (command_str) {
                    success = ExecuteHardwareCommand(command_str);
                    free(command_str);
                }
            }
            
            // 发送AI硬件命令结果响应
            char response[256];
            snprintf(response, sizeof(response), 
                     "{\"type\":\"aiHardwareCommandResponse\",\"success\":%s}", 
                     success ? "true" : "false");
            web_server_->SendWebSocketMessage(req, response);
        }
        else if (msg_type == "getControlHistory") {
            // 获取控制历史请求
            ESP_LOGI(TAG, "Processing getControlHistory WebSocket request");
            
            cJSON* limit = cJSON_GetObjectItem(root, "limit");
            int history_limit = cJSON_IsNumber(limit) ? limit->valueint : 10;
            
            std::string history_data = GetControlHistory(history_limit);
            
            // 发送控制历史响应
            std::string response = "{\"type\":\"controlHistoryResponse\",\"data\":" + history_data + "}";
            web_server_->SendWebSocketMessage(req, response);
        }
        else if (msg_type == "startSensorPush") {
            // 开始传感器数据推送
            ESP_LOGI(TAG, "Processing startSensorPush WebSocket request");
            
            cJSON* interval = cJSON_GetObjectItem(root, "interval");
            int push_interval = cJSON_IsNumber(interval) ? interval->valueint : 1000;
            
            // 设置传感器数据回调，通过WebSocket推送数据
            RegisterSensorDataCallback([this, req](const std::string& sensor_data) {
                if (web_server_) {
                    std::string response = "{\"type\":\"sensorDataPush\",\"data\":" + sensor_data + "}";
                    web_server_->SendWebSocketMessage(req, response);
                }
            });
            
            StartSensorDataPush(push_interval);
            
            // 发送确认响应
            char response[128];
            snprintf(response, sizeof(response), 
                     "{\"type\":\"sensorPushStarted\",\"interval\":%d}", push_interval);
            web_server_->SendWebSocketMessage(req, response);
        }
        else if (msg_type == "stopSensorPush") {
            // 停止传感器数据推送
            ESP_LOGI(TAG, "Processing stopSensorPush WebSocket request");
            
            StopSensorDataPush();
            
            // 发送确认响应
            std::string response = "{\"type\":\"sensorPushStopped\"}";
            web_server_->SendWebSocketMessage(req, response);
        }
    }
    
    cJSON_Delete(root);
}

// 网页UI处理
void AI::InitHandlers() {
    if (!web_server_) {
        ESP_LOGE(TAG, "Web server not initialized");
        return;
    }
    
    ESP_LOGI(TAG, "Registering AI handlers");
    
    // 注册Web页面处理器
    web_server_->RegisterHandler(HttpMethod::HTTP_GET, "/ai", [this](httpd_req_t* req) {
        ESP_LOGI(TAG, "Processing AI UI request");
        
        // 设置内容类型
        httpd_resp_set_type(req, "text/html");
        
        // 从请求头中获取语言设置
        char lang_buf[64] = {0};
        httpd_req_get_hdr_value_str(req, "Accept-Language", lang_buf, sizeof(lang_buf));
        std::string html = GetAIHtml(lang_buf[0] ? lang_buf : nullptr);
        
        httpd_resp_send(req, html.c_str(), html.length());
        return ESP_OK;
    });
    
    // 注册API处理器
    web_server_->RegisterApiHandler(HttpMethod::HTTP_POST, "/api/speech-to-text", 
                                   [this](httpd_req_t* req) {
                                       return HandleSpeechToText(req);
                                   });
                                   
    web_server_->RegisterApiHandler(HttpMethod::HTTP_POST, "/api/text-to-speech", 
                                   [this](httpd_req_t* req) {
                                       return HandleTextToSpeech(req);
                                   });
                                   
    web_server_->RegisterApiHandler(HttpMethod::HTTP_POST, "/api/chat", 
                                   [this](httpd_req_t* req) {
                                       return HandleAIChat(req);
                                   });
                                   
    // 注册WebSocket消息回调
    web_server_->RegisterWebSocketMessageCallback([this](httpd_req_t* req, const std::string& message) {
        // 检查消息是否与AI相关（包括硬件控制）
        if (message.find("\"type\":\"") != std::string::npos &&
            (message.find("\"recognition") != std::string::npos ||
             message.find("\"audio") != std::string::npos ||
             message.find("\"chat") != std::string::npos ||
             message.find("\"getSensorData") != std::string::npos ||
             message.find("\"controlMotor") != std::string::npos ||
             message.find("\"controlServo") != std::string::npos ||
             message.find("\"aiHardwareCommand") != std::string::npos ||
             message.find("\"getControlHistory") != std::string::npos ||
             message.find("\"startSensorPush") != std::string::npos ||
             message.find("\"stopSensorPush") != std::string::npos)) {
            HandleWebSocketMessage(req, message);
        }
    });
}

std::string AI::GetAIHtml(const char* language) {
    // 这里应该通过资源文件或嵌入式二进制获取HTML内容
    // 实际内容将由Web内容子系统提供
    // 这里只返回一个通用页面框架，具体内容由前端JS填充
    
    return "<html>"
           "<head>"
           "  <title>AI对话</title>"
           "  <meta name='viewport' content='width=device-width, initial-scale=1'>"
           "  <link rel='stylesheet' href='/css/bootstrap.min.css'>"
           "  <link rel='stylesheet' href='/css/ai.css'>"
           "</head>"
           "<body>"
           "  <div class='container'>"
           "    <h1>AI对话</h1>"
           "    <div id='chat-container'></div>"
           "    <div id='voice-controls'></div>"
           "  </div>"
           "  <script src='/js/common.js'></script>"
           "  <script src='/js/bootstrap.bundle.min.js'></script>"
           "  <script src='/js/ai.js'></script>"
           "</body>"
           "</html>";
}

// API响应处理
ApiResponse AI::HandleSpeechToText(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing speech-to-text request");
    
    // 获取POST数据（音频文件）
    std::string post_data = Web::GetPostData(req);
    
    // 处理音频文件
    std::string text = ProcessAudioFile(post_data);
    
    // 构建JSON响应
    char response[256];
    snprintf(response, sizeof(response), "{\"success\":true,\"text\":\"%s\"}", text.c_str());
    
    return ApiResponse(response);
}

ApiResponse AI::HandleTextToSpeech(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing text-to-speech request");
    
    // 获取POST数据（文本）
    std::string post_data = Web::GetPostData(req);
    
    // 解析JSON请求
    cJSON* root = cJSON_Parse(post_data.c_str());
    if (!root) {
        return ApiResponse("{\"success\":false,\"error\":\"Invalid JSON data\"}");
    }
    
    cJSON* text = cJSON_GetObjectItem(root, "text");
    if (!cJSON_IsString(text) || !text->valuestring) {
        cJSON_Delete(root);
        return ApiResponse("{\"success\":false,\"error\":\"Missing text parameter\"}");
    }
    
    std::string text_to_speak = text->valuestring;
    cJSON_Delete(root);
    
    // 合成语音
    std::string speech_data = SynthesizeSpeech(text_to_speak);
    
    // 构建JSON响应
    std::string response = "{\"success\":true,\"data\":\"" + speech_data + "\"}";
    
    return ApiResponse(response);
}

ApiResponse AI::HandleAIChat(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing AI chat request");
    
    // 获取POST数据
    std::string post_data = Web::GetPostData(req);
    
    // 解析JSON请求
    cJSON* root = cJSON_Parse(post_data.c_str());
    if (!root) {
        return ApiResponse("{\"success\":false,\"error\":\"Invalid JSON data\"}");
    }
    
    cJSON* query = cJSON_GetObjectItem(root, "query");
    if (!cJSON_IsString(query) || !query->valuestring) {
        cJSON_Delete(root);
        return ApiResponse("{\"success\":false,\"error\":\"Missing query parameter\"}");
    }
    
    std::string user_query = query->valuestring;
    cJSON_Delete(root);
    
    // 生成AI响应
    std::string response_text = GenerateTextResponse(user_query);
    
    // 构建JSON响应
    std::string response = "{\"success\":true,\"response\":\"" + response_text + "\"}";
    
    return ApiResponse(response);
}

// 实用功能
std::string AI::ProcessAudioFile(const std::string& audio_data) {
    ESP_LOGI(TAG, "Processing audio data (%zu bytes)", audio_data.size());
    
    // 这里应该实现实际的语音识别逻辑
    // 目前返回一个模拟响应
    return "Speech recognition result";
}

std::string AI::SynthesizeSpeech(const std::string& text) {
    ESP_LOGI(TAG, "Synthesizing speech: %s", text.c_str());
    
    // 这里应该实现实际的语音合成逻辑
    // 目前返回一个空字符串
    return "";
}

void AI::RecordControlHistory(const std::string& command_type, int device_id, int value, bool success) {
    ControlHistoryEntry entry;
    entry.timestamp = esp_timer_get_time() / 1000;  // 转换为毫秒
    entry.command_type = command_type;
    entry.device_id = device_id;
    entry.value = value;
    entry.success = success;
    
    control_history_.push_back(entry);
    
    // 限制历史记录数量，避免内存溢出
    const size_t MAX_HISTORY_SIZE = 100;
    if (control_history_.size() > MAX_HISTORY_SIZE) {
        control_history_.erase(control_history_.begin());
    }
    
    ESP_LOGD(TAG, "Control history recorded: %s %d=%d (%s)", 
             command_type.c_str(), device_id, value, success ? "OK" : "FAIL");
} 