#include "api.h"
#include "esp_log.h"
#include "../hardware/hardware_manager.h"
#include "../hardware/simple_error_handler.h"
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_app_desc.h>
#include <sys/time.h>
#include <string.h>
#include <algorithm>

#define TAG "API"

// 全局硬件管理器指针
static HardwareManager* g_hardware_manager = nullptr;

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
    
    // 注册摄像头API
    RegisterApiHandler(web, HttpMethod::HTTP_GET, "/camera/status", HandleCameraStatus);
    RegisterApiHandler(web, HttpMethod::HTTP_GET, "/camera/stream", HandleCameraStream);
    RegisterApiHandler(web, HttpMethod::HTTP_GET, "/camera/capture", HandleCameraCapture);
    RegisterApiHandler(web, HttpMethod::HTTP_POST, "/camera/settings", HandleCameraSettings);
    
    // 注册传感器API
    RegisterApiHandler(web, HttpMethod::HTTP_GET, "/sensors", HandleSensorData);
    RegisterApiHandler(web, HttpMethod::HTTP_GET, "/sensors/*", HandleSensorDataById);
    
    // 注册执行器控制API
    RegisterApiHandler(web, HttpMethod::HTTP_POST, "/motors/control", HandleMotorControl);
    RegisterApiHandler(web, HttpMethod::HTTP_POST, "/servos/control", HandleServoControl);
    
    // 注册硬件状态API
    RegisterApiHandler(web, HttpMethod::HTTP_GET, "/hardware/status", HandleHardwareStatus);
    
    // 注册硬件配置管理API
    RegisterApiHandler(web, HttpMethod::HTTP_GET, "/hardware/config", HandleHardwareConfig);
    RegisterApiHandler(web, HttpMethod::HTTP_POST, "/hardware/config", HandleHardwareConfig);
    
    // 注册错误查询API
    RegisterApiHandler(web, HttpMethod::HTTP_GET, "/errors", HandleErrorQuery);
    RegisterApiHandler(web, HttpMethod::HTTP_DELETE, "/errors", HandleErrorQuery);
    
    ESP_LOGI(TAG, "API initialization completed with hardware endpoints");
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
    response.type = ApiResponseType::JSON;
    
    // 创建响应JSON
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", message.c_str());
    
    // 添加时间戳
    struct timeval tv;
    gettimeofday(&tv, NULL);
    cJSON_AddNumberToObject(root, "timestamp", tv.tv_sec * 1000 + tv.tv_usec / 1000);
    
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
    response.type = ApiResponseType::JSON;
    
    // 创建响应JSON
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", false);
    cJSON_AddStringToObject(root, "message", message.c_str());
    cJSON_AddNumberToObject(root, "code", status_code);
    
    // 添加时间戳
    struct timeval tv;
    gettimeofday(&tv, NULL);
    cJSON_AddNumberToObject(root, "timestamp", tv.tv_sec * 1000 + tv.tv_usec / 1000);
    
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

// 摄像头状态API
ApiResponse HandleCameraStatus(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing camera status request");
    
    // 创建相机状态数据
    cJSON* data = cJSON_CreateObject();
    
    // 这里应从实际的相机组件获取数据
    // 目前使用模拟数据进行测试
    bool has_camera = true; // 实际应检测摄像头是否存在
    bool is_streaming = false; // 实际应检查是否正在流式传输
    
    cJSON_AddBoolToObject(data, "has_camera", has_camera);
    cJSON_AddBoolToObject(data, "is_streaming", is_streaming);
    cJSON_AddNumberToObject(data, "width", 640); // 模拟数据
    cJSON_AddNumberToObject(data, "height", 480); // 模拟数据
    
    return CreateApiSuccessResponse("Camera status retrieved", data);
}

