#include "location_controller.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
#include <cstring>
#include <algorithm>
#include <chrono>

static const char* TAG = "LocationController";

// 静态实例初始化为nullptr
LocationController* LocationController::instance_ = nullptr;

// 获取单例实例
LocationController* LocationController::GetInstance() {
    if (instance_ == nullptr) {
        instance_ = new LocationController();
    }
    return instance_;
}

// 构造函数
LocationController::LocationController()
    : running_(false), 
      current_mode_(MODE_UWB), 
      current_position_() {
    ESP_LOGI(TAG, "LocationController构造函数");
    
    // 初始化配置
    config_.use_uwb = true;      // 默认启用UWB
    config_.use_gps = false;     // 默认不启用GPS
    config_.use_fusion = false;  // 默认不启用融合定位
    
    // 初始化定位模式 - 根据Kconfig配置设置默认值
#if defined(CONFIG_LOCATION_MODE_UWB)
    current_mode_ = MODE_UWB;
    config_.use_uwb = true;
#elif defined(CONFIG_LOCATION_MODE_GPS)
    current_mode_ = MODE_GPS;
    config_.use_gps = true;
#elif defined(CONFIG_LOCATION_MODE_FUSION)
    current_mode_ = MODE_FUSION;
    config_.use_uwb = true;
    config_.use_gps = true;
    config_.use_fusion = true;
#endif

    // 设置更新间隔
#if defined(CONFIG_LOCATION_UPDATE_INTERVAL_MS)
    config_.update_interval = CONFIG_LOCATION_UPDATE_INTERVAL_MS / 1000.0f;  // 转换为秒
#else
    config_.update_interval = 0.1f;  // 默认100ms更新一次
#endif

    ESP_LOGI(TAG, "LocationController初始化完成: 模式=%s, 更新间隔=%.2f秒", 
             ModeToString(current_mode_), config_.update_interval);
}

// 析构函数
LocationController::~LocationController() {
    ESP_LOGI(TAG, "LocationController析构函数");
    Stop();
}

// 启动控制器
bool LocationController::Start() {
    ESP_LOGI(TAG, "启动LocationController");
    
    if (running_) {
        ESP_LOGI(TAG, "LocationController已在运行");
        return true;
    }
    
    // 检查Kconfig配置，如果未启用位置定位功能则返回
#ifndef CONFIG_ENABLE_LOCATION_CONTROLLER
    ESP_LOGW(TAG, "位置定位功能在Kconfig中未启用，不启动LocationController");
    return false;
#endif
    
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    // 根据配置初始化适当的定位系统
    bool initialized = false;
    
    if (config_.use_uwb) {
        initialized |= InitUWB();
    }
    
    if (config_.use_gps) {
        initialized |= InitGPS();
    }
    
    if (!initialized) {
        ESP_LOGE(TAG, "没有可用的定位系统初始化成功");
        return false;
    }
    
    // 设置初始定位模式
    if (config_.use_uwb) {
        current_mode_ = MODE_UWB;
    } else if (config_.use_gps) {
        current_mode_ = MODE_GPS;
    } else if (config_.use_fusion) {
        current_mode_ = MODE_FUSION;
    }
    
    running_ = true;
    return true;
}

// 停止控制器
void LocationController::Stop() {
    ESP_LOGI(TAG, "停止LocationController");
    
    if (!running_) {
        return;
    }
    
    running_ = false;
    ESP_LOGI(TAG, "LocationController已停止");
}

