#include "vision.h"
#include "../boards/common/esp32_camera.h"
#include "esp_camera.h"
#include "board.h"
#include "esp_log.h"
#include "settings.h"
#include <esp_timer.h>
#include <cJSON.h>
#include <memory>
#include <sstream>
#include <algorithm>

#define TAG "Vision"

// MJPEG streaming constants
#define PART_BOUNDARY "123456789000000000000987654321"
#define STREAM_BOUNDARY "\r\n--" PART_BOUNDARY "\r\n"
#define STREAM_PART "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;

// Stream task
static TaskHandle_t stream_task_handle = nullptr;
static bool stream_active = false;

// Stream task function
static void stream_task(void* param) {
    Vision* vision = static_cast<Vision*>(param);
    if (!vision) {
        ESP_LOGE(TAG, "Invalid vision pointer");
        vTaskDelete(nullptr);
        return;
    }
    
    ESP_LOGI(TAG, "Stream task started");
    
    while (stream_active) {
        // Get a frame from the camera
        camera_fb_t* fb = vision->GetFrame();
        if (!fb) {
            ESP_LOGE(TAG, "Failed to get frame");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // Process the frame (in a real implementation, this would send the frame to clients)
        ESP_LOGD(TAG, "Got frame: %dx%d, len=%d", fb->width, fb->height, fb->len);
        
        // Return the frame to the pool
        vision->ReturnFrame(fb);
        
        // Delay to control frame rate
        vTaskDelay(pdMS_TO_TICKS(100)); // ~10 FPS
    }
    
    ESP_LOGI(TAG, "Stream task stopped");
    stream_task_handle = nullptr;
    vTaskDelete(nullptr);
}

// 初始化平滑过滤器
ra_filter_t* Vision::RaFilterInit(ra_filter_t* filter, size_t sample_size) {
    memset(filter, 0, sizeof(ra_filter_t));
    filter->values = (int *)malloc(sample_size * sizeof(int));
    if (!filter->values) {
        return nullptr;
    }
    memset(filter->values, 0, sample_size * sizeof(int));
    filter->size = sample_size;
    return filter;
}

// 运行平滑过滤器
int Vision::RaFilterRun(ra_filter_t* filter, int value) {
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

// JPEG编码回调
size_t Vision::JpegEncodeStream(void *arg, size_t index, const void *data, size_t len) {
    httpd_req_t *req = (httpd_req_t *)arg;
    if (httpd_resp_send_chunk(req, (const char *)data, len) != ESP_OK) {
        return 0;
    }
    return len;
}

//------------------------------------------------------------------------------
// Vision Class Implementation
//------------------------------------------------------------------------------

Vision::Vision(Web* server) 
    : camera_(nullptr), 
      running_(false),
      is_streaming_(false),
      flash_intensity_(0),
      webserver_(server) {
    ESP_LOGI(TAG, "Vision created");
}

Vision::~Vision() {
    Stop();
    ESP_LOGI(TAG, "Vision destroyed");
}

bool Vision::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Vision already running");
        return true;
    }
    
    // Detect and initialize camera
    DetectCamera();
    
    if (!camera_) {
        ESP_LOGE(TAG, "No camera available");
        return false;
    }
    
    // Register handlers if webserver is available
    if (webserver_) {
        RegisterHttpHandlers(webserver_);
        RegisterWebSocketHandlers(webserver_);
        ESP_LOGI(TAG, "Vision handlers registered with webserver");
    } else {
        ESP_LOGW(TAG, "No webserver provided, HTTP/WebSocket handlers not registered");
    }

    if (camera_) {
        Settings settings("camera", false);
        int b = settings.GetInt("brightness", camera_->GetBrightness());
        int c = settings.GetInt("contrast", camera_->GetContrast());
        int s = settings.GetInt("saturation", camera_->GetSaturation());
        bool hm = settings.GetBool("hmirror", camera_->GetHMirror());
        bool vf = settings.GetBool("vflip", camera_->GetVFlip());
        int fl = settings.GetInt("flash_level", flash_intensity_);
        camera_->SetBrightness(b);
        camera_->SetContrast(c);
        camera_->SetSaturation(s);
        camera_->SetHMirror(hm);
        camera_->SetVFlip(vf);
        if (camera_->HasFlash()) {
            SetLedIntensity(fl);
        }
    }
    
    running_ = true;
    ESP_LOGI(TAG, "Vision started");
    return true;
}

