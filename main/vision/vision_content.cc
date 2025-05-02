#include "vision_content.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <img_converters.h>
#include <cJSON.h>
#include <sys/socket.h>
#include <esp_wifi.h>
#include "sdkconfig.h"
#if CONFIG_ENABLE_WEB_CONTENT
#include "web/html_content.h"
#endif

#define TAG "VisionContent"

// 使用html_content.h中定义的HTML内容函数

// 多部分HTTP相关定义
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

// JPG分块结构
typedef struct {
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

VisionContent::VisionContent(WebServer* server)
    : server_(server), vision_controller_(nullptr), running_(false) {
    // 初始化滑动平均过滤器
    RaFilterInit(&ra_filter_, 20);
}

VisionContent::~VisionContent() {
    Stop();
    
    // 释放滑动平均过滤器内存
    if (ra_filter_.values) {
        free(ra_filter_.values);
        ra_filter_.values = nullptr;
    }
}

bool VisionContent::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Vision content already running");
        return true;
    }

    if (!server_ || !server_->IsRunning()) {
        ESP_LOGE(TAG, "Web server not running, cannot start vision content");
        return false;
    }

    // 获取视觉控制器
    vision_controller_ = GetVisionController();
    if (!vision_controller_) {
        ESP_LOGE(TAG, "Vision controller not found, cannot start vision content");
        return false;
    }
    
    // 如果视觉控制器未运行，尝试启动它
    if (!vision_controller_->IsRunning()) {
        ESP_LOGI(TAG, "Vision controller not running, attempting to start it");
        if (!vision_controller_->Start()) {
            ESP_LOGE(TAG, "Failed to start vision controller");
            return false;
        }
        ESP_LOGI(TAG, "Vision controller started successfully");
    }

    InitHandlers();
    
    running_ = true;
    ESP_LOGI(TAG, "Vision content started with integrated camera functionality");
    return true;
}

void VisionContent::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    ESP_LOGI(TAG, "Vision content stopped");
}

bool VisionContent::IsRunning() const {
    return running_;
}

const char* VisionContent::GetName() const {
    return "VisionContent";
}

void VisionContent::InitHandlers() {
    if (!server_ || !server_->IsRunning()) {
        ESP_LOGE(TAG, "Web server not running, cannot initialize Vision handlers");
        return;
    }

    ESP_LOGI(TAG, "Initializing Vision URI handlers");
    
    // Safe URI registration with logging
    auto safeRegisterUri = [this](const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*), void* ctx) {
        if (!server_->IsUriRegistered(uri)) {
            server_->RegisterUri(uri, method, handler, ctx);
            ESP_LOGI(TAG, "Registered Vision URI handler: %s", uri);
        } else {
            ESP_LOGW(TAG, "URI already registered, skipping: %s", uri);
        }
    };

    // 注册视觉相关URI处理函数
    safeRegisterUri("/camera", HTTP_GET, HandleCamera, this);
    safeRegisterUri("/stream", HTTP_GET, StreamHandler, this);
    safeRegisterUri("/capture", HTTP_GET, CaptureHandler, this);
    safeRegisterUri("/bmp", HTTP_GET, BmpHandler, this);
    safeRegisterUri("/led", HTTP_GET, LedHandler, this);
    safeRegisterUri("/control", HTTP_GET, CommandHandler, this);
    
    // 注册特殊的状态URI，避免与WebContent冲突
    safeRegisterUri("/vision/status", HTTP_GET, StatusHandler, this);
    
    // WebSocket注册 - 更新为全局统一的WebSocket
    if (!server_->IsUriRegistered("/ws")) {
        ESP_LOGI(TAG, "Registering global WebSocket handler");
        server_->RegisterWebSocket("/ws", [this](int client_index, const PSRAMString& message) {
            HandleWebSocketMessage(client_index, message);
        });
        ESP_LOGI(TAG, "Registered WebSocket handler for vision");
    } else {
        ESP_LOGW(TAG, "WebSocket URI /ws already registered by another component");
        // 尝试注册自己的消息处理回调
        // 注意：这里我们不能直接访问其他组件设置的回调，但我们的回调会通过HandleWsMessageInternal分发
    }
    
    ESP_LOGI(TAG, "Vision URI handlers initialization complete");
}

esp_err_t VisionContent::HandleCamera(httpd_req_t *req) {
#if CONFIG_ENABLE_WEB_CONTENT
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "identity");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, VISION_HTML, get_vision_html_size());
    return ESP_OK;
