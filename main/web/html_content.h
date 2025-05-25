#ifndef HTML_CONTENT_H
#define HTML_CONTENT_H

#include <stddef.h>

// Make sure this header is included in all files that need HTML content
#ifdef __cplusplus
extern "C" {  // This ensures C++ code can call these functions with C linkage
#endif

// 获取HTML内容大小的函数
size_t get_index_html_size();
size_t get_move_html_size();
size_t get_ai_html_size();
size_t get_vision_html_size();
size_t get_motor_html_size(); // 为向后兼容而保留，现在使用 /car 替代 /motor

// 获取HTML内容的函数
const char* get_index_html_content();
const char* get_move_html_content();
const char* get_ai_html_content();
const char* get_vision_html_content();

// 外部声明供C代码访问的常量
extern const char* INDEX_HTML;
extern const char* MOVE_HTML;
extern const char* AI_HTML;
extern const char* VISION_HTML;

#ifdef __cplusplus
}
#endif

#endif // HTML_CONTENT_H