#include "web_content.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <cJSON.h>
#include <esp_timer.h>
#include <esp_chip_info.h>
#include "sdkconfig.h"
#include "html_content.h"
#include <sys/param.h>
#include <wifi_station.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>

#define TAG CONFIG_WEB_CONTENT_TAG

// 初始化静态成员变量 - 移除PSRAM变量，保留大小变量用于引用内嵌资源
size_t WebContent::favicon_ico_size = 0;
size_t WebContent::style_css_size = 0;
size_t WebContent::script_js_size = 0;

// 构造函数
WebContent::WebContent(WebServer* server) 
    : server_(server), running_(false) {
    ESP_LOGI(TAG, "WebContent实例创建");
}

// 析构函数
WebContent::~WebContent() {
    ESP_LOGI(TAG, "WebContent实例销毁");
    Stop();
}

// 启动Web内容服务
bool WebContent::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Web内容服务已在运行");
        return true;
    }

    if (!server_ || !server_->IsRunning()) {
        ESP_LOGE(TAG, "Web服务器未运行，无法启动Web内容服务");
        return false;
    }

    // 计算嵌入资源大小
    ComputeResourceSizes();
    
    // 注册静态资源处理器
    RegisterStaticContent();
    
    // 注册WebSocket消息处理器
    RegisterWebSocketHandlers();

    running_ = true;
    ESP_LOGI(TAG, "Web内容服务启动成功");
    return true;
}

// 停止Web内容服务
void WebContent::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    ESP_LOGI(TAG, "Web内容服务已停止");
}

// 检查是否正在运行
bool WebContent::IsRunning() const {
    return running_;
}

// 获取组件名称
const char* WebContent::GetName() const {
    return "WebContent";
}

// 注册静态资源处理器
void WebContent::RegisterStaticContent() {
    ESP_LOGI(TAG, "注册静态资源处理器");
    
    // 不再单独注册每个静态资源路径，使用通配符注册器
    // 检查是否已存在通配符处理器
    bool wildcard_exists = server_->HasUriHandler("/*");
    
    if (!wildcard_exists) {
        // 如果没有通配符处理器，注册一个
        server_->RegisterHttpHandler("/*", HTTP_GET, [this](httpd_req_t* req) {
            // 处理路径
            const char* uri = req->uri;
            ESP_LOGI(TAG, "处理静态资源请求: %s", uri);
            
            // 检查是否是CSS文件
            if (strncmp(uri, "/css/", 5) == 0) {
                return HandleCssFile(req);
            }
            // 检查是否是JS文件
            else if (strncmp(uri, "/js/", 4) == 0) {
                return HandleJsFile(req);
            }
            // 检查是否是特定的HTML文件或其他静态资源
            else if (strcmp(uri, "/favicon.ico") == 0) {
                return HandleStaticFile(req);
            }
            // 其他静态资源
            else {
                // 通过Content-Type判断文件类型
                const char* content_type = GetContentType(PSRAMString(uri));
                httpd_resp_set_type(req, content_type);
                
                // 尝试找到对应的内嵌资源，如果找不到则返回404
                ESP_LOGW(TAG, "未找到静态资源: %s", uri);
                httpd_resp_send_404(req);
                return ESP_OK;
            }
        });
        ESP_LOGI(TAG, "注册通配符处理器: /*");
    } else {
        ESP_LOGI(TAG, "通配符处理器 /* 已存在，跳过注册");
    }
    
    // 不再单独注册CSS和JS处理器
    ESP_LOGI(TAG, "静态资源将通过通配符处理器处理");
}

// 注册WebSocket消息处理器
void WebContent::RegisterWebSocketHandlers() {
    ESP_LOGI(TAG, "注册WebSocket消息处理器");
    
    // 注册各种消息类型的处理函数
    server_->RegisterWebSocketHandler("system", [this](int client_id, const PSRAMString& message, const PSRAMString& type) {
        HandleSystemMessage(client_id, message, type);
    });
    
    server_->RegisterWebSocketHandler("control", [this](int client_id, const PSRAMString& message, const PSRAMString& type) {
        HandleControlMessage(client_id, message, type);
    });
    
    server_->RegisterWebSocketHandler("heartbeat", [this](int client_id, const PSRAMString& message, const PSRAMString& type) {
        HandleHeartbeatMessage(client_id, message, type);
    });
    
    server_->RegisterWebSocketHandler("status", [this](int client_id, const PSRAMString& message, const PSRAMString& type) {
        HandleStatusMessage(client_id, message, type);
    });
}

