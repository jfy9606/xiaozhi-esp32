#ifndef LOCATION_CONTROLLER_H
#define LOCATION_CONTROLLER_H

#include "components.h"
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <cmath>
#include <functional>
#include "esp_log.h"
#include "sdkconfig.h"
#include "iot/thing.h"

// 定位模式枚举
enum LocationMode {
    MODE_GPS,    // 使用GPS定位
    MODE_UWB,    // 使用UWB定位
    MODE_FUSION  // 融合定位
};

// 位置信息结构体
struct PositionInfo {
    float x;          // X坐标 (米)
    float y;          // Y坐标 (米)
    float orientation; // 朝向角度 (度数，0-360)
    float accuracy;    // 精度 (米)
    float timestamp;   // 时间戳
    
    // 默认构造函数
    PositionInfo() : x(0), y(0), orientation(0), accuracy(0), timestamp(0) {}
    
    // 构造函数
    PositionInfo(float _x, float _y, float _orientation = 0, float _accuracy = 0, float _timestamp = 0)
        : x(_x), y(_y), orientation(_orientation), accuracy(_accuracy), timestamp(_timestamp) {}
    
    // 计算两个位置之间的距离
    float distanceTo(const PositionInfo& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        return std::sqrt(dx*dx + dy*dy);
    }
};

// 定位配置结构体
struct LocationConfig {
    bool use_gps;    // 是否使用GPS
    bool use_uwb;    // 是否使用UWB
    bool use_fusion; // 是否使用融合定位
    float update_interval; // 更新间隔 (秒)
    
    // 默认构造函数
    LocationConfig() : use_gps(false), use_uwb(true), use_fusion(false), update_interval(1.0) {}
};

// 位置控制器类
class LocationController : public Component {
public:
    // 单例接口
    static LocationController* GetInstance();
    
    // 获取组件名称
    const char* GetName() const override { return "LocationController"; }
    
    // 组件类型
    ComponentType GetType() const override { return COMPONENT_TYPE_LOCATION; }
    
    // 初始化和启动
    bool Start() override;
    void Stop() override;
    bool IsRunning() const override { return running_; }
    
    // 定位功能接口
    bool SetLocationMode(LocationMode mode);
    LocationMode GetLocationMode() const { return current_mode_; }
    PositionInfo GetCurrentPosition() const;
    bool CalibratePosition(float x = 0, float y = 0, float orientation = 0);
    bool SaveLocationMap(const std::string& filename = "");
    
    // 更新位置信息
    void UpdatePosition(float x, float y, float orientation = 0, float accuracy = 0);
    
    // 注册位置更新回调
    using PositionUpdateCallback = std::function<void(const PositionInfo&)>;
    void RegisterPositionUpdateCallback(PositionUpdateCallback callback);
    
    // 设置配置参数
    bool SetConfig(const LocationConfig& config);
    LocationConfig GetConfig() const { return config_; }
    
    // 将定位模式转换为字符串
    static const char* ModeToString(LocationMode mode);
    
    // 构造函数和析构函数
    LocationController();
    virtual ~LocationController();
    
protected:
    // 初始化定位功能
    bool InitGPS();
    bool InitUWB();
    
    // 定位方法
    PositionInfo GetPositionByGPS() const;
    PositionInfo GetPositionByUWB() const;
    PositionInfo GetFusionPosition() const;
    
private:
    // 私有成员变量
    static LocationController* instance_;
    bool running_;
    LocationMode current_mode_;
    PositionInfo current_position_;
    LocationConfig config_;
    std::vector<PositionUpdateCallback> update_callbacks_;
    mutable std::mutex position_mutex_;
    mutable std::mutex config_mutex_;
    
    // 禁止复制和赋值
    LocationController(const LocationController&) = delete;
    LocationController& operator=(const LocationController&) = delete;
};

#endif // LOCATION_CONTROLLER_H 