// 摄像头流API
ApiResponse HandleCameraStream(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing camera stream request");
    
    // 解析查询参数
    std::map<std::string, std::string> params = Web::ParseQueryParams(req);
    
    // 获取分辨率参数(如果指定了)
    int width = 640; // 默认值
    int height = 480; // 默认值
    
    if (params.find("width") != params.end()) {
        try {
            width = std::stoi(params["width"]);
        } catch (...) {
            ESP_LOGW(TAG, "Invalid width parameter: %s", params["width"].c_str());
        }
    }
    
    if (params.find("height") != params.end()) {
        try {
            height = std::stoi(params["height"]);
        } catch (...) {
            ESP_LOGW(TAG, "Invalid height parameter: %s", params["height"].c_str());
        }
    }
    
    // 创建响应数据
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "stream_type", "mjpeg");
    cJSON_AddStringToObject(data, "url", "/stream");  // 实际流媒体URL
    cJSON_AddNumberToObject(data, "width", width);
    cJSON_AddNumberToObject(data, "height", height);
    
    return CreateApiSuccessResponse("Camera stream information", data);
}

// 摄像头捕获API
ApiResponse HandleCameraCapture(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing camera capture request");
    
    // 实际上应该捕获一张照片并返回
    // 目前仅返回模拟数据
    ApiResponse response;
    response.status_code = 200;
    response.type = ApiResponseType::JSON;
    
    // 创建响应数据
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "capture_url", "/captures/latest.jpg");  // 模拟数据
    cJSON_AddStringToObject(data, "timestamp", "2023-10-18T15:30:45Z");  // 模拟数据
    
    char* json_str = cJSON_PrintUnformatted(data);
    if (json_str) {
        response.content = json_str;
        free(json_str);
    } else {
        response.content = "{\"success\":true,\"message\":\"Image captured\",\"data\":{\"capture_url\":\"/captures/latest.jpg\"}}";
    }
    
    cJSON_Delete(data);
    
    return response;
}

// 摄像头设置API
ApiResponse HandleCameraSettings(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing camera settings request");
    
    // 解析JSON请求体
    cJSON* json = ParseRequestJson(req);
    if (!json) {
        return CreateApiErrorResponse(400, "Invalid JSON request");
    }
    
    // 分析设置项
    bool success = false;
    std::string message;
    
    // 亮度
    cJSON* brightness = cJSON_GetObjectItem(json, "brightness");
    if (brightness && cJSON_IsNumber(brightness)) {
        int brightness_val = brightness->valueint;
        // 应用亮度设置，实际应调用相机组件的功能
        ESP_LOGI(TAG, "Setting camera brightness to: %d", brightness_val);
        success = true;
    }
    
    // 对比度
    cJSON* contrast = cJSON_GetObjectItem(json, "contrast");
    if (contrast && cJSON_IsNumber(contrast)) {
        int contrast_val = contrast->valueint;
        // 应用对比度设置，实际应调用相机组件的功能
        ESP_LOGI(TAG, "Setting camera contrast to: %d", contrast_val);
        success = true;
    }
    
    // 饱和度
    cJSON* saturation = cJSON_GetObjectItem(json, "saturation");
    if (saturation && cJSON_IsNumber(saturation)) {
        int saturation_val = saturation->valueint;
        // 应用饱和度设置，实际应调用相机组件的功能
        ESP_LOGI(TAG, "Setting camera saturation to: %d", saturation_val);
        success = true;
    }
    
    // 分辨率
    cJSON* resolution = cJSON_GetObjectItem(json, "resolution");
    if (resolution && cJSON_IsObject(resolution)) {
        cJSON* width = cJSON_GetObjectItem(resolution, "width");
        cJSON* height = cJSON_GetObjectItem(resolution, "height");
        
        if (width && cJSON_IsNumber(width) && height && cJSON_IsNumber(height)) {
            int width_val = width->valueint;
            int height_val = height->valueint;
            // 应用分辨率设置，实际应调用相机组件的功能
            ESP_LOGI(TAG, "Setting camera resolution to: %dx%d", width_val, height_val);
            success = true;
        }
    }
    
    cJSON_Delete(json);
    
    if (success) {
        message = "Camera settings updated";
        return CreateApiSuccessResponse(message);
    } else {
        message = "No valid camera settings found in request";
        return CreateApiErrorResponse(400, message);
    }
}

