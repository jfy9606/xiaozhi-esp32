#include "vision_controller.h"
#include <esp_log.h>
#include <esp_camera.h>
#include "sdkconfig.h"

#define TAG "VisionController"

VisionController::VisionController() 
    : running_(false), streaming_(false), led_intensity_(0) {
}

VisionController::~VisionController() {
    Stop();
}

bool VisionController::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Vision controller already running");
        return true;
    }

    if (!InitCamera()) {
        ESP_LOGE(TAG, "Failed to initialize camera");
        return false;
    }

    if (!InitLed()) {
        ESP_LOGW(TAG, "Failed to initialize LED");
        // LED failure is not critical
    }
    
    running_ = true;
    ESP_LOGI(TAG, "Vision controller started");
    return true;
}

void VisionController::Stop() {
    if (!running_) {
        return;
    }

    // 停止摄像头
    esp_camera_deinit();
    
    running_ = false;
    ESP_LOGI(TAG, "Vision controller stopped");
}

bool VisionController::IsRunning() const {
    return running_;
}

const char* VisionController::GetName() const {
    return "VisionController";
}

camera_fb_t* VisionController::GetFrame() {
    if (!running_) {
        ESP_LOGW(TAG, "Vision controller not running");
        return nullptr;
    }
    
    return esp_camera_fb_get();
}

void VisionController::ReturnFrame(camera_fb_t* fb) {
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

void VisionController::SetLedIntensity(int intensity) {
    if (intensity < 0) intensity = 0;
    if (intensity > 255) intensity = 255;
    
    led_intensity_ = intensity;
    UpdateLed();
}

int VisionController::GetLedIntensity() const {
    return led_intensity_;
}

bool VisionController::IsStreaming() const {
    return streaming_;
}

void VisionController::SetStreaming(bool streaming) {
    streaming_ = streaming;
    UpdateLed();
}

bool VisionController::InitCamera() {
    // 使用esp32-camera组件的默认配置
    camera_config_t config;
    
    // 配置camera引脚 - 使用头文件中定义的引脚
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;  
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    
    // 摄像头基本配置
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    
    // 根据可用PSRAM初始化帧缓冲区大小
    if (psramFound()) {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }
    
    // 初始化摄像头
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return false;
    }
    
    // 获取摄像头传感器并配置默认参数
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        // 默认设置：使用esp32-camera组件的通用设置
        s->set_framesize(s, FRAMESIZE_VGA);
        s->set_quality(s, 10);
        s->set_brightness(s, 0);
        s->set_contrast(s, 0);
        s->set_saturation(s, 0);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_wb_mode(s, 0);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 0);
        s->set_ae_level(s, 0);
        s->set_aec_value(s, 300);
        s->set_gain_ctrl(s, 1);
        s->set_agc_gain(s, 0);
        s->set_gainceiling(s, (gainceiling_t)0);
        s->set_bpc(s, 0);
        s->set_wpc(s, 1);
        s->set_raw_gma(s, 1);
        s->set_lenc(s, 1);
        s->set_vflip(s, 0);
        s->set_hmirror(s, 0);
        s->set_dcw(s, 1);
        s->set_colorbar(s, 0);
    }
    
    return true;
}

bool VisionController::InitLed() {
    // 配置LED引脚
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << LED_PIN);
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    
    // 配置GPIO
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LED pin config failed with error 0x%x", err);
        return false;
    }
    
    // 初始时LED关闭
    led_intensity_ = 0;
    gpio_set_level((gpio_num_t)LED_PIN, 0);
    
    return true;
}

void VisionController::UpdateLed() {
    if (!running_) {
        return;
    }
    
    // 如果在直播模式且亮度超过上限，则限制亮度
    int intensity = led_intensity_;
    if (streaming_ && intensity > 128) {
        intensity = 128;  // 在流媒体模式下限制亮度以避免过热
    }
    
    // 简单的开关控制，后续可以扩展为PWM控制
    gpio_set_level((gpio_num_t)LED_PIN, intensity > 0 ? 1 : 0);
} 