// 处理系统相关消息
void WebContent::HandleSystemMessage(int client_id, const PSRAMString& message, const PSRAMString& type) {
    ESP_LOGI(TAG, "处理系统消息: %s", message.c_str());
    
    // 解析JSON消息
    cJSON* root = cJSON_Parse(message.c_str());
    if (!root) {
        ESP_LOGE(TAG, "解析JSON失败");
        return;
    }
    
    // 使用RAII方式管理JSON资源
    struct JsonCleanup {
        cJSON* json;
        JsonCleanup(cJSON* j) : json(j) {}
        ~JsonCleanup() { cJSON_Delete(json); }
    } cleanup(root);
    
    // 获取操作类型
    cJSON* action_obj = cJSON_GetObjectItem(root, "action");
    if (!action_obj || !cJSON_IsString(action_obj)) {
        ESP_LOGE(TAG, "消息缺少action字段");
        return;
    }
    
    PSRAMString action = action_obj->valuestring;
    
    // 处理不同的系统操作
    if (action == "reboot") {
        // 重启设备
        ESP_LOGI(TAG, "收到重启命令");
        
        // 发送确认消息
        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "system");
        cJSON_AddStringToObject(response, "action", "reboot");
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "System is rebooting...");
        
        char* json_str = cJSON_PrintUnformatted(response);
        server_->SendWebSocketMessage(client_id, json_str);
        free(json_str);
        cJSON_Delete(response);
        
        // 延迟1秒后重启
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    else if (action == "get_info") {
        // 获取系统信息
        ESP_LOGI(TAG, "收到获取系统信息命令");
        
        // 发送系统信息
        PSRAMString status_json = GetSystemStatus();
        server_->SendWebSocketMessage(client_id, status_json);
    }
    else if (action == "wifi_config") {
        // 配置WiFi
        ESP_LOGI(TAG, "收到WiFi配置命令");
        
        cJSON* ssid_obj = cJSON_GetObjectItem(root, "ssid");
        cJSON* password_obj = cJSON_GetObjectItem(root, "password");
        
        if (!ssid_obj || !cJSON_IsString(ssid_obj)) {
            ESP_LOGE(TAG, "缺少SSID参数");
            return;
        }
        
        PSRAMString ssid = ssid_obj->valuestring;
        PSRAMString password = password_obj && cJSON_IsString(password_obj) ? 
                              password_obj->valuestring : "";
        
        bool success = ConfigureWifi(ssid, password);
        
        // 发送配置结果
        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "system");
        cJSON_AddStringToObject(response, "action", "wifi_config");
        cJSON_AddStringToObject(response, "status", success ? "ok" : "error");
        cJSON_AddStringToObject(response, "message", success ? 
                               "WiFi configuration updated" : 
                               "Failed to update WiFi configuration");
        
        char* json_str = cJSON_PrintUnformatted(response);
        server_->SendWebSocketMessage(client_id, json_str);
        free(json_str);
        cJSON_Delete(response);
    }
    else {
        ESP_LOGW(TAG, "未知的系统命令: %s", action.c_str());
    }
}

// 处理控制消息
void WebContent::HandleControlMessage(int client_id, const PSRAMString& message, const PSRAMString& type) {
    ESP_LOGI(TAG, "处理控制消息: %s", message.c_str());
    
    // 解析JSON消息
    cJSON* root = cJSON_Parse(message.c_str());
    if (!root) {
        ESP_LOGE(TAG, "解析JSON失败");
        return;
    }
    
    // 使用RAII方式管理JSON资源
    struct JsonCleanup {
        cJSON* json;
        JsonCleanup(cJSON* j) : json(j) {}
        ~JsonCleanup() { cJSON_Delete(json); }
    } cleanup(root);
    
    // 获取控制目标
    cJSON* target_obj = cJSON_GetObjectItem(root, "target");
    if (!target_obj || !cJSON_IsString(target_obj)) {
        ESP_LOGE(TAG, "消息缺少target字段");
        return;
    }
    
    PSRAMString target = target_obj->valuestring;
    
    // 根据不同的控制目标转发消息
    if (target == "car") {
        // 转发给电机处理系统
        server_->BroadcastWebSocketMessage(message, "car");
    }
    else if (target == "vision") {
        // 转发给视觉系统
        server_->BroadcastWebSocketMessage(message, "vision");
    }
    else if (target == "ai") {
        // 转发给AI系统
        server_->BroadcastWebSocketMessage(message, "ai");
    }
    else {
        ESP_LOGW(TAG, "未知的控制目标: %s", target.c_str());
    }
}