#else
    // 当Web内容未启用时，返回一个简单的消息
    const char* message = "<html><body><h1>Vision Content Disabled</h1><p>The web content feature is not enabled in this build.</p></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, message, strlen(message));
    return ESP_OK;
#endif
}

size_t VisionContent::JpegEncodeStream(void *arg, size_t index, const void *data, size_t len) {
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if (!index) {
        j->len = 0;
    }
    if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
        return 0;
    }
    j->len += len;
    return len;
}

esp_err_t VisionContent::StreamHandler(httpd_req_t *req) {
    VisionContent* content = static_cast<VisionContent*>(req->user_ctx);
    if (!content) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid request context");
        return ESP_FAIL;
    }
    
    // 安全检查：验证视觉控制器是否存在
    VisionController* vision = content->vision_controller_;
    if (!vision) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Vision controller not available");
        return ESP_FAIL;
    }
    
    // 设置视频流状态
    vision->SetStreaming(true);
    
    // 设置响应类型
    httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "24");
    
    esp_err_t res = ESP_OK;
    char part_buf[128];
    
    static int64_t last_frame = 0;
    
    // 每帧间隔时间
    int64_t frame_time = esp_timer_get_time();
    if (!last_frame) {
        last_frame = frame_time;
    }
    
    // FPS计算
    int fps = 24;
    
    // 最大连续帧计数
    const int MAX_FRAMES = 1000; // 限制连续流帧数
    int frame_count = 0;
    
    while (frame_count < MAX_FRAMES) {
        // 获取摄像头帧
        camera_fb_t *fb = vision->GetFrame();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }
        
        // 计算FPS
        frame_time = esp_timer_get_time();
        int64_t frame_delta = frame_time - last_frame;
        last_frame = frame_time;
        
        if (frame_delta > 0) {
            fps = static_cast<int>(1000000 / frame_delta);
            fps = content->RaFilterRun(&content->ra_filter_, fps);
        }
        
        // 发送MJPEG Stream开始
        size_t hlen = snprintf(part_buf, 128, _STREAM_BOUNDARY);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send boundary, stopping stream");
            vision->ReturnFrame(fb);
            break;
        }
        
        // 准备图像帧头
        struct timeval tv;
        gettimeofday(&tv, NULL);
        hlen = snprintf(part_buf, 128, _STREAM_PART, fb->len, tv.tv_sec, tv.tv_usec);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send header, stopping stream");
            vision->ReturnFrame(fb);
            break;
        }
        
        // 发送JPEG数据
        res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        if (res != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send JPEG data, stopping stream");
            vision->ReturnFrame(fb);
            break;
        }
        
        // 释放帧缓冲区
        vision->ReturnFrame(fb);
        
        // 不要满负荷运行，以防过热
        if (fps > 24) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        // 增加帧计数
        frame_count++;
    }
    
    // 结束流媒体
    vision->SetStreaming(false);
    
    // 最后发送一个空块表示结束
    httpd_resp_send_chunk(req, NULL, 0);
    
    return res;
}

esp_err_t VisionContent::CaptureHandler(httpd_req_t *req) {
    VisionContent* content = static_cast<VisionContent*>(req->user_ctx);
    if (!content) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid request context");
        return ESP_FAIL;
    }
    
    // 安全检查：验证视觉控制器是否存在
    VisionController* vision = content->vision_controller_;
    if (!vision) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Vision controller not available");
        return ESP_FAIL;
    }
    
    // 获取摄像头帧
    camera_fb_t *fb = vision->GetFrame();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera capture failed");
        return ESP_FAIL;
    }
    
    // 设置响应头
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    // 设置时间戳
    struct timeval tv;
    gettimeofday(&tv, NULL);
    char ts[32];
    snprintf(ts, 32, "%lld.%06ld", static_cast<long long>(tv.tv_sec), static_cast<long>(tv.tv_usec));
    httpd_resp_set_hdr(req, "X-Timestamp", ts);
    
    // 发送图像数据
    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    
    // 释放帧缓冲区
    vision->ReturnFrame(fb);
    
    return res;
}

