#ifndef _VISION_CONTROLLER_H_
#define _VISION_CONTROLLER_H_

#include <esp_camera.h>
#include "../components.h"
#include "sdkconfig.h"
#include <driver/gpio.h>

// 定义摄像头接口引脚 - 为bread-compact-wifi板
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5
#define Y2_GPIO_NUM       11
#define Y3_GPIO_NUM       9
#define Y4_GPIO_NUM       8
#define Y5_GPIO_NUM       10
#define Y6_GPIO_NUM       12
#define Y7_GPIO_NUM       18
#define Y8_GPIO_NUM       17
#define Y9_GPIO_NUM       16
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM     13

// LED闪光灯定义 - 根据不同ESP32芯片配置不同引脚
#if CONFIG_IDF_TARGET_ESP32
// ESP32芯片使用GPIO4作为LED引脚（有效范围0-39）
#define LED_PIN          4
#elif CONFIG_IDF_TARGET_ESP32S3
// ESP32-S3芯片可以使用GPIO45（有效范围0-47）
#define LED_PIN          45
#elif CONFIG_IDF_TARGET_ESP32C3
// ESP32-C3芯片使用GPIO7（有效范围0-21）
#define LED_PIN          13
#else
// 默认使用低编号引脚确保安全
#define LED_PIN          4
#endif

// 视觉控制器组件
class VisionController : public Component {
public:
    VisionController();
    virtual ~VisionController();

    // Component接口实现
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;

    // 获取摄像头帧
    camera_fb_t* GetFrame();
    
    // 释放摄像头帧
    void ReturnFrame(camera_fb_t* fb);
    
    // 设置LED闪光灯亮度
    void SetLedIntensity(int intensity);
    
    // 获取LED闪光灯亮度
    int GetLedIntensity() const;
    
    // 是否正在直播
    bool IsStreaming() const;
    
    // 设置直播状态
    void SetStreaming(bool streaming);

private:
    bool running_;
    bool streaming_;
    int led_intensity_;
    
    // 初始化摄像头
    bool InitCamera();
    
    // 初始化LED闪光灯
    bool InitLed();
    
    // 更新LED闪光灯状态
    void UpdateLed();
};

#endif // _VISION_CONTROLLER_H_ 