// 处理心跳消息
void WebContent::HandleHeartbeatMessage(int client_id, const PSRAMString& message, const PSRAMString& type) {
    // 简单响应心跳，不需要详细日志
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "heartbeat_response");
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "timestamp", esp_timer_get_time() / 1000);
    
    char* json_str = cJSON_PrintUnformatted(response);
    server_->SendWebSocketMessage(client_id, json_str);
    free(json_str);
    cJSON_Delete(response);
}

// 处理状态查询消息
void WebContent::HandleStatusMessage(int client_id, const PSRAMString& message, const PSRAMString& type) {
    ESP_LOGI(TAG, "处理状态查询消息");
    
    // 解析JSON消息
    cJSON* root = cJSON_Parse(message.c_str());
    if (!root) {
        ESP_LOGE(TAG, "解析JSON失败");
        return;
    }
    
    // 使用RAII方式管理JSON资源
    struct JsonCleanup {
        cJSON* json;
        JsonCleanup(cJSON* j) : json(j) {}
        ~JsonCleanup() { cJSON_Delete(json); }
    } cleanup(root);
    
    // 获取查询类型
    cJSON* query_obj = cJSON_GetObjectItem(root, "query");
    if (!query_obj || !cJSON_IsString(query_obj)) {
        ESP_LOGE(TAG, "消息缺少query字段");
        return;
    }
    
    PSRAMString query = query_obj->valuestring;
    
    // 根据不同的查询类型返回状态
    if (query == "system") {
        // 返回系统状态
        PSRAMString status_json = GetSystemStatus();
        server_->SendWebSocketMessage(client_id, status_json);
    }
    else if (query == "wifi") {
        // 返回WiFi状态
        PSRAMString wifi_json = GetWifiStatus();
        server_->SendWebSocketMessage(client_id, wifi_json);
    }
    else {
        ESP_LOGW(TAG, "未知的状态查询类型: %s", query.c_str());
    }
}