esp_err_t VisionContent::BmpHandler(httpd_req_t *req) {
    VisionContent* content = static_cast<VisionContent*>(req->user_ctx);
    if (!content) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid request context");
        return ESP_FAIL;
    }
    
    // 安全检查：验证视觉控制器是否存在
    VisionController* vision = content->vision_controller_;
    if (!vision) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Vision controller not available");
        return ESP_FAIL;
    }
    
    // 获取摄像头帧
    camera_fb_t *fb = vision->GetFrame();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera capture failed");
        return ESP_FAIL;
    }
    
    // 设置响应头
    httpd_resp_set_type(req, "image/x-windows-bmp");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.bmp");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    // 设置时间戳
    struct timeval tv;
    gettimeofday(&tv, NULL);
    char ts[32];
    snprintf(ts, 32, "%lld.%06ld", static_cast<long long>(tv.tv_sec), static_cast<long>(tv.tv_usec));
    httpd_resp_set_hdr(req, "X-Timestamp", ts);
    
    // 转换为BMP格式
    uint8_t *buf = NULL;
    size_t buf_len = 0;
    bool converted = frame2bmp(fb, &buf, &buf_len);
    
    // 释放帧缓冲区
    vision->ReturnFrame(fb);
    
    if (!converted) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to convert frame to BMP");
        return ESP_FAIL;
    }
    
    // 发送BMP数据
    esp_err_t res = httpd_resp_send(req, (const char *)buf, buf_len);
    
    // 释放BMP缓冲区
    free(buf);
    
    return res;
}

esp_err_t VisionContent::LedHandler(httpd_req_t *req) {
    VisionContent* content = static_cast<VisionContent*>(req->user_ctx);
    if (!content) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid request context");
        return ESP_FAIL;
    }
    
    // 安全检查：验证视觉控制器是否存在
    VisionController* vision = content->vision_controller_;
    if (!vision) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Vision controller not available");
        return ESP_FAIL;
    }
    
    // 解析查询参数
    char query[32];
    int ret = httpd_req_get_url_query_str(req, query, sizeof(query));
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid query string");
        return ESP_FAIL;
    }
    
    char param[32];
    if (httpd_query_key_value(query, "intensity", param, sizeof(param)) == ESP_OK) {
        int intensity = atoi(param);
        vision->SetLedIntensity(intensity);
    }
    
    // 返回当前LED亮度
    int current_intensity = vision->GetLedIntensity();
    
    // 创建JSON响应
    cJSON* doc = cJSON_CreateObject();
    cJSON_AddNumberToObject(doc, "intensity", current_intensity);
    
    char* json = cJSON_Print(doc);
    if (!json) {
        cJSON_Delete(doc);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to generate JSON response");
        return ESP_FAIL;
    }
    
    // 返回响应
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    cJSON_Delete(doc);
    free(json);
    
    return ESP_OK;
}

