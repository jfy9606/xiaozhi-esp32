#pragma once

#include "../components.h"
#include "../web/web.h"
#include <string>
#include <memory>
#include <functional>
#include <vector>

// GPS坐标结构
struct GpsCoordinate {
    double latitude;
    double longitude;
    double altitude;
    double speed;
    double course;
    bool valid;
    
    GpsCoordinate() : latitude(0), longitude(0), altitude(0), 
                      speed(0), course(0), valid(false) {}
};

/**
 * @brief 位置服务组件 - 提供GPS定位和地图服务
 * 
 * 此类整合了之前的LocationController和LocationContent功能
 * 主要负责:
 * 1. GPS数据处理
 * 2. 定位服务
 * 3. 地图显示
 * 4. Web界面交互
 */
class Location : public Component {
public:
    // 回调类型定义
    using LocationUpdateCallback = std::function<void(const GpsCoordinate&)>;
    
    // 构造函数和析构函数
    explicit Location(Web* web_server = nullptr);
    virtual ~Location();
    
    // Component接口实现
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;
    
    // GPS和定位功能
    bool StartGPS();
    void StopGPS();
    bool IsGpsRunning() const;
    GpsCoordinate GetCurrentLocation() const;
    
    // 注册位置更新回调
    void RegisterLocationUpdateCallback(LocationUpdateCallback callback);
    
    // WebSocket方法
    void HandleWebSocketMessage(httpd_req_t* req, const std::string& message);
    
protected:
    // 网页UI处理
    void InitHandlers();
    std::string GetLocationHtml(const char* language = nullptr);
    
    // API响应处理
    ApiResponse HandleGetLocation(httpd_req_t* req);
    ApiResponse HandleStartTracking(httpd_req_t* req);
    ApiResponse HandleStopTracking(httpd_req_t* req);
    
    // GPS任务和处理
    static void GpsTask(void* param);
    void ProcessNmeaData(const std::string& nmea_data);
    
private:
    // 成员变量
    Web* web_server_;
    bool running_;
    bool gps_running_;
    GpsCoordinate current_location_;
    TaskHandle_t gps_task_handle_;
    std::vector<LocationUpdateCallback> location_callbacks_;
    
    // 禁止复制和赋值
    Location(const Location&) = delete;
    Location& operator=(const Location&) = delete;
}; 