// 获取系统状态信息
PSRAMString WebContent::GetSystemStatus() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "status_response");
    cJSON_AddStringToObject(root, "query", "system");
    
    // 添加系统信息
    cJSON* system = cJSON_CreateObject();
    
    // 获取ESP32型号
    cJSON_AddStringToObject(system, "model", "ESP32");
    
    // 获取IDF版本
    cJSON_AddStringToObject(system, "idf_version", esp_get_idf_version());
    
    // 获取芯片信息
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    char chip_model[32];
    snprintf(chip_model, sizeof(chip_model), "%s Rev %d", 
            chip_info.model == CHIP_ESP32 ? "ESP32" :
            chip_info.model == CHIP_ESP32S2 ? "ESP32-S2" :
            chip_info.model == CHIP_ESP32S3 ? "ESP32-S3" :
            chip_info.model == CHIP_ESP32C3 ? "ESP32-C3" : "Unknown",
            chip_info.revision);
    
    cJSON_AddStringToObject(system, "chip", chip_model);
    cJSON_AddNumberToObject(system, "cores", chip_info.cores);
    
    // 添加内存信息
    cJSON_AddNumberToObject(system, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(system, "min_free_heap", esp_get_minimum_free_heap_size());
    
    // 添加PSRAM信息
    #if WEB_SERVER_HAS_PSRAM
    size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    cJSON_AddNumberToObject(system, "psram_total", psram_size);
    cJSON_AddNumberToObject(system, "psram_free", psram_free);
    cJSON_AddBoolToObject(system, "psram_enabled", WEB_SERVER_USE_PSRAM);
    #else
    cJSON_AddBoolToObject(system, "psram_available", false);
    #endif
    
    // 添加运行时间
    uint32_t uptime = esp_timer_get_time() / 1000000; // 转换为秒
    cJSON_AddNumberToObject(system, "uptime", uptime);
    
    // 添加到响应
    cJSON_AddItemToObject(root, "system", system);
    
    // 添加WiFi信息
    cJSON* wifi = cJSON_CreateObject();
    
    // 获取WiFi模式
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        cJSON_AddStringToObject(wifi, "mode", 
                              mode == WIFI_MODE_NULL ? "NULL" :
                              mode == WIFI_MODE_STA ? "STA" :
                              mode == WIFI_MODE_AP ? "AP" :
                              mode == WIFI_MODE_APSTA ? "APSTA" : "Unknown");
    }
    
    // 如果在STA模式，获取连接信息
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            cJSON_AddStringToObject(wifi, "ssid", (char*)ap_info.ssid);
            cJSON_AddNumberToObject(wifi, "rssi", ap_info.rssi);
            
            char mac_str[18];
            snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                   ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2],
                   ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5]);
            cJSON_AddStringToObject(wifi, "bssid", mac_str);
            
            // 获取本地IP
            esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif) {
                esp_netif_ip_info_t ip_info;
                if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                    char ip_str[16];
                    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
                    cJSON_AddStringToObject(wifi, "ip", ip_str);
                    
                    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.netmask));
                    cJSON_AddStringToObject(wifi, "netmask", ip_str);
                    
                    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.gw));
                    cJSON_AddStringToObject(wifi, "gateway", ip_str);
                }
            }
        }
    }
    
    // 如果在AP模式，获取AP信息
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        wifi_config_t ap_config;
        if (esp_wifi_get_config(WIFI_IF_AP, &ap_config) == ESP_OK) {
            cJSON_AddStringToObject(wifi, "ap_ssid", (char*)ap_config.ap.ssid);
            
            // 获取AP IP
            esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
            if (netif) {
                esp_netif_ip_info_t ip_info;
                if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                    char ip_str[16];
                    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
                    cJSON_AddStringToObject(wifi, "ap_ip", ip_str);
                }
            }
        }
    }
    
    // 添加到响应
    cJSON_AddItemToObject(root, "wifi", wifi);
    
    // 转换为字符串并使用PSRAM存储
    char* json_str = cJSON_PrintUnformatted(root);
    PSRAMString result;
    
    if (json_str) {
        result = PSRAMString(json_str);
        free(json_str);
    }
    
    cJSON_Delete(root);
    return result;
}

// 获取WiFi状态信息
PSRAMString WebContent::GetWifiStatus() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "status_response");
    cJSON_AddStringToObject(root, "query", "wifi");
    
    // 添加WiFi信息
    cJSON* wifi = cJSON_CreateObject();
    
    // 获取WiFi模式
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        cJSON_AddStringToObject(wifi, "mode", 
                              mode == WIFI_MODE_NULL ? "NULL" :
                              mode == WIFI_MODE_STA ? "STA" :
                              mode == WIFI_MODE_AP ? "AP" :
                              mode == WIFI_MODE_APSTA ? "APSTA" : "Unknown");
    }
    
    // 如果在STA模式，获取连接信息
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            cJSON_AddStringToObject(wifi, "ssid", (char*)ap_info.ssid);
            cJSON_AddNumberToObject(wifi, "rssi", ap_info.rssi);
            cJSON_AddStringToObject(wifi, "auth_mode", 
                                  ap_info.authmode == WIFI_AUTH_OPEN ? "OPEN" :
                                  ap_info.authmode == WIFI_AUTH_WEP ? "WEP" :
                                  ap_info.authmode == WIFI_AUTH_WPA_PSK ? "WPA_PSK" :
                                  ap_info.authmode == WIFI_AUTH_WPA2_PSK ? "WPA2_PSK" :
                                  ap_info.authmode == WIFI_AUTH_WPA_WPA2_PSK ? "WPA_WPA2_PSK" :
                                  "Unknown");
            
            // 获取本地IP
            esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif) {
                esp_netif_ip_info_t ip_info;
                if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                    char ip_str[16];
                    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
                    cJSON_AddStringToObject(wifi, "ip", ip_str);
                }
            }
        }
    }
    
    // 获取已保存的WiFi配置
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        // 获取STA SSID
        size_t required_size = 0;
        err = nvs_get_str(nvs_handle, "sta_ssid", NULL, &required_size);
        if (err == ESP_OK && required_size > 0) {
            // 使用PSRAM分配内存
            char* ssid = (char*)WEB_SERVER_MALLOC(required_size);
            if (ssid) {
                err = nvs_get_str(nvs_handle, "sta_ssid", ssid, &required_size);
                if (err == ESP_OK) {
                    cJSON_AddStringToObject(wifi, "saved_ssid", ssid);
                }
                WEB_SERVER_FREE(ssid);
            }
        }
        
        nvs_close(nvs_handle);
    }
    
    // 添加到响应
    cJSON_AddItemToObject(root, "wifi", wifi);
    
    // 转换为字符串并使用PSRAM存储
    char* json_str = cJSON_PrintUnformatted(root);
    PSRAMString result;
    
    if (json_str) {
        result = PSRAMString(json_str);
        free(json_str);
    }
    
    cJSON_Delete(root);
    return result;
}