esp_err_t VisionContent::StatusHandler(httpd_req_t *req) {
    VisionContent* content = static_cast<VisionContent*>(req->user_ctx);
    if (!content) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid request context");
        return ESP_FAIL;
    }
    
    // 设置CORS头和类型
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    
    // 创建基础JSON响应
    cJSON* doc = cJSON_CreateObject();
    
    // 添加基本系统信息
    cJSON_AddStringToObject(doc, "version", "1.0.0");
    cJSON_AddBoolToObject(doc, "system_ready", true);
    
    // 安全检查：验证视觉控制器是否存在
    VisionController* vision = content->vision_controller_;
    if (!vision) {
        cJSON_AddBoolToObject(doc, "camera_available", false);
        cJSON_AddStringToObject(doc, "camera_error", "Vision controller not initialized");
        
        char* json = cJSON_Print(doc);
        if (!json) {
            cJSON_Delete(doc);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to generate JSON response");
            return ESP_FAIL;
        }
        
        httpd_resp_send(req, json, strlen(json));
        
        cJSON_Delete(doc);
        free(json);
        return ESP_OK;
    }
    
    // 添加视觉控制器状态
    cJSON_AddBoolToObject(doc, "camera_available", true);
    cJSON_AddBoolToObject(doc, "camera_running", vision->IsRunning());
    cJSON_AddBoolToObject(doc, "camera_streaming", vision->IsStreaming());
    cJSON_AddNumberToObject(doc, "led_intensity", vision->GetLedIntensity());
    
    // 获取摄像头传感器状态
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        cJSON_AddBoolToObject(doc, "sensor_available", false);
    } else {
        cJSON_AddBoolToObject(doc, "sensor_available", true);
        
        // 创建嵌套的sensor对象
        cJSON* sensor = cJSON_CreateObject();
        cJSON_AddItemToObject(doc, "sensor", sensor);
        
        // 添加传感器信息
        cJSON_AddNumberToObject(sensor, "framesize", s->status.framesize);
        cJSON_AddNumberToObject(sensor, "quality", s->status.quality);
        cJSON_AddNumberToObject(sensor, "brightness", s->status.brightness);
        cJSON_AddNumberToObject(sensor, "contrast", s->status.contrast);
        cJSON_AddNumberToObject(sensor, "saturation", s->status.saturation);
        cJSON_AddNumberToObject(sensor, "sharpness", s->status.sharpness);
        cJSON_AddNumberToObject(sensor, "denoise", s->status.denoise);
        cJSON_AddNumberToObject(sensor, "special_effect", s->status.special_effect);
        cJSON_AddNumberToObject(sensor, "wb_mode", s->status.wb_mode);
        cJSON_AddNumberToObject(sensor, "awb", s->status.awb);
        cJSON_AddNumberToObject(sensor, "awb_gain", s->status.awb_gain);
        cJSON_AddNumberToObject(sensor, "aec", s->status.aec);
        cJSON_AddNumberToObject(sensor, "aec2", s->status.aec2);
        cJSON_AddNumberToObject(sensor, "ae_level", s->status.ae_level);
        cJSON_AddNumberToObject(sensor, "aec_value", s->status.aec_value);
        cJSON_AddNumberToObject(sensor, "agc", s->status.agc);
        cJSON_AddNumberToObject(sensor, "agc_gain", s->status.agc_gain);
        cJSON_AddNumberToObject(sensor, "gainceiling", s->status.gainceiling);
        cJSON_AddNumberToObject(sensor, "bpc", s->status.bpc);
        cJSON_AddNumberToObject(sensor, "wpc", s->status.wpc);
        cJSON_AddNumberToObject(sensor, "raw_gma", s->status.raw_gma);
        cJSON_AddNumberToObject(sensor, "lenc", s->status.lenc);
        cJSON_AddNumberToObject(sensor, "hmirror", s->status.hmirror);
        cJSON_AddNumberToObject(sensor, "vflip", s->status.vflip);
        cJSON_AddNumberToObject(sensor, "dcw", s->status.dcw);
        cJSON_AddNumberToObject(sensor, "colorbar", s->status.colorbar);
    }
    
    // 添加内存使用信息
    cJSON_AddNumberToObject(doc, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(doc, "min_free_heap", esp_get_minimum_free_heap_size());
    
    // 添加时间戳
    struct timeval tv;
    gettimeofday(&tv, NULL);
    cJSON_AddNumberToObject(doc, "timestamp", tv.tv_sec);
    
    char* json = cJSON_Print(doc);
    if (!json) {
        cJSON_Delete(doc);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to generate JSON response");
        return ESP_FAIL;
    }
    
    // 返回响应
    httpd_resp_send(req, json, strlen(json));
    
    cJSON_Delete(doc);
    free(json);
    
    return ESP_OK;
}

esp_err_t VisionContent::CommandHandler(httpd_req_t *req) {
    VisionContent* content = static_cast<VisionContent*>(req->user_ctx);
    if (!content || !content->vision_controller_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Vision controller not available");
        return ESP_FAIL;
    }
    
    // 获取摄像头传感器
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get sensor data");
        return ESP_FAIL;
    }
    
    // 解析查询参数
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid query string");
        return ESP_FAIL;
    }
    
    // 设置响应头
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    // 解析并应用命令参数
    char val[32];
    
    // 检查各种摄像头参数，有则设置
    if (httpd_query_key_value(buf, "framesize", val, sizeof(val)) == ESP_OK) {
        int v = atoi(val);
        if (v >= 0 && v <= 13) {
            s->set_framesize(s, (framesize_t)v);
        }
    }
    else if (httpd_query_key_value(buf, "quality", val, sizeof(val)) == ESP_OK) {
        int v = atoi(val);
        if (v >= 0 && v <= 63) {
            s->set_quality(s, v);
        }
    }
    else if (httpd_query_key_value(buf, "contrast", val, sizeof(val)) == ESP_OK) {
        int v = atoi(val);
        if (v >= -2 && v <= 2) {
            s->set_contrast(s, v);
        }
    }
    else if (httpd_query_key_value(buf, "brightness", val, sizeof(val)) == ESP_OK) {
        int v = atoi(val);
        if (v >= -2 && v <= 2) {
            s->set_brightness(s, v);
        }
    }
    else if (httpd_query_key_value(buf, "saturation", val, sizeof(val)) == ESP_OK) {
        int v = atoi(val);
        if (v >= -2 && v <= 2) {
            s->set_saturation(s, v);
        }
    }
    else if (httpd_query_key_value(buf, "hmirror", val, sizeof(val)) == ESP_OK) {
        int v = atoi(val);
        if (v == 0 || v == 1) {
            s->set_hmirror(s, v);
        }
    }
    else if (httpd_query_key_value(buf, "vflip", val, sizeof(val)) == ESP_OK) {
        int v = atoi(val);
        if (v == 0 || v == 1) {
            s->set_vflip(s, v);
        }
    }
    
    // 返回成功响应
    httpd_resp_send(req, "{\"status\":\"ok\"}", -1);
    
    return ESP_OK;
}