void Vision::Stop() {
    if (!running_) {
        return;
    }

    // Stop streaming if active
    if (is_streaming_) {
        StopStreaming();
    }
    
    running_ = false;
    ESP_LOGI(TAG, "Vision stopped");
}

bool Vision::IsRunning() const {
    return running_;
}

const char* Vision::GetName() const {
    return "VisionController";
}

void Vision::DetectCamera() {
    ESP_LOGI(TAG, "Detecting camera...");
    
    // Try to get camera from the board
    camera_ = GetBoardCamera();
    
    if (camera_) {
        ESP_LOGI(TAG, "Camera detected");
    } else {
        ESP_LOGW(TAG, "No camera detected or initialization failed");
    }
}

Camera* Vision::GetBoardCamera() {
    // Get camera from board
    auto& board = Board::GetInstance();
    return board.GetCamera();
}

bool Vision::StartStreaming() {
    if (!running_ || !camera_) {
        ESP_LOGE(TAG, "Cannot start streaming: controller not running or no camera");
        return false;
    }
    
    if (is_streaming_) {
        ESP_LOGW(TAG, "Streaming already active");
        return true;
    }
    
    // Start camera streaming
    if (!camera_->StartStreaming()) {
        ESP_LOGE(TAG, "Failed to start camera streaming");
        return false;
    }
    
    // Create streaming task
    stream_active = true;
    BaseType_t ret = xTaskCreate(
        stream_task,
        "stream_task",
        4096,
        this,
        5,
        &stream_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create stream task");
        camera_->StopStreaming();
        stream_active = false;
        return false;
    }
    
    is_streaming_ = true;
    ESP_LOGI(TAG, "Streaming started");
    
    // Send status update to all clients
    SendStatusUpdate();
    
    return true;
}