// 设置WiFi配置
bool WebContent::ConfigureWifi(const PSRAMString& ssid, const PSRAMString& password) {
    if (ssid.empty()) {
        ESP_LOGE(TAG, "SSID不能为空");
        return false;
    }
    
    ESP_LOGI(TAG, "配置WiFi，SSID: %s", ssid.c_str());
    
    // 保存到NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS失败: %s", esp_err_to_name(err));
        return false;
    }
    
    // 保存SSID
    err = nvs_set_str(nvs_handle, "sta_ssid", ssid.c_str());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存SSID失败: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    
    // 保存密码
    err = nvs_set_str(nvs_handle, "sta_password", password.c_str());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存密码失败: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    
    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "提交NVS更改失败: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    
    nvs_close(nvs_handle);
    
    // 配置WiFi连接
    wifi_config_t wifi_config = {0};
    
    // 复制SSID
    strlcpy((char*)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid));
    
    // 复制密码
    if (!password.empty()) {
        strlcpy((char*)wifi_config.sta.password, password.c_str(), sizeof(wifi_config.sta.password));
    }
    
    // 获取当前WiFi模式
    wifi_mode_t current_mode;
    esp_wifi_get_mode(&current_mode);
    
    // 如果当前不是STA或APSTA模式，设置为STA模式
    if (current_mode != WIFI_MODE_STA && current_mode != WIFI_MODE_APSTA) {
        if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
            ESP_LOGE(TAG, "设置WiFi模式失败");
            return false;
        }
    }
    
    // 设置WiFi配置
    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_config) != ESP_OK) {
        ESP_LOGE(TAG, "设置WiFi配置失败");
        return false;
    }
    
    // 重新连接WiFi
    if (esp_wifi_disconnect() != ESP_OK) {
        ESP_LOGW(TAG, "断开WiFi连接失败");
    }
    
    if (esp_wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "连接WiFi失败");
        return false;
    }
    
    ESP_LOGI(TAG, "WiFi配置更新成功");
    return true;
}

// 获取文件的MIME类型
const char* WebContent::GetContentType(const PSRAMString& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".js")) return "application/javascript";
    if (path.ends_with(".css")) return "text/css";
    if (path.ends_with(".png")) return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg")) return "image/jpeg";
    if (path.ends_with(".ico")) return "image/x-icon";
    if (path.ends_with(".svg")) return "image/svg+xml";
    if (path.ends_with(".json")) return "application/json";
    if (path.ends_with(".xml")) return "application/xml";
    if (path.ends_with(".pdf")) return "application/pdf";
    if (path.ends_with(".zip")) return "application/zip";
    if (path.ends_with(".mp3")) return "audio/mpeg";
    if (path.ends_with(".mp4")) return "video/mp4";
    return "text/plain";
}

