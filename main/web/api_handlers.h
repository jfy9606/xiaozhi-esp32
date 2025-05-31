/**
 * @file api_handlers.h
 * @brief API处理器声明
 */

#ifndef API_HANDLERS_H
#define API_HANDLERS_H

#include "api_definitions.h"

// 初始化所有API处理器
void InitializeApiHandlers(ApiRouter* router);

// ===================
// 系统API处理器
// ===================

// 获取系统信息
ApiResponse HandleSystemInfo(httpd_req_t* req, cJSON* request_json);

// 重启系统
ApiResponse HandleSystemRestart(httpd_req_t* req, cJSON* request_json);

// ===================
// 舵机API处理器
// ===================

// 获取舵机状态
ApiResponse HandleServoStatus(httpd_req_t* req, cJSON* request_json);

// 设置舵机角度
ApiResponse HandleSetServoAngle(httpd_req_t* req, cJSON* request_json);

// 设置舵机PWM频率
ApiResponse HandleSetServoFrequency(httpd_req_t* req, cJSON* request_json);

// WebSocket舵机控制消息处理
void HandleServoWsMessage(int client_id, cJSON* json, const std::string& type);

// 处理设置舵机角度命令
void HandleServoSetAngleCommand(int client_id, cJSON* json);

// 处理设置舵机频率命令
void HandleServoSetFrequencyCommand(int client_id, cJSON* json);

// 发送舵机错误响应
void SendServoErrorResponse(int client_id, const char* error_msg);

// 发送舵机成功响应
void SendServoSuccessResponse(int client_id, const char* cmd, int channel, int angle);

// ===================
// 传感器API处理器
// ===================

// WebSocket传感器数据消息处理
void HandleSensorWsMessage(int client_id, cJSON* json, const std::string& type);

// 发送传感器错误响应
void SendSensorErrorResponse(int client_id, const char* error_msg);

// 发送传感器成功响应
void SendSensorSuccessResponse(int client_id, const char* cmd);

// 发送传感器数据到所有客户端
void BroadcastSensorData(const float* values, int count, int64_t timestamp);

// ===================
// 音频API处理器
// ===================

// WebSocket音频数据消息处理
void HandleAudioWsMessage(int client_id, cJSON* json, const std::string& type);

// 发送音频错误响应
void SendAudioErrorResponse(int client_id, const char* error_msg);

// 发送音频成功响应
void SendAudioSuccessResponse(int client_id, const char* cmd, cJSON* data = nullptr);

// ===================
// 设备配置API处理器
// ===================

// 获取设备配置
ApiResponse HandleGetDeviceConfig(httpd_req_t* req, cJSON* request_json);

// 更新设备配置
ApiResponse HandleUpdateDeviceConfig(httpd_req_t* req, cJSON* request_json);

#endif // API_HANDLERS_H