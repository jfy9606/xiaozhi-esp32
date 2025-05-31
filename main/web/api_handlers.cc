/**
 * @file api_handlers.cc
 * @brief API处理器实现
 */

#include "api_handlers.h"
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cJSON.h>
#include <string.h>
#include <esp_timer.h>
#include <esp_app_desc.h>
#include "ext/include/lu9685.h"
#include "../components.h"

static const char* TAG = "ApiHandlers";

// 初始化所有API处理器
void InitializeApiHandlers(ApiRouter* router) {
    if (!router) {
        ESP_LOGE(TAG, "API Router is null, cannot initialize handlers");
        return;
    }
    
    // 注册系统API
    router->RegisterHttpApi(HTTP_API_SYSTEM_INFO, HTTP_GET, HandleSystemInfo);
    router->RegisterHttpApi(HTTP_API_SYSTEM_RESTART, HTTP_POST, HandleSystemRestart);
    
    // 注册舵机API
    router->RegisterHttpApi(HTTP_API_SERVO_STATUS, HTTP_GET, HandleServoStatus);
    router->RegisterHttpApi(HTTP_API_SERVO_ANGLE, HTTP_POST, HandleSetServoAngle);
    router->RegisterHttpApi(HTTP_API_SERVO_FREQUENCY, HTTP_POST, HandleSetServoFrequency);
    
    // 注册设备配置API
    router->RegisterHttpApi(HTTP_API_DEVICE_CONFIG, HTTP_GET, HandleGetDeviceConfig);
    router->RegisterHttpApi(HTTP_API_DEVICE_CONFIG, HTTP_POST, HandleUpdateDeviceConfig);
    
    // 注册WebSocket处理器
    router->RegisterWsApi(WS_MSG_TYPE_SERVO, HandleServoWsMessage);
    router->RegisterWsApi(WS_MSG_TYPE_SENSOR, HandleSensorWsMessage);
    router->RegisterWsApi(WS_MSG_TYPE_AUDIO, HandleAudioWsMessage);
    
    ESP_LOGI(TAG, "API handlers initialized successfully");
}

//======================
// 系统API处理器实现
//======================