// 处理静态文件请求
esp_err_t WebContent::HandleStaticFile(httpd_req_t* req) {
    const char* uri = req->uri;
    ESP_LOGI(TAG, "处理静态文件请求: %s", uri);
    
    // 获取文件内容类型
    const char* content_type = GetContentType(PSRAMString(uri));
    
    // 检查是否为已知的静态资源
    if (strcmp(uri, "/favicon.ico") == 0) {
        // 处理favicon.ico
        #if defined(_binary_favicon_ico_start) && defined(_binary_favicon_ico_end)
        // 只有当favicon.ico被编译到固件中时才尝试访问
        extern const uint8_t _binary_favicon_ico_start[] asm("_binary_favicon_ico_start");
        extern const uint8_t _binary_favicon_ico_end[]   asm("_binary_favicon_ico_end");
        size_t len = _binary_favicon_ico_end - _binary_favicon_ico_start;
        
        httpd_resp_set_type(req, "image/x-icon");
        return httpd_resp_send(req, (const char*)_binary_favicon_ico_start, len);
        #else
        // 资源不存在
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Favicon not available", -1);
        return ESP_OK;
        #endif
    }
    else if (strcmp(uri, "/style.css") == 0) {
        // 处理CSS文件
        #if defined(_binary_style_css_start) && defined(_binary_style_css_end)
        // 只有当style.css被编译到固件中时才尝试访问
        extern const uint8_t _binary_style_css_start[] asm("_binary_style_css_start");
        extern const uint8_t _binary_style_css_end[]   asm("_binary_style_css_end");
        size_t len = _binary_style_css_end - _binary_style_css_start;
        
        httpd_resp_set_type(req, "text/css");
        return httpd_resp_send(req, (const char*)_binary_style_css_start, len);
        #else
        // 资源不存在
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "CSS not available", -1);
        return ESP_OK;
        #endif
    }
    else if (strcmp(uri, "/script.js") == 0) {
        // 处理JavaScript文件
        #if defined(_binary_script_js_start) && defined(_binary_script_js_end)
        // 只有当script.js被编译到固件中时才尝试访问
        extern const uint8_t _binary_script_js_start[] asm("_binary_script_js_start");
        extern const uint8_t _binary_script_js_end[]   asm("_binary_script_js_end");
        size_t len = _binary_script_js_end - _binary_script_js_start;
        
        httpd_resp_set_type(req, "application/javascript");
        return httpd_resp_send(req, (const char*)_binary_script_js_start, len);
        #else
        // 资源不存在
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "JavaScript not available", -1);
        return ESP_OK;
        #endif
    }
    
    // 如果没有找到对应的静态资源，返回404
    httpd_resp_set_type(req, content_type);  // Set content type even for 404 response
    httpd_resp_send_404(req);
    return ESP_OK;
}

// 初始化静态资源处理器
void WebContent::InitStaticHandlers() {
    ESP_LOGI(TAG, "初始化静态资源处理器");
    
    // 由于我们已经在RegisterStaticContent方法中使用通配符处理器，这里不需要额外操作
    ESP_LOGI(TAG, "静态资源将通过通配符处理器处理");
}

// 计算嵌入资源大小
void WebContent::ComputeResourceSizes() {
    ESP_LOGI(TAG, "计算嵌入资源大小...");
    
    // 检查常见的嵌入资源是否存在
    #if defined(_binary_favicon_ico_start) && defined(_binary_favicon_ico_end)
    extern const uint8_t _binary_favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const uint8_t _binary_favicon_ico_end[] asm("_binary_favicon_ico_end");
    favicon_ico_size = _binary_favicon_ico_end - _binary_favicon_ico_start;
    ESP_LOGI(TAG, "favicon.ico 大小: %zu 字节", favicon_ico_size);
    #else
    favicon_ico_size = 0;
    ESP_LOGW(TAG, "favicon.ico 未在固件中找到");
    #endif
    
    #if defined(_binary_style_css_start) && defined(_binary_style_css_end)
    extern const uint8_t _binary_style_css_start[] asm("_binary_style_css_start");
    extern const uint8_t _binary_style_css_end[] asm("_binary_style_css_end");
    style_css_size = _binary_style_css_end - _binary_style_css_start;
    ESP_LOGI(TAG, "style.css 大小: %zu 字节", style_css_size);
    #else
    style_css_size = 0;
    ESP_LOGW(TAG, "style.css 未在固件中找到");
    #endif
    
    #if defined(_binary_script_js_start) && defined(_binary_script_js_end)
    extern const uint8_t _binary_script_js_start[] asm("_binary_script_js_start");
    extern const uint8_t _binary_script_js_end[] asm("_binary_script_js_end");
    script_js_size = _binary_script_js_end - _binary_script_js_start;
    ESP_LOGI(TAG, "script.js 大小: %zu 字节", script_js_size);
    #else
    script_js_size = 0;
    ESP_LOGW(TAG, "script.js 未在固件中找到");
    #endif
    
    ESP_LOGI(TAG, "嵌入资源大小计算完成");
}

