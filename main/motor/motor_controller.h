#ifndef _MOTOR_CONTROLLER_H_
#define _MOTOR_CONTROLLER_H_

#include <driver/gpio.h>
#include <driver/ledc.h>
#include "../components.h"

// 电机控制引脚默认定义
#define DEFAULT_ENA_PIN 2  // 电机A使能
#define DEFAULT_ENB_PIN 1  // 电机B使能
#define DEFAULT_IN1_PIN 47 // 电机A输入1
#define DEFAULT_IN2_PIN 21 // 电机A输入2
#define DEFAULT_IN3_PIN 20 // 电机B输入1
#define DEFAULT_IN4_PIN 19 // 电机B输入2

// 速度控制参数
#define DEFAULT_SPEED 100
#define MIN_SPEED 100
#define MAX_SPEED 255

// 电机控制器组件
class MotorController : public Component {
public:
    MotorController(int ena_pin = DEFAULT_ENA_PIN, 
                   int enb_pin = DEFAULT_ENB_PIN,
                   int in1_pin = DEFAULT_IN1_PIN,
                   int in2_pin = DEFAULT_IN2_PIN,
                   int in3_pin = DEFAULT_IN3_PIN,
                   int in4_pin = DEFAULT_IN4_PIN);
    virtual ~MotorController();

    // Component接口实现
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual const char* GetName() const override;

    // 设置控制参数
    void SetControlParams(float distance, int dirX, int dirY);
    
    // 设置电机状态
    void Forward(int speed = DEFAULT_SPEED);
    void Backward(int speed = DEFAULT_SPEED);
    void TurnLeft(int speed = DEFAULT_SPEED);
    void TurnRight(int speed = DEFAULT_SPEED);
    void Stop(bool brake = true);
    
    // 设置速度
    void SetSpeed(int speed);
    
    // 获取当前状态
    int GetCurrentSpeed() const { return motor_speed_; }
    int GetDirectionX() const { return direction_x_; }
    int GetDirectionY() const { return direction_y_; }

private:
    // 电机控制引脚
    int ena_pin_;
    int enb_pin_;
    int in1_pin_;
    int in2_pin_;
    int in3_pin_;
    int in4_pin_;

    // 状态标志
    bool running_;
    
    // 控制参数
    int direction_x_; // X轴方向值
    int direction_y_; // Y轴方向值
    int motor_speed_; // 电机速度
    float distance_percent_; // 摇杆拖动距离百分比
    
    // 缓存值
    int last_dir_x_;  // 缓存上一次的X方向
    int last_dir_y_;  // 缓存上一次的Y方向
    float cached_angle_degrees_; // 缓存计算的角度

    // 初始化GPIO
    void InitGPIO();
    
    // 控制电机函数
    void ControlMotor(int in1, int in2, int in3, int in4);
};

#endif // _MOTOR_CONTROLLER_H_ 