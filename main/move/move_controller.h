#pragma once

#include "../components.h"
#include <vector>

// 默认参数定义
#define DEFAULT_SPEED 150
#define MIN_SPEED 100
#define MAX_SPEED 255

// 舵机控制参数
#define DEFAULT_SERVO_ANGLE 90
#define MIN_SERVO_ANGLE 0
#define MAX_SERVO_ANGLE 180

// 移动控制器组件类型
enum MoveControllerType {
    MOVE_TYPE_MOTOR,      // 使用电机控制
    MOVE_TYPE_SERVO,      // 使用舵机控制
    MOVE_TYPE_HYBRID      // 混合控制（同时使用电机和舵机）
};

class MoveController : public Component {
public:
    // 构造函数 - 电机控制配置
    MoveController(int ena_pin, int enb_pin, int in1_pin, int in2_pin, int in3_pin, int in4_pin);
    
    // 构造函数 - 舵机控制配置
    MoveController(int steering_servo_pin, int throttle_servo_pin = -1);
    
    // 混合控制构造函数
    MoveController(int ena_pin, int enb_pin, int in1_pin, int in2_pin, int in3_pin, int in4_pin,
                  int steering_servo_pin, int throttle_servo_pin = -1);
    
    virtual ~MoveController();

    // Component接口实现
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;
    virtual ComponentType GetType() const override { return COMPONENT_TYPE_MOTOR; }

    // 设置控制参数
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

    // 辅助方法
    inline int map(int x, int in_min, int in_max, int out_min, int out_max) {
        return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    }

    // 获取当前状态
    int GetCurrentSpeed() const { return motor_speed_; }
    int GetDirectionX() const { return direction_x_; }
    int GetDirectionY() const { return direction_y_; }
    MoveControllerType GetControllerType() const { return controller_type_; }
    
    // 获取舵机角度
    int GetSteeringAngle() const { return steering_angle_; }
    int GetThrottlePosition() const { return throttle_position_; }

private:
    MoveControllerType controller_type_;
    
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
    
    bool running_;
    
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

    // 初始化方法
    void InitGPIO();
    void InitServos();
    
    // 控制方法
    void ControlMotor(int in1, int in2, int in3, int in4);
    void ControlSteeringServo(int angle);
    void ControlThrottleServo(int position);
}; 