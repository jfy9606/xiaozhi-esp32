#pragma once

#include <esp_http_server.h>
#include "../components.h"
#include "web_server.h"
#include <string>
#include <map>

// 配置选项
#ifndef CONFIG_WEB_CONTENT_TAG
#define CONFIG_WEB_CONTENT_TAG "WebContent"
#endif

// 所有内存分配根据WEB_SERVER_USE_PSRAM决定是否使用PSRAM宏

// Web内容处理组件
class WebContent : public Component {
public:
    WebContent(WebServer* server);
    virtual ~WebContent();

    // Component接口实现
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;

    // 获取MIME类型
    static const char* GetContentType(const PSRAMString& path);

    // 注册静态资源处理器
    void RegisterStaticContent();
    
    // 注册WebSocket消息处理器
    void RegisterWebSocketHandlers();

    // WebSocket消息处理函数
    // 每种类型定义一个处理函数
    void HandleSystemMessage(int client_id, const PSRAMString& message, const PSRAMString& type);
    void HandleControlMessage(int client_id, const PSRAMString& message, const PSRAMString& type);
    void HandleHeartbeatMessage(int client_id, const PSRAMString& message, const PSRAMString& type);
    void HandleStatusMessage(int client_id, const PSRAMString& message, const PSRAMString& type);
    
    // 获取系统状态信息
    PSRAMString GetSystemStatus();
    
    // 获取WiFi状态信息
    PSRAMString GetWifiStatus();
    
    // 设置WiFi配置
    bool ConfigureWifi(const PSRAMString& ssid, const PSRAMString& password);

    // 处理CSS和JS文件请求
    static esp_err_t HandleCssFile(httpd_req_t* req);
    static esp_err_t HandleJsFile(httpd_req_t* req);

private:
    WebServer* server_;
    bool running_;
    
    // 处理静态文件请求
    static esp_err_t HandleStaticFile(httpd_req_t* req);
    
    // 初始化静态资源处理器
    void InitStaticHandlers();
    
    // 预加载静态资源到PSRAM
    void PreloadStaticAssets();
    
    // 缓存的静态资源
    static char* favicon_ico_psram;
    static size_t favicon_ico_size;
    static char* style_css_psram;
    static size_t style_css_size;
    static char* script_js_psram;
    static size_t script_js_size;
};

// 导出HTML内容获取函数，供C代码调用
extern "C" {
    size_t get_index_html_size();
    size_t get_move_html_size();
    size_t get_ai_html_size();
    size_t get_vision_html_size();
    size_t get_motor_html_size();
    
    const char* get_index_html_content();
    const char* get_move_html_content();
    const char* get_ai_html_content();
    const char* get_vision_html_content();
} 