ApiResponse HandleSystemInfo(httpd_req_t* req, cJSON* request_json) {
    ESP_LOGI(TAG, "Processing system info request");
    
    cJSON* data = cJSON_CreateObject();
    
    // 添加系统基本信息
    cJSON_AddStringToObject(data, "version", "1.0.0");
    cJSON_AddNumberToObject(data, "uptime_ms", esp_timer_get_time() / 1000);
    
    // 添加内存信息
    cJSON_AddNumberToObject(data, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(data, "min_free_heap", esp_get_minimum_free_heap_size());
    
    // 添加芯片信息
    const esp_app_desc_t* app_desc = esp_app_get_description();
    if (app_desc) {
        cJSON_AddStringToObject(data, "app_name", app_desc->project_name);
        cJSON_AddStringToObject(data, "app_version", app_desc->version);
        cJSON_AddStringToObject(data, "compile_time", app_desc->time);
        cJSON_AddStringToObject(data, "compile_date", app_desc->date);
    }
    
    return ApiRouter::CreateSuccessResponse(data);
}

ApiResponse HandleSystemRestart(httpd_req_t* req, cJSON* request_json) {
    ESP_LOGI(TAG, "Processing system restart request");
    
    // 创建响应
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "message", "System will restart in 3 seconds");
    
    // 创建并发送响应
    ApiResponse response = ApiRouter::CreateSuccessResponse(data);
    
    // 启动一个任务来延迟重启系统
    xTaskCreate([](void* param) {
        ESP_LOGI(TAG, "Restarting system in 3 seconds...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        ESP_LOGI(TAG, "Restarting now!");
        esp_restart();
    }, "restart_task", 2048, NULL, 5, NULL);
    
    return response;
}

//======================
// 舵机API处理器实现
//======================

ApiResponse HandleServoStatus(httpd_req_t* req, cJSON* request_json) {
    ESP_LOGI(TAG, "Processing servo status request");
    
    cJSON* data = cJSON_CreateObject();
    
    // 检查LU9685舵机控制器是否已初始化
    bool initialized = lu9685_is_initialized();
    cJSON_AddBoolToObject(data, "initialized", initialized);
    
    if (initialized) {
        lu9685_handle_t handle = lu9685_get_handle();
        if (handle) {
            // 添加控制器状态信息
            // 注意：我们需要访问频率等信息，但当前LU9685 API没有提供获取这些信息的函数
            // 可以考虑扩展LU9685 API来获取更多状态信息
            cJSON_AddStringToObject(data, "controller_type", "LU9685-20CU");
            cJSON_AddNumberToObject(data, "max_channels", 16);
            cJSON_AddNumberToObject(data, "max_frequency_hz", 300);
        }
    } else {
        cJSON_AddStringToObject(data, "error", "Servo controller not initialized");
    }
    
    return ApiRouter::CreateSuccessResponse(data);
}

ApiResponse HandleSetServoAngle(httpd_req_t* req, cJSON* request_json) {
    ESP_LOGI(TAG, "Processing set servo angle request");
    
    if (!request_json) {
        return ApiRouter::CreateErrorResponse(ApiStatusCode::BAD_REQUEST, "Request body is required");
    }
    
    // 检查LU9685舵机控制器是否已初始化
    if (!lu9685_is_initialized()) {
        return ApiRouter::CreateErrorResponse(ApiStatusCode::INTERNAL_ERROR, "Servo controller not initialized");
    }
    
    // 提取参数
    cJSON* channel_json = cJSON_GetObjectItem(request_json, "channel");
    cJSON* angle_json = cJSON_GetObjectItem(request_json, "angle");
    
    if (!cJSON_IsNumber(channel_json) || !cJSON_IsNumber(angle_json)) {
        return ApiRouter::CreateErrorResponse(ApiStatusCode::BAD_REQUEST, "Invalid or missing channel/angle parameters");
    }
    
    int channel = channel_json->valueint;
    int angle = angle_json->valueint;
    
    // 验证参数
    if (channel < 0 || channel > 15) {
        return ApiRouter::CreateErrorResponse(ApiStatusCode::BAD_REQUEST, "Channel must be between 0 and 15");
    }
    
    if (angle < 0 || angle > 180) {
        return ApiRouter::CreateErrorResponse(ApiStatusCode::BAD_REQUEST, "Angle must be between 0 and 180");
    }
    
    // 设置舵机角度
    lu9685_handle_t handle = lu9685_get_handle();
    esp_err_t ret = lu9685_set_channel_angle(handle, (uint8_t)channel, (uint8_t)angle);
    
    if (ret != ESP_OK) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "Failed to set servo angle: %s", esp_err_to_name(ret));
        return ApiRouter::CreateErrorResponse(ApiStatusCode::INTERNAL_ERROR, error_msg);
    }
    
    // 创建成功响应
    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "channel", channel);
    cJSON_AddNumberToObject(data, "angle", angle);
    
    return ApiRouter::CreateSuccessResponse(data);
}