void VisionContent::HandleWebSocketMessage(int client_index, const PSRAMString& message) {
    // 记录接收到的消息 - 仅在DEBUG级别记录
    ESP_LOGD(TAG, "WebSocket message from client %d: %s", 
        client_index, message.c_str());
    
    // 解析JSON消息
    cJSON* doc = cJSON_Parse(message.c_str());
    if (!doc) {
        ESP_LOGW(TAG, "Invalid JSON in WebSocket message");
        return;
    }
    
    // 处理不同类型的消息
    cJSON* type_obj = cJSON_GetObjectItem(doc, "type");
    if (!type_obj || !type_obj->valuestring) {
        ESP_LOGW(TAG, "Missing message type");
        cJSON_Delete(doc);
        return;
    }
    
    const char* type = type_obj->valuestring;
    
    // 处理视觉/摄像头相关的消息类型
    if (strcmp(type, "led_intensity") == 0) {
        // 检查摄像头控制器是否可用
        if (vision_controller_ == nullptr) {
            ESP_LOGW(TAG, "VisionController not available");
            server_->SendWebSocketMessage(client_index, 
                "{\"type\":\"error\",\"message\":\"Camera controller not available\"}");
            cJSON_Delete(doc);
            return;
        }
        
        // 设置LED强度
        cJSON* intensity_obj = cJSON_GetObjectItem(doc, "intensity");
        if (intensity_obj) {
            int intensity = intensity_obj->valueint;
            vision_controller_->SetLedIntensity(intensity);
        
            // 发送确认消息
            cJSON* respDoc = cJSON_CreateObject();
            cJSON_AddStringToObject(respDoc, "type", "led_status");
            cJSON_AddNumberToObject(respDoc, "intensity", vision_controller_->GetLedIntensity());
            
            char* response = cJSON_Print(respDoc);
            
            server_->SendWebSocketMessage(client_index, response);
            
            cJSON_Delete(respDoc);
            free(response);
        }
    }
    else if (strcmp(type, "camera_control") == 0) {
        // 检查摄像头控制器是否可用
        if (vision_controller_ == nullptr) {
            ESP_LOGW(TAG, "VisionController not available");
            server_->SendWebSocketMessage(client_index, 
                "{\"type\":\"error\",\"message\":\"Camera controller not available\"}");
            cJSON_Delete(doc);
            return;
        }
        
        // 获取摄像头传感器
        sensor_t *s = esp_camera_sensor_get();
        if (!s) {
            ESP_LOGW(TAG, "Failed to get sensor data");
            server_->SendWebSocketMessage(client_index, 
                "{\"type\":\"error\",\"message\":\"Camera sensor not available\"}");
            cJSON_Delete(doc);
            return;
        }
        
        // 处理各种摄像头参数
        bool updated = false;
        
        cJSON* framesize_obj = cJSON_GetObjectItem(doc, "framesize");
        if (framesize_obj) {
            int v = framesize_obj->valueint;
            if (v >= 0 && v <= 13) {
                s->set_framesize(s, (framesize_t)v);
                updated = true;
            }
        }
        
        cJSON* quality_obj = cJSON_GetObjectItem(doc, "quality");
        if (quality_obj) {
            int v = quality_obj->valueint;
            if (v >= 0 && v <= 63) {
                s->set_quality(s, v);
                updated = true;
            }
        }
        
        cJSON* contrast_obj = cJSON_GetObjectItem(doc, "contrast");
        if (contrast_obj) {
            int v = contrast_obj->valueint;
            if (v >= -2 && v <= 2) {
                s->set_contrast(s, v);
                updated = true;
            }
        }
        
        cJSON* brightness_obj = cJSON_GetObjectItem(doc, "brightness");
        if (brightness_obj) {
            int v = brightness_obj->valueint;
            if (v >= -2 && v <= 2) {
                s->set_brightness(s, v);
                updated = true;
            }
        }
        
        cJSON* saturation_obj = cJSON_GetObjectItem(doc, "saturation");
        if (saturation_obj) {
            int v = saturation_obj->valueint;
            if (v >= -2 && v <= 2) {
                s->set_saturation(s, v);
                updated = true;
            }
        }
        
        cJSON* hmirror_obj = cJSON_GetObjectItem(doc, "hmirror");
        if (hmirror_obj) {
            int v = hmirror_obj->valueint;
            if (v == 0 || v == 1) {
                s->set_hmirror(s, v);
                updated = true;
            }
        }
        
        cJSON* vflip_obj = cJSON_GetObjectItem(doc, "vflip");
        if (vflip_obj) {
            int v = vflip_obj->valueint;
            if (v == 0 || v == 1) {
                s->set_vflip(s, v);
                updated = true;
            }
        }
        
        // 发送确认消息
        if (updated) {
            server_->SendWebSocketMessage(client_index, 
                "{\"type\":\"camera_control_ack\",\"status\":\"ok\"}");
        } else {
            server_->SendWebSocketMessage(client_index, 
                "{\"type\":\"camera_control_ack\",\"status\":\"no_change\"}");
        }
    }
    else if (strcmp(type, "camera_status_request") == 0) {
        // 专门处理摄像头状态请求
        // 检查摄像头控制器是否可用
        if (vision_controller_ == nullptr) {
            ESP_LOGW(TAG, "VisionController not available for status request");
            cJSON* statusDoc = cJSON_CreateObject();
            cJSON_AddStringToObject(statusDoc, "type", "camera_status");
            cJSON_AddBoolToObject(statusDoc, "available", false);
            cJSON_AddStringToObject(statusDoc, "error", "Camera controller not initialized");
            
            char* response = cJSON_Print(statusDoc);
            server_->SendWebSocketMessage(client_index, response);
            
            cJSON_Delete(statusDoc);
            free(response);
            cJSON_Delete(doc);
            return;
        }
        
        // 获取摄像头传感器
        sensor_t *s = esp_camera_sensor_get();
        if (!s) {
            ESP_LOGW(TAG, "Failed to get sensor data");
            cJSON* statusDoc = cJSON_CreateObject();
            cJSON_AddStringToObject(statusDoc, "type", "camera_status");
            cJSON_AddBoolToObject(statusDoc, "available", true);
            cJSON_AddBoolToObject(statusDoc, "running", vision_controller_->IsRunning());
            cJSON_AddBoolToObject(statusDoc, "sensor_error", true);
            
            char* response = cJSON_Print(statusDoc);
            server_->SendWebSocketMessage(client_index, response);
            
            cJSON_Delete(statusDoc);
            free(response);
            cJSON_Delete(doc);
            return;
        }
        
        // 发送状态信息
        cJSON* statusDoc = cJSON_CreateObject();
        cJSON_AddStringToObject(statusDoc, "type", "camera_status");
        cJSON_AddBoolToObject(statusDoc, "running", vision_controller_->IsRunning());
        cJSON_AddBoolToObject(statusDoc, "streaming", vision_controller_->IsStreaming());
        cJSON_AddNumberToObject(statusDoc, "led_intensity", vision_controller_->GetLedIntensity());
        
        // 创建嵌套的sensor对象
        cJSON* sensor = cJSON_CreateObject();
        cJSON_AddItemToObject(statusDoc, "sensor", sensor);
        
        // 添加传感器信息
        cJSON_AddNumberToObject(sensor, "framesize", s->status.framesize);
        cJSON_AddNumberToObject(sensor, "quality", s->status.quality);
        cJSON_AddNumberToObject(sensor, "brightness", s->status.brightness);
        cJSON_AddNumberToObject(sensor, "contrast", s->status.contrast);
        cJSON_AddNumberToObject(sensor, "saturation", s->status.saturation);
        cJSON_AddNumberToObject(sensor, "sharpness", s->status.sharpness);
        cJSON_AddNumberToObject(sensor, "hmirror", s->status.hmirror);
        cJSON_AddNumberToObject(sensor, "vflip", s->status.vflip);
        
        char* response = cJSON_Print(statusDoc);
        
        server_->SendWebSocketMessage(client_index, response);
        
        cJSON_Delete(statusDoc);
        free(response);
    }
    // 还可以处理通用状态请求，添加摄像头信息
    else if (strcmp(type, "status_request") == 0) {
        // 添加摄像头状态信息到全局状态
        if (vision_controller_ != nullptr) {
            cJSON* statusDoc = cJSON_CreateObject();
            cJSON_AddStringToObject(statusDoc, "type", "camera_status_update");
            cJSON_AddBoolToObject(statusDoc, "running", vision_controller_->IsRunning());
            cJSON_AddBoolToObject(statusDoc, "streaming", vision_controller_->IsStreaming());
            cJSON_AddNumberToObject(statusDoc, "led_intensity", vision_controller_->GetLedIntensity());
            
            char* response = cJSON_Print(statusDoc);
            server_->SendWebSocketMessage(client_index, response);
            
            cJSON_Delete(statusDoc);
            free(response);
        }
    }
    
    cJSON_Delete(doc);
}

