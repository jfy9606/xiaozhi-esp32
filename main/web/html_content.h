#ifndef _HTML_CONTENT_H_
#define _HTML_CONTENT_H_

#include <stddef.h>

// Make sure this header is included in all files that need HTML content
#ifdef __cplusplus
extern "C" {  // This ensures C++ code can call these functions with C linkage
#endif

// 获取HTML内容大小的函数
size_t get_index_html_size();
size_t get_motor_html_size();
size_t get_ai_html_size();
size_t get_vision_html_size();

// 获取HTML内容的函数
const char* get_index_html_content();
const char* get_motor_html_content();
const char* get_ai_html_content();
const char* get_vision_html_content();

// 向后兼容的常量别名 - 不推荐直接使用
extern const char* INDEX_HTML;
extern const char* MOTOR_HTML;
extern const char* AI_HTML;
extern const char* VISION_HTML;

#ifdef __cplusplus
}
#endif

#endif // _HTML_CONTENT_H_