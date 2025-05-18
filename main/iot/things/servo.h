#pragma once

namespace iot {

/**
 * @brief 舵机控制类
 * 
 * 用于控制舵机的角度、扫描、连续旋转等功能。
 */
class ServoThing;

/**
 * @brief 注册舵机控制Thing
 * 
 * 创建并注册舵机控制Thing实例。
 */
void RegisterServo();

} // namespace iot 