ApiResponse HandleSetServoFrequency(httpd_req_t* req, cJSON* request_json) {
    ESP_LOGI(TAG, "Processing set servo frequency request");
    
    if (!request_json) {
        return ApiRouter::CreateErrorResponse(ApiStatusCode::BAD_REQUEST, "Request body is required");
    }
    
    // 检查LU9685舵机控制器是否已初始化
    if (!lu9685_is_initialized()) {
        return ApiRouter::CreateErrorResponse(ApiStatusCode::INTERNAL_ERROR, "Servo controller not initialized");
    }
    
    // 提取频率参数
    cJSON* freq_json = cJSON_GetObjectItem(request_json, "frequency");
    
    if (!cJSON_IsNumber(freq_json)) {
        return ApiRouter::CreateErrorResponse(ApiStatusCode::BAD_REQUEST, "Invalid or missing frequency parameter");
    }
    
    int frequency = freq_json->valueint;
    
    // 验证频率范围
    if (frequency < 50 || frequency > 300) {
        return ApiRouter::CreateErrorResponse(ApiStatusCode::BAD_REQUEST, "Frequency must be between 50 and 300 Hz");
    }
    
    // 设置PWM频率
    lu9685_handle_t handle = lu9685_get_handle();
    esp_err_t ret = lu9685_set_frequency(handle, (uint16_t)frequency);
    
    if (ret != ESP_OK) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "Failed to set frequency: %s", esp_err_to_name(ret));
        return ApiRouter::CreateErrorResponse(ApiStatusCode::INTERNAL_ERROR, error_msg);
    }
    
    // 创建成功响应
    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "frequency", frequency);
    
    return ApiRouter::CreateSuccessResponse(data);
}

//======================
// WebSocket API处理器实现
//======================

void HandleServoWsMessage(int client_id, cJSON* json, const std::string& type) {
    if (!json) {
        ESP_LOGW(TAG, "WebSocket servo message is null");
        return;
    }
    
    // 检查LU9685舵机控制器是否已初始化
    if (!lu9685_is_initialized()) {
        ESP_LOGW(TAG, "Servo controller not initialized");
        SendServoErrorResponse(client_id, "Servo controller not initialized");
        return;
    }
    
    // 提取命令类型
    cJSON* cmd_json = cJSON_GetObjectItem(json, "cmd");
    if (!cJSON_IsString(cmd_json)) {
        ESP_LOGW(TAG, "Missing or invalid 'cmd' in servo message");
        SendServoErrorResponse(client_id, "Missing or invalid command");
        return;
    }
    
    const char* cmd = cmd_json->valuestring;
    
    // 处理不同的命令
    if (strcmp(cmd, "set_angle") == 0) {
        // 处理设置角度命令
        HandleServoSetAngleCommand(client_id, json);
    } 
    else if (strcmp(cmd, "set_frequency") == 0) {
        // 处理设置频率命令
        HandleServoSetFrequencyCommand(client_id, json);
    }
    else {
        ESP_LOGW(TAG, "Unknown servo command: %s", cmd);
        SendServoErrorResponse(client_id, "Unknown command");
    }
}

void HandleServoSetAngleCommand(int client_id, cJSON* json) {
    // 提取参数
    cJSON* channel_json = cJSON_GetObjectItem(json, "channel");
    cJSON* angle_json = cJSON_GetObjectItem(json, "angle");
    
    if (!cJSON_IsNumber(channel_json) || !cJSON_IsNumber(angle_json)) {
        SendServoErrorResponse(client_id, "Invalid or missing channel/angle parameters");
        return;
    }
    
    int channel = channel_json->valueint;
    int angle = angle_json->valueint;
    
    // 验证参数
    if (channel < 0 || channel > 15) {
        SendServoErrorResponse(client_id, "Channel must be between 0 and 15");
        return;
    }
    
    if (angle < 0 || angle > 180) {
        SendServoErrorResponse(client_id, "Angle must be between 0 and 180");
        return;
    }
    
    // 设置舵机角度
    lu9685_handle_t handle = lu9685_get_handle();
    esp_err_t ret = lu9685_set_channel_angle(handle, (uint8_t)channel, (uint8_t)angle);
    
    if (ret != ESP_OK) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "Failed to set servo angle: %s", esp_err_to_name(ret));
        SendServoErrorResponse(client_id, error_msg);
        return;
    }
    
    // 发送成功响应
    SendServoSuccessResponse(client_id, "set_angle", channel, angle);
}