//=====================
// 硬件管理器设置函数
//=====================

void SetHardwareManager(HardwareManager* manager) {
    g_hardware_manager = manager;
    ESP_LOGI(TAG, "Hardware manager set for API module");
}

//=====================
// 硬件API处理函数实现
//=====================

// 获取所有传感器数据API
ApiResponse HandleSensorData(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing get all sensors data request");
    
    if (!g_hardware_manager) {
        return CreateApiErrorResponse(503, "Hardware manager not available");
    }
    
    try {
        // 获取所有传感器数据
        std::vector<sensor_reading_t> readings = g_hardware_manager->ReadAllSensors();
        
        // 创建响应数据
        cJSON* data = cJSON_CreateObject();
        cJSON* sensors_array = cJSON_CreateArray();
        
        // 添加时间戳
        struct timeval tv;
        gettimeofday(&tv, NULL);
        cJSON_AddNumberToObject(data, "timestamp", tv.tv_sec * 1000 + tv.tv_usec / 1000);
        
        // 转换传感器数据为JSON
        for (const auto& reading : readings) {
            cJSON* sensor_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(sensor_obj, "id", reading.sensor_id.c_str());
            cJSON_AddStringToObject(sensor_obj, "type", reading.type.c_str());
            cJSON_AddNumberToObject(sensor_obj, "value", reading.value);
            cJSON_AddStringToObject(sensor_obj, "unit", reading.unit.c_str());
            cJSON_AddNumberToObject(sensor_obj, "timestamp", reading.timestamp);
            cJSON_AddBoolToObject(sensor_obj, "valid", reading.valid);
            
            cJSON_AddItemToArray(sensors_array, sensor_obj);
        }
        
        cJSON_AddItemToObject(data, "sensors", sensors_array);
        cJSON_AddNumberToObject(data, "count", readings.size());
        
        return CreateApiSuccessResponse("Sensor data retrieved successfully", data);
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in HandleSensorData: %s", e.what());
        return CreateApiErrorResponse(500, std::string("Failed to read sensor data: ") + e.what());
    }
}

// 获取特定传感器数据API
ApiResponse HandleSensorDataById(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing get sensor data by ID request");
    
    if (!g_hardware_manager) {
        return CreateApiErrorResponse(503, "Hardware manager not available");
    }
    
    // 解析URL参数获取传感器ID
    std::map<std::string, std::string> params = Web::ParseQueryParams(req);
    std::string sensor_id;
    
    // 从URI路径中提取传感器ID (格式: /api/sensors/{id})
    std::string uri = req->uri;
    size_t last_slash = uri.find_last_of('/');
    if (last_slash != std::string::npos && last_slash < uri.length() - 1) {
        sensor_id = uri.substr(last_slash + 1);
    }
    
    if (sensor_id.empty()) {
        return CreateApiErrorResponse(400, "Sensor ID is required");
    }
    
    try {
        // 读取特定传感器数据
        sensor_reading_t reading = g_hardware_manager->ReadSensor(sensor_id);
        
        if (!reading.valid) {
            return CreateApiErrorResponse(404, "Sensor not found or reading invalid: " + sensor_id);
        }
        
        // 创建响应数据
        cJSON* data = cJSON_CreateObject();
        cJSON* sensor_obj = cJSON_CreateObject();
        
        cJSON_AddStringToObject(sensor_obj, "id", reading.sensor_id.c_str());
        cJSON_AddStringToObject(sensor_obj, "type", reading.type.c_str());
        cJSON_AddNumberToObject(sensor_obj, "value", reading.value);
        cJSON_AddStringToObject(sensor_obj, "unit", reading.unit.c_str());
        cJSON_AddNumberToObject(sensor_obj, "timestamp", reading.timestamp);
        cJSON_AddBoolToObject(sensor_obj, "valid", reading.valid);
        
        cJSON_AddItemToObject(data, "sensor", sensor_obj);
        
        return CreateApiSuccessResponse("Sensor data retrieved successfully", data);
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in HandleSensorDataById: %s", e.what());
        return CreateApiErrorResponse(500, std::string("Failed to read sensor data: ") + e.what());
    }
}