void Vision::StopStreaming() {
    if (!is_streaming_) {
        return;
    }
    
    // Stop streaming task
    stream_active = false;
    
    // Wait for task to terminate
    if (stream_task_handle) {
        for (int i = 0; i < 10; i++) {
            if (!stream_task_handle) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        if (stream_task_handle) {
            vTaskDelete(stream_task_handle);
            stream_task_handle = nullptr;
        }
    }
    
    // Stop camera streaming
    if (camera_) {
        camera_->StopStreaming();
    }

    is_streaming_ = false;
    ESP_LOGI(TAG, "Streaming stopped");
    
    // Send status update to all clients
    SendStatusUpdate();
}

bool Vision::Capture(CaptureCallback callback) {
    if (!running_ || !camera_) {
        ESP_LOGE(TAG, "Cannot capture: controller not running or no camera");
        return false;
    }
    
    // First capture a frame
    bool result = camera_->Capture();
    
    if (!result) {
        ESP_LOGE(TAG, "Capture failed");
        return false;
    }
    
    // If callback provided, get the frame and pass to callback
    if (callback) {
        camera_fb_t* fb = GetFrame();
        if (fb) {
            callback(fb->buf, fb->len);
            ReturnFrame(fb);
        } else {
            ESP_LOGE(TAG, "Failed to get frame after capture");
            return false;
        }
    }
    
    ESP_LOGI(TAG, "Capture successful");
    return true;
}

std::string Vision::GetStatusJson() const {
    cJSON* root = cJSON_CreateObject();
    
    // Add camera status
    cJSON_AddBoolToObject(root, "has_camera", camera_ != nullptr);
    cJSON_AddBoolToObject(root, "is_streaming", is_streaming_);
    
    if (camera_) {
        cJSON_AddStringToObject(root, "sensor", camera_->GetSensorName());
        cJSON_AddBoolToObject(root, "has_flash", camera_->HasFlash());
        cJSON_AddNumberToObject(root, "flash_level", flash_intensity_);
        cJSON_AddNumberToObject(root, "brightness", camera_->GetBrightness());
        cJSON_AddNumberToObject(root, "contrast", camera_->GetContrast());
        cJSON_AddNumberToObject(root, "saturation", camera_->GetSaturation());
        cJSON_AddBoolToObject(root, "hmirror", camera_->GetHMirror());
        cJSON_AddBoolToObject(root, "vflip", camera_->GetVFlip());
    }
    
    char* json_str = cJSON_PrintUnformatted(root);
    std::string result(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(root);
    
    return result;
}

void Vision::RegisterWebSocketHandlers(Web* webserver) {
    if (!webserver) {
        ESP_LOGE(TAG, "No webserver provided");
        return;
    }
    
    // Register WebSocket handler for /ws/vision
    webserver->RegisterWebSocketHandler("/ws/vision", [this](int client_index, const std::string& message) {
        this->HandleWebSocketMessage(client_index, message);
    });
    
    ESP_LOGI(TAG, "WebSocket handlers registered");
}

void Vision::RegisterHttpHandlers(Web* webserver) {
    if (!webserver) {
        ESP_LOGE(TAG, "No webserver provided");
        return;
    }
    
    // Register HTTP handlers
    webserver->RegisterHandler(HttpMethod::HTTP_GET, "/vision", Vision::HandleVision);
    webserver->RegisterHandler(HttpMethod::HTTP_GET, "/stream", Vision::StreamHandler);
    webserver->RegisterHandler(HttpMethod::HTTP_GET, "/capture", Vision::CaptureHandler);
    webserver->RegisterHandler(HttpMethod::HTTP_GET, "/led", Vision::LedHandler);
    webserver->RegisterHandler(HttpMethod::HTTP_GET, "/vision/status", Vision::StatusHandler);
    webserver->RegisterHandler(HttpMethod::HTTP_POST, "/vision/cmd", Vision::CommandHandler);
    webserver->RegisterHandler(HttpMethod::HTTP_GET, "/bmp", Vision::BmpHandler);
    
    ESP_LOGI(TAG, "HTTP handlers registered");
}

void Vision::HandleWebSocketMessage(int client_index, const std::string& message) {
    ESP_LOGI(TAG, "Received WebSocket message: %s", message.c_str());
    
    // Parse message
    cJSON* root = cJSON_Parse(message.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse WebSocket message");
        return;
    }
    
    // Process command
    cJSON* cmd = cJSON_GetObjectItem(root, "cmd");
    if (cJSON_IsString(cmd)) {
        std::string cmd_str = cmd->valuestring;
        
        if (cmd_str == "start_stream") {
            // Start streaming
            if (StartStreaming()) {
                // Send success response
                char response[64];
                snprintf(response, sizeof(response), "{\"status\":\"ok\",\"cmd\":\"start_stream\"}");
                if (webserver_) {
                    webserver_->SendWebSocketMessage(client_index, response);
                }
            }
        }
        else if (cmd_str == "stop_stream") {
            // Stop streaming
            StopStreaming();
            // Send success response
            char response[64];
            snprintf(response, sizeof(response), "{\"status\":\"ok\",\"cmd\":\"stop_stream\"}");
            if (webserver_) {
                webserver_->SendWebSocketMessage(client_index, response);
            }
        }
        else if (cmd_str == "led") {
            // Control LED
            cJSON* intensity = cJSON_GetObjectItem(root, "intensity");
            if (cJSON_IsNumber(intensity)) {
                SetLedIntensity(intensity->valueint);
                Settings settings("camera", true);
                settings.SetInt("flash_level", GetLedIntensity());
                SendStatusUpdate(client_index);
            }
        }
        else if (cmd_str == "set_brightness") {
            cJSON* val = cJSON_GetObjectItem(root, "value");
            if (cJSON_IsNumber(val) && camera_) {
                camera_->SetBrightness(val->valueint);
                Settings settings("camera", true);
                settings.SetInt("brightness", val->valueint);
                SendStatusUpdate(client_index);
            }
        }
        else if (cmd_str == "set_contrast") {
            cJSON* val = cJSON_GetObjectItem(root, "value");
            if (cJSON_IsNumber(val) && camera_) {
                camera_->SetContrast(val->valueint);
                Settings settings("camera", true);
                settings.SetInt("contrast", val->valueint);
                SendStatusUpdate(client_index);
            }
        }
        else if (cmd_str == "set_saturation") {
            cJSON* val = cJSON_GetObjectItem(root, "value");
            if (cJSON_IsNumber(val) && camera_) {
                camera_->SetSaturation(val->valueint);
                Settings settings("camera", true);
                settings.SetInt("saturation", val->valueint);
                SendStatusUpdate(client_index);
            }
        }
        else if (cmd_str == "set_hmirror") {
            cJSON* val = cJSON_GetObjectItem(root, "value");
            if ((cJSON_IsBool(val) || cJSON_IsNumber(val)) && camera_) {
                bool v = cJSON_IsBool(val) ? cJSON_IsTrue(val) : (val->valueint != 0);
                camera_->SetHMirror(v);
                Settings settings("camera", true);
                settings.SetBool("hmirror", v);
                SendStatusUpdate(client_index);
            }
        }
        else if (cmd_str == "set_vflip") {
            cJSON* val = cJSON_GetObjectItem(root, "value");
            if ((cJSON_IsBool(val) || cJSON_IsNumber(val)) && camera_) {
                bool v = cJSON_IsBool(val) ? cJSON_IsTrue(val) : (val->valueint != 0);
                camera_->SetVFlip(v);
                Settings settings("camera", true);
                settings.SetBool("vflip", v);
                SendStatusUpdate(client_index);
            }
        }
        else if (cmd_str == "get_status") {
            // Get camera status
            std::string status = GetStatusJson();
            if (webserver_) {
                webserver_->SendWebSocketMessage(client_index, status);
            }
        }
    }
    
    cJSON_Delete(root);
}

void Vision::SendStatusUpdate(uint32_t client_id) {
    if (!webserver_) {
        return;
    }
    
    // Create status message
    std::string status_json = GetStatusJson();
    std::string message = "{\"type\":\"status\",\"data\":" + status_json + "}";
    
    if (client_id > 0) {
        // Send to specific client
        webserver_->SendWebSocketMessage(client_id, message);
        } else {
        // Send to all clients
        for (uint32_t id : ws_clients_) {
            webserver_->SendWebSocketMessage(id, message);
        }
    }
}

void Vision::SendDetectionResult(const std::string& result, uint32_t client_id) {
    if (!webserver_) {
        return;
    }
    
    // Create detection message
    std::string message = "{\"type\":\"detection\",\"data\":" + result + "}";
    
    if (client_id > 0) {
        // Send to specific client
        webserver_->SendWebSocketMessage(client_id, message);
    } else {
        // Send to all clients
        for (uint32_t id : ws_clients_) {
            webserver_->SendWebSocketMessage(id, message);
        }
    }
}

bool Vision::RunDetection(const std::string& model_name, DetectionCallback callback) {
    if (!running_ || !camera_) {
        ESP_LOGE(TAG, "Cannot run detection: controller not running or no camera");
        return false;
    }
    
    // Capture a frame
    bool result = Capture([this, model_name, callback](const uint8_t* data, size_t len) {
        // In a real implementation, this would send the image to an AI model for processing
        // For now, we'll just return a dummy result
        std::string detection_result = "{ \"model\": \"" + model_name + "\", \"results\": [] }";
        
        if (callback) {
            callback(detection_result);
        }

        SendDetectionResult(detection_result);
    });
    
    return result;
}

bool Vision::SetBrightness(int brightness) {
    if (!running_ || !camera_) {
        ESP_LOGE(TAG, "Cannot set brightness: controller not running or no camera");
        return false;
    }
    
    bool result = camera_->SetBrightness(brightness);
    if (result) {
        SendStatusUpdate();
    }
    
    return result;
}

bool Vision::SetContrast(int contrast) {
    if (!running_ || !camera_) {
        ESP_LOGE(TAG, "Cannot set contrast: controller not running or no camera");
        return false;
    }
    
    bool result = camera_->SetContrast(contrast);
    if (result) {
        SendStatusUpdate();
    }
    
    return result;
}

bool Vision::SetSaturation(int saturation) {
    if (!running_ || !camera_) {
        ESP_LOGE(TAG, "Cannot set saturation: controller not running or no camera");
        return false;
    }
    
    bool result = camera_->SetSaturation(saturation);
    if (result) {
        SendStatusUpdate();
    }
    
    return result;
}

bool Vision::SetHMirror(bool enable) {
    if (!running_ || !camera_) {
        ESP_LOGE(TAG, "Cannot set hmirror: controller not running or no camera");
        return false;
    }
    
    bool result = camera_->SetHMirror(enable);
    if (result) {
        SendStatusUpdate();
    }
    
    return result;
}

bool Vision::SetVFlip(bool enable) {
    if (!running_ || !camera_) {
        ESP_LOGE(TAG, "Cannot set vflip: controller not running or no camera");
        return false;
    }
    
    bool result = camera_->SetVFlip(enable);
    if (result) {
        SendStatusUpdate();
    }
    
    return result;
}

bool Vision::SetLedIntensity(int intensity) {
    if (!running_ || !camera_) {
        ESP_LOGE(TAG, "Cannot set LED intensity: controller not running or no camera");
        return false;
    }
    
    if (!camera_->HasFlash()) {
        ESP_LOGE(TAG, "Camera does not have flash capability");
        return false;
    }
    
    bool result = camera_->SetFlashLevel(intensity);
    if (result) {
        flash_intensity_ = intensity;
        SendStatusUpdate();
    }
    
    return result;
}

camera_fb_t* Vision::GetFrame() {
    if (!running_ || !camera_) {
        ESP_LOGE(TAG, "Cannot get frame: controller not running or no camera");
        return nullptr;
    }
    
    return (camera_fb_t*)camera_->GetFrame();
}

void Vision::ReturnFrame(camera_fb_t* fb) {
    if (camera_ && fb) {
        camera_->ReturnFrame(fb);
    }
}

// HTTP 处理器实现
esp_err_t Vision::HandleVision(httpd_req_t *req) {
    // Get vision.html content
    extern const uint8_t vision_html_start[] asm("_binary_vision_html_start");
    extern const uint8_t vision_html_end[] asm("_binary_vision_html_end");
    
    size_t len = vision_html_end - vision_html_start;
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char*)vision_html_start, len);
    
    return ESP_OK;
}

