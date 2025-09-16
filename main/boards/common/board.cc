#include "board.h"
#include "system_info.h"
#include "settings.h"
#include "display/display.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_chip_info.h>
#include <esp_random.h>
#include <driver/i2c_master.h>
#include <cstdlib>

#define TAG "Board"

// 静态变量初始化
board_config_t Board::board_config_ = {0};
bool Board::config_initialized_ = false;

namespace iot {

board_config_t* board_get_config() {
    return Board::GetBoardConfig();
}

} // namespace iot

// 全局C函数，用于获取I2C总线句柄
extern "C" i2c_master_bus_handle_t board_get_i2c_bus_handle(void) {
    return Board::GetInstance().GetDisplayI2CBusHandle();
}

// 全局C函数，用于获取板级配置
extern "C" board_config_t* board_get_config(void) {
    return Board::GetBoardConfig();
}

Board::Board() {
    Settings settings("board", true);
    uuid_ = settings.GetString("uuid");
    if (uuid_.empty()) {
        uuid_ = GenerateUuid();
        settings.SetString("uuid", uuid_);
    }
    ESP_LOGI(TAG, "UUID=%s SKU=%s", uuid_.c_str(), BOARD_NAME);
    
    // 确保板级配置已初始化
    InitBoardConfig();
}