// 电机控制API
ApiResponse HandleMotorControl(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing motor control request");
    
    if (!g_hardware_manager) {
        return CreateApiErrorResponse(503, "Hardware manager not available");
    }
    
    // 解析JSON请求体
    cJSON* json = ParseRequestJson(req);
    if (!json) {
        return CreateApiErrorResponse(400, "Invalid JSON request");
    }
    
    // 验证必需参数
    cJSON* motor_id_obj = cJSON_GetObjectItem(json, "motor_id");
    cJSON* speed_obj = cJSON_GetObjectItem(json, "speed");
    
    if (!motor_id_obj || !cJSON_IsNumber(motor_id_obj)) {
        cJSON_Delete(json);
        return CreateApiErrorResponse(400, "Missing or invalid 'motor_id' field");
    }
    
    if (!speed_obj || !cJSON_IsNumber(speed_obj)) {
        cJSON_Delete(json);
        return CreateApiErrorResponse(400, "Missing or invalid 'speed' field");
    }
    
    int motor_id = motor_id_obj->valueint;
    int speed = speed_obj->valueint;
    
    // 参数验证
    if (motor_id < 0 || motor_id > 15) {
        cJSON_Delete(json);
        return CreateApiErrorResponse(400, "Motor ID must be between 0 and 15");
    }
    
    if (speed < -255 || speed > 255) {
        cJSON_Delete(json);
        return CreateApiErrorResponse(400, "Speed must be between -255 and 255");
    }
    
    try {
        // 执行电机控制
        bool success = g_hardware_manager->SetMotorSpeed(motor_id, speed);
        
        cJSON_Delete(json);
        
        if (success) {
            // 创建响应数据
            cJSON* data = cJSON_CreateObject();
            cJSON_AddNumberToObject(data, "motor_id", motor_id);
            cJSON_AddNumberToObject(data, "speed", speed);
            
            struct timeval tv;
            gettimeofday(&tv, NULL);
            cJSON_AddNumberToObject(data, "timestamp", tv.tv_sec * 1000 + tv.tv_usec / 1000);
            
            return CreateApiSuccessResponse("Motor control executed successfully", data);
        } else {
            return CreateApiErrorResponse(500, "Failed to control motor");
        }
        
    } catch (const std::exception& e) {
        cJSON_Delete(json);
        ESP_LOGE(TAG, "Exception in HandleMotorControl: %s", e.what());
        return CreateApiErrorResponse(500, std::string("Motor control error: ") + e.what());
    }
}

