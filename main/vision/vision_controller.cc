#include "vision_controller.h"
#include "vision_content.h"
#include "web_server.h"
#include "board.h"
#include "../boards/common/esp32_camera.h"
#include "esp_log.h"
#include <esp_timer.h>
#include <cJSON.h>
#include <memory>
#include <sstream>
#include <algorithm>

#define TAG "VisionController"

// Using cJSON for JSON handling

// MJPEG boundary string
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;

// Stream task
static TaskHandle_t stream_task_handle = nullptr;
static bool stream_active = false;

// Stream task function
static void stream_task(void* param) {
    VisionController* controller = static_cast<VisionController*>(param);
    if (!controller) {
        ESP_LOGE(TAG, "Invalid controller pointer");
        vTaskDelete(nullptr);
        return;
    }
    
    ESP_LOGI(TAG, "Stream task started");
    
    while (stream_active) {
        // Get a frame from the camera
        camera_fb_t* fb = controller->GetFrame();
        if (!fb) {
            ESP_LOGE(TAG, "Failed to get frame");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // Process the frame (in a real implementation, this would send the frame to clients)
        ESP_LOGD(TAG, "Got frame: %dx%d, len=%d", fb->width, fb->height, fb->len);
        
        // Return the frame to the pool
        controller->ReturnFrame(fb);
        
        // Delay to control frame rate
        vTaskDelay(pdMS_TO_TICKS(100)); // ~10 FPS
    }
    
    ESP_LOGI(TAG, "Stream task stopped");
    stream_task_handle = nullptr;
    vTaskDelete(nullptr);
}

/**
 * 构造函数，初始化视觉控制器
 */
VisionController::VisionController() 
    : camera_(nullptr), 
      running_(false),
      is_streaming_(false),
      flash_intensity_(0),
      webserver_(nullptr) {
    ESP_LOGI(TAG, "VisionController created");
}

/**
 * 析构函数，清理资源
 */
VisionController::~VisionController() {
    Stop();
    ESP_LOGI(TAG, "VisionController destroyed");
}

/**
 * 初始化视觉控制器
 * @param webserver WebServer指针
 * @return 是否初始化成功
 */
bool VisionController::Start() {
    if (running_) {
        ESP_LOGW(TAG, "VisionController already running");
        return true;
    }
    
    // Detect and initialize camera
    DetectCamera();
    
    if (!camera_) {
        ESP_LOGE(TAG, "No camera available");
        return false;
    }
    
    running_ = true;
    ESP_LOGI(TAG, "VisionController started");
    return true;
}

/**
 * 停止视觉控制器
 */
void VisionController::Stop() {
    if (!running_) {
        return;
    }

    // Stop streaming if active
    if (is_streaming_) {
        StopStreaming();
    }
    
    running_ = false;
    ESP_LOGI(TAG, "VisionController stopped");
}

/**
 * 检查视觉控制器是否正在运行
 * @return 是否正在运行
 */
bool VisionController::IsRunning() const {
    return running_;
}

/**
 * 初始化视觉控制器
 * @param webserver WebServer指针
 * @return 是否初始化成功
 */
bool VisionController::Initialize(WebServer* webserver) {
    if (!webserver) {
        ESP_LOGE(TAG, "Invalid WebServer pointer");
        return false;
    }
    
    webserver_ = webserver;
    
    // Register HTTP and WebSocket handlers
    RegisterHttpHandlers(webserver);
    RegisterWebSocketHandlers(webserver);
    
    ESP_LOGI(TAG, "VisionController initialized with WebServer");
    return true;
}

/**
 * 自动检测并获取可用摄像头
 */
void VisionController::DetectCamera() {
    ESP_LOGI(TAG, "Detecting camera...");
    
    // Try to get camera from the board
    camera_ = GetBoardCamera();
    
    if (camera_) {
        ESP_LOGI(TAG, "Camera detected");
    } else {
        ESP_LOGW(TAG, "No camera detected or initialization failed");
    }
}

/**
 * 从Board获取摄像头
 * @return 摄像头指针
 */
Camera* VisionController::GetBoardCamera() {
    // Get camera from board
    auto& board = Board::GetInstance();
    return board.GetCamera();
}

/**
 * 启动视频流
 * @return 是否成功启动
 */
bool VisionController::StartStreaming() {
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

/**
 * 停止视频流
 */
void VisionController::StopStreaming() {
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

/**
 * 拍照
 * @param callback 拍照完成回调函数
 * @return 是否成功拍照
 */
bool VisionController::Capture(CaptureCallback callback) {
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

/**
 * 获取摄像头状态JSON
 * @return 摄像头状态JSON字符串
 */
std::string VisionController::GetStatusJson() const {
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

/**
 * 注册WebSocket处理程序
 * @param webserver WebServer指针
 */
void VisionController::RegisterWebSocketHandlers(WebServer* webserver) {
    if (!webserver) {
        return;
    }
    
    // Register WebSocket handler for camera messages
    webserver->RegisterWebSocketHandler("camera", [this](int client_id, const PSRAMString& data, const PSRAMString& type) {
        // Handle message data
        HandleWebSocketMessage(data.c_str(), client_id);
        
        // Add client to our tracking list if not already there
        if (std::find(ws_clients_.begin(), ws_clients_.end(), client_id) == ws_clients_.end()) {
            ws_clients_.push_back(client_id);
            ESP_LOGI(TAG, "WebSocket client added to tracking: %d", client_id);
        }
    });
    
    ESP_LOGI(TAG, "WebSocket handlers registered");
}

/**
 * 注册HTTP处理程序
 * @param webserver WebServer指针
 */
void VisionController::RegisterHttpHandlers(WebServer* webserver) {
    if (!webserver) {
        return;
    }
    
    // Register HTTP handlers for camera operations
    webserver->RegisterHttpHandler("/api/camera/status", HTTP_GET, [this](httpd_req_t* req) -> esp_err_t {
        // Set content type header
        httpd_resp_set_type(req, "application/json");
        
        // Get the status as JSON
        std::string status_json = GetStatusJson();
        
        // Send the response
        httpd_resp_sendstr(req, status_json.c_str());
        return ESP_OK;
    });
    
    webserver->RegisterHttpHandler("/api/camera/stream", HTTP_GET, [this](httpd_req_t* req) -> esp_err_t {
        if (!camera_) {
            httpd_resp_set_status(req, "404 Not Found");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"error\":\"Camera not available\"}");
            return ESP_OK;
        }
        
        httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
        httpd_resp_set_hdr(req, "Expires", "0");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        
        // Start streaming if not already started
        if (!is_streaming_) {
            StartStreaming();
        }
        
        // In a real implementation, this would stream frames to the client
        // For now, we'll just send a simple response
        httpd_resp_sendstr(req, "Camera streaming started");
        return ESP_OK;
    });
    
    webserver->RegisterHttpHandler("/api/camera/capture", HTTP_GET, [this](httpd_req_t* req) -> esp_err_t {
        if (!camera_) {
            httpd_resp_set_status(req, "404 Not Found");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"error\":\"Camera not available\"}");
            return ESP_OK;
        }
        
        // Capture a frame
        camera_fb_t* fb = GetFrame();
        if (!fb) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"error\":\"Failed to capture image\"}");
            return ESP_OK;
        }
        
        // Set response headers
        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        
        // Send the image data
        esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
        
        // Return the frame to the pool
        ReturnFrame(fb);
        
        return res;
    });
    
    ESP_LOGI(TAG, "HTTP handlers registered");
}

/**
 * 处理来自WebSocket的消息
 * @param message 消息内容
 * @param client_id 客户端ID
 */
void VisionController::HandleWebSocketMessage(const std::string& message, uint32_t client_id) {
    ESP_LOGI(TAG, "Received WebSocket message from client %" PRIu32 ": %s", client_id, message.c_str());
    
    // Parse JSON message
    cJSON* root = cJSON_Parse(message.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON message");
        return;
    }
    
    // Get command
    cJSON* cmd_obj = cJSON_GetObjectItem(root, "cmd");
    if (!cmd_obj || !cJSON_IsString(cmd_obj)) {
        ESP_LOGE(TAG, "Invalid or missing 'cmd' field");
        cJSON_Delete(root);
        return;
    }
    
    std::string cmd = cmd_obj->valuestring;
    
    if (cmd == "get_status") {
        // Send status update
        SendStatusUpdate(client_id);
    } else if (cmd == "start_stream") {
        // Start streaming
        StartStreaming();
    } else if (cmd == "stop_stream") {
        // Stop streaming
        StopStreaming();
    } else if (cmd == "capture") {
        // Capture a frame
        Capture();
    } else if (cmd == "set_brightness") {
        // Set brightness
        cJSON* value_obj = cJSON_GetObjectItem(root, "value");
        if (value_obj && cJSON_IsNumber(value_obj)) {
            SetBrightness(value_obj->valueint);
        }
    } else if (cmd == "set_contrast") {
        // Set contrast
        cJSON* value_obj = cJSON_GetObjectItem(root, "value");
        if (value_obj && cJSON_IsNumber(value_obj)) {
            SetContrast(value_obj->valueint);
        }
    } else if (cmd == "set_saturation") {
        // Set saturation
        cJSON* value_obj = cJSON_GetObjectItem(root, "value");
        if (value_obj && cJSON_IsNumber(value_obj)) {
            SetSaturation(value_obj->valueint);
        }
    } else if (cmd == "set_hmirror") {
        // Set horizontal mirror
        cJSON* value_obj = cJSON_GetObjectItem(root, "value");
        if (value_obj && cJSON_IsBool(value_obj)) {
            SetHMirror(cJSON_IsTrue(value_obj));
        }
    } else if (cmd == "set_vflip") {
        // Set vertical flip
        cJSON* value_obj = cJSON_GetObjectItem(root, "value");
        if (value_obj && cJSON_IsBool(value_obj)) {
            SetVFlip(cJSON_IsTrue(value_obj));
        }
    } else if (cmd == "set_flash") {
        // Set flash level
        cJSON* value_obj = cJSON_GetObjectItem(root, "value");
        if (value_obj && cJSON_IsNumber(value_obj)) {
            SetLedIntensity(value_obj->valueint);
        }
    } else if (cmd == "run_detection") {
        // Run detection
        cJSON* model_obj = cJSON_GetObjectItem(root, "model");
        if (model_obj && cJSON_IsString(model_obj)) {
            RunDetection(model_obj->valuestring);
        }
    } else {
        ESP_LOGW(TAG, "Unknown command: %s", cmd.c_str());
    }
    
    cJSON_Delete(root);
}

/**
 * 发送状态更新到WebSocket客户端
 * @param client_id 客户端ID，0表示广播给所有客户端
 */
void VisionController::SendStatusUpdate(uint32_t client_id) {
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

/**
 * 发送AI检测结果到WebSocket客户端
 * @param result 检测结果JSON字符串
 * @param client_id 客户端ID，0表示广播给所有客户端
 */
void VisionController::SendDetectionResult(const std::string& result, uint32_t client_id) {
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

/**
 * 运行AI视觉识别
 * @param model_name 模型名称
 * @param callback 识别结果回调函数
 * @return 是否成功启动识别
 */
bool VisionController::RunDetection(const std::string& model_name, DetectionCallback callback) {
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

/**
 * 设置摄像头亮度
 * @param brightness 亮度值，范围-2到2
 * @return 是否设置成功
 */
bool VisionController::SetBrightness(int brightness) {
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

/**
 * 设置摄像头对比度
 * @param contrast 对比度值，范围-2到2
 * @return 是否设置成功
 */
bool VisionController::SetContrast(int contrast) {
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

/**
 * 设置摄像头饱和度
 * @param saturation 饱和度值，范围-2到2
 * @return 是否设置成功
 */
bool VisionController::SetSaturation(int saturation) {
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

/**
 * 设置摄像头水平镜像
 * @param enable 是否启用
 * @return 是否设置成功
 */
bool VisionController::SetHMirror(bool enable) {
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

/**
 * 设置摄像头垂直翻转
 * @param enable 是否启用
 * @return 是否设置成功
 */
bool VisionController::SetVFlip(bool enable) {
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

/**
 * 设置闪光灯强度
 * @param intensity 强度值，范围0-255
 * @return 是否设置成功
 */
bool VisionController::SetLedIntensity(int intensity) {
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

/**
 * 获取摄像头帧
 * @return 摄像头帧
 */
camera_fb_t* VisionController::GetFrame() {
    if (!running_ || !camera_) {
        ESP_LOGE(TAG, "Cannot get frame: controller not running or no camera");
        return nullptr;
    }
    
    return camera_->GetFrame();
}

/**
 * 返回摄像头帧
 * @param fb 摄像头帧
 */
void VisionController::ReturnFrame(camera_fb_t* fb) {
    if (camera_) {
        camera_->ReturnFrame(fb);
    }
}

// InitVisionComponents is implemented in vision_content.cc