void HandleServoSetFrequencyCommand(int client_id, cJSON* json) {
    // 提取频率参数
    cJSON* freq_json = cJSON_GetObjectItem(json, "frequency");
    
    if (!cJSON_IsNumber(freq_json)) {
        SendServoErrorResponse(client_id, "Invalid or missing frequency parameter");
        return;
    }
    
    int frequency = freq_json->valueint;
    
    // 验证频率范围
    if (frequency < 50 || frequency > 300) {
        SendServoErrorResponse(client_id, "Frequency must be between 50 and 300 Hz");
        return;
    }
    
    // 设置PWM频率
    lu9685_handle_t handle = lu9685_get_handle();
    esp_err_t ret = lu9685_set_frequency(handle, (uint16_t)frequency);
    
    if (ret != ESP_OK) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "Failed to set frequency: %s", esp_err_to_name(ret));
        SendServoErrorResponse(client_id, error_msg);
        return;
    }
    
    // 发送成功响应
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "cmd", "set_frequency");
    cJSON_AddNumberToObject(response, "frequency", frequency);
    
    char* resp_str = cJSON_PrintUnformatted(response);
    WebServer::GetActiveInstance()->SendWebSocketMessage(client_id, resp_str);
    free(resp_str);
    cJSON_Delete(response);
}

void SendServoErrorResponse(int client_id, const char* error_msg) {
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "error");
    cJSON_AddStringToObject(response, "message", error_msg);
    
    char* resp_str = cJSON_PrintUnformatted(response);
    WebServer::GetActiveInstance()->SendWebSocketMessage(client_id, resp_str);
    free(resp_str);
    cJSON_Delete(response);
}

void SendServoSuccessResponse(int client_id, const char* cmd, int channel, int angle) {
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "cmd", cmd);
    cJSON_AddNumberToObject(response, "channel", channel);
    cJSON_AddNumberToObject(response, "angle", angle);
    
    char* resp_str = cJSON_PrintUnformatted(response);
    WebServer::GetActiveInstance()->SendWebSocketMessage(client_id, resp_str);
    free(resp_str);
    cJSON_Delete(response);
}

//======================
// 传感器API处理器实现
//======================

void HandleSensorWsMessage(int client_id, cJSON* json, const std::string& type) {
    if (!json) {
        ESP_LOGW(TAG, "WebSocket sensor message is null");
        return;
    }
    
    // 提取命令类型
    cJSON* cmd_json = cJSON_GetObjectItem(json, "cmd");
    if (!cJSON_IsString(cmd_json)) {
        ESP_LOGW(TAG, "Missing or invalid 'cmd' in sensor message");
        SendSensorErrorResponse(client_id, "Missing or invalid command");
        return;
    }
    
    const char* cmd = cmd_json->valuestring;
    
    // 处理订阅命令
    if (strcmp(cmd, "subscribe") == 0) {
        // 处理传感器数据订阅
        // 这里可以记录客户端ID，以便后续发送传感器数据
        SendSensorSuccessResponse(client_id, "subscribe");
    } 
    else if (strcmp(cmd, "unsubscribe") == 0) {
        // 处理取消订阅
        SendSensorSuccessResponse(client_id, "unsubscribe");
    }
    else {
        ESP_LOGW(TAG, "Unknown sensor command: %s", cmd);
        SendSensorErrorResponse(client_id, "Unknown command");
    }
}

void SendSensorErrorResponse(int client_id, const char* error_msg) {
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "error");
    cJSON_AddStringToObject(response, "message", error_msg);
    
    char* resp_str = cJSON_PrintUnformatted(response);
    WebServer::GetActiveInstance()->SendWebSocketMessage(client_id, resp_str);
    free(resp_str);
    cJSON_Delete(response);
}

void SendSensorSuccessResponse(int client_id, const char* cmd) {
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "cmd", cmd);
    
    char* resp_str = cJSON_PrintUnformatted(response);
    WebServer::GetActiveInstance()->SendWebSocketMessage(client_id, resp_str);
    free(resp_str);
    cJSON_Delete(response);
}