// 舵机控制API
ApiResponse HandleServoControl(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing servo control request");
    
    if (!g_hardware_manager) {
        return CreateApiErrorResponse(503, "Hardware manager not available");
    }
    
    // 解析JSON请求体
    cJSON* json = ParseRequestJson(req);
    if (!json) {
        return CreateApiErrorResponse(400, "Invalid JSON request");
    }
    
    // 验证必需参数
    cJSON* servo_id_obj = cJSON_GetObjectItem(json, "servo_id");
    cJSON* angle_obj = cJSON_GetObjectItem(json, "angle");
    
    if (!servo_id_obj || !cJSON_IsNumber(servo_id_obj)) {
        cJSON_Delete(json);
        return CreateApiErrorResponse(400, "Missing or invalid 'servo_id' field");
    }
    
    if (!angle_obj || !cJSON_IsNumber(angle_obj)) {
        cJSON_Delete(json);
        return CreateApiErrorResponse(400, "Missing or invalid 'angle' field");
    }
    
    int servo_id = servo_id_obj->valueint;
    int angle = angle_obj->valueint;
    
    // 参数验证
    if (servo_id < 0 || servo_id > 15) {
        cJSON_Delete(json);
        return CreateApiErrorResponse(400, "Servo ID must be between 0 and 15");
    }
    
    if (angle < 0 || angle > 180) {
        cJSON_Delete(json);
        return CreateApiErrorResponse(400, "Angle must be between 0 and 180 degrees");
    }
    
    try {
        // 执行舵机控制
        bool success = g_hardware_manager->SetServoAngle(servo_id, angle);
        
        cJSON_Delete(json);
        
        if (success) {
            // 创建响应数据
            cJSON* data = cJSON_CreateObject();
            cJSON_AddNumberToObject(data, "servo_id", servo_id);
            cJSON_AddNumberToObject(data, "angle", angle);
            
            struct timeval tv;
            gettimeofday(&tv, NULL);
            cJSON_AddNumberToObject(data, "timestamp", tv.tv_sec * 1000 + tv.tv_usec / 1000);
            
            return CreateApiSuccessResponse("Servo control executed successfully", data);
        } else {
            return CreateApiErrorResponse(500, "Failed to control servo");
        }
        
    } catch (const std::exception& e) {
        cJSON_Delete(json);
        ESP_LOGE(TAG, "Exception in HandleServoControl: %s", e.what());
        return CreateApiErrorResponse(500, std::string("Servo control error: ") + e.what());
    }
}

