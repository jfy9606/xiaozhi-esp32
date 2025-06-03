#include "api.h"
#include "esp_log.h"
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_app_desc.h>
#include <sys/time.h>
#include <string.h>
#include <algorithm>

#define TAG "API"

// 初始化API系统
bool InitializeApi(Web* web) {
    if (!web) {
        ESP_LOGE(TAG, "Web instance is null, cannot initialize API");
        return false;
    }

    ESP_LOGI(TAG, "Initializing API module");
    
    // 注册系统API
    RegisterApiHandler(web, HttpMethod::HTTP_GET, "/system/info", HandleSystemInfo);
    RegisterApiHandler(web, HttpMethod::HTTP_POST, "/system/restart", HandleSystemRestart);
    RegisterApiHandler(web, HttpMethod::HTTP_GET, "/system/status", HandleServiceStatus);
    
    // 注册配置API
    RegisterApiHandler(web, HttpMethod::HTTP_GET, "/config", HandleConfigGet);
    RegisterApiHandler(web, HttpMethod::HTTP_POST, "/config", HandleConfigSet);
    
    ESP_LOGI(TAG, "API initialization completed");
    return true;
}

// 注册API处理器
void RegisterApiHandler(Web* web, HttpMethod method, const std::string& uri, ApiHandler handler) {
    if (!web) {
        ESP_LOGE(TAG, "Web instance is null, cannot register API handler");
        return;
    }
    
    // 为路径添加API前缀
    std::string api_uri = uri;
    if (api_uri.find("/") != 0) {
        api_uri = "/" + api_uri;
    }
    
    // 包装处理器函数
    ApiHandler wrapped_handler = [handler](httpd_req_t* req) -> ApiResponse {
        ESP_LOGI(TAG, "Processing API request: %s", req->uri);
        try {
            return handler(req);
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Exception in API handler: %s", e.what());
            return CreateApiErrorResponse(500, std::string("Internal server error: ") + e.what());
        } catch (...) {
            ESP_LOGE(TAG, "Unknown exception in API handler");
            return CreateApiErrorResponse(500, "Unknown internal server error");
        }
    };
    
    // 注册到Web
    web->RegisterApiHandler(method, api_uri, wrapped_handler);
}

// 解析请求JSON数据
cJSON* ParseRequestJson(httpd_req_t* req) {
    if (!req || req->content_len == 0) {
        return nullptr;
    }
    
    // 分配内存缓冲区
    char* buffer = (char*)malloc(req->content_len + 1);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for JSON parsing");
        return nullptr;
    }
    
    // 读取POST数据
    int received = httpd_req_recv(req, buffer, req->content_len);
    if (received <= 0) {
        ESP_LOGE(TAG, "Error receiving request data");
        free(buffer);
        return nullptr;
    }
    
    buffer[received] = '\0';
    
    // 解析JSON
    cJSON* json = cJSON_Parse(buffer);
    free(buffer);
    
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON: %s", cJSON_GetErrorPtr());
        return nullptr;
    }
    
    return json;
}

// 创建API响应
ApiResponse CreateApiSuccessResponse(const std::string& message, cJSON* data) {
    ApiResponse response;
    response.status_code = 200;
    
    // 创建响应JSON
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", message.c_str());
    
    if (data) {
        cJSON_AddItemToObject(root, "data", data);
    }
    
    // 序列化为字符串
    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        response.content = json_str;
        free(json_str);
    } else {
        response.content = "{\"success\":true,\"message\":\"" + message + "\"}";
    }
    
    cJSON_Delete(root);
    
    return response;
}

ApiResponse CreateApiErrorResponse(int status_code, const std::string& message) {
    ApiResponse response;
    response.status_code = status_code;
    
    // 创建响应JSON
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", false);
    cJSON_AddStringToObject(root, "message", message.c_str());
    cJSON_AddNumberToObject(root, "code", status_code);
    
    // 序列化为字符串
    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        response.content = json_str;
        free(json_str);
    } else {
        response.content = "{\"success\":false,\"message\":\"" + message + 
                          "\",\"code\":" + std::to_string(status_code) + "}";
    }
    
    cJSON_Delete(root);
    
    return response;
}

//=====================
// API处理函数实现
//=====================

