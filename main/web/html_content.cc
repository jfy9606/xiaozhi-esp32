#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include "sdkconfig.h"
#include "html_content.h"
#include "web_server.h"

#define TAG "HtmlContent"

// 从assets/html目录导入HTML内容
#define HTML_STR(x) #x

// 声明外部嵌入的HTML文件
extern const uint8_t index_html_start[] asm("_binary_main_assets_html_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_main_assets_html_index_html_end");

extern const uint8_t move_html_start[] asm("_binary_main_assets_html_move_html_start");
extern const uint8_t move_html_end[] asm("_binary_main_assets_html_move_html_end");

extern const uint8_t ai_html_start[] asm("_binary_main_assets_html_ai_html_start");
extern const uint8_t ai_html_end[] asm("_binary_main_assets_html_ai_html_end");

extern const uint8_t vision_html_start[] asm("_binary_main_assets_html_vision_html_start");
extern const uint8_t vision_html_end[] asm("_binary_main_assets_html_vision_html_end");

// HTML内容大小
static size_t index_html_size = 0;
static size_t move_html_size = 0;
static size_t ai_html_size = 0;
static size_t vision_html_size = 0;

// 初始化函数 - 计算HTML内容大小
static void init_html_content() {
    static bool initialized = false;
    if (initialized) return;
    
    ESP_LOGI(TAG, "初始化HTML内容...");
    
    // 计算大小
    index_html_size = index_html_end - index_html_start;
    move_html_size = move_html_end - move_html_start;
    ai_html_size = ai_html_end - ai_html_start;
    vision_html_size = vision_html_end - vision_html_start;
    
    ESP_LOGI(TAG, "HTML文件大小: index=%zu, move=%zu, ai=%zu, vision=%zu",
            index_html_size, move_html_size, ai_html_size, vision_html_size);
    
    ESP_LOGI(TAG, "HTML内容将直接从flash读取");
    
    initialized = true;
}

// 为保持向后兼容而提供的常量别名
extern "C" {
    const char* INDEX_HTML = (const char*)index_html_start;
    const char* MOVE_HTML = (const char*)move_html_start;
    const char* AI_HTML = (const char*)ai_html_start;
    const char* VISION_HTML = (const char*)vision_html_start;
    
    // 导出HTML大小获取函数，供外部C代码调用
    size_t get_index_html_size() {
        init_html_content();
        return index_html_size;
    }

    size_t get_move_html_size() {
        init_html_content();
        return move_html_size;
    }

    size_t get_ai_html_size() {
        init_html_content();
        return ai_html_size;
    }

    size_t get_vision_html_size() {
        init_html_content();
        return vision_html_size;
    }

    size_t get_motor_html_size() {
        // 返回move.html大小，因为motor.html已被合并到move.html
        init_html_content();
        return move_html_size;
    }

    // 获取HTML内容的函数 - 直接返回flash中的内容
    const char* get_index_html_content() {
        init_html_content();
        return (const char*)index_html_start;
    }

    const char* get_move_html_content() {
        init_html_content();
        return (const char*)move_html_start;
    }

    const char* get_ai_html_content() {
        init_html_content();
        return (const char*)ai_html_start;
    }

    const char* get_vision_html_content() {
        init_html_content();
        return (const char*)vision_html_start;
    }
}