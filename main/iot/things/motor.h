#pragma once

namespace iot {

/**
 * @brief 电机控制Thing类
 * 
 * 用于控制小车电机的Thing类，提供前进、后退、转向和停止功能。
 */
class Motor;

/**
 * @brief 注册电机控制Thing
 * 
 * 创建并注册电机控制Thing实例。
 */
void RegisterMotor();

} // namespace iot 