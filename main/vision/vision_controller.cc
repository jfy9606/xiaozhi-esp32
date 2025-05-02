#include "vision_controller.h"
#include <esp_log.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <driver/ledc.h>
#include "sdkconfig.h"

#define TAG "VisionController"

// LEDC PWM配置 - 用于LED控制
#define LED_LEDC_TIMER     LEDC_TIMER_0
#define LED_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define LED_LEDC_CHANNEL   LEDC_CHANNEL_2
#define LED_LEDC_DUTY_RES  LEDC_TIMER_8_BIT  // 8位分辨率，0-255
#define LED_LEDC_FREQ      5000              // PWM频率5kHz

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

    ESP_LOGI(TAG, "Starting VisionController with integrated camera functionality");

    // 先尝试预留DMA内存 - 模仿car_idf项目
    size_t psram_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t dram_size = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "Memory available - PSRAM: %zu bytes, DRAM: %zu bytes", psram_size, dram_size);
    ESP_LOGI(TAG, "Maximum contiguous DMA block: %zu bytes", 
             heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));

    ESP_LOGI(TAG, "Initializing camera subsystem");
    if (!InitCamera()) {
        ESP_LOGE(TAG, "Failed to initialize camera subsystem");
        return false;
    }
    ESP_LOGI(TAG, "Camera subsystem initialized successfully");

    ESP_LOGI(TAG, "Initializing LED subsystem");
    if (!InitLed()) {
        ESP_LOGW(TAG, "Failed to initialize LED subsystem - continuing without LED functionality");
        // LED failure is not critical
    } else {
        ESP_LOGI(TAG, "LED subsystem initialized successfully");
    }
    
    // 获取摄像头传感器信息并记录
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        ESP_LOGI(TAG, "Camera sensor information:");
        ESP_LOGI(TAG, "  - ID: 0x%x", s->id.PID);
        ESP_LOGI(TAG, "  - Resolution: %d x %d", s->status.framesize, s->status.framesize);
        ESP_LOGI(TAG, "  - JPEG Quality: %d", s->status.quality);
    }
    
    running_ = true;
    ESP_LOGI(TAG, "VisionController started successfully, camera ready for use");
    return true;
}

