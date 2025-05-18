#include "iot/thing.h"
#include "board.h"
#include "../boards/common/board_config.h"

#include <esp_log.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_task_wdt.h>

#define TAG "CamThing"

// 定义摄像头接口默认引脚 - 将由板级配置提供实际引脚号
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     -1
#define SIOD_GPIO_NUM     -1
#define SIOC_GPIO_NUM     -1
#define Y2_GPIO_NUM       -1
#define Y3_GPIO_NUM       -1
#define Y4_GPIO_NUM       -1
#define Y5_GPIO_NUM       -1
#define Y6_GPIO_NUM       -1
#define Y7_GPIO_NUM       -1
#define Y8_GPIO_NUM       -1
#define Y9_GPIO_NUM       -1
#define VSYNC_GPIO_NUM    -1
#define HREF_GPIO_NUM     -1
#define PCLK_GPIO_NUM     -1

// LED闪光灯默认引脚 - 将由板级配置提供实际引脚号
#define LED_PIN          -1

// LEDC PWM配置 - 用于LED控制
#define LED_LEDC_TIMER     LEDC_TIMER_0
#define LED_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define LED_LEDC_CHANNEL   LEDC_CHANNEL_2
#define LED_LEDC_DUTY_RES  LEDC_TIMER_8_BIT  // 8位分辨率，0-255
#define LED_LEDC_FREQ      5000              // PWM频率5kHz

// 摄像头默认参数 - 替代Kconfig
#define DEFAULT_XCLK_FREQ_HZ  15000000
#define DEFAULT_I2C_PORT      1
#define DEFAULT_FRAME_SIZE    FRAMESIZE_VGA
#define DEFAULT_JPEG_QUALITY  12

namespace iot {

class Cam : public Thing {
private:
    bool running_;
    bool streaming_;
    int led_intensity_;
    SemaphoreHandle_t cam_mutex_;  // 用于保护摄像头操作的互斥量
    
    // 摄像头引脚配置
    int pwdn_pin_;
    int reset_pin_;
    int xclk_pin_;
    int siod_pin_;
    int sioc_pin_;
    int y2_pin_;
    int y3_pin_;
    int y4_pin_;
    int y5_pin_;
    int y6_pin_;
    int y7_pin_;
    int y8_pin_;
    int y9_pin_;
    int vsync_pin_;
    int href_pin_;
    int pclk_pin_;
    int led_pin_;
    
    // 摄像头配置参数 - 动态可调整
    int xclk_freq_hz_;
    int i2c_port_;
    framesize_t frame_size_;
    int jpeg_quality_;
    
    // 初始化摄像头引脚
    void InitCameraPins() {
        // 从board配置中获取摄像头引脚
        board_config_t* config = board_get_config();
        if (config) {
            // 如果板级配置中定义了摄像头引脚，则使用板级配置
            if (config->pwdn_pin >= -1) pwdn_pin_ = config->pwdn_pin;
            if (config->reset_pin >= -1) reset_pin_ = config->reset_pin;
            if (config->xclk_pin >= 0) xclk_pin_ = config->xclk_pin;
            if (config->siod_pin >= 0) siod_pin_ = config->siod_pin;
            if (config->sioc_pin >= 0) sioc_pin_ = config->sioc_pin;
            if (config->y2_pin >= 0) y2_pin_ = config->y2_pin;
            if (config->y3_pin >= 0) y3_pin_ = config->y3_pin;
            if (config->y4_pin >= 0) y4_pin_ = config->y4_pin;
            if (config->y5_pin >= 0) y5_pin_ = config->y5_pin;
            if (config->y6_pin >= 0) y6_pin_ = config->y6_pin;
            if (config->y7_pin >= 0) y7_pin_ = config->y7_pin;
            if (config->y8_pin >= 0) y8_pin_ = config->y8_pin;
            if (config->y9_pin >= 0) y9_pin_ = config->y9_pin;
            if (config->vsync_pin >= 0) vsync_pin_ = config->vsync_pin;
            if (config->href_pin >= 0) href_pin_ = config->href_pin;
            if (config->pclk_pin >= 0) pclk_pin_ = config->pclk_pin;
            if (config->cam_led_pin >= -1) led_pin_ = config->cam_led_pin;
        }
        
        ESP_LOGI(TAG, "Camera pins initialized: PWDN=%d, RESET=%d, XCLK=%d, SIOD=%d, SIOC=%d",
                 pwdn_pin_, reset_pin_, xclk_pin_, siod_pin_, sioc_pin_);
        ESP_LOGI(TAG, "Camera data pins: Y2=%d, Y3=%d, Y4=%d, Y5=%d, Y6=%d, Y7=%d, Y8=%d, Y9=%d",
                 y2_pin_, y3_pin_, y4_pin_, y5_pin_, y6_pin_, y7_pin_, y8_pin_, y9_pin_);
        ESP_LOGI(TAG, "Camera sync pins: VSYNC=%d, HREF=%d, PCLK=%d, LED=%d",
                 vsync_pin_, href_pin_, pclk_pin_, led_pin_);
    }
    
