#include "location.h"
#include <esp_log.h>
#include <cJSON.h>
#include <algorithm>
#include <string>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "Location"

// 构造函数
Location::Location(Web* web_server)
    : web_server_(web_server), 
      running_(false),
      gps_running_(false),
      gps_task_handle_(nullptr) {
    ESP_LOGI(TAG, "Location component created");
}

// 析构函数
Location::~Location() {
    if (running_) {
        Stop();
    }
    
    ESP_LOGI(TAG, "Location component destroyed");
}

// Component接口实现
bool Location::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Location already running");
        return true;
    }
    
    ESP_LOGI(TAG, "Starting Location component");
    
    // 如果Web服务器可用，初始化处理器
    if (web_server_ && web_server_->IsRunning()) {
        InitHandlers();
    }
    
    running_ = true;
    return true;
}

void Location::Stop() {
    if (!running_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping Location component");
    
    // 停止GPS任务
    if (gps_running_) {
        StopGPS();
    }
    
    running_ = false;
}

bool Location::IsRunning() const {
    return running_;
}

const char* Location::GetName() const {
    return "Location";
}

// GPS和定位功能
bool Location::StartGPS() {
    if (!running_) {
        ESP_LOGW(TAG, "Location component not running");
        return false;
    }
    
    if (gps_running_) {
        ESP_LOGW(TAG, "GPS already running");
        return true;
    }
    
    ESP_LOGI(TAG, "Starting GPS");
    
    // 创建GPS任务
    BaseType_t result = xTaskCreate(
        GpsTask,
        "gps_task",
        4096,  // 堆栈大小
        this,  // 任务参数
        5,     // 优先级
        &gps_task_handle_);
        
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GPS task");
        return false;
    }
    
    gps_running_ = true;
    return true;
}

void Location::StopGPS() {
    if (!gps_running_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping GPS");
    
    // 停止GPS任务
    if (gps_task_handle_) {
        vTaskDelete(gps_task_handle_);
        gps_task_handle_ = nullptr;
    }
    
    gps_running_ = false;
}

bool Location::IsGpsRunning() const {
    return gps_running_;
}

GpsCoordinate Location::GetCurrentLocation() const {
    return current_location_;
}

// 注册位置更新回调
void Location::RegisterLocationUpdateCallback(LocationUpdateCallback callback) {
    if (callback) {
        location_callbacks_.push_back(callback);
    }
}

// WebSocket消息处理
void Location::HandleWebSocketMessage(httpd_req_t* req, const std::string& message) {
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
        
        if (msg_type == "startGps") {
            // 启动GPS
            if (StartGPS()) {
                // 发送确认消息
                std::string response = "{\"type\":\"gpsStatus\",\"status\":\"started\"}";
                web_server_->SendWebSocketMessage(req, response);
            }
        }
        else if (msg_type == "stopGps") {
            // 停止GPS
            StopGPS();
            // 发送确认消息
            std::string response = "{\"type\":\"gpsStatus\",\"status\":\"stopped\"}";
            web_server_->SendWebSocketMessage(req, response);
        }
        else if (msg_type == "getLocation") {
            // 获取当前位置
            GpsCoordinate location = GetCurrentLocation();
            
            // 构建JSON响应
            char response[256];
            snprintf(response, sizeof(response), 
                    "{\"type\":\"locationData\","
                    "\"latitude\":%f,"
                    "\"longitude\":%f,"
                    "\"altitude\":%f,"
                    "\"speed\":%f,"
                    "\"course\":%f,"
                    "\"valid\":%s}",
                    location.latitude, location.longitude, 
                    location.altitude, location.speed, 
                    location.course, location.valid ? "true" : "false");
                    
            web_server_->SendWebSocketMessage(req, response);
        }
    }
    
    cJSON_Delete(root);
}

// 网页UI处理
void Location::InitHandlers() {
    if (!web_server_) {
        ESP_LOGE(TAG, "Web server not initialized");
        return;
    }
    
    ESP_LOGI(TAG, "Registering Location handlers");
    
    // 注册Web页面处理器
    web_server_->RegisterHandler(HttpMethod::HTTP_GET, "/location", [this](httpd_req_t* req) {
        ESP_LOGI(TAG, "Processing Location UI request");
        
        // 设置内容类型
        httpd_resp_set_type(req, "text/html");
        
        // 从请求头中获取语言设置
        char lang_buf[64] = {0};
        httpd_req_get_hdr_value_str(req, "Accept-Language", lang_buf, sizeof(lang_buf));
        std::string html = GetLocationHtml(lang_buf[0] ? lang_buf : nullptr);
        
        httpd_resp_send(req, html.c_str(), html.length());
        return ESP_OK;
    });
    
    // 注册API处理器 - 只保留位置获取API
    web_server_->RegisterApiHandler(HttpMethod::HTTP_GET, "/location", 
                                   [this](httpd_req_t* req) {
                                       return HandleGetLocation(req);
                                   });
                                   
    // 注册WebSocket消息回调
    web_server_->RegisterWebSocketMessageCallback([this](httpd_req_t* req, const std::string& message) {
        // 检查消息是否与Location相关
        if (message.find("\"type\":\"") != std::string::npos &&
            (message.find("\"gps") != std::string::npos ||
             message.find("\"location") != std::string::npos)) {
            // 使用正确的函数签名版本
            this->HandleWebSocketMessage(req, message);
        }
    });
}