// 设置定位模式
bool LocationController::SetLocationMode(LocationMode mode) {
    if (!running_) {
        ESP_LOGW(TAG, "LocationController未运行，无法切换模式");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    // 检查所请求的模式是否被配置和初始化
    switch (mode) {
        case MODE_GPS:
            if (!config_.use_gps) {
                ESP_LOGW(TAG, "GPS模式未启用");
                return false;
            }
            break;
            
        case MODE_UWB:
            if (!config_.use_uwb) {
                ESP_LOGW(TAG, "UWB模式未启用");
                return false;
            }
            break;
            
        case MODE_FUSION:
            if (!config_.use_fusion || (!config_.use_gps && !config_.use_uwb)) {
                ESP_LOGW(TAG, "融合模式需要至少两种定位系统");
                return false;
            }
            break;
    }
    
    // 切换模式
    current_mode_ = mode;
    ESP_LOGI(TAG, "定位模式切换为: %s", ModeToString(mode));
    
    return true;
}

// 获取当前位置
PositionInfo LocationController::GetCurrentPosition() const {
    std::lock_guard<std::mutex> lock(position_mutex_);
    
    if (!running_) {
        return PositionInfo(); // 返回默认位置
    }
    
    // 根据当前模式获取位置
    PositionInfo position;
    
    switch (current_mode_) {
        case MODE_GPS:
            position = GetPositionByGPS();
            break;
            
        case MODE_UWB:
            position = GetPositionByUWB();
            break;
            
        case MODE_FUSION:
            position = GetFusionPosition();
            break;
    }
    
    return position;
}

// 校准位置
bool LocationController::CalibratePosition(float x, float y, float orientation) {
    if (!running_) {
        ESP_LOGW(TAG, "LocationController未运行，无法校准");
        return false;
    }
    
    // 检查校准功能是否启用
#ifndef CONFIG_LOCATION_CALIBRATION_ENABLED
    ESP_LOGW(TAG, "位置校准功能在Kconfig中未启用");
    return false;
#endif
    
    ESP_LOGI(TAG, "校准位置: x=%.2f, y=%.2f, orientation=%.2f", x, y, orientation);
    
    // 在实际实现中，这里应该与硬件交互进行实际校准
    // 但在这个示例中，我们只是更新当前位置
    
    std::lock_guard<std::mutex> lock(position_mutex_);
    current_position_ = PositionInfo(
        x, 
        y, 
        orientation, 
        0.05,  // 假设校准后精度为5厘米
        esp_timer_get_time() / 1000000.0f  // 当前时间（秒）
    );
    
    // 触发回调
    for (const auto& callback : update_callbacks_) {
        if (callback) {
            callback(current_position_);
        }
    }
    
    return true;
}

// 保存地图
bool LocationController::SaveLocationMap(const std::string& filename) {
    if (!running_) {
        ESP_LOGW(TAG, "LocationController未运行，无法保存地图");
        return false;
    }
    
    // 检查地图保存功能是否启用
#ifndef CONFIG_LOCATION_SAVE_MAP_ENABLED
    ESP_LOGW(TAG, "地图保存功能在Kconfig中未启用");
    return false;
#endif
    
    std::string map_name = filename;
    if (map_name.empty()) {
        map_name = "/spiffs/location_map.json"; // 默认文件名
    }
    
    ESP_LOGI(TAG, "保存位置地图到: %s", map_name.c_str());
    
    // 在实际实现中，应该在这里保存整个地图数据
    // 但在这个示例中，我们仅保存当前位置作为示例
    
    // 此处可以使用cJSON或其他库创建JSON，然后写入文件
    
    return true; // 实际上应该根据保存结果返回
}

// 更新位置信息
void LocationController::UpdatePosition(float x, float y, float orientation, float accuracy) {
    if (!running_) {
        ESP_LOGW(TAG, "LocationController未运行，忽略位置更新");
        return;
    }
    
    ESP_LOGD(TAG, "更新位置: x=%.2f, y=%.2f, orientation=%.2f, accuracy=%.2f", 
            x, y, orientation, accuracy);
    
    std::lock_guard<std::mutex> lock(position_mutex_);
    current_position_ = PositionInfo(
        x, 
        y, 
        orientation, 
        accuracy,
        esp_timer_get_time() / 1000000.0f  // 当前时间（秒）
    );
    
    // 触发回调
    for (const auto& callback : update_callbacks_) {
        if (callback) {
            callback(current_position_);
        }
    }
}

// 注册位置更新回调
void LocationController::RegisterPositionUpdateCallback(PositionUpdateCallback callback) {
    if (callback) {
        update_callbacks_.push_back(callback);
        ESP_LOGI(TAG, "注册位置更新回调，当前回调数量: %zu", update_callbacks_.size());
    }
}

// 设置配置参数
bool LocationController::SetConfig(const LocationConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    // 如果控制器已启动，某些配置可能无法立即生效
    if (running_) {
        ESP_LOGW(TAG, "LocationController已运行，某些配置可能需要重启才能生效");
    }
    
    config_ = config;
    ESP_LOGI(TAG, "定位配置已更新: GPS=%d, UWB=%d, Fusion=%d, interval=%.2f", 
            config.use_gps, config.use_uwb, config.use_fusion, config.update_interval);
    
    return true;
}

// 将定位模式转换为字符串
const char* LocationController::ModeToString(LocationMode mode) {
    switch (mode) {
        case MODE_GPS:
            return "GPS";
        case MODE_UWB:
            return "UWB";
        case MODE_FUSION:
            return "FUSION";
        default:
            return "UNKNOWN";
    }
}

// 初始化GPS
bool LocationController::InitGPS() {
    ESP_LOGI(TAG, "初始化GPS定位系统");
    
    // 在实际实现中，这里应该初始化GPS硬件和通信
    // 但在这个示例中，我们仅进行模拟
    
    return true; // 假设初始化成功
}

// 初始化UWB
bool LocationController::InitUWB() {
    ESP_LOGI(TAG, "初始化UWB定位系统");
    
    // 在实际实现中，这里应该初始化UWB硬件和通信
    // 但在这个示例中，我们仅进行模拟
    
    return true; // 假设初始化成功
}

// 通过GPS获取位置
PositionInfo LocationController::GetPositionByGPS() const {
    // 在实际实现中，这里应该访问GPS硬件获取真实位置
    // 但在这个示例中，我们仅返回当前缓存的位置，并模拟一些随机波动
    
    // 添加一些随机波动，模拟GPS的不稳定性
    float noise_x = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.5f;  // -0.25 to 0.25
    float noise_y = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.5f;  // -0.25 to 0.25
    
    return PositionInfo(
        current_position_.x + noise_x,
        current_position_.y + noise_y,
        current_position_.orientation,
        1.0f,  // GPS通常精度较低，约1米
        esp_timer_get_time() / 1000000.0f  // 当前时间（秒）
    );
}

// 通过UWB获取位置
PositionInfo LocationController::GetPositionByUWB() const {
    // 在实际实现中，这里应该访问UWB硬件获取真实位置
    // 但在这个示例中，我们仅返回当前缓存的位置，并模拟一些随机波动
    
    // 添加一些随机波动，UWB通常比GPS更稳定
    float noise_x = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.1f;  // -0.05 to 0.05
    float noise_y = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.1f;  // -0.05 to 0.05
    
    return PositionInfo(
        current_position_.x + noise_x,
        current_position_.y + noise_y,
        current_position_.orientation,
        0.1f,  // UWB通常精度较高，约10厘米
        esp_timer_get_time() / 1000000.0f  // 当前时间（秒）
    );
}

// 获取融合位置
PositionInfo LocationController::GetFusionPosition() const {
    // 在实际实现中，这里应该使用卡尔曼滤波等算法融合不同的定位数据
    // 但在这个示例中，我们简化为GPS和UWB的加权平均
    
    // 获取各系统的位置
    PositionInfo gps_pos = GetPositionByGPS();
    PositionInfo uwb_pos = GetPositionByUWB();
    
    // GPS权重为0.2，UWB权重为0.8（因为UWB精度更高）
    float gps_weight = 0.2f;
    float uwb_weight = 0.8f;
    
    // 计算加权平均值
    return PositionInfo(
        gps_weight * gps_pos.x + uwb_weight * uwb_pos.x,
        gps_weight * gps_pos.y + uwb_weight * uwb_pos.y,
        uwb_pos.orientation,  // 直接使用UWB的方向
        0.08f,  // 融合后精度提高，约8厘米
        esp_timer_get_time() / 1000000.0f  // 当前时间（秒）
    );
} 