// 硬件状态API
ApiResponse HandleHardwareStatus(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing hardware status request");
    
    if (!g_hardware_manager) {
        return CreateApiErrorResponse(503, "Hardware manager not available");
    }
    
    try {
        // 创建响应数据
        cJSON* data = cJSON_CreateObject();
        
        // 添加时间戳
        struct timeval tv;
        gettimeofday(&tv, NULL);
        cJSON_AddNumberToObject(data, "timestamp", tv.tv_sec * 1000 + tv.tv_usec / 1000);
        
        // 硬件管理器状态
        cJSON* hardware_manager_status = cJSON_CreateObject();
        cJSON_AddBoolToObject(hardware_manager_status, "initialized", true);
        cJSON_AddStringToObject(hardware_manager_status, "version", "1.0.0");
        cJSON_AddItemToObject(data, "hardware_manager", hardware_manager_status);
        
        // 多路复用器状态
        cJSON* expanders = cJSON_CreateObject();
        
        // PCA9548A多路复用器状态
        cJSON* pca9548a = cJSON_CreateObject();
        cJSON_AddBoolToObject(pca9548a, "initialized", true);
        cJSON_AddStringToObject(pca9548a, "type", "I2C Multiplexer");
        cJSON_AddNumberToObject(pca9548a, "channels", 8);
        cJSON_AddItemToObject(expanders, "pca9548a", pca9548a);
        
        // PCF8575 GPIO多路复用器状态
        cJSON* pcf8575 = cJSON_CreateObject();
        cJSON_AddBoolToObject(pcf8575, "initialized", true);
        cJSON_AddStringToObject(pcf8575, "type", "GPIO Multiplexer");
        cJSON_AddNumberToObject(pcf8575, "pins", 16);
        cJSON_AddItemToObject(expanders, "pcf8575", pcf8575);
        
        // LU9685 PWM控制器状态
        cJSON* lu9685 = cJSON_CreateObject();
        cJSON_AddBoolToObject(lu9685, "initialized", true);
        cJSON_AddStringToObject(lu9685, "type", "PWM Controller");
        cJSON_AddNumberToObject(lu9685, "channels", 16);
        cJSON_AddItemToObject(expanders, "lu9685", lu9685);
        
        // HW178模拟多路复用器状态
        cJSON* hw178 = cJSON_CreateObject();
        cJSON_AddBoolToObject(hw178, "initialized", true);
        cJSON_AddStringToObject(hw178, "type", "Analog Multiplexer");
        cJSON_AddNumberToObject(hw178, "channels", 8);
        cJSON_AddItemToObject(expanders, "hw178", hw178);
        
        cJSON_AddItemToObject(data, "expanders", expanders);
        
        // 获取执行器状态
        std::vector<actuator_status_t> actuator_statuses = g_hardware_manager->GetActuatorStatus();
        
        // 传感器状态摘要
        cJSON* sensors_summary = cJSON_CreateObject();
        std::vector<sensor_reading_t> readings = g_hardware_manager->ReadAllSensors();
        
        int active_sensors = 0;
        int valid_readings = 0;
        for (const auto& reading : readings) {
            active_sensors++;
            if (reading.valid) {
                valid_readings++;
            }
        }
        
        cJSON_AddNumberToObject(sensors_summary, "total_configured", active_sensors);
        cJSON_AddNumberToObject(sensors_summary, "active", valid_readings);
        cJSON_AddNumberToObject(sensors_summary, "inactive", active_sensors - valid_readings);
        cJSON_AddItemToObject(data, "sensors_summary", sensors_summary);
        
        // 执行器状态摘要
        cJSON* actuators_summary = cJSON_CreateObject();
        int total_actuators = actuator_statuses.size();
        int enabled_actuators = 0;
        int motors = 0;
        int servos = 0;
        
        for (const auto& status : actuator_statuses) {
            if (status.enabled) {
                enabled_actuators++;
            }
            if (status.type == "motor") {
                motors++;
            } else if (status.type == "servo") {
                servos++;
            }
        }
        
        cJSON_AddNumberToObject(actuators_summary, "total_configured", total_actuators);
        cJSON_AddNumberToObject(actuators_summary, "enabled", enabled_actuators);
        cJSON_AddNumberToObject(actuators_summary, "motors", motors);
        cJSON_AddNumberToObject(actuators_summary, "servos", servos);
        cJSON_AddItemToObject(data, "actuators_summary", actuators_summary);
        
        // 系统健康状态
        cJSON* health = cJSON_CreateObject();
        bool overall_healthy = (valid_readings == active_sensors) && (enabled_actuators > 0 || total_actuators == 0);
        cJSON_AddBoolToObject(health, "overall_status", overall_healthy);
        cJSON_AddStringToObject(health, "status_message", overall_healthy ? "All systems operational" : "Some issues detected");
        cJSON_AddItemToObject(data, "health", health);
        
        return CreateApiSuccessResponse("Hardware status retrieved successfully", data);
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in HandleHardwareStatus: %s", e.what());
        return CreateApiErrorResponse(500, std::string("Failed to get hardware status: ") + e.what());
    }
}

