#pragma once

#include "../components.h"
#include "../iot/thing.h"
#include "../iot/thing_manager.h"
#include "../web/web.h"
#include <vector>
#include <string>

// 默认参数定义
#define DEFAULT_SPEED 150
#define MIN_SPEED 100
#define MAX_SPEED 255

// 舵机控制参数
#define DEFAULT_SERVO_ANGLE 90
#define MIN_SERVO_ANGLE 0
#define MAX_SERVO_ANGLE 180

// 车辆控制器类型
enum VehicleType {
    VEHICLE_TYPE_MOTOR,      // 使用电机控制
    VEHICLE_TYPE_SERVO,      // 使用舵机控制
    VEHICLE_TYPE_HYBRID,     // 混合控制（同时使用电机和舵机）
    VEHICLE_TYPE_MOTOR_CAMERA, // 带云台的电机小车
    VEHICLE_TYPE_SERVO_CAMERA  // 带云台的舵机小车
};

/**
 * Vehicle组件：整合车辆控制和Web内容管理
 * 
 * 此类负责:
 * 1. 电机与舵机的底层控制
 * 2. 车辆运动控制逻辑
 * 3. Web界面相关功能的处理
 * 4. WebSocket实时通信
 */
class Vehicle : public Component {
public:
    // 构造函数
    // 电机控制构造函数
    Vehicle(Web* server, int ena_pin, int enb_pin, int in1_pin, int in2_pin, int in3_pin, int in4_pin);
    
    // 舵机控制构造函数
    Vehicle(Web* server, int steering_servo_pin, int throttle_servo_pin = -1);
    
    // 带云台的电机小车构造函数
    Vehicle(Web* server, int ena_pin, int enb_pin, int in1_pin, int in2_pin, int in3_pin, int in4_pin,
            int camera_h_servo_pin, int camera_v_servo_pin);
    
    // 带云台的舵机小车构造函数
    Vehicle(Web* server, int steering_servo_pin, int throttle_servo_pin,
            int camera_h_servo_pin, int camera_v_servo_pin);
    
    virtual ~Vehicle();

    // Component接口实现
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;
    virtual ComponentType GetType() const override { return COMPONENT_TYPE_MOTOR; }

    // 车辆控制方法
    void SetControlParams(float distance, int dirX, int dirY);
    
    // 电机控制方法
    void Forward(int speed = DEFAULT_SPEED);
    void Backward(int speed = DEFAULT_SPEED);
    void TurnLeft(int speed = DEFAULT_SPEED);
    void TurnRight(int speed = DEFAULT_SPEED);
    void Stop(bool brake);
    void SetSpeed(int speed);

    // 舵机控制方法
    void SetSteeringAngle(int angle);
    void SetThrottlePosition(int position);
    
    // Web相关方法
    void HandleWebSocketMessage(int client_index, const std::string& message);

    // 辅助方法和获取器
    inline int map(int x, int in_min, int in_max, int out_min, int out_max) {
        return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    }

    // 获取当前状态
    int GetCurrentSpeed() const { return motor_speed_; }
    int GetDirectionX() const { return direction_x_; }
    int GetDirectionY() const { return direction_y_; }
    VehicleType GetControllerType() const { return vehicle_type_; }
    
    // 获取舵机角度
    int GetSteeringAngle() const { return steering_angle_; }
    int GetThrottlePosition() const { return throttle_position_; }

    // 物联网集成 Thing相关逻辑
    static void SendUltrasonicData(Web* server, iot::Thing* thing);
    static void SendServoData(Web* server, iot::Thing* thing);

private:
    // 车辆类型
    VehicleType vehicle_type_;
    bool running_;
    
    // WebServer引用
    Web* webserver_;
    
    // 电机控制引脚
    int ena_pin_;
    int enb_pin_;
    int in1_pin_;
    int in2_pin_;
    int in3_pin_;
    int in4_pin_;
    
    // 舵机控制引脚
    int steering_servo_pin_;
    int throttle_servo_pin_;
    
    // 控制参数
    int direction_x_;
    int direction_y_;
    int motor_speed_;
    float distance_percent_;
    
    // 缓存上一次方向
    int last_dir_x_;
    int last_dir_y_;
    float cached_angle_degrees_;
    
    // 舵机角度
    int steering_angle_;
    int throttle_position_;

    // 带云台的引脚
    int camera_h_servo_pin_;
    int camera_v_servo_pin_;

    // HTTP处理器
    static esp_err_t HandleVehicle(httpd_req_t *req);
    static esp_err_t HandleServo(httpd_req_t *req);
    
    // 初始化方法
    void InitHandlers();
    void InitGPIO();
    void InitServos();
    
    // 控制方法
    void ControlMotor(int in1, int in2, int in3, int in4);
    void ControlSteeringServo(int angle);
    void ControlThrottleServo(int position);
    bool ControlServoWithLU9685(int channel, int angle);
    
    // 数据发送任务
    static void UltrasonicDataTask(void* pvParameters);
    static void ServoDataTask(void* pvParameters);
};

// 全局初始化函数
void InitVehicleComponent(Web* server); 