void Board::InitBoardConfig() {
    if (config_initialized_) {
        return;
    }
    
    ESP_LOGI(TAG, "Initializing board configuration");
    
    static int servo_pins_array[8] = {-1, -1, -1, -1, -1, -1, -1, -1}; // 最多支持8个舵机
    
    // 初始化舵机引脚数组
    board_config_.servo_pins = servo_pins_array;
    board_config_.servo_count = 0;  // 默认没有舵机
    
    // 尝试根据板子的配置头文件和Kconfig初始化引脚
    // 电机引脚配置 - 从Kconfig中读取
#ifdef CONFIG_ENABLE_MOTOR_CONTROLLER
    // 使用定义的电机引脚常量，确保从Kconfig中正确读取
    #ifdef CONFIG_MOTOR_ENA_PIN
    board_config_.ena_pin = CONFIG_MOTOR_ENA_PIN;
    ESP_LOGI(TAG, "Motor ENA pin from Kconfig: %d", CONFIG_MOTOR_ENA_PIN);
    #else
    board_config_.ena_pin = MOTOR_ENA_PIN; // 使用Kconfig定义的默认值
    ESP_LOGI(TAG, "Motor ENA pin using default from board.h: %d", MOTOR_ENA_PIN);
    #endif
    
    #ifdef CONFIG_MOTOR_ENB_PIN
    board_config_.enb_pin = CONFIG_MOTOR_ENB_PIN;
    ESP_LOGI(TAG, "Motor ENB pin from Kconfig: %d", CONFIG_MOTOR_ENB_PIN);
    #else
    board_config_.enb_pin = MOTOR_ENB_PIN; // 使用Kconfig定义的默认值
    ESP_LOGI(TAG, "Motor ENB pin using default from board.h: %d", MOTOR_ENB_PIN);
    #endif
    
    // 根据电机连接方式选择IN1-IN4引脚
    #ifdef CONFIG_MOTOR_CONNECTION_DIRECT
        // 直接连接方式：从CONFIG_MOTOR_INx_PIN获取引脚
        #ifdef CONFIG_MOTOR_IN1_PIN
        board_config_.in1_pin = CONFIG_MOTOR_IN1_PIN;
        ESP_LOGI(TAG, "Motor IN1 pin from Kconfig (direct): %d", CONFIG_MOTOR_IN1_PIN);
        #else
        board_config_.in1_pin = MOTOR_IN1_PIN; // 使用board.h默认值
        ESP_LOGI(TAG, "Motor IN1 pin using default from board.h: %d", MOTOR_IN1_PIN);
        #endif
        
        #ifdef CONFIG_MOTOR_IN2_PIN
        board_config_.in2_pin = CONFIG_MOTOR_IN2_PIN;
        ESP_LOGI(TAG, "Motor IN2 pin from Kconfig (direct): %d", CONFIG_MOTOR_IN2_PIN);
        #else
        board_config_.in2_pin = MOTOR_IN2_PIN; // 使用board.h默认值
        ESP_LOGI(TAG, "Motor IN2 pin using default from board.h: %d", MOTOR_IN2_PIN);
        #endif
        
        #ifdef CONFIG_MOTOR_IN3_PIN
        board_config_.in3_pin = CONFIG_MOTOR_IN3_PIN;
        ESP_LOGI(TAG, "Motor IN3 pin from Kconfig (direct): %d", CONFIG_MOTOR_IN3_PIN);
        #else
        board_config_.in3_pin = MOTOR_IN3_PIN; // 使用board.h默认值
        ESP_LOGI(TAG, "Motor IN3 pin using default from board.h: %d", MOTOR_IN3_PIN);
        #endif
        
        #ifdef CONFIG_MOTOR_IN4_PIN
        board_config_.in4_pin = CONFIG_MOTOR_IN4_PIN;
        ESP_LOGI(TAG, "Motor IN4 pin from Kconfig (direct): %d", CONFIG_MOTOR_IN4_PIN);
        #else
        board_config_.in4_pin = MOTOR_IN4_PIN; // 使用board.h默认值
        ESP_LOGI(TAG, "Motor IN4 pin using default from board.h: %d", MOTOR_IN4_PIN);
        #endif
    #elif defined(CONFIG_MOTOR_CONNECTION_PCF8575)
        // PCF8575连接方式：电机控制引脚通过PCF8575扩展器连接
        // 这种情况下，实际引脚编号由PCF8575控制器确定，我们在这里使用-1表示通过扩展器控制
        board_config_.in1_pin = -1;
        board_config_.in2_pin = -1;
        board_config_.in3_pin = -1;
        board_config_.in4_pin = -1;
        ESP_LOGI(TAG, "Motor control pins using PCF8575 expander");
        #ifdef CONFIG_MOTOR_PCF8575_IN1_PIN
        ESP_LOGI(TAG, "PCF8575 Motor IN1 pin: %d", CONFIG_MOTOR_PCF8575_IN1_PIN);
        #endif
        #ifdef CONFIG_MOTOR_PCF8575_IN2_PIN
        ESP_LOGI(TAG, "PCF8575 Motor IN2 pin: %d", CONFIG_MOTOR_PCF8575_IN2_PIN);
        #endif
        #ifdef CONFIG_MOTOR_PCF8575_IN3_PIN
        ESP_LOGI(TAG, "PCF8575 Motor IN3 pin: %d", CONFIG_MOTOR_PCF8575_IN3_PIN);
        #endif
        #ifdef CONFIG_MOTOR_PCF8575_IN4_PIN
        ESP_LOGI(TAG, "PCF8575 Motor IN4 pin: %d", CONFIG_MOTOR_PCF8575_IN4_PIN);
        #endif
    #else
        // 默认情况
        board_config_.in1_pin = MOTOR_IN1_PIN;
        board_config_.in2_pin = MOTOR_IN2_PIN;
        board_config_.in3_pin = MOTOR_IN3_PIN;
        board_config_.in4_pin = MOTOR_IN4_PIN;
        ESP_LOGI(TAG, "Motor pins using defaults: IN1=%d, IN2=%d, IN3=%d, IN4=%d", 
                 board_config_.in1_pin, board_config_.in2_pin, board_config_.in3_pin, board_config_.in4_pin);
    #endif
    
    // 检查是否有引脚冲突
    if (board_config_.ena_pin == board_config_.in1_pin || 
        board_config_.ena_pin == board_config_.in2_pin || 
        board_config_.ena_pin == board_config_.in3_pin || 
        board_config_.ena_pin == board_config_.in4_pin ||
        board_config_.enb_pin == board_config_.in1_pin || 
        board_config_.enb_pin == board_config_.in2_pin || 
        board_config_.enb_pin == board_config_.in3_pin || 
        board_config_.enb_pin == board_config_.in4_pin) {
        ESP_LOGW(TAG, "Warning: Motor pin conflict detected! Check your configuration");
    }
#else
    // 如果未启用电机控制器，则设置为无效值
    board_config_.ena_pin = -1;
    board_config_.enb_pin = -1;
    board_config_.in1_pin = -1;
    board_config_.in2_pin = -1;
    board_config_.in3_pin = -1;
    board_config_.in4_pin = -1;
    ESP_LOGI(TAG, "Motor controller disabled");
#endif
    
    // 舵机引脚配置 - 从Kconfig中读取
#ifdef CONFIG_ENABLE_SERVO_CONTROLLER
    #ifdef CONFIG_SERVO_CONNECTION_DIRECT
        // 直接连接方式：使用GPIO引脚
        board_config_.servo_count = SERVO_COUNT > 8 ? 8 : SERVO_COUNT; // 限制最多8个舵机
        
        ESP_LOGI(TAG, "Setting up %d servos from Kconfig (direct GPIO connection)", board_config_.servo_count);
        
        // 默认初始化所有引脚为-1（无效值）
        for (int i = 0; i < 8; i++) {
            servo_pins_array[i] = -1;
        }
        
        // 直接使用Kconfig定义的舵机引脚
        #ifdef CONFIG_SERVO_PIN_1
        servo_pins_array[0] = CONFIG_SERVO_PIN_1;
        ESP_LOGI(TAG, "Servo 1 pin from Kconfig: %d", CONFIG_SERVO_PIN_1);
        #elif defined(SERVO_PIN_1)
        servo_pins_array[0] = SERVO_PIN_1;
        ESP_LOGI(TAG, "Servo 1 pin using default from board.h: %d", SERVO_PIN_1);
        #else
        ESP_LOGI(TAG, "Servo 1 pin not defined");
        #endif
        
        #ifdef CONFIG_SERVO_PIN_2
        if (board_config_.servo_count >= 2) {
            servo_pins_array[1] = CONFIG_SERVO_PIN_2;
            ESP_LOGI(TAG, "Servo 2 pin from Kconfig: %d", CONFIG_SERVO_PIN_2);
        }
        #elif defined(SERVO_PIN_2)
        if (board_config_.servo_count >= 2) {
            servo_pins_array[1] = SERVO_PIN_2;
            ESP_LOGI(TAG, "Servo 2 pin using default from board.h: %d", SERVO_PIN_2);
        }
        #else
        ESP_LOGI(TAG, "Servo 2 pin not defined");
        #endif
        
        #ifdef CONFIG_SERVO_PIN_3
        if (board_config_.servo_count >= 3) {
            servo_pins_array[2] = CONFIG_SERVO_PIN_3;
            ESP_LOGI(TAG, "Servo 3 pin from Kconfig: %d", CONFIG_SERVO_PIN_3);
        }
        #elif defined(SERVO_PIN_3)
        if (board_config_.servo_count >= 3) {
            servo_pins_array[2] = SERVO_PIN_3;
            ESP_LOGI(TAG, "Servo 3 pin using default from board.h: %d", SERVO_PIN_3);
        }
        #else
        ESP_LOGI(TAG, "Servo 3 pin not defined");
        #endif
        
        #ifdef CONFIG_SERVO_PIN_4
        if (board_config_.servo_count >= 4) {
            servo_pins_array[3] = CONFIG_SERVO_PIN_4;
            ESP_LOGI(TAG, "Servo 4 pin from Kconfig: %d", CONFIG_SERVO_PIN_4);
        }
        #elif defined(SERVO_PIN_4)
        if (board_config_.servo_count >= 4) {
            servo_pins_array[3] = SERVO_PIN_4;
            ESP_LOGI(TAG, "Servo 4 pin using default from board.h: %d", SERVO_PIN_4);
        }
        #else
        ESP_LOGI(TAG, "Servo 4 pin not defined");
        #endif
        
        #ifdef CONFIG_SERVO_PIN_5
        if (board_config_.servo_count >= 5) {
            servo_pins_array[4] = CONFIG_SERVO_PIN_5;
            ESP_LOGI(TAG, "Servo 5 pin from Kconfig: %d", CONFIG_SERVO_PIN_5);
        }
        #elif defined(SERVO_PIN_5)
        if (board_config_.servo_count >= 5) {
            servo_pins_array[4] = SERVO_PIN_5;
            ESP_LOGI(TAG, "Servo 5 pin using default from board.h: %d", SERVO_PIN_5);
        }
        #else
        ESP_LOGI(TAG, "Servo 5 pin not defined");
        #endif
        
        #ifdef CONFIG_SERVO_PIN_6
        if (board_config_.servo_count >= 6) {
            servo_pins_array[5] = CONFIG_SERVO_PIN_6;
            ESP_LOGI(TAG, "Servo 6 pin from Kconfig: %d", CONFIG_SERVO_PIN_6);
        }
        #elif defined(SERVO_PIN_6)
        if (board_config_.servo_count >= 6) {
            servo_pins_array[5] = SERVO_PIN_6;
            ESP_LOGI(TAG, "Servo 6 pin using default from board.h: %d", SERVO_PIN_6);
        }
        #else
        ESP_LOGI(TAG, "Servo 6 pin not defined");
        #endif
        
        #ifdef CONFIG_SERVO_PIN_7
        if (board_config_.servo_count >= 7) {
            servo_pins_array[6] = CONFIG_SERVO_PIN_7;
            ESP_LOGI(TAG, "Servo 7 pin from Kconfig: %d", CONFIG_SERVO_PIN_7);
        }
        #elif defined(SERVO_PIN_7)
        if (board_config_.servo_count >= 7) {
            servo_pins_array[6] = SERVO_PIN_7;
            ESP_LOGI(TAG, "Servo 7 pin using default from board.h: %d", SERVO_PIN_7);
        }
        #else
        ESP_LOGI(TAG, "Servo 7 pin not defined");
        #endif
        
        #ifdef CONFIG_SERVO_PIN_8
        if (board_config_.servo_count >= 8) {
            servo_pins_array[7] = CONFIG_SERVO_PIN_8;
            ESP_LOGI(TAG, "Servo 8 pin from Kconfig: %d", CONFIG_SERVO_PIN_8);
        }
        #elif defined(SERVO_PIN_8)
        if (board_config_.servo_count >= 8) {
            servo_pins_array[7] = SERVO_PIN_8;
            ESP_LOGI(TAG, "Servo 8 pin using default from board.h: %d", SERVO_PIN_8);
        }
        #else
        ESP_LOGI(TAG, "Servo 8 pin not defined");
        #endif
        
        ESP_LOGI(TAG, "Servo count: %d", board_config_.servo_count);
        for (int i = 0; i < board_config_.servo_count; i++) {
            ESP_LOGI(TAG, "Servo %d pin: %d", i+1, servo_pins_array[i]);
        }
    #elif defined(CONFIG_SERVO_CONNECTION_LU9685)
        // LU9685连接方式：使用LU9685-20CU舵机控制器
        // 这种情况下，舵机通过I2C控制器连接，不使用GPIO引脚
        // 设置舵机数量为4（标准配置为左、右、上、下四个舵机）
        board_config_.servo_count = 4;
        ESP_LOGI(TAG, "Using LU9685 servo controller with %d servos", board_config_.servo_count);
        
        // 标记所有GPIO引脚为无效，因为使用LU9685
        for (int i = 0; i < 8; i++) {
            servo_pins_array[i] = -1;
        }
        
        // 输出LU9685通道配置
        #ifdef CONFIG_SERVO_LU9685_LEFT_CHANNEL
        ESP_LOGI(TAG, "LU9685 Left servo channel: %d", CONFIG_SERVO_LU9685_LEFT_CHANNEL);
        #endif
        
        #ifdef CONFIG_SERVO_LU9685_RIGHT_CHANNEL
        ESP_LOGI(TAG, "LU9685 Right servo channel: %d", CONFIG_SERVO_LU9685_RIGHT_CHANNEL);
        #endif
        
        #ifdef CONFIG_SERVO_LU9685_UP_CHANNEL
        ESP_LOGI(TAG, "LU9685 Up servo channel: %d", CONFIG_SERVO_LU9685_UP_CHANNEL);
        #endif
        
        #ifdef CONFIG_SERVO_LU9685_DOWN_CHANNEL
        ESP_LOGI(TAG, "LU9685 Down servo channel: %d", CONFIG_SERVO_LU9685_DOWN_CHANNEL);
        #endif
    #else
        // 未知连接方式，设置为0舵机
        board_config_.servo_count = 0;
        ESP_LOGW(TAG, "Unknown servo connection type, disabling servos");
    #endif
#else
    board_config_.servo_count = 0;
    ESP_LOGI(TAG, "Servo controller disabled");
#endif
    
    // 摄像头引脚配置
    #if defined(CAM_PWDN_PIN)
    board_config_.pwdn_pin = CAM_PWDN_PIN;
    #else
    board_config_.pwdn_pin = -1;
    #endif
    
    #if defined(CAM_RESET_PIN)
    board_config_.reset_pin = CAM_RESET_PIN;
    #else
    board_config_.reset_pin = -1;
    #endif
    
    #if defined(CAM_XCLK_PIN)
    board_config_.xclk_pin = CAM_XCLK_PIN;
    #else
    board_config_.xclk_pin = -1;
    #endif
    
    #if defined(CAM_SIOD_PIN)
    board_config_.siod_pin = CAM_SIOD_PIN;
    #else
    board_config_.siod_pin = -1;
    #endif
    
    #if defined(CAM_SIOC_PIN)
    board_config_.sioc_pin = CAM_SIOC_PIN;
    #else
    board_config_.sioc_pin = -1;
    #endif
    
    #if defined(CAM_Y2_PIN)
    board_config_.y2_pin = CAM_Y2_PIN;
    #else
    board_config_.y2_pin = -1;
    #endif
    
    #if defined(CAM_Y3_PIN)
    board_config_.y3_pin = CAM_Y3_PIN;
    #else
    board_config_.y3_pin = -1;
    #endif
    
    #if defined(CAM_Y4_PIN)
    board_config_.y4_pin = CAM_Y4_PIN;
    #else
    board_config_.y4_pin = -1;
    #endif
    
    #if defined(CAM_Y5_PIN)
    board_config_.y5_pin = CAM_Y5_PIN;
    #else
    board_config_.y5_pin = -1;
    #endif
    
    #if defined(CAM_Y6_PIN)
    board_config_.y6_pin = CAM_Y6_PIN;
    #else
    board_config_.y6_pin = -1;
    #endif
    
    #if defined(CAM_Y7_PIN)
    board_config_.y7_pin = CAM_Y7_PIN;
    #else
    board_config_.y7_pin = -1;
    #endif
    
    #if defined(CAM_Y8_PIN)
    board_config_.y8_pin = CAM_Y8_PIN;
    #else
    board_config_.y8_pin = -1;
    #endif
    
    #if defined(CAM_Y9_PIN)
    board_config_.y9_pin = CAM_Y9_PIN;
    #else
    board_config_.y9_pin = -1;
    #endif
    
    #if defined(CAM_VSYNC_PIN)
    board_config_.vsync_pin = CAM_VSYNC_PIN;
    #else
    board_config_.vsync_pin = -1;
    #endif
    
    #if defined(CAM_HREF_PIN)
    board_config_.href_pin = CAM_HREF_PIN;
    #else
    board_config_.href_pin = -1;
    #endif
    
    #if defined(CAM_PCLK_PIN)
    board_config_.pclk_pin = CAM_PCLK_PIN;
    #else
    board_config_.pclk_pin = -1;
    #endif
    
    #if defined(CAM_LED_PIN)
    board_config_.cam_led_pin = CAM_LED_PIN;
    #else
    board_config_.cam_led_pin = -1;
    #endif
    
    // 摄像头支持标志
    board_config_.camera_supported = (board_config_.xclk_pin > 0);
    board_config_.has_camera = false;  // 默认未初始化
    
    // 配置已完成
    config_initialized_ = true;
    
    ESP_LOGI(TAG, "--------- Board configuration summary ---------");
    ESP_LOGI(TAG, "Motor pins (from Kconfig):");
    ESP_LOGI(TAG, "  ENA: %d, ENB: %d", board_config_.ena_pin, board_config_.enb_pin);
    #ifdef CONFIG_MOTOR_CONNECTION_PCF8575
    // PCF8575连接方式显示扩展器引脚
    ESP_LOGI(TAG, "  IN1: -1, IN2: -1, IN3: -1, IN4: -1 (Using PCF8575 expander)");
    ESP_LOGI(TAG, "  PCF8575 pins: IN1: %d, IN2: %d, IN3: %d, IN4: %d", 
             CONFIG_MOTOR_PCF8575_IN1_PIN, CONFIG_MOTOR_PCF8575_IN2_PIN,
             CONFIG_MOTOR_PCF8575_IN3_PIN, CONFIG_MOTOR_PCF8575_IN4_PIN);
    #else
    ESP_LOGI(TAG, "  IN1: %d, IN2: %d, IN3: %d, IN4: %d", 
             board_config_.in1_pin, board_config_.in2_pin, board_config_.in3_pin, board_config_.in4_pin);
    #endif
    
    ESP_LOGI(TAG, "Servo pins (from Kconfig):");
    #ifdef CONFIG_SERVO_CONNECTION_LU9685
    // LU9685连接方式显示通道信息
    ESP_LOGI(TAG, "  Using LU9685 servo controller on PCA9548A channel %d", CONFIG_LU9685_PCA9548A_CHANNEL);
    ESP_LOGI(TAG, "  LU9685 channels: Left: %d, Right: %d, Up: %d, Down: %d",
             CONFIG_SERVO_LU9685_LEFT_CHANNEL, CONFIG_SERVO_LU9685_RIGHT_CHANNEL,
             CONFIG_SERVO_LU9685_UP_CHANNEL, CONFIG_SERVO_LU9685_DOWN_CHANNEL);
    #endif
    for (int i = 0; i < board_config_.servo_count; i++) {
        ESP_LOGI(TAG, "  Servo %d: %d", i+1, servo_pins_array[i]);
    }
    
    ESP_LOGI(TAG, "Camera pins (from board-specific defines):");
    ESP_LOGI(TAG, "  XCLK: %d, SIOD: %d, SIOC: %d", 
             board_config_.xclk_pin, board_config_.siod_pin, board_config_.sioc_pin);
    ESP_LOGI(TAG, "  VSYNC: %d, HREF: %d, PCLK: %d, LED: %d",
             board_config_.vsync_pin, board_config_.href_pin, board_config_.pclk_pin, board_config_.cam_led_pin);
    ESP_LOGI(TAG, "--------------------------------------------");
}