std::string Location::GetLocationHtml(const char* language) {
    // 这里应该通过资源文件或嵌入式二进制获取HTML内容
    // 实际内容将由Web内容子系统提供
    // 这里只返回一个通用页面框架，具体内容由前端JS填充
    
    return "<html>"
           "<head>"
           "  <title>位置服务</title>"
           "  <meta name='viewport' content='width=device-width, initial-scale=1'>"
           "  <link rel='stylesheet' href='/css/bootstrap.min.css'>"
           "  <link rel='stylesheet' href='/css/location.css'>"
           "</head>"
           "<body>"
           "  <div class='container'>"
           "    <h1>位置服务</h1>"
           "    <div id='map-container'></div>"
           "    <div id='gps-controls'></div>"
           "    <div id='location-data'></div>"
           "  </div>"
           "  <script src='/js/common.js'></script>"
           "  <script src='/js/bootstrap.bundle.min.js'></script>"
           "  <script src='/js/location.js'></script>"
           "</body>"
           "</html>";
}

// API响应处理
ApiResponse Location::HandleGetLocation(httpd_req_t* req) {
    ESP_LOGI(TAG, "Processing get location request");
    
    GpsCoordinate location = GetCurrentLocation();
    
    // 构建JSON响应
    char response[256];
    snprintf(response, sizeof(response), 
            "{\"success\":true,"
            "\"latitude\":%f,"
            "\"longitude\":%f,"
            "\"altitude\":%f,"
            "\"speed\":%f,"
            "\"course\":%f,"
            "\"valid\":%s}",
            location.latitude, location.longitude, 
            location.altitude, location.speed, 
            location.course, location.valid ? "true" : "false");
            
    return ApiResponse(response);
}

// GPS任务和处理
void Location::GpsTask(void* param) {
    Location* location = static_cast<Location*>(param);
    if (!location) {
        ESP_LOGE(TAG, "Invalid Location instance for GPS task");
        vTaskDelete(nullptr);
        return;
    }
    
    ESP_LOGI(TAG, "GPS task started");
    
    // 模拟NMEA数据，用于测试
    const char* nmea_samples[] = {
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A",
        "$GPRMC,123520,A,4807.039,N,01131.001,E,022.5,084.5,230394,003.1,W*6B",
        "$GPRMC,123521,A,4807.040,N,01131.002,E,022.6,084.6,230394,003.1,W*6C"
    };
    
    int sample_index = 0;
    const int sample_count = sizeof(nmea_samples) / sizeof(nmea_samples[0]);
    
    // 每秒发送一次位置更新
    const TickType_t xDelay = pdMS_TO_TICKS(1000);
    
    while (true) {
        // 处理NMEA数据
        location->ProcessNmeaData(nmea_samples[sample_index]);
        
        // 切换到下一个样本
        sample_index = (sample_index + 1) % sample_count;
        
        // 延时
        vTaskDelay(xDelay);
    }
}

void Location::ProcessNmeaData(const std::string& nmea_data) {
    ESP_LOGI(TAG, "Processing NMEA data: %s", nmea_data.c_str());
    
    // 简单的NMEA解析示例（GPRMC格式）
    // 实际应用中应使用完整的NMEA库
    
    // 检查是否为GPRMC消息
    if (nmea_data.find("$GPRMC") == 0) {
        // 分割字段
        std::vector<std::string> fields;
        size_t pos = 0;
        std::string token;
        std::string data = nmea_data;
        
        while ((pos = data.find(",")) != std::string::npos) {
            token = data.substr(0, pos);
            fields.push_back(token);
            data.erase(0, pos + 1);
        }
        fields.push_back(data); // 添加最后一个字段
        
        // 检查有效性
        if (fields.size() >= 12 && fields[2] == "A") {
            // 提取纬度
            double lat = 0;
            if (!fields[3].empty()) {
                lat = std::stod(fields[3].substr(0, 2)) + 
                      std::stod(fields[3].substr(2)) / 60.0;
                      
                if (fields[4] == "S") lat = -lat;
            }
            
            // 提取经度
            double lon = 0;
            if (!fields[5].empty()) {
                lon = std::stod(fields[5].substr(0, 3)) + 
                      std::stod(fields[5].substr(3)) / 60.0;
                      
                if (fields[6] == "W") lon = -lon;
            }
            
            // 提取速度和航向
            double speed = fields[7].empty() ? 0 : std::stod(fields[7]);
            double course = fields[8].empty() ? 0 : std::stod(fields[8]);
            
            // 更新位置
            current_location_.latitude = lat;
            current_location_.longitude = lon;
            current_location_.speed = speed;
            current_location_.course = course;
            current_location_.valid = true;
            
            ESP_LOGI(TAG, "Location updated: lat=%.6f, lon=%.6f, speed=%.1f, course=%.1f",
                    lat, lon, speed, course);
            
            // 通知所有注册的回调
            for (auto& callback : location_callbacks_) {
                callback(current_location_);
            }
            
            // 如果Web服务器可用且有客户端连接，广播位置更新
            if (web_server_) {
                char location_json[256];
                snprintf(location_json, sizeof(location_json), 
                        "{\"type\":\"locationUpdate\","
                        "\"latitude\":%f,"
                        "\"longitude\":%f,"
                        "\"altitude\":%f,"
                        "\"speed\":%f,"
                        "\"course\":%f,"
                        "\"valid\":true}",
                        lat, lon, current_location_.altitude, speed, course);
                        
                web_server_->BroadcastWebSocketMessage(location_json);
            }
        }
        else {
            ESP_LOGW(TAG, "Invalid GPRMC data");
            current_location_.valid = false;
        }
    }
} 