// 硬件配置管理API
ApiResponse HandleHardwareConfig(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing hardware config request");
    
    if (!g_hardware_manager) {
        return CreateApiErrorResponse(503, "Hardware manager not available");
    }
    
    if (req->method == HTTP_GET) {
        // 获取当前硬件配置
        try {
            cJSON* data = cJSON_CreateObject();
            
            // 添加配置信息（这里应该从实际配置文件读取）
            cJSON* config = cJSON_CreateObject();
            
            // 传感器配置示例
            cJSON* sensors = cJSON_CreateArray();
            cJSON* sensor1 = cJSON_CreateObject();
            cJSON_AddStringToObject(sensor1, "id", "temp_01");
            cJSON_AddStringToObject(sensor1, "type", "temperature");
            cJSON_AddStringToObject(sensor1, "multiplexer", "hw178");
            cJSON_AddNumberToObject(sensor1, "channel", 0);
            cJSON_AddItemToArray(sensors, sensor1);
            cJSON_AddItemToObject(config, "sensors", sensors);
            
            // 电机配置示例
            cJSON* motors = cJSON_CreateArray();
            cJSON* motor1 = cJSON_CreateObject();
            cJSON_AddNumberToObject(motor1, "id", 0);
            cJSON_AddStringToObject(motor1, "connection_type", "pcf8575");
            cJSON* pins = cJSON_CreateObject();
            cJSON_AddNumberToObject(pins, "ena", 2);
            cJSON_AddNumberToObject(pins, "in1", 0);
            cJSON_AddNumberToObject(pins, "in2", 1);
            cJSON_AddItemToObject(motor1, "pins", pins);
            cJSON_AddItemToArray(motors, motor1);
            cJSON_AddItemToObject(config, "motors", motors);
            
            // 舵机配置示例
            cJSON* servos = cJSON_CreateArray();
            cJSON* servo1 = cJSON_CreateObject();
            cJSON_AddNumberToObject(servo1, "id", 0);
            cJSON_AddStringToObject(servo1, "connection_type", "lu9685");
            cJSON_AddNumberToObject(servo1, "channel", 0);
            cJSON_AddNumberToObject(servo1, "min_angle", 0);
            cJSON_AddNumberToObject(servo1, "max_angle", 180);
            cJSON_AddItemToArray(servos, servo1);
            cJSON_AddItemToObject(config, "servos", servos);
            
            cJSON_AddItemToObject(data, "hardware_config", config);
            
            return CreateApiSuccessResponse("Hardware configuration retrieved", data);
            
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Exception in HandleHardwareConfig (GET): %s", e.what());
            return CreateApiErrorResponse(500, std::string("Failed to get hardware config: ") + e.what());
        }
    }
    else if (req->method == HTTP_POST) {
        // 更新硬件配置
        cJSON* json = ParseRequestJson(req);
        if (!json) {
            return CreateApiErrorResponse(400, "Invalid JSON request");
        }
        
        try {
            // 这里应该验证配置并保存到文件
            // 目前只是模拟响应
            cJSON_Delete(json);
            
            cJSON* data = cJSON_CreateObject();
            cJSON_AddStringToObject(data, "status", "Configuration updated successfully");
            cJSON_AddBoolToObject(data, "restart_required", true);
            
            return CreateApiSuccessResponse("Hardware configuration updated", data);
            
        } catch (const std::exception& e) {
            cJSON_Delete(json);
            ESP_LOGE(TAG, "Exception in HandleHardwareConfig (POST): %s", e.what());
            return CreateApiErrorResponse(500, std::string("Failed to update hardware config: ") + e.what());
        }
    }
    else {
        return CreateApiErrorResponse(405, "Method not allowed");
    }
}

/*
 * 硬件API端点总结:
 * 
 * 传感器API:
 * - GET /api/sensors - 获取所有传感器数据
 * - GET /api/sensors/{id} - 获取特定传感器数据
 * 
 * 执行器控制API:
 * - POST /api/motors/control - 控制电机 (参数: motor_id, speed)
 * - POST /api/servos/control - 控制舵机 (参数: servo_id, angle)
 * 
 * 硬件状态API:
 * - GET /api/hardware/status - 获取硬件状态摘要
 * 
 * 配置管理API:
 * - GET /api/hardware/config - 获取硬件配置
 * - POST /api/hardware/config - 更新硬件配置
 * 
 * 所有API都返回统一的JSON格式:
 * {
 *   "success": true/false,
 *   "message": "描述信息",
 *   "timestamp": 时间戳,
 *   "data": {...} // 成功时的数据
 *   "code": 错误码 // 失败时的错误码
 * }
 */ 