std::string Board::GenerateUuid() {
    // UUID v4 需要 16 字节的随机数据
    uint8_t uuid[16];
    
    // 使用 ESP32 的硬件随机数生成器
    esp_fill_random(uuid, sizeof(uuid));
    
    // 设置版本 (版本 4) 和变体位
    uuid[6] = (uuid[6] & 0x0F) | 0x40;    // 版本 4
    uuid[8] = (uuid[8] & 0x3F) | 0x80;    // 变体 1
    
    // 将字节转换为标准的 UUID 字符串格式
    char uuid_str[37];
    snprintf(uuid_str, sizeof(uuid_str),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2], uuid[3],
        uuid[4], uuid[5], uuid[6], uuid[7],
        uuid[8], uuid[9], uuid[10], uuid[11],
        uuid[12], uuid[13], uuid[14], uuid[15]);
    
    return std::string(uuid_str);
}

bool Board::GetBatteryLevel(int &level, bool& charging, bool& discharging) {
    return false;
}

bool Board::GetTemperature(float& esp32temp){
    return false;
}

Display* Board::GetDisplay() {
    static NoDisplay display;
    return &display;
}

Camera* Board::GetCamera() {
    return nullptr;
}

Led* Board::GetLed() {
    static NoLed led;
    return &led;
}