// 系统信息API
ApiResponse HandleSystemInfo(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing system info request");
    
    cJSON* data = cJSON_CreateObject();
    
    // 系统信息
    const esp_app_desc_t* app_desc = esp_app_get_description();
    if (app_desc) {
        cJSON_AddStringToObject(data, "project_name", app_desc->project_name);
        cJSON_AddStringToObject(data, "version", app_desc->version);
        cJSON_AddStringToObject(data, "idf_ver", app_desc->idf_ver);
        cJSON_AddStringToObject(data, "compile_time", app_desc->time);
        cJSON_AddStringToObject(data, "compile_date", app_desc->date);
    }
    
    // 运行时信息
    struct timeval tv;
    gettimeofday(&tv, NULL);
    cJSON_AddNumberToObject(data, "timestamp", tv.tv_sec);
    cJSON_AddNumberToObject(data, "uptime_ms", esp_timer_get_time() / 1000);
    
    // 内存信息
    cJSON_AddNumberToObject(data, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(data, "min_free_heap", esp_get_minimum_free_heap_size());
    
    // 已注册组件
    cJSON* components = cJSON_CreateArray();
    // 这里需要实际获取组件列表，暂时使用占位符
    cJSON_AddItemToArray(components, cJSON_CreateString("Web"));
    cJSON_AddItemToArray(components, cJSON_CreateString("Vehicle"));
    cJSON_AddItemToArray(components, cJSON_CreateString("Vision"));
    cJSON_AddItemToArray(components, cJSON_CreateString("AI"));
    cJSON_AddItemToArray(components, cJSON_CreateString("Location"));
    cJSON_AddItemToObject(data, "components", components);
    
    return CreateApiSuccessResponse("System information retrieved successfully", data);
}

// 系统重启API
ApiResponse HandleSystemRestart(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing system restart request");
    
    // 解析参数（可选）
    cJSON* json = ParseRequestJson(req);
    int delay_ms = 3000; // 默认3秒后重启
    
    if (json) {
        cJSON* delay = cJSON_GetObjectItem(json, "delay_ms");
        if (delay && cJSON_IsNumber(delay)) {
            delay_ms = delay->valueint;
            // 限制范围
            if (delay_ms < 100) delay_ms = 100;
            if (delay_ms > 10000) delay_ms = 10000;
        }
        cJSON_Delete(json);
    }
    
    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "restart_delay_ms", delay_ms);
    
    // 启动一个任务进行延迟重启
    esp_timer_handle_t restart_timer;
    esp_timer_create_args_t timer_args = {
        .callback = [](void*) {
            ESP_LOGI(TAG, "Restarting system now...");
            esp_restart();
        },
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "restart_timer"
    };
    
    esp_timer_create(&timer_args, &restart_timer);
    esp_timer_start_once(restart_timer, delay_ms * 1000);
    
    return CreateApiSuccessResponse("System will restart in " + std::to_string(delay_ms) + " ms", data);
}

// 服务状态API
ApiResponse HandleServiceStatus(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing service status request");
    
    cJSON* data = cJSON_CreateObject();
    
    // Web服务状态
    cJSON* web_status = cJSON_CreateObject();
    cJSON_AddBoolToObject(web_status, "running", true);
    cJSON_AddNumberToObject(web_status, "port", 80); // 实际应从Web组件获取
    cJSON_AddItemToObject(data, "web", web_status);
    
    // 车辆状态
    cJSON* vehicle_status = cJSON_CreateObject();
    cJSON_AddBoolToObject(vehicle_status, "running", true); // 实际应从Vehicle组件获取
    cJSON_AddStringToObject(vehicle_status, "controller_type", "motor"); // 实际应从Vehicle组件获取
    cJSON_AddItemToObject(data, "vehicle", vehicle_status);
    
    // 视觉状态
    cJSON* vision_status = cJSON_CreateObject();
    cJSON_AddBoolToObject(vision_status, "running", true); // 实际应从Vision组件获取
    cJSON_AddBoolToObject(vision_status, "has_camera", true); // 实际应从Vision组件获取
    cJSON_AddBoolToObject(vision_status, "streaming", false); // 实际应从Vision组件获取
    cJSON_AddItemToObject(data, "vision", vision_status);
    
    return CreateApiSuccessResponse("Service status retrieved", data);
}