ra_filter_t* VisionContent::RaFilterInit(ra_filter_t* filter, size_t sample_size) {
    memset(filter, 0, sizeof(ra_filter_t));
    
    filter->values = (int *)malloc(sample_size * sizeof(int));
    if (!filter->values) {
        return NULL;
    }
    memset(filter->values, 0, sample_size * sizeof(int));
    
    filter->size = sample_size;
    return filter;
}

int VisionContent::RaFilterRun(ra_filter_t* filter, int value) {
    if (!filter->values) {
        return value;
    }
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index++;
    filter->index = filter->index % filter->size;
    if (filter->count < filter->size) {
        filter->count++;
    }
    return filter->sum / filter->count;
}

// 新增GetVisionController方法实现
VisionController* VisionContent::GetVisionController() {
    if (vision_controller_ == nullptr) {
        // 从ComponentManager获取VisionController实例
        auto& manager = ComponentManager::GetInstance();
        Component* component = manager.GetComponent("VisionController");
        
        if (component) {
            // 安全检查：验证组件名称与期望的类型是否匹配
            if (strcmp(component->GetName(), "VisionController") == 0) {
                vision_controller_ = static_cast<VisionController*>(component);
            } else {
                ESP_LOGE(TAG, "Component found but not a VisionController (name=%s)", 
                         component->GetName());
            }
        } else {
            ESP_LOGW(TAG, "VisionController not found in ComponentManager");
        }
    }
    
    if (vision_controller_ == nullptr) {
        ESP_LOGE(TAG, "Failed to get VisionController reference");
    }
    
    return vision_controller_;
}

