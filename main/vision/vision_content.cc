#include "vision_content.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <img_converters.h>
#include <ArduinoJson.h>
#include "sdkconfig.h"

#define TAG "VisionContent"

// 声明外部HTML内容获取函数（在html_content.cc中定义）
extern const char* CAM_HTML;
extern size_t get_cam_html_size();

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

VisionContent::VisionContent(WebServer* server, VisionController* vision_controller)
    : server_(server), vision_controller_(vision_controller), running_(false) {
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

    if (!vision_controller_ || !vision_controller_->IsRunning()) {
        ESP_LOGE(TAG, "Vision controller not running, cannot start vision content");
        return false;
    }

    InitHandlers();
    
    running_ = true;
    ESP_LOGI(TAG, "Vision content started");
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
    // 注册URI处理函数
    server_->RegisterUri("/camera", HTTP_GET, HandleCamera, this);
    server_->RegisterUri("/stream", HTTP_GET, StreamHandler, this);
    server_->RegisterUri("/capture", HTTP_GET, CaptureHandler, this);
    server_->RegisterUri("/bmp", HTTP_GET, BmpHandler, this);
    server_->RegisterUri("/led", HTTP_GET, LedHandler, this);
    server_->RegisterUri("/status", HTTP_GET, StatusHandler, this);
    server_->RegisterUri("/control", HTTP_GET, CommandHandler, this);
    
    // 注册WebSocket处理
    server_->RegisterWebSocket("/ws", [this](int client_index, const std::string& message) {
        HandleWebSocketMessage(client_index, message);
    });
}

esp_err_t VisionContent::HandleCamera(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "identity");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, CAM_HTML, get_cam_html_size());
    return ESP_OK;
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
    if (!content || !content->vision_controller_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Vision controller not available");
        return ESP_FAIL;
    }
    
    VisionController* vision = content->vision_controller_;
    
    // 设置视频流状态
    vision->SetStreaming(true);
    
    // 设置响应类型
    httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "24");
    
    // 声明JPEG分块结构
    jpg_chunking_t jchunk = {req, 0};
    
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
    
    while (true) {
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
            break;
        }
        
        // 准备图像帧头
        struct timeval tv;
        gettimeofday(&tv, NULL);
        hlen = snprintf(part_buf, 128, _STREAM_PART, fb->len, tv.tv_sec, tv.tv_usec);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res != ESP_OK) {
            break;
        }
        
        // 发送JPEG数据
        res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        if (res != ESP_OK) {
            break;
        }
        
        // 释放帧缓冲区
        vision->ReturnFrame(fb);
        
        // 不要满负荷运行，以防过热
        if (fps > 24) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        // 检查客户端连接
        if (httpd_req_is_cancelling(req)) {
            break;
        }
    }
    
    vision->SetStreaming(false);
    
    // 最后发送一个空块表示结束
    httpd_resp_send_chunk(req, NULL, 0);
    
    return res;
}

esp_err_t VisionContent::CaptureHandler(httpd_req_t *req) {
    VisionContent* content = static_cast<VisionContent*>(req->user_ctx);
    if (!content || !content->vision_controller_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Vision controller not available");
        return ESP_FAIL;
    }
    
    VisionController* vision = content->vision_controller_;
    
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
    if (!content || !content->vision_controller_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Vision controller not available");
        return ESP_FAIL;
    }
    
    VisionController* vision = content->vision_controller_;
    
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
    if (!content || !content->vision_controller_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Vision controller not available");
        return ESP_FAIL;
    }
    
    VisionController* vision = content->vision_controller_;
    
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
    DynamicJsonDocument doc(128);
    doc["intensity"] = current_intensity;
    
    String response;
    serializeJson(doc, response);
    
    // 返回响应
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response.c_str(), response.length());
    
    return ESP_OK;
}

esp_err_t VisionContent::StatusHandler(httpd_req_t *req) {
    VisionContent* content = static_cast<VisionContent*>(req->user_ctx);
    if (!content || !content->vision_controller_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Vision controller not available");
        return ESP_FAIL;
    }
    
    VisionController* vision = content->vision_controller_;
    
    // 获取摄像头状态
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get sensor data");
        return ESP_FAIL;
    }
    
    // 创建JSON响应
    DynamicJsonDocument doc(1024);
    doc["running"] = vision->IsRunning();
    doc["streaming"] = vision->IsStreaming();
    doc["led_intensity"] = vision->GetLedIntensity();
    
    JsonObject sensor = doc.createNestedObject("sensor");
    sensor["framesize"] = s->status.framesize;
    sensor["quality"] = s->status.quality;
    sensor["brightness"] = s->status.brightness;
    sensor["contrast"] = s->status.contrast;
    sensor["saturation"] = s->status.saturation;
    sensor["sharpness"] = s->status.sharpness;
    sensor["denoise"] = s->status.denoise;
    sensor["special_effect"] = s->status.special_effect;
    sensor["wb_mode"] = s->status.wb_mode;
    sensor["awb"] = s->status.awb;
    sensor["awb_gain"] = s->status.awb_gain;
    sensor["aec"] = s->status.aec;
    sensor["aec2"] = s->status.aec2;
    sensor["ae_level"] = s->status.ae_level;
    sensor["aec_value"] = s->status.aec_value;
    sensor["agc"] = s->status.agc;
    sensor["agc_gain"] = s->status.agc_gain;
    sensor["gainceiling"] = s->status.gainceiling;
    sensor["bpc"] = s->status.bpc;
    sensor["wpc"] = s->status.wpc;
    sensor["raw_gma"] = s->status.raw_gma;
    sensor["lenc"] = s->status.lenc;
    sensor["hmirror"] = s->status.hmirror;
    sensor["vflip"] = s->status.vflip;
    sensor["dcw"] = s->status.dcw;
    sensor["colorbar"] = s->status.colorbar;
    
    String response;
    serializeJson(doc, response);
    
    // 返回响应
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response.c_str(), response.length());
    
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
    int res = 0;
    
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