// 错误查询API
ApiResponse HandleErrorQuery(httpd_req_t* req) {
    ESP_LOGI(TAG, "Handling error query request");
    
    try {
        if (req->method == HTTP_GET) {
            // 获取查询参数
            std::map<std::string, std::string> params = Web::ParseQueryParams(req);
            
            // 检查是否查询特定组件的错误
            std::string component = params.count("component") ? params["component"] : "";
            
            // 获取最大记录数
            size_t max_count = 50;
            if (params.count("limit")) {
                try {
                    max_count = std::stoul(params["limit"]);
                    if (max_count > 200) max_count = 200; // 限制最大值
                } catch (...) {
                    max_count = 50;
                }
            }
            
            cJSON* data = cJSON_CreateObject();
            
            // 添加错误统计信息
            cJSON* stats = cJSON_CreateObject();
            cJSON_AddNumberToObject(stats, "total", SimpleErrorHandler::GetTotalErrorCount());
            cJSON_AddNumberToObject(stats, "info", SimpleErrorHandler::GetErrorCount(SimpleErrorHandler::ErrorLevel::INFO));
            cJSON_AddNumberToObject(stats, "warning", SimpleErrorHandler::GetErrorCount(SimpleErrorHandler::ErrorLevel::WARNING));
            cJSON_AddNumberToObject(stats, "error", SimpleErrorHandler::GetErrorCount(SimpleErrorHandler::ErrorLevel::ERROR));
            cJSON_AddNumberToObject(stats, "critical", SimpleErrorHandler::GetErrorCount(SimpleErrorHandler::ErrorLevel::CRITICAL));
            cJSON_AddBoolToObject(stats, "has_critical", SimpleErrorHandler::HasCriticalErrors());
            cJSON_AddItemToObject(data, "statistics", stats);
            
            // 获取错误记录
            std::vector<SimpleErrorHandler::ErrorRecord> errors;
            if (!component.empty()) {
                errors = SimpleErrorHandler::GetComponentErrors(component, max_count);
            } else {
                errors = SimpleErrorHandler::GetRecentErrors(max_count);
            }
            
            // 转换为JSON数组
            cJSON* errors_array = cJSON_CreateArray();
            for (const auto& error : errors) {
                cJSON* error_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(error_obj, "level", SimpleErrorHandler::ErrorLevelToString(error.level).c_str());
                cJSON_AddStringToObject(error_obj, "component", error.component.c_str());
                cJSON_AddStringToObject(error_obj, "message", error.message.c_str());
                cJSON_AddNumberToObject(error_obj, "count", error.count);
                
                // 转换时间戳
                auto time_t = std::chrono::system_clock::to_time_t(error.timestamp);
                cJSON_AddNumberToObject(error_obj, "timestamp", time_t);
                
                cJSON_AddItemToArray(errors_array, error_obj);
            }
            cJSON_AddItemToObject(data, "errors", errors_array);
            
            // 如果查询特定组件，添加恢复建议
            if (!component.empty()) {
                auto suggestions = SimpleErrorHandler::GetRecoverySuggestions(component);
                cJSON* suggestions_array = cJSON_CreateArray();
                for (const auto& suggestion : suggestions) {
                    cJSON_AddItemToArray(suggestions_array, cJSON_CreateString(suggestion.c_str()));
                }
                cJSON_AddItemToObject(data, "recovery_suggestions", suggestions_array);
            }
            
            return CreateApiSuccessResponse("Error information retrieved successfully", data);
            
        } else if (req->method == HTTP_DELETE) {
            // 清除错误记录
            cJSON* json = ParseRequestJson(req);
            if (json) {
                cJSON* component_json = cJSON_GetObjectItem(json, "component");
                if (component_json && cJSON_IsString(component_json)) {
                    std::string component = cJSON_GetStringValue(component_json);
                    SimpleErrorHandler::ClearComponentErrors(component);
                    cJSON_Delete(json);
                    return CreateApiSuccessResponse("Component errors cleared: " + component);
                }
                cJSON_Delete(json);
            }
            
            // 清除所有错误
            SimpleErrorHandler::ClearErrors();
            return CreateApiSuccessResponse("All errors cleared");
            
        } else {
            return CreateApiErrorResponse(405, "Method not allowed");
        }
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in HandleErrorQuery: %s", e.what());
        return CreateApiErrorResponse(500, std::string("Failed to query errors: ") + e.what());
    }
}