// 获取配置API
ApiResponse HandleConfigGet(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing get config request");
    
    // 解析查询参数
    std::map<std::string, std::string> params = Web::ParseQueryParams(req);
    
    cJSON* data = cJSON_CreateObject();
    
    // 如果指定了类别，则仅返回该类别的配置
    if (params.find("category") != params.end()) {
        std::string category = params["category"];
        
        if (category == "network") {
            // 网络配置
            cJSON* network = cJSON_CreateObject();
            cJSON_AddStringToObject(network, "wifi_mode", "AP"); // 模拟数据
            cJSON_AddStringToObject(network, "ap_ssid", "XiaoZhi-ESP32"); // 模拟数据
            cJSON_AddItemToObject(data, "network", network);
        }
        else if (category == "vehicle") {
            // 车辆配置
            cJSON* vehicle = cJSON_CreateObject();
            cJSON_AddNumberToObject(vehicle, "default_speed", 150); // 模拟数据
            cJSON_AddNumberToObject(vehicle, "max_speed", 255); // 模拟数据
            cJSON_AddItemToObject(data, "vehicle", vehicle);
        }
        else if (category == "vision") {
            // 视觉配置
            cJSON* vision = cJSON_CreateObject();
            cJSON_AddNumberToObject(vision, "default_brightness", 0); // 模拟数据
            cJSON_AddNumberToObject(vision, "default_contrast", 0); // 模拟数据
            cJSON_AddItemToObject(data, "vision", vision);
        }
        else {
            // 未知类别
            return CreateApiErrorResponse(400, "Unknown configuration category: " + category);
        }
    }
    else {
        // 返回所有配置
        
        // 网络配置
        cJSON* network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "wifi_mode", "AP"); // 模拟数据
        cJSON_AddStringToObject(network, "ap_ssid", "XiaoZhi-ESP32"); // 模拟数据
        cJSON_AddItemToObject(data, "network", network);
        
        // 车辆配置
        cJSON* vehicle = cJSON_CreateObject();
        cJSON_AddNumberToObject(vehicle, "default_speed", 150); // 模拟数据
        cJSON_AddNumberToObject(vehicle, "max_speed", 255); // 模拟数据
        cJSON_AddItemToObject(data, "vehicle", vehicle);
        
        // 视觉配置
        cJSON* vision = cJSON_CreateObject();
        cJSON_AddNumberToObject(vision, "default_brightness", 0); // 模拟数据
        cJSON_AddNumberToObject(vision, "default_contrast", 0); // 模拟数据
        cJSON_AddItemToObject(data, "vision", vision);
    }
    
    return CreateApiSuccessResponse("Configuration retrieved", data);
}

// 设置配置API
ApiResponse HandleConfigSet(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing set config request");
    
    // 解析JSON请求体
    cJSON* json = ParseRequestJson(req);
    if (!json) {
        return CreateApiErrorResponse(400, "Invalid JSON request");
    }
    
    // 检查配置类别
    std::string category;
    cJSON* cat_obj = cJSON_GetObjectItem(json, "category");
    if (cat_obj && cJSON_IsString(cat_obj)) {
        category = cat_obj->valuestring;
    } else {
        cJSON_Delete(json);
        return CreateApiErrorResponse(400, "Missing 'category' field");
    }
    
    // 获取配置数据
    cJSON* config_obj = cJSON_GetObjectItem(json, "config");
    if (!config_obj || !cJSON_IsObject(config_obj)) {
        cJSON_Delete(json);
        return CreateApiErrorResponse(400, "Missing or invalid 'config' field");
    }
    
    // 处理不同类别的配置
    bool success = false;
    std::string message;
    
    if (category == "network") {
        // 处理网络配置
        cJSON* wifi_mode = cJSON_GetObjectItem(config_obj, "wifi_mode");
        if (wifi_mode && cJSON_IsString(wifi_mode)) {
            // 应用wifi_mode配置，实际应调用相应组件的功能
            ESP_LOGI(TAG, "Setting WiFi mode to: %s", wifi_mode->valuestring);
            success = true;
            message = "Network configuration updated";
        }
    }
    else if (category == "vehicle") {
        // 处理车辆配置
        cJSON* default_speed = cJSON_GetObjectItem(config_obj, "default_speed");
        if (default_speed && cJSON_IsNumber(default_speed)) {
            // 应用default_speed配置，实际应调用相应组件的功能
            ESP_LOGI(TAG, "Setting default speed to: %d", default_speed->valueint);
            success = true;
            message = "Vehicle configuration updated";
        }
    }
    else if (category == "vision") {
        // 处理视觉配置
        cJSON* default_brightness = cJSON_GetObjectItem(config_obj, "default_brightness");
        if (default_brightness && cJSON_IsNumber(default_brightness)) {
            // 应用default_brightness配置，实际应调用相应组件的功能
            ESP_LOGI(TAG, "Setting default brightness to: %d", default_brightness->valueint);
            success = true;
            message = "Vision configuration updated";
        }
    }
    else {
        message = "Unknown configuration category: " + category;
    }
    
    cJSON_Delete(json);
    
    if (success) {
        return CreateApiSuccessResponse(message);
    } else {
        return CreateApiErrorResponse(400, message.empty() ? "Failed to update configuration" : message);
    }
} 