esp_err_t Vision::StreamHandler(httpd_req_t *req) {
    Vision* vision = static_cast<Vision*>(req->user_ctx);
    if (!vision || !vision->camera_ || !vision->IsRunning()) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "Camera not available", -1);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char part_buf[64];
    while (true) {
        camera_fb_t* fb = vision->GetFrame();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }
        int hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, (unsigned)fb->len);
        if (httpd_resp_sendstr_chunk(req, STREAM_BOUNDARY) != ESP_OK) {
            vision->ReturnFrame(fb);
            break;
        }
        if (httpd_resp_send_chunk(req, part_buf, hlen) != ESP_OK) {
            vision->ReturnFrame(fb);
            break;
        }
        if (httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len) != ESP_OK) {
            vision->ReturnFrame(fb);
            break;
        }
        vision->ReturnFrame(fb);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t Vision::CaptureHandler(httpd_req_t *req) {
    ESP_LOGD(TAG, "Capture handler called");
    Vision* vision = static_cast<Vision*>(req->user_ctx);
    
    if (!vision || !vision->camera_ || !vision->IsRunning()) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "Camera not available", -1);
        return ESP_FAIL;
    }
    
    // Capture and get frame
    camera_fb_t* fb = vision->GetFrame();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    // Send JPEG image
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
    
    // Return the frame to the pool
    vision->ReturnFrame(fb);
    
    return res;
}