void VisionController::Stop() {
    if (!running_) {
        return;
    }

    // 首先关闭LED
    if (LED_PIN >= 0 && LED_PIN < GPIO_NUM_MAX) {
        // 使用LEDC API关闭LED
        ledc_set_duty(LED_LEDC_MODE, LED_LEDC_CHANNEL, 0);
        ledc_update_duty(LED_LEDC_MODE, LED_LEDC_CHANNEL);
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
    ESP_LOGI(TAG, "Initializing camera with memory-safe settings");
    
    // 检查PSRAM大小并调整摄像头配置
    size_t psram_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t dram_size = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "Memory available - PSRAM: %zu bytes, DRAM: %zu bytes", psram_size, dram_size);
    
    // 使用esp32-camera组件的默认配置
    camera_config_t config = {0}; // 初始化为0，避免未初始化数据
    
    // 验证摄像头引脚配置是否有效
    bool pins_valid = true;
    
    // 安全检查XCLK, SIOD, SIOC引脚（必须有效）
    if (XCLK_GPIO_NUM < 0 || SIOD_GPIO_NUM < 0 || SIOC_GPIO_NUM < 0) {
        ESP_LOGE(TAG, "Invalid essential camera pins: XCLK=%d, SIOD=%d, SIOC=%d", 
                 XCLK_GPIO_NUM, SIOD_GPIO_NUM, SIOC_GPIO_NUM);
        pins_valid = false;
    }
    
    // 安全检查数据引脚和同步引脚（必须有效）
    if (Y2_GPIO_NUM < 0 || Y3_GPIO_NUM < 0 || Y4_GPIO_NUM < 0 || Y5_GPIO_NUM < 0 ||
        Y6_GPIO_NUM < 0 || Y7_GPIO_NUM < 0 || Y8_GPIO_NUM < 0 || Y9_GPIO_NUM < 0 ||
        VSYNC_GPIO_NUM < 0 || HREF_GPIO_NUM < 0 || PCLK_GPIO_NUM < 0) {
        ESP_LOGE(TAG, "Invalid camera data pins");
        pins_valid = false;
    }
    
    if (!pins_valid) {
        ESP_LOGE(TAG, "Camera pin configuration invalid, cannot proceed");
        return false;
    }
    
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
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    
    // 将I2C通道从默认的0更改为1（SCCB是I2C的一种变体，用于相机通信）
    config.sccb_i2c_port = 1;
    
    // 使用与car_idf项目相同的分阶段初始化策略
    esp_err_t ret = ESP_FAIL;
    
    // 尝试不同的配置，从高质量到低质量
    framesize_t framesizes[] = {FRAMESIZE_VGA, FRAMESIZE_HVGA, FRAMESIZE_QVGA};
    int qualities[] = {10, 12, 15};
    int fb_counts[] = {2, 1, 1};
    
    for (int attempt = 0; attempt < 3 && ret != ESP_OK; attempt++) {
        // 根据可用PSRAM和尝试次数调整配置
        if (psram_size > 1024 * 1024 && attempt == 0) {
            // 大内存配置
            config.frame_size = framesizes[0];
            config.jpeg_quality = qualities[0];
            config.fb_count = fb_counts[0];
            ESP_LOGI(TAG, "Attempt %d: High memory config: %s, quality %d, %d buffers", 
                     attempt+1, "VGA", qualities[0], fb_counts[0]);
        } else if (psram_size > 400 * 1024 && attempt <= 1) {
            // 中等内存配置
            config.frame_size = framesizes[1];
            config.jpeg_quality = qualities[1];
            config.fb_count = fb_counts[1];
            ESP_LOGI(TAG, "Attempt %d: Medium memory config: %s, quality %d, %d buffers", 
                     attempt+1, "HVGA", qualities[1], fb_counts[1]);
        } else {
            // 低内存配置
            config.frame_size = framesizes[2];
            config.jpeg_quality = qualities[2];
            config.fb_count = fb_counts[2];
            ESP_LOGI(TAG, "Attempt %d: Low memory config: %s, quality %d, %d buffers", 
                     attempt+1, "QVGA", qualities[2], fb_counts[2]);
        }
        
        // 根据尝试次数降低时钟频率
        if (attempt > 0) {
            config.xclk_freq_hz = 10000000; // 降低到10MHz
        }
        
        // 检查是否有足够的PSRAM，决定帧缓冲区位置
        if (psram_size > 0) {
            config.fb_location = CAMERA_FB_IN_PSRAM;
        } else {
            config.fb_location = CAMERA_FB_IN_DRAM;
            // 如果没有PSRAM，强制使用最低配置
            config.frame_size = FRAMESIZE_QVGA;
            config.jpeg_quality = 15;
            config.fb_count = 1;
        }
        
        ESP_LOGI(TAG, "Camera init attempt %d with: resolution=%d, quality=%d, fb_count=%d", 
                 attempt+1, config.frame_size, config.jpeg_quality, config.fb_count);
        
        // 初始化摄像头
        TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
        if (current_task) {
            // 添加看门狗保护，但使用安全的方式，使用当前任务句柄
            esp_task_wdt_add(current_task);
            esp_task_wdt_reset();
        }
        
        // 初始化摄像头
        ESP_LOGI(TAG, "Calling esp_camera_init");
        ret = esp_camera_init(&config);
        
        // 重设看门狗
        if (current_task) {
            esp_task_wdt_reset();
            esp_task_wdt_delete(current_task);
        }
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Camera initialization successful on attempt %d", attempt+1);
            break;
        } else {
            ESP_LOGE(TAG, "Camera init failed on attempt %d with error 0x%x", attempt+1, ret);
            // 短暂延迟让系统稳定
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "All camera initialization attempts failed");
        return false;
    }
    
    // 获取摄像头传感器并配置默认参数
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        // 默认设置：使用esp32-camera组件的通用设置
        ESP_LOGI(TAG, "Configuring camera sensor");
        s->set_framesize(s, (framesize_t)config.frame_size);
        s->set_quality(s, config.jpeg_quality);
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
    } else {
        ESP_LOGW(TAG, "Failed to get camera sensor");
        // 继续运行，因为摄像头初始化成功，即使无法获取传感器
    }
    
    ESP_LOGI(TAG, "Camera initialization complete");
    return true;
}