// 板级配置访问方法
board_config_t* Board::GetBoardConfig() {
    if (!config_initialized_) {
        InitBoardConfig();
    }
    return &board_config_;
}

// 硬件工具方法
int Board::GetDefaultI2CPort() {
    #if defined(CONFIG_IDF_TARGET_ESP32S3)
    return 0; // ESP32-S3默认I2C端口
    #elif defined(CONFIG_IDF_TARGET_ESP32)
    return 0; // ESP32默认I2C端口
    #elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return 0; // ESP32-C3默认I2C端口
    #else
    return 0; // 其他目标默认I2C端口
    #endif
}

bool Board::IsI2CDeviceConnected(int port, uint8_t addr) {
    // Simplified implementation for ESP-IDF 5.x
    // This is a placeholder implementation that always returns false
    // Since this function may not be critical for the actual operation
    ESP_LOGW(TAG, "IsI2CDeviceConnected is not fully implemented in this version");
    return false;
}

std::string Board::GetSystemInfoJson() {
    /* 
        {
            "version": 2,
            "flash_size": 4194304,
            "psram_size": 0,
            "minimum_free_heap_size": 123456,
            "mac_address": "00:00:00:00:00:00",
            "uuid": "00000000-0000-0000-0000-000000000000",
            "chip_model_name": "esp32s3",
            "chip_info": {
                "model": 1,
                "cores": 2,
                "revision": 0,
                "features": 0
            },
            "application": {
                "name": "my-app",
                "version": "1.0.0",
                "compile_time": "2021-01-01T00:00:00Z"
                "idf_version": "4.2-dev"
                "elf_sha256": ""
            },
            "partition_table": [
                "app": {
                    "label": "app",
                    "type": 1,
                    "subtype": 2,
                    "address": 0x10000,
                    "size": 0x100000
                }
            ],
            "ota": {
                "label": "ota_0"
            },
            "board": {
                ...
            }
        }
    */
    std::string json = R"({"version":2,"language":")" + std::string(Lang::CODE) + R"(",)";
    json += R"("flash_size":)" + std::to_string(SystemInfo::GetFlashSize()) + R"(,)";
    json += R"("minimum_free_heap_size":")" + std::to_string(SystemInfo::GetMinimumFreeHeapSize()) + R"(",)";
    json += R"("mac_address":")" + SystemInfo::GetMacAddress() + R"(",)";
    json += R"("uuid":")" + uuid_ + R"(",)";
    json += R"("chip_model_name":")" + SystemInfo::GetChipModelName() + R"(",)";

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    json += R"("chip_info":{)";
    json += R"("model":)" + std::to_string(chip_info.model) + R"(,)";
    json += R"("cores":)" + std::to_string(chip_info.cores) + R"(,)";
    json += R"("revision":)" + std::to_string(chip_info.revision) + R"(,)";
    json += R"("features":)" + std::to_string(chip_info.features) + R"(},)";

    auto app_desc = esp_app_get_description();
    json += R"("application":{)";
    json += R"("name":")" + std::string(app_desc->project_name) + R"(",)";
    json += R"("version":")" + std::string(app_desc->version) + R"(",)";
    json += R"("compile_time":")" + std::string(app_desc->date) + R"(T)" + std::string(app_desc->time) + R"(Z",)";
    json += R"("idf_version":")" + std::string(app_desc->idf_ver) + R"(",)";
    char sha256_str[65];
    for (int i = 0; i < 32; i++) {
        snprintf(sha256_str + i * 2, sizeof(sha256_str) - i * 2, "%02x", app_desc->app_elf_sha256[i]);
    }
    json += R"("elf_sha256":")" + std::string(sha256_str) + R"(")";
    json += R"(},)";

    json += R"("partition_table": [)";
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it) {
        const esp_partition_t *partition = esp_partition_get(it);
        json += R"({)";
        json += R"("label":")" + std::string(partition->label) + R"(",)";
        json += R"("type":)" + std::to_string(partition->type) + R"(,)";
        json += R"("subtype":)" + std::to_string(partition->subtype) + R"(,)";
        json += R"("address":)" + std::to_string(partition->address) + R"(,)";
        json += R"("size":)" + std::to_string(partition->size) + R"(},)";;
        it = esp_partition_next(it);
    }
    json.pop_back(); // Remove the last comma
    json += R"(],)";

    json += R"("ota":{)";
    auto ota_partition = esp_ota_get_running_partition();
    json += R"("label":")" + std::string(ota_partition->label) + R"(")";
    json += R"(},)";

    json += R"("board":)" + GetBoardJson();

    // Close the JSON object
    json += R"(})";
    return json;
}

Assets* Board::GetAssets() {
#ifdef DEFAULT_ASSETS
    static Assets assets(DEFAULT_ASSETS);
    return &assets;
#else
    return nullptr;
#endif
}