esp_err_t Vision::LedHandler(httpd_req_t *req) {
    ESP_LOGD(TAG, "LED handler called");
    Vision* vision = static_cast<Vision*>(req->user_ctx);
    
    if (!vision || !vision->camera_ || !vision->IsRunning()) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "Camera not available", -1);
        return ESP_FAIL;
    }
    
    int total_len = req->content_len;
    int cur_len = 0;
    char buf[100];
    
    while (cur_len < total_len) {
        int received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';
    
    cJSON* root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON* intensity = cJSON_GetObjectItem(root, "intensity");
    bool success = false;
    
    if (intensity && cJSON_IsNumber(intensity)) {
        success = vision->SetLedIntensity(intensity->valueint);
    }
    
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    char response[64];
    snprintf(response, sizeof(response), "{\"success\":%s}", success ? "true" : "false");
    httpd_resp_send(req, response, strlen(response));
    
    return ESP_OK;
}

esp_err_t Vision::StatusHandler(httpd_req_t *req) {
    ESP_LOGD(TAG, "Status handler called");
    Vision* vision = static_cast<Vision*>(req->user_ctx);
    
    if (!vision) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "Vision component not available", -1);
        return ESP_FAIL;
    }
    
    std::string status = vision->GetStatusJson();
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, status.c_str(), status.size());
    
    return ESP_OK;
}