    // 初始化摄像头
    bool InitCamera() {
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
        if (xclk_pin_ < 0 || siod_pin_ < 0 || sioc_pin_ < 0) {
            ESP_LOGE(TAG, "Invalid essential camera pins: XCLK=%d, SIOD=%d, SIOC=%d", 
                     xclk_pin_, siod_pin_, sioc_pin_);
            pins_valid = false;
        }
        
        // 安全检查数据引脚和同步引脚（必须有效）
        if (y2_pin_ < 0 || y3_pin_ < 0 || y4_pin_ < 0 || y5_pin_ < 0 ||
            y6_pin_ < 0 || y7_pin_ < 0 || y8_pin_ < 0 || y9_pin_ < 0 ||
            vsync_pin_ < 0 || href_pin_ < 0 || pclk_pin_ < 0) {
            ESP_LOGE(TAG, "Invalid camera data pins");
            pins_valid = false;
        }
        
        if (!pins_valid) {
            ESP_LOGE(TAG, "Camera pin configuration invalid, cannot proceed");
            return false;
        }
        
        // 配置camera引脚 - 使用从板级配置中获取的引脚
        config.pin_pwdn = pwdn_pin_;
        config.pin_reset = reset_pin_;
        config.pin_xclk = xclk_pin_;  
        config.pin_sccb_sda = siod_pin_;
        config.pin_sccb_scl = sioc_pin_;
        config.pin_d0 = y2_pin_;
        config.pin_d1 = y3_pin_;
        config.pin_d2 = y4_pin_;
        config.pin_d3 = y5_pin_;
        config.pin_d4 = y6_pin_;
        config.pin_d5 = y7_pin_;
        config.pin_d6 = y8_pin_;
        config.pin_d7 = y9_pin_;
        config.pin_vsync = vsync_pin_;
        config.pin_href = href_pin_;
        config.pin_pclk = pclk_pin_;
        
        // 摄像头基本配置
        config.ledc_channel = LEDC_CHANNEL_0;
        config.ledc_timer = LEDC_TIMER_0;
        config.xclk_freq_hz = xclk_freq_hz_;  // 使用类成员变量
        config.pixel_format = PIXFORMAT_JPEG;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        
        // 使用类成员变量中的I2C配置
        config.sccb_i2c_port = i2c_port_;
        
        // 使用与car_idf项目相同的分阶段初始化策略
        esp_err_t ret = ESP_FAIL;
        
        // 使用类成员变量定义的帧大小
        framesize_t framesizes[] = {frame_size_, FRAMESIZE_QVGA, FRAMESIZE_QQVGA};
        int qualities[] = {jpeg_quality_, jpeg_quality_ + 2, jpeg_quality_ + 5};
        int fb_counts[] = {2, 1, 1};
        
        for (int attempt = 0; attempt < 3 && ret != ESP_OK; attempt++) {
            // 根据可用PSRAM和尝试次数调整配置
            if (psram_size > 1024 * 1024 && attempt == 0) {
                // 大内存配置
                config.frame_size = framesizes[0];
                config.jpeg_quality = qualities[0];
                config.fb_count = fb_counts[0];
                ESP_LOGI(TAG, "Attempt %d: High memory config: %d, quality %d, %d buffers", 
                         attempt+1, config.frame_size, qualities[0], fb_counts[0]);
            } else if (psram_size > 400 * 1024 && attempt <= 1) {
                // 中等内存配置
                config.frame_size = framesizes[1];
                config.jpeg_quality = qualities[1];
                config.fb_count = fb_counts[1];
                ESP_LOGI(TAG, "Attempt %d: Medium memory config: %d, quality %d, %d buffers", 
                         attempt+1, config.frame_size, qualities[1], fb_counts[1]);
            } else {
                // 低内存配置
                config.frame_size = framesizes[2];
                config.jpeg_quality = qualities[2];
                config.fb_count = fb_counts[2];
                ESP_LOGI(TAG, "Attempt %d: Low memory config: %d, quality %d, %d buffers", 
                         attempt+1, config.frame_size, qualities[2], fb_counts[2]);
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
    
    // 初始化LED闪光灯
    bool InitLed() {
        ESP_LOGI(TAG, "Initializing LED on pin %d", led_pin_);
        
        // 运行时验证LED引脚
        if (led_pin_ < 0 || led_pin_ >= GPIO_NUM_MAX) {
            ESP_LOGE(TAG, "Invalid LED pin value: %d (GPIO_NUM_MAX=%d)", led_pin_, GPIO_NUM_MAX);
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
            .gpio_num = led_pin_,
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
    
    // 更新LED状态
    void UpdateLed() {
        // 如果LED引脚无效则直接返回
        if (led_pin_ < 0 || led_pin_ >= GPIO_NUM_MAX) {
            return;
        }
        
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

public:
    Cam() : Thing("Camera", "摄像头控制"),
            running_(false), streaming_(false), led_intensity_(0),
            pwdn_pin_(PWDN_GPIO_NUM),
            reset_pin_(RESET_GPIO_NUM),
            xclk_pin_(XCLK_GPIO_NUM),
            siod_pin_(SIOD_GPIO_NUM),
            sioc_pin_(SIOC_GPIO_NUM),
            y2_pin_(Y2_GPIO_NUM),
            y3_pin_(Y3_GPIO_NUM),
            y4_pin_(Y4_GPIO_NUM),
            y5_pin_(Y5_GPIO_NUM),
            y6_pin_(Y6_GPIO_NUM),
            y7_pin_(Y7_GPIO_NUM),
            y8_pin_(Y8_GPIO_NUM),
            y9_pin_(Y9_GPIO_NUM),
            vsync_pin_(VSYNC_GPIO_NUM),
            href_pin_(HREF_GPIO_NUM),
            pclk_pin_(PCLK_GPIO_NUM),
            led_pin_(LED_PIN),
            xclk_freq_hz_(DEFAULT_XCLK_FREQ_HZ),
            i2c_port_(DEFAULT_I2C_PORT),
            frame_size_(DEFAULT_FRAME_SIZE),
            jpeg_quality_(DEFAULT_JPEG_QUALITY) {
        
        // 创建互斥量
        cam_mutex_ = xSemaphoreCreateMutex();
        if (cam_mutex_ == NULL) {
            ESP_LOGE(TAG, "Failed to create camera mutex");
        }
        
        // 初始化摄像头引脚
        InitCameraPins();
        
        // 初始化摄像头
        ESP_LOGI(TAG, "Starting camera with integrated functionality");
        if (!InitCamera()) {
            ESP_LOGE(TAG, "Failed to initialize camera subsystem");
            return;
        }
        
        // 初始化LED闪光灯
        if (!InitLed()) {
            ESP_LOGW(TAG, "Failed to initialize LED subsystem - continuing without LED functionality");
        }
        
        // 摄像头成功初始化
        running_ = true;
        
        // 定义设备的属性
        properties_.AddBooleanProperty("running", "摄像头是否运行中", [this]() -> bool {
            return running_;
        });
        
        properties_.AddBooleanProperty("streaming", "摄像头是否在直播", [this]() -> bool {
            return streaming_;
        });
        
        properties_.AddNumberProperty("ledIntensity", "LED闪光灯亮度 (0-255)", [this]() -> int {
            return led_intensity_;
        });
        
        // 添加摄像头配置参数属性
        properties_.AddNumberProperty("xclkFrequency", "XCLK频率(Hz)", [this]() -> int {
            return xclk_freq_hz_;
        });
        
        properties_.AddNumberProperty("i2cPort", "I2C端口号", [this]() -> int {
            return i2c_port_;
        });
        
        properties_.AddNumberProperty("frameSize", "帧大小", [this]() -> int {
            return static_cast<int>(frame_size_);
        });
        
        properties_.AddNumberProperty("jpegQuality", "JPEG品质(5-63)", [this]() -> int {
            return jpeg_quality_;
        });
        
        // 定义设备可以被远程执行的指令
        methods_.AddMethod("StartStreaming", "开始直播", ParameterList(), [this](const ParameterList& parameters) {
            if (!running_) {
                ESP_LOGW(TAG, "Camera not running");
                return;
            }
            streaming_ = true;
            ESP_LOGI(TAG, "Camera streaming started");
            UpdateLed(); // 更新LED状态
        });
        
        methods_.AddMethod("StopStreaming", "停止直播", ParameterList(), [this](const ParameterList& parameters) {
            streaming_ = false;
            ESP_LOGI(TAG, "Camera streaming stopped");
            UpdateLed(); // 更新LED状态
        });
        
        ParameterList ledParams;
        ledParams.AddParameter(Parameter("intensity", "亮度 (0-255)", kValueTypeNumber));
        
        methods_.AddMethod("SetLedIntensity", "设置LED闪光灯亮度", ledParams, [this](const ParameterList& parameters) {
            int intensity = parameters["intensity"].number();
            if (intensity < 0) intensity = 0;
            if (intensity > 255) intensity = 255;
            
            led_intensity_ = intensity;
            UpdateLed();
        });
        
        // 添加摄像头参数配置方法
        ParameterList xclkParams;
        xclkParams.AddParameter(Parameter("frequency", "频率(Hz)", kValueTypeNumber));
        
        methods_.AddMethod("SetXclkFrequency", "设置XCLK频率", xclkParams, [this](const ParameterList& parameters) {
            int frequency = parameters["frequency"].number();
            if (frequency < 10000000) frequency = 10000000;  // 最小10MHz
            if (frequency > 20000000) frequency = 20000000;  // 最大20MHz
            
            xclk_freq_hz_ = frequency;
            ESP_LOGI(TAG, "XCLK frequency set to %d Hz, restart required to take effect", xclk_freq_hz_);
        });
        
        ParameterList i2cParams;
        i2cParams.AddParameter(Parameter("port", "I2C端口号(0/1)", kValueTypeNumber));
        
        methods_.AddMethod("SetI2CPort", "设置I2C端口号", i2cParams, [this](const ParameterList& parameters) {
            int port = parameters["port"].number();
            if (port != 0 && port != 1) {
                ESP_LOGW(TAG, "Invalid I2C port %d, must be 0 or 1", port);
                return;
            }
            
            i2c_port_ = port;
            ESP_LOGI(TAG, "I2C port set to %d, restart required to take effect", i2c_port_);
        });
        
        ParameterList frameSizeParams;
        frameSizeParams.AddParameter(Parameter("size", "帧大小(0-9)", kValueTypeNumber));
        
        methods_.AddMethod("SetFrameSize", "设置帧大小", frameSizeParams, [this](const ParameterList& parameters) {
            int size = parameters["size"].number();
            if (size < 0) size = 0;  // QQVGA
            if (size > 9) size = 9;  // UXGA
            
            framesize_t frameSize = static_cast<framesize_t>(size);
            
            // 如果摄像头正在运行，直接应用新设置
            if (running_) {
                sensor_t* s = esp_camera_sensor_get();
                if (s) {
                    s->set_framesize(s, frameSize);
                    ESP_LOGI(TAG, "Camera frame size updated to %d", size);
                } else {
                    ESP_LOGW(TAG, "Failed to get camera sensor");
                }
            }
            
            frame_size_ = frameSize;
        });
        
        ParameterList qualityParams;
        qualityParams.AddParameter(Parameter("quality", "JPEG品质(5-63)", kValueTypeNumber));
        
        methods_.AddMethod("SetJpegQuality", "设置JPEG品质", qualityParams, [this](const ParameterList& parameters) {
            int quality = parameters["quality"].number();
            if (quality < 5) quality = 5;     // 最高品质
            if (quality > 63) quality = 63;   // 最低品质
            
            // 如果摄像头正在运行，直接应用新设置
            if (running_) {
                sensor_t* s = esp_camera_sensor_get();
                if (s) {
                    s->set_quality(s, quality);
                    ESP_LOGI(TAG, "Camera JPEG quality updated to %d", quality);
                } else {
                    ESP_LOGW(TAG, "Failed to get camera sensor");
                }
            }
            
            jpeg_quality_ = quality;
        });
        
        // 重新初始化摄像头方法
        methods_.AddMethod("RestartCamera", "重新初始化摄像头", ParameterList(), [this](const ParameterList& parameters) {
            ESP_LOGI(TAG, "Restarting camera with new parameters");
            
            // 关闭摄像头
            if (running_) {
                running_ = false;
                streaming_ = false;
                esp_camera_deinit();
                
                // 短暂延迟确保资源释放
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            
            // 重新初始化摄像头
            if (InitCamera()) {
                running_ = true;
                ESP_LOGI(TAG, "Camera restarted successfully");
            } else {
                ESP_LOGE(TAG, "Failed to restart camera");
            }
        });
        
        methods_.AddMethod("TakePhoto", "拍照", ParameterList(), [this](const ParameterList& parameters) {
            if (!running_) {
                ESP_LOGW(TAG, "Camera not running");
                return;
            }
            
            // 短暂开启闪光灯
            int savedIntensity = led_intensity_;
            led_intensity_ = 255;
            UpdateLed();
            
            // 延迟一小段时间让闪光灯稳定
            vTaskDelay(pdMS_TO_TICKS(100));
            
            // 拍照
            ESP_LOGI(TAG, "Taking photo");
            
            if (xSemaphoreTake(cam_mutex_, pdMS_TO_TICKS(5000)) == pdTRUE) {
                camera_fb_t* fb = esp_camera_fb_get();
                if (fb) {
                    // 在实际应用中，这里可以存储照片或通过其他方式发送
                    // 目前我们只是简单地获取并释放帧
                    ESP_LOGI(TAG, "Photo taken: %dx%d (%d bytes)", 
                             fb->width, fb->height, fb->len);
                    esp_camera_fb_return(fb);
                } else {
                    ESP_LOGE(TAG, "Failed to take photo");
                }
                xSemaphoreGive(cam_mutex_);
            } else {
                ESP_LOGE(TAG, "Failed to acquire camera mutex");
            }
            
            // 恢复LED亮度
            led_intensity_ = savedIntensity;
            UpdateLed();
        });
    }
    
    ~Cam() {
        // 首先关闭LED
        if (led_pin_ >= 0 && led_pin_ < GPIO_NUM_MAX) {
            // 使用LEDC API关闭LED
            ledc_set_duty(LED_LEDC_MODE, LED_LEDC_CHANNEL, 0);
            ledc_update_duty(LED_LEDC_MODE, LED_LEDC_CHANNEL);
        }

        // 停止摄像头
        if (running_) {
            esp_camera_deinit();
        }
        
        // 删除互斥量
        if (cam_mutex_ != NULL) {
            vSemaphoreDelete(cam_mutex_);
            cam_mutex_ = NULL;
        }
    }
    
    // 获取摄像头帧
    camera_fb_t* GetFrame() {
        if (!running_) {
            ESP_LOGW(TAG, "Camera not running");
            return nullptr;
        }
        
        return esp_camera_fb_get();
    }
    
    // 释放摄像头帧
    void ReturnFrame(camera_fb_t* fb) {
        if (fb) {
            esp_camera_fb_return(fb);
        }
    }
    
    // 获取配置参数方法
    int GetXclkFrequency() const { return xclk_freq_hz_; }
    int GetI2CPort() const { return i2c_port_; }
    framesize_t GetFrameSize() const { return frame_size_; }
    int GetJpegQuality() const { return jpeg_quality_; }
};

} // namespace iot

DECLARE_THING(Cam); 