bool VisionController::InitLed() {
    ESP_LOGI(TAG, "Initializing LED on pin %d", LED_PIN);
    
    // 编译时验证LED_PIN - 根据目标芯片检查
    #if defined(CONFIG_IDF_TARGET_ESP32) && (LED_PIN < 0 || LED_PIN >= 40)
    #error "Invalid LED_PIN value for ESP32. Must be between 0 and 39."
    #elif defined(CONFIG_IDF_TARGET_ESP32S3) && (LED_PIN < 0 || LED_PIN >= 48)
    #error "Invalid LED_PIN value for ESP32-S3. Must be between 0 and 47."
    #elif defined(CONFIG_IDF_TARGET_ESP32C3) && (LED_PIN < 0 || LED_PIN >= 22)
    #error "Invalid LED_PIN value for ESP32-C3. Must be between 0 and 21."
    #elif LED_PIN < 0
    #error "Invalid LED_PIN value. Cannot be negative."
    #endif
    
    // 运行时验证 - 即使编译时已验证，保留此检查为安全措施
    if (LED_PIN < 0 || LED_PIN >= GPIO_NUM_MAX) {
        ESP_LOGE(TAG, "Invalid LED_PIN value: %d (GPIO_NUM_MAX=%d)", LED_PIN, GPIO_NUM_MAX);
        return false;
    }
    
    // 初始化LEDC用于LED PWM控制
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LED_LEDC_MODE,
        .duty_resolution = LED_LEDC_DUTY_RES,
        .timer_num = LED_LEDC_TIMER,
        .freq_hz = LED_LEDC_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    
    esp_err_t err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed with error 0x%x", err);
        return false;
    }
    
    ledc_channel_config_t ledc_channel = {
        .gpio_num = LED_PIN,
        .speed_mode = LED_LEDC_MODE,
        .channel = LED_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LED_LEDC_TIMER,
        .duty = 0, // 初始亮度为0
        .hpoint = 0
    };
    
    err = ledc_channel_config(&ledc_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed with error 0x%x", err);
        return false;
    }
    
    // 初始时LED关闭
    led_intensity_ = 0;
    ledc_set_duty(LED_LEDC_MODE, LED_LEDC_CHANNEL, 0);
    ledc_update_duty(LED_LEDC_MODE, LED_LEDC_CHANNEL);
    
    ESP_LOGI(TAG, "LED initialization complete");
    return true;
}

void VisionController::UpdateLed() {
    // 没有条件检查，已在InitLed中执行了编译时验证
    
    // 如果在直播模式且亮度超过上限，则限制亮度
    int intensity = led_intensity_;
    if (streaming_ && intensity > 128) {
        intensity = 128;  // 在流媒体模式下限制亮度以避免过热
    }
    
    // 使用LEDC API控制LED亮度
    esp_err_t err = ledc_set_duty(LED_LEDC_MODE, LED_LEDC_CHANNEL, intensity);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Setting LED duty failed: 0x%x", err);
        return;
    }
    
    err = ledc_update_duty(LED_LEDC_MODE, LED_LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Updating LED duty failed: 0x%x", err);
        return;
    }
} 