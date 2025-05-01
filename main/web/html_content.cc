#include <esp_log.h>
#include <esp_http_server.h>

#define TAG "HtmlContent"

// 从assets/html目录导入HTML内容
#define HTML_STR(x) #x

// 声明外部嵌入的HTML文件
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

extern const uint8_t car_html_start[] asm("_binary_car_html_start");
extern const uint8_t car_html_end[] asm("_binary_car_html_end");

extern const uint8_t ai_html_start[] asm("_binary_ai_html_start");
extern const uint8_t ai_html_end[] asm("_binary_ai_html_end");

extern const uint8_t cam_html_start[] asm("_binary_cam_html_start");
extern const uint8_t cam_html_end[] asm("_binary_cam_html_end");

// 提供HTML内容的函数
const char* INDEX_HTML = (const char*)index_html_start;
const char* CAR_HTML = (const char*)car_html_start;
const char* AI_HTML = (const char*)ai_html_start;
const char* CAM_HTML = (const char*)cam_html_start;

// 获取HTML文件大小的辅助函数
size_t get_index_html_size() {
    return index_html_end - index_html_start;
}

size_t get_car_html_size() {
    return car_html_end - car_html_start;
}

size_t get_ai_html_size() {
    return ai_html_end - ai_html_start;
}

size_t get_cam_html_size() {
    return cam_html_end - cam_html_start;
} 