// 初始化视觉组件
void InitVisionComponents(WebServer* web_server) {
#if defined(CONFIG_ENABLE_VISION_CONTROLLER)
    if (!web_server) {
        ESP_LOGE(TAG, "Web server is null, cannot initialize vision components");
        return;
    }
    
    auto& manager = ComponentManager::GetInstance();
    
    // 检查VisionController是否已经存在
    VisionController* vision_controller = nullptr;
    Component* existing_controller = manager.GetComponent("VisionController");
    
    if (existing_controller) {
        ESP_LOGI(TAG, "VisionController already exists, using existing instance");
        vision_controller = static_cast<VisionController*>(existing_controller);
    } else {
        // 创建视觉控制器组件
        vision_controller = new VisionController();
        if (!vision_controller) {
            ESP_LOGE(TAG, "Failed to allocate memory for VisionController");
            return;
        }
        manager.RegisterComponent(vision_controller);
        ESP_LOGI(TAG, "Created new VisionController instance");
    }
    
    // 检查VisionContent是否已经存在
    if (manager.GetComponent("VisionContent")) {
        ESP_LOGI(TAG, "VisionContent already exists, skipping creation");
    } else {
        // 创建视觉内容处理组件
        VisionContent* vision_content = new VisionContent(web_server);
        if (!vision_content) {
            ESP_LOGE(TAG, "Failed to allocate memory for VisionContent");
            return;
        }
        manager.RegisterComponent(vision_content);
        ESP_LOGI(TAG, "Created new VisionContent instance");
    }
    
    ESP_LOGI(TAG, "Vision components initialized successfully");
#else
    ESP_LOGI(TAG, "Vision controller disabled in configuration");
#endif
}