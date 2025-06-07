#pragma once

#include "web.h"
#include <esp_http_server.h>
#include <cJSON.h>
#include <string>
#include <functional>

/**
 * @brief API模块 - 提供API接口管理功能
 * 
 * 本模块整合了API定义、路由和处理功能
 */

// API版本常量
#define API_VERSION "v1"
#define API_BASE_PATH "/api/" API_VERSION

// API响应状态码与类型同web.h中的定义
using ApiResponse = struct ApiResponse;
using ApiStatusCode = int;
using ApiHandler = std::function<ApiResponse(httpd_req_t*)>;

/**
 * @brief API初始化函数
 * 
 * 初始化API路由与处理器
 * 
 * @param web Web组件实例指针
 * @return 初始化是否成功
 */
bool InitializeApi(Web* web);

/**
 * @brief 注册API处理器
 * 
 * @param web Web组件实例指针
 * @param method HTTP方法
 * @param uri API路径
 * @param handler 处理函数
 */
void RegisterApiHandler(Web* web, HttpMethod method, const std::string& uri, ApiHandler handler);

/**
 * @brief 创建API成功响应
 * 
 * @param message 成功消息
 * @param data JSON数据(可选)
 * @return ApiResponse 响应对象
 */
ApiResponse CreateApiSuccessResponse(const std::string& message, cJSON* data = nullptr);

/**
 * @brief 创建API错误响应
 * 
 * @param status_code 错误状态码
 * @param message 错误消息
 * @return ApiResponse 响应对象
 */
ApiResponse CreateApiErrorResponse(int status_code, const std::string& message);

/**
 * @brief 解析请求JSON数据
 * 
 * @param req HTTP请求
 * @return cJSON* JSON对象，失败返回nullptr
 */
cJSON* ParseRequestJson(httpd_req_t* req);

// API处理函数声明
ApiResponse HandleSystemInfo(httpd_req_t* req);
ApiResponse HandleSystemRestart(httpd_req_t* req);
ApiResponse HandleServiceStatus(httpd_req_t* req);
ApiResponse HandleConfigGet(httpd_req_t* req);
ApiResponse HandleConfigSet(httpd_req_t* req); 

// 摄像头API处理函数
ApiResponse HandleCameraStatus(httpd_req_t* req);
ApiResponse HandleCameraStream(httpd_req_t* req);
ApiResponse HandleCameraCapture(httpd_req_t* req);
ApiResponse HandleCameraSettings(httpd_req_t* req); 