void VisionContent::HandleWebSocketMessage(int client_index, const std::string& message) {
    if (!vision_controller_) {
        ESP_LOGW(TAG, "Vision controller not available");
        return;
    }
    
    // 解析JSON消息
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, message.c_str());
    if (error) {
        ESP_LOGW(TAG, "Invalid JSON in WebSocket message: %s", error.c_str());
        return;
    }
    
    // 处理不同类型的消息
    const char* type = doc["type"];
    if (!type) {
        ESP_LOGW(TAG, "Missing message type");
        return;
    }
    
    if (strcmp(type, "led_intensity") == 0) {
        // 设置LED强度
        int intensity = doc["intensity"];
        vision_controller_->SetLedIntensity(intensity);
        
        // 发送确认消息
        DynamicJsonDocument respDoc(128);
        respDoc["type"] = "led_status";
        respDoc["intensity"] = vision_controller_->GetLedIntensity();
        
        String response;
        serializeJson(respDoc, response);
        
        server_->SendWebSocketMessage(client_index, response.c_str());
    }
    else if (strcmp(type, "camera_control") == 0) {
        // 获取摄像头传感器
        sensor_t *s = esp_camera_sensor_get();
        if (!s) {
            ESP_LOGW(TAG, "Failed to get sensor data");
            return;
        }
        
        // 处理各种摄像头参数
        if (doc.containsKey("framesize")) {
            int v = doc["framesize"];
            if (v >= 0 && v <= 13) {
                s->set_framesize(s, (framesize_t)v);
            }
        }
        if (doc.containsKey("quality")) {
            int v = doc["quality"];
            if (v >= 0 && v <= 63) {
                s->set_quality(s, v);
            }
        }
        if (doc.containsKey("contrast")) {
            int v = doc["contrast"];
            if (v >= -2 && v <= 2) {
                s->set_contrast(s, v);
            }
        }
        if (doc.containsKey("brightness")) {
            int v = doc["brightness"];
            if (v >= -2 && v <= 2) {
                s->set_brightness(s, v);
            }
        }
        if (doc.containsKey("saturation")) {
            int v = doc["saturation"];
            if (v >= -2 && v <= 2) {
                s->set_saturation(s, v);
            }
        }
        if (doc.containsKey("hmirror")) {
            int v = doc["hmirror"];
            if (v == 0 || v == 1) {
                s->set_hmirror(s, v);
            }
        }
        if (doc.containsKey("vflip")) {
            int v = doc["vflip"];
            if (v == 0 || v == 1) {
                s->set_vflip(s, v);
            }
        }
        
        // 发送确认消息
        server_->SendWebSocketMessage(client_index, "{\"type\":\"camera_control_ack\",\"status\":\"ok\"}");
    }
    else if (strcmp(type, "status_request") == 0) {
        // 获取摄像头传感器
        sensor_t *s = esp_camera_sensor_get();
        if (!s) {
            ESP_LOGW(TAG, "Failed to get sensor data");
            return;
        }
        
        // 发送状态信息
        DynamicJsonDocument statusDoc(1024);
        statusDoc["type"] = "camera_status";
        statusDoc["running"] = vision_controller_->IsRunning();
        statusDoc["streaming"] = vision_controller_->IsStreaming();
        statusDoc["led_intensity"] = vision_controller_->GetLedIntensity();
        
        JsonObject sensor = statusDoc.createNestedObject("sensor");
        sensor["framesize"] = s->status.framesize;
        sensor["quality"] = s->status.quality;
        sensor["brightness"] = s->status.brightness;
        sensor["contrast"] = s->status.contrast;
        sensor["saturation"] = s->status.saturation;
        sensor["sharpness"] = s->status.sharpness;
        sensor["hmirror"] = s->status.hmirror;
        sensor["vflip"] = s->status.vflip;
        
        String response;
        serializeJson(statusDoc, response);
        
        server_->SendWebSocketMessage(client_index, response.c_str());
    }
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

// 初始化视觉组件
void InitVisionComponents(WebServer* web_server) {
#if defined(CONFIG_ENABLE_VISION_CONTROLLER)
    // 创建视觉控制器组件
    VisionController* vision_controller = new VisionController();
    ComponentManager::GetInstance().RegisterComponent(vision_controller);
    
    // 创建视觉内容处理组件
    VisionContent* vision_content = new VisionContent(web_server, vision_controller);
    ComponentManager::GetInstance().RegisterComponent(vision_content);
    
    ESP_LOGI(TAG, "Vision components initialized");
#else
    ESP_LOGI(TAG, "Vision controller disabled in configuration");
#endif
} 