esp_err_t Vision::CommandHandler(httpd_req_t *req) {
    ESP_LOGD(TAG, "Command handler called");
    Vision* vision = static_cast<Vision*>(req->user_ctx);
    
    if (!vision || !vision->camera_ || !vision->IsRunning()) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "Camera not available", -1);
        return ESP_FAIL;
    }
    
    int total_len = req->content_len;
    int cur_len = 0;
    char* buf = (char*)malloc(total_len + 1);
    
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    while (cur_len < total_len) {
        int received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            free(buf);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';
    
    cJSON* root = cJSON_Parse(buf);
    free(buf);
    
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    // Process command
    bool success = false;
    cJSON* cmd = cJSON_GetObjectItem(root, "cmd");
    if (cmd && cJSON_IsString(cmd)) {
        std::string command = cmd->valuestring;
        
        if (command == "set_brightness") {
            cJSON* value = cJSON_GetObjectItem(root, "value");
            if (value && cJSON_IsNumber(value)) {
                success = vision->SetBrightness(value->valueint);
            }
        } else if (command == "set_contrast") {
            cJSON* value = cJSON_GetObjectItem(root, "value");
            if (value && cJSON_IsNumber(value)) {
                success = vision->SetContrast(value->valueint);
            }
        } else if (command == "set_saturation") {
            cJSON* value = cJSON_GetObjectItem(root, "value");
            if (value && cJSON_IsNumber(value)) {
                success = vision->SetSaturation(value->valueint);
            }
        } else if (command == "set_hmirror") {
            cJSON* value = cJSON_GetObjectItem(root, "value");
            if (value && cJSON_IsBool(value)) {
                success = vision->SetHMirror(cJSON_IsTrue(value));
            }
        } else if (command == "set_vflip") {
            cJSON* value = cJSON_GetObjectItem(root, "value");
            if (value && cJSON_IsBool(value)) {
                success = vision->SetVFlip(cJSON_IsTrue(value));
            }
        }
    }
    
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    char response[64];
    snprintf(response, sizeof(response), "{\"success\":%s}", success ? "true" : "false");
    httpd_resp_send(req, response, strlen(response));
    
    return ESP_OK;
}

esp_err_t Vision::BmpHandler(httpd_req_t *req) {
    ESP_LOGD(TAG, "BMP handler called");
    Vision* vision = static_cast<Vision*>(req->user_ctx);
    
    if (!vision || !vision->camera_ || !vision->IsRunning()) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "Camera not available", -1);
        return ESP_FAIL;
    }
    
    // Currently not implemented - would convert JPEG to BMP
    httpd_resp_set_status(req, "501 Not Implemented");
    httpd_resp_send(req, "BMP conversion not implemented", -1);
    
    return ESP_OK;
}

// Global initialization function
void InitVisionComponent(Web* web_server) {
    ESP_LOGI(TAG, "Initializing Vision component");
    
    // Create Vision component
    static Vision* vision = new Vision(web_server);
    if (!vision) {
        ESP_LOGE(TAG, "Failed to create Vision component");
        return;
    }
    
    // Register the component
    auto& manager = ComponentManager::GetInstance();
    manager.RegisterComponent(vision);
    
    // Start the component
    if (vision->Start()) {
        ESP_LOGI(TAG, "Vision component started successfully");
    } else {
        ESP_LOGE(TAG, "Failed to start Vision component");
    }
}