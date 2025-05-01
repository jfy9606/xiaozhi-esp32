#include "web_content.h"
#include <esp_log.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "sdkconfig.h"

#define TAG "WebContent"

// 引入HTML内容
extern const char* INDEX_HTML;
extern const char* CAR_HTML;
extern const char* AI_HTML;
extern const char* CAM_HTML;

// HTML文件大小获取函数
extern size_t get_index_html_size();
extern size_t get_car_html_size();
extern size_t get_ai_html_size();
extern size_t get_cam_html_size();

WebContent::WebContent(WebServer* server) 
    : server_(server), running_(false) {
}

WebContent::~WebContent() {
    Stop();
}

bool WebContent::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Web content already running");
        return true;
    }

    if (!server_ || !server_->IsRunning()) {
        ESP_LOGE(TAG, "Web server not running, cannot start web content");
        return false;
    }

    InitHandlers();
    
    // 注册WebSocket处理
    server_->RegisterWebSocket("/ws", [this](int client_index, const std::string& message) {
        HandleWebSocketMessage(client_index, message);
    });

    running_ = true;
    ESP_LOGI(TAG, "Web content started");
    return true;
}

void WebContent::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    ESP_LOGI(TAG, "Web content stopped");
}

bool WebContent::IsRunning() const {
    return running_;
}

const char* WebContent::GetName() const {
    return "WebContent";
}

void WebContent::InitHandlers() {
    // 注册URI处理函数
    server_->RegisterUri("/", HTTP_GET, HandleRoot);
    server_->RegisterUri("/car", HTTP_GET, HandleCar);
    server_->RegisterUri("/ai", HTTP_GET, HandleAI);
    server_->RegisterUri("/camera", HTTP_GET, HandleCamera);
    server_->RegisterUri("/status", HTTP_GET, HandleStatus);
    server_->RegisterUri("/wifi/settings", HTTP_GET, HandleWifiSettings);
    server_->RegisterUri("/wifi/config", HTTP_POST, HandleWifiConfig);
}

esp_err_t WebContent::HandleRoot(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, INDEX_HTML, get_index_html_size());
    return ESP_OK;
}

esp_err_t WebContent::HandleCar(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, CAR_HTML, get_car_html_size());
    return ESP_OK;
}

esp_err_t WebContent::HandleAI(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, AI_HTML, get_ai_html_size());
    return ESP_OK;
}

esp_err_t WebContent::HandleCamera(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, CAM_HTML, get_cam_html_size());
    return ESP_OK;
}

esp_err_t WebContent::HandleStatus(httpd_req_t *req) {
    DynamicJsonDocument statusDoc(1024);
    
    // 设置WiFi状态
    if (WiFi.status() == WL_CONNECTED) {
        statusDoc["status"] = "connected";
        statusDoc["ip"] = WiFi.localIP().toString();
        statusDoc["ssid"] = WiFi.SSID();
        statusDoc["rssi"] = WiFi.RSSI();
        // 添加AP IP地址信息
        statusDoc["ap_ip"] = WiFi.softAPIP().toString();
        statusDoc["ap_clients"] = WiFi.softAPgetStationNum();
    } else if (WiFi.getMode() == WIFI_AP) {
        statusDoc["status"] = "ap_only";
        statusDoc["ap_ip"] = WiFi.softAPIP().toString();
        // 获取AP SSID
        statusDoc["ap_ssid"] = WiFi.softAPSSID();
        statusDoc["clients"] = WiFi.softAPgetStationNum();
    } else {
        statusDoc["status"] = "disconnected";
    }
    
    String response;
    serializeJson(statusDoc, response);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, response.c_str(), response.length());
}

esp_err_t WebContent::HandleWifiConfig(httpd_req_t *req) {
    char buf[256];
    int ret, remaining = req->content_len;
    
    if (remaining > sizeof(buf) - 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large");
        return ESP_FAIL;
    }
    
    int received = 0;
    while (remaining > 0) {
        ret = httpd_req_recv(req, buf + received, remaining);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        received += ret;
        remaining -= ret;
    }
    buf[received] = '\0';
    
    // 解析JSON
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, buf);
    if (error) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    // 获取参数
    const char* ap_ssid = doc["ap_ssid"];
    const char* ap_password = doc["ap_password"];
    bool ap_enabled = doc["ap_enabled"];
    
    if (!ap_ssid || !ap_password) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing required parameters");
        return ESP_FAIL;
    }
    
    if (strlen(ap_password) < 8) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password must be at least 8 characters");
        return ESP_FAIL;
    }
    
    // 保存到Preferences
    Preferences prefs;
    prefs.begin("wifi_config", false);
    prefs.putString("ap_ssid", ap_ssid);
    prefs.putString("ap_password", ap_password);
    prefs.putBool("ap_enabled", ap_enabled);
    prefs.end();
    
    httpd_resp_sendstr(req, "WiFi settings saved, will take effect after restart");
    return ESP_OK;
}

esp_err_t WebContent::HandleWifiSettings(httpd_req_t *req) {
    // 获取AP设置
    Preferences prefs;
    prefs.begin("wifi_config", false);
    String ap_ssid = prefs.getString("ap_ssid", "ESP32-DevKit");
    bool ap_enabled = prefs.getBool("ap_enabled", true);
    prefs.end();
    
    // 获取WiFi客户端设置
    prefs.begin("wifi_client", false);
    DynamicJsonDocument doc(1024);
    
    doc["ap"]["ssid"] = ap_ssid;
    doc["ap"]["enabled"] = ap_enabled;
    
    JsonArray clients = doc.createNestedArray("clients");
    
    // 获取保存的WiFi客户端列表
    for (int i = 0; i < 5; i++) {
        String ssidKey = "ssid_" + String(i);
        String ssid = prefs.getString(ssidKey.c_str(), "");
        if (ssid.length() > 0) {
            JsonObject client = clients.createNestedObject();
            client["id"] = i;
            client["ssid"] = ssid;
        }
    }
    prefs.end();
    
    String response;
    serializeJson(doc, response);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response.c_str(), response.length());
    return ESP_OK;
}

void WebContent::HandleWebSocketMessage(int client_index, const std::string& message) {
    ESP_LOGI(TAG, "Received WebSocket message from client %d: %s", 
        client_index, message.c_str());
        
    // 处理状态请求
    if (message == "status_request") {
        DynamicJsonDocument statusDoc(1024);
        
        // 设置WiFi状态
        if (WiFi.status() == WL_CONNECTED) {
            statusDoc["status"] = "connected";
            statusDoc["ip"] = WiFi.localIP().toString();
            statusDoc["ssid"] = WiFi.SSID();
            statusDoc["rssi"] = WiFi.RSSI();
            statusDoc["ap_ip"] = WiFi.softAPIP().toString();
            statusDoc["ap_clients"] = WiFi.softAPgetStationNum();
        } else if (WiFi.getMode() == WIFI_AP) {
            statusDoc["status"] = "ap_only";
            statusDoc["ap_ip"] = WiFi.softAPIP().toString();
            statusDoc["ap_ssid"] = WiFi.softAPSSID();
            statusDoc["clients"] = WiFi.softAPgetStationNum();
        } else {
            statusDoc["status"] = "disconnected";
        }
        
        String response;
        serializeJson(statusDoc, response);
        
        server_->SendWebSocketMessage(client_index, response);
    }
    // 处理心跳
    else if (message.find("heartbeat") != std::string::npos) {
        server_->SendWebSocketMessage(client_index, "{\"status\":\"ok\"}");
    }
    // 其他消息类型可以在这里处理
} 