void BroadcastSensorData(const float* values, int count, int64_t timestamp) {
    WebServer* web_server = WebServer::GetActiveInstance();
    if (!web_server) {
        ESP_LOGW(TAG, "Web server not available, cannot broadcast sensor data");
        return;
    }
    
    // 创建传感器数据JSON
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "type", "sensor_data");
    
    // 添加时间戳
    cJSON_AddNumberToObject(data, "timestamp", timestamp);
    
    // 添加传感器值数组
    cJSON* values_array = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(values_array, cJSON_CreateNumber(values[i]));
    }
    cJSON_AddItemToObject(data, "values", values_array);
    
    // 转换为字符串并广播
    char* data_str = cJSON_PrintUnformatted(data);
    web_server->BroadcastWebSocketMessage(data_str, WS_MSG_TYPE_SENSOR);
    
    // 释放资源
    free(data_str);
    cJSON_Delete(data);
}

//======================
// 音频API处理器实现
//======================

void HandleAudioWsMessage(int client_id, cJSON* json, const std::string& type) {
    if (!json) {
        ESP_LOGW(TAG, "WebSocket audio message is null");
        return;
    }
    
    // 提取命令类型
    cJSON* cmd_json = cJSON_GetObjectItem(json, "cmd");
    if (!cJSON_IsString(cmd_json)) {
        ESP_LOGW(TAG, "Missing or invalid 'cmd' in audio message");
        SendAudioErrorResponse(client_id, "Missing or invalid command");
        return;
    }
    
    const char* cmd = cmd_json->valuestring;
    
    // 处理不同的音频命令
    if (strcmp(cmd, "start_stream") == 0) {
        // 启动音频流处理
        ESP_LOGI(TAG, "Starting audio stream for client %d", client_id);
        SendAudioSuccessResponse(client_id, "start_stream");
    } 
    else if (strcmp(cmd, "stop_stream") == 0) {
        // 停止音频流处理
        ESP_LOGI(TAG, "Stopping audio stream for client %d", client_id);
        SendAudioSuccessResponse(client_id, "stop_stream");
    }
    else if (strcmp(cmd, "volume") == 0) {
        // 处理音量调整
        cJSON* volume_json = cJSON_GetObjectItem(json, "value");
        if (!cJSON_IsNumber(volume_json)) {
            SendAudioErrorResponse(client_id, "Invalid or missing volume value");
            return;
        }
        
        int volume = volume_json->valueint;
        if (volume < 0 || volume > 100) {
            SendAudioErrorResponse(client_id, "Volume must be between 0 and 100");
            return;
        }
        
        ESP_LOGI(TAG, "Setting volume to %d for client %d", volume, client_id);
        
        // 创建响应数据
        cJSON* data = cJSON_CreateObject();
        cJSON_AddNumberToObject(data, "volume", volume);
        SendAudioSuccessResponse(client_id, "volume", data);
    }
    else {
        ESP_LOGW(TAG, "Unknown audio command: %s", cmd);
        SendAudioErrorResponse(client_id, "Unknown command");
    }
}

void SendAudioErrorResponse(int client_id, const char* error_msg) {
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "error");
    cJSON_AddStringToObject(response, "message", error_msg);
    
    char* resp_str = cJSON_PrintUnformatted(response);
    WebServer::GetActiveInstance()->SendWebSocketMessage(client_id, resp_str);
    free(resp_str);
    cJSON_Delete(response);
}

void SendAudioSuccessResponse(int client_id, const char* cmd, cJSON* data) {
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "cmd", cmd);
    
    if (data) {
        cJSON_AddItemToObject(response, "data", data);
    }
    
    char* resp_str = cJSON_PrintUnformatted(response);
    WebServer::GetActiveInstance()->SendWebSocketMessage(client_id, resp_str);
    free(resp_str);
    cJSON_Delete(response);
}