// 处理CSS文件请求
esp_err_t WebContent::HandleCssFile(httpd_req_t* req) {
    const char* uri = req->uri;
    ESP_LOGI(TAG, "处理CSS文件请求: %s", uri);
    
    // 提取文件名 - 例如从 "/css/common.css" 提取 "common.css"
    const char* filename = strrchr(uri, '/');
    if (!filename) {
        ESP_LOGE(TAG, "无法从URI中提取文件名: %s", uri);
        httpd_resp_send_404(req);
        return ESP_OK;
    }
    filename++; // 跳过'/'字符
    
    // 构建符合ESP-IDF嵌入文件命名规范的符号名
    // ESP-IDF通常将文件名中的非字母数字字符替换为下划线
    char sym_name[64] = {0};
    strncpy(sym_name, filename, sizeof(sym_name) - 1);
    
    // 将.替换为_
    for (char* p = sym_name; *p; ++p) {
        if (*p == '.') *p = '_';
    }
    
    char start_sym[80];
    char end_sym[80];
    snprintf(start_sym, sizeof(start_sym), "_binary_%s_start", sym_name);
    snprintf(end_sym, sizeof(end_sym), "_binary_%s_end", sym_name);
    
    ESP_LOGI(TAG, "尝试加载CSS文件: %s, 查找符号: %s", filename, start_sym);
    
    // 设置内容类型为CSS
    httpd_resp_set_type(req, "text/css");
    
    // 尝试使用常见的CSS文件名模式
    if (strcmp(filename, "common.css") == 0) {
        extern const uint8_t _binary_common_css_start[] asm("_binary_common_css_start");
        extern const uint8_t _binary_common_css_end[] asm("_binary_common_css_end");
        size_t len = _binary_common_css_end - _binary_common_css_start;
        ESP_LOGI(TAG, "发送common.css, 大小: %d字节", (int)len);
        return httpd_resp_send(req, (const char*)_binary_common_css_start, len);
    } 
    else if (strcmp(filename, "index.css") == 0) {
        extern const uint8_t _binary_index_css_start[] asm("_binary_index_css_start");
        extern const uint8_t _binary_index_css_end[] asm("_binary_index_css_end");
        size_t len = _binary_index_css_end - _binary_index_css_start;
        ESP_LOGI(TAG, "发送index.css, 大小: %d字节", (int)len);
        return httpd_resp_send(req, (const char*)_binary_index_css_start, len);
    }
    else if (strcmp(filename, "move.css") == 0) {
        extern const uint8_t _binary_move_css_start[] asm("_binary_move_css_start");
        extern const uint8_t _binary_move_css_end[] asm("_binary_move_css_end");
        size_t len = _binary_move_css_end - _binary_move_css_start;
        ESP_LOGI(TAG, "发送move.css, 大小: %d字节", (int)len);
        return httpd_resp_send(req, (const char*)_binary_move_css_start, len);
    }
    else if (strcmp(filename, "ai.css") == 0) {
        extern const uint8_t _binary_ai_css_start[] asm("_binary_ai_css_start");
        extern const uint8_t _binary_ai_css_end[] asm("_binary_ai_css_end");
        size_t len = _binary_ai_css_end - _binary_ai_css_start;
        ESP_LOGI(TAG, "发送ai.css, 大小: %d字节", (int)len);
        return httpd_resp_send(req, (const char*)_binary_ai_css_start, len);
    }
    else if (strcmp(filename, "vision.css") == 0) {
        extern const uint8_t _binary_vision_css_start[] asm("_binary_vision_css_start");
        extern const uint8_t _binary_vision_css_end[] asm("_binary_vision_css_end");
        size_t len = _binary_vision_css_end - _binary_vision_css_start;
        ESP_LOGI(TAG, "发送vision.css, 大小: %d字节", (int)len);
        return httpd_resp_send(req, (const char*)_binary_vision_css_start, len);
    }
    
    // 如果没有找到匹配的CSS文件
    ESP_LOGW(TAG, "未找到CSS文件: %s", filename);
    httpd_resp_send_404(req);
    return ESP_OK;
}

