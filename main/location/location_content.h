#ifndef _LOCATION_CONTENT_H_
#define _LOCATION_CONTENT_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取位置页面HTML内容
 * @return 指向HTML内容的指针
 */
const char* get_location_html_content();

/**
 * @brief 获取位置页面HTML内容大小
 * @return HTML内容的大小（字节数）
 */
size_t get_location_html_size();

#ifdef __cplusplus
}
#endif

#endif // _LOCATION_CONTENT_H_ 