#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include "sdkconfig.h"
#include "html_content.h"
#include "web_server.h"

#define TAG "HtmlContent"

// PSRAM内存分配宏 - 根据CONFIG_WEB_SERVER_USE_PSRAM决定是否使用PSRAM
#if defined(CONFIG_WEB_SERVER_USE_PSRAM) && CONFIG_WEB_SERVER_USE_PSRAM && WEB_SERVER_HAS_PSRAM
#define HTML_MALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define HTML_FREE(ptr) heap_caps_free(ptr)
#define USING_PSRAM_FOR_HTML 1
#else
#define HTML_MALLOC(size) malloc(size)
#define HTML_FREE(ptr) free(ptr)
#define USING_PSRAM_FOR_HTML 0
#endif

// 从assets/html目录导入HTML内容
#define HTML_STR(x) #x

// 声明外部嵌入的HTML文件
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

extern const uint8_t motor_html_start[] asm("_binary_motor_html_start");
extern const uint8_t motor_html_end[] asm("_binary_motor_html_end");

extern const uint8_t ai_html_start[] asm("_binary_ai_html_start");
extern const uint8_t ai_html_end[] asm("_binary_ai_html_end");

extern const uint8_t vision_html_start[] asm("_binary_vision_html_start");
extern const uint8_t vision_html_end[] asm("_binary_vision_html_end");

// PSRAM存储的HTML内容指针
static char* index_html_psram = NULL;
static char* motor_html_psram = NULL;
static char* ai_html_psram = NULL;
static char* vision_html_psram = NULL;

// HTML内容大小
static size_t index_html_size = 0;
static size_t motor_html_size = 0;
static size_t ai_html_size = 0;
static size_t vision_html_size = 0;

// 初始化函数 - 将HTML内容复制到PSRAM
static void init_html_content() {
    static bool initialized = false;
    if (initialized) return;
    
    ESP_LOGI(TAG, "初始化HTML内容...");
    
    // 计算大小
    index_html_size = index_html_end - index_html_start;
    motor_html_size = motor_html_end - motor_html_start;
    ai_html_size = ai_html_end - ai_html_start;
    vision_html_size = vision_html_end - vision_html_start;
    
    // 分配PSRAM内存并复制内容
    #if USING_PSRAM_FOR_HTML
    ESP_LOGI(TAG, "将HTML内容复制到PSRAM...");
    
    index_html_psram = (char*)HTML_MALLOC(index_html_size + 1);
    if (index_html_psram) {
        memcpy(index_html_psram, index_html_start, index_html_size);
        index_html_psram[index_html_size] = '\0';
        ESP_LOGI(TAG, "index.html 复制到PSRAM: %zu 字节", index_html_size);
    } else {
        ESP_LOGW(TAG, "无法分配PSRAM用于index.html (%zu 字节), 将使用内部flash存储", index_html_size);
    }
    
    motor_html_psram = (char*)HTML_MALLOC(motor_html_size + 1);
    if (motor_html_psram) {
        memcpy(motor_html_psram, motor_html_start, motor_html_size);
        motor_html_psram[motor_html_size] = '\0';
        ESP_LOGI(TAG, "motor.html 复制到PSRAM: %zu 字节", motor_html_size);
    } else {
        ESP_LOGW(TAG, "无法分配PSRAM用于motor.html (%zu 字节), 将使用内部flash存储", motor_html_size);
    }
    
    ai_html_psram = (char*)HTML_MALLOC(ai_html_size + 1);
    if (ai_html_psram) {
        memcpy(ai_html_psram, ai_html_start, ai_html_size);
        ai_html_psram[ai_html_size] = '\0';
        ESP_LOGI(TAG, "ai.html 复制到PSRAM: %zu 字节", ai_html_size);
    } else {
        ESP_LOGW(TAG, "无法分配PSRAM用于ai.html (%zu 字节), 将使用内部flash存储", ai_html_size);
    }
    
    vision_html_psram = (char*)HTML_MALLOC(vision_html_size + 1);
    if (vision_html_psram) {
        memcpy(vision_html_psram, vision_html_start, vision_html_size);
        vision_html_psram[vision_html_size] = '\0';
        ESP_LOGI(TAG, "vision.html 复制到PSRAM: %zu 字节", vision_html_size);
    } else {
        ESP_LOGW(TAG, "无法分配PSRAM用于vision.html (%zu 字节), 将使用内部flash存储", vision_html_size);
    }
    
    // 检查是否所有文件都成功复制到PSRAM
    int success_count = 0;
    if (index_html_psram) success_count++;
    if (motor_html_psram) success_count++;
    if (ai_html_psram) success_count++;
    if (vision_html_psram) success_count++;
    
    ESP_LOGI(TAG, "%d/4 HTML文件成功复制到PSRAM", success_count);
    #else
    ESP_LOGI(TAG, "PSRAM存储HTML未启用，使用内部flash存储");
    #endif
    
    initialized = true;
}

// 为保持向后兼容而提供的常量别名
extern "C" {
    const char* INDEX_HTML = (const char*)index_html_start;
    const char* MOTOR_HTML = (const char*)motor_html_start;
    const char* AI_HTML = (const char*)ai_html_start;
    const char* VISION_HTML = (const char*)vision_html_start;
    
    // 导出HTML大小获取函数，供外部C代码调用
    size_t get_index_html_size() {
        init_html_content();
        return index_html_size;
    }

    size_t get_motor_html_size() {
        init_html_content();
        return motor_html_size;
    }

    size_t get_ai_html_size() {
        init_html_content();
        return ai_html_size;
    }

    size_t get_vision_html_size() {
        init_html_content();
        return vision_html_size;
    }

    // 获取HTML内容的函数 - 优先返回PSRAM中的内容
    const char* get_index_html_content() {
        init_html_content();
        return index_html_psram ? index_html_psram : (const char*)index_html_start;
    }

    const char* get_motor_html_content() {
        init_html_content();
        return motor_html_psram ? motor_html_psram : (const char*)motor_html_start;
    }

    const char* get_ai_html_content() {
        init_html_content();
        return ai_html_psram ? ai_html_psram : (const char*)ai_html_start;
    }

    const char* get_vision_html_content() {
        init_html_content();
        return vision_html_psram ? vision_html_psram : (const char*)vision_html_start;
    }
}