// 处理JavaScript文件请求
esp_err_t WebContent::HandleJsFile(httpd_req_t* req) {
    const char* uri = req->uri;
    ESP_LOGI(TAG, "处理JS文件请求: %s", uri);
    
    // 提取文件名 - 例如从 "/js/app.js" 提取 "app.js"
    const char* filename = strrchr(uri, '/');
    if (!filename) {
        ESP_LOGE(TAG, "无法从URI中提取文件名: %s", uri);
        httpd_resp_send_404(req);
        return ESP_OK;
    }
    filename++; // 跳过'/'字符
    
    // 构建符合ESP-IDF嵌入文件命名规范的符号名
    char sym_name[64] = {0};
    strncpy(sym_name, filename, sizeof(sym_name) - 1);
    
    // 将.替换为_
    for (char* p = sym_name; *p; ++p) {
        if (*p == '.') *p = '_';
    }
    
    char start_sym[80];
    char end_sym[80];
    snprintf(start_sym, sizeof(start_sym), "_binary_%s_start", sym_name);
    snprintf(end_sym, sizeof(end_sym), "_binary_%s_end", sym_name);
    
    ESP_LOGI(TAG, "尝试加载JS文件: %s, 查找符号: %s", filename, start_sym);
    
    // 设置内容类型为JavaScript
    httpd_resp_set_type(req, "application/javascript");
    
    // 尝试使用常见的JS文件名模式
    if (strcmp(filename, "api_client.js") == 0) {
        extern const uint8_t _binary_api_client_js_start[] asm("_binary_api_client_js_start");
        extern const uint8_t _binary_api_client_js_end[] asm("_binary_api_client_js_end");
        size_t len = _binary_api_client_js_end - _binary_api_client_js_start;
        ESP_LOGI(TAG, "发送api_client.js, 大小: %d字节", (int)len);
        return httpd_resp_send(req, (const char*)_binary_api_client_js_start, len);
    } 
    else if (strcmp(filename, "index.js") == 0) {
        extern const uint8_t _binary_index_js_start[] asm("_binary_index_js_start");
        extern const uint8_t _binary_index_js_end[] asm("_binary_index_js_end");
        size_t len = _binary_index_js_end - _binary_index_js_start;
        ESP_LOGI(TAG, "发送index.js, 大小: %d字节", (int)len);
        return httpd_resp_send(req, (const char*)_binary_index_js_start, len);
    }
    else if (strcmp(filename, "move.js") == 0) {
        extern const uint8_t _binary_move_js_start[] asm("_binary_move_js_start");
        extern const uint8_t _binary_move_js_end[] asm("_binary_move_js_end");
        size_t len = _binary_move_js_end - _binary_move_js_start;
        ESP_LOGI(TAG, "发送move.js, 大小: %d字节", (int)len);
        return httpd_resp_send(req, (const char*)_binary_move_js_start, len);
    }
    else if (strcmp(filename, "ai.js") == 0) {
        extern const uint8_t _binary_ai_js_start[] asm("_binary_ai_js_start");
        extern const uint8_t _binary_ai_js_end[] asm("_binary_ai_js_end");
        size_t len = _binary_ai_js_end - _binary_ai_js_start;
        ESP_LOGI(TAG, "发送ai.js, 大小: %d字节", (int)len);
        return httpd_resp_send(req, (const char*)_binary_ai_js_start, len);
    }
    else if (strcmp(filename, "vision.js") == 0) {
        extern const uint8_t _binary_vision_js_start[] asm("_binary_vision_js_start");
        extern const uint8_t _binary_vision_js_end[] asm("_binary_vision_js_end");
        size_t len = _binary_vision_js_end - _binary_vision_js_start;
        ESP_LOGI(TAG, "发送vision.js, 大小: %d字节", (int)len);
        return httpd_resp_send(req, (const char*)_binary_vision_js_start, len);
    }
    
    // 如果没有找到匹配的JS文件
    ESP_LOGW(TAG, "未找到JS文件: %s", filename);
    httpd_resp_send_404(req);
    return ESP_OK;
} 