//======================
// 设备配置API处理器实现
//======================

// 获取设备配置
ApiResponse HandleGetDeviceConfig(httpd_req_t* req, cJSON* request_json) {
    ESP_LOGI(TAG, "Processing get device config request");
    
    cJSON* data = cJSON_CreateObject();
    
    // 添加设备配置信息
    cJSON_AddStringToObject(data, "device_name", "Xiaozhi ESP32");
    cJSON_AddStringToObject(data, "firmware_version", "1.0.0");
    
    // 网络配置
    cJSON* network = cJSON_CreateObject();
    cJSON_AddStringToObject(network, "wifi_mode", "AP"); // AP or STA
    cJSON_AddStringToObject(network, "ap_ssid", "XiaoZhi-ESP32");
    cJSON_AddStringToObject(network, "sta_ssid", "");
    cJSON_AddBoolToObject(network, "dhcp_enabled", true);
    cJSON_AddItemToObject(data, "network", network);
    
    // 音频配置
    cJSON* audio = cJSON_CreateObject();
    cJSON_AddNumberToObject(audio, "volume", 80);
    cJSON_AddNumberToObject(audio, "sample_rate", 16000);
    cJSON_AddItemToObject(data, "audio", audio);
    
    // 舵机配置
    cJSON* servo = cJSON_CreateObject();
    cJSON_AddNumberToObject(servo, "default_frequency", 50);
    cJSON_AddItemToObject(data, "servo", servo);
    
    return ApiRouter::CreateSuccessResponse(data);
}

// 更新设备配置
ApiResponse HandleUpdateDeviceConfig(httpd_req_t* req, cJSON* request_json) {
    ESP_LOGI(TAG, "Processing update device config request");
    
    if (!request_json) {
        return ApiRouter::CreateErrorResponse(ApiStatusCode::BAD_REQUEST, "Request body is required");
    }
    
    // 处理设备配置更新逻辑
    // 这里仅作示例，您可以根据实际需求扩展
    cJSON* device_name = cJSON_GetObjectItem(request_json, "device_name");
    if (cJSON_IsString(device_name)) {
        ESP_LOGI(TAG, "Updating device name to: %s", device_name->valuestring);
        // 实际应用中，应该将配置保存到NVS或其他存储介质
    }
    
    // 处理网络配置更新
    cJSON* network = cJSON_GetObjectItem(request_json, "network");
    if (cJSON_IsObject(network)) {
        cJSON* wifi_mode = cJSON_GetObjectItem(network, "wifi_mode");
        if (cJSON_IsString(wifi_mode)) {
            ESP_LOGI(TAG, "Updating WiFi mode to: %s", wifi_mode->valuestring);
            // 实际应用中，应该更新WiFi设置
        }
    }
    
    // 处理音频配置更新
    cJSON* audio = cJSON_GetObjectItem(request_json, "audio");
    if (cJSON_IsObject(audio)) {
        cJSON* volume = cJSON_GetObjectItem(audio, "volume");
        if (cJSON_IsNumber(volume)) {
            ESP_LOGI(TAG, "Updating audio volume to: %d", volume->valueint);
            // 实际应用中，应该更新音频设置
        }
    }
    
    // 处理舵机配置更新
    cJSON* servo = cJSON_GetObjectItem(request_json, "servo");
    if (cJSON_IsObject(servo)) {
        cJSON* freq = cJSON_GetObjectItem(servo, "default_frequency");
        if (cJSON_IsNumber(freq)) {
            ESP_LOGI(TAG, "Updating default servo frequency to: %d", freq->valueint);
            // 实际应用中，应该更新舵机设置
        }
    }
    
    // 创建响应数据
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "message", "Configuration updated successfully");
    
    return ApiRouter::CreateSuccessResponse(data);
}