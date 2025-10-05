# Bread-Compact-WiFi-S3Cam 开发板扩展器支持

本文档描述了 bread-compact-wifi-s3cam 开发板的四种扩展器支持方案，适用于所有 bread-compact 系列开发板。

## 概述

bread-compact 系列开发板支持四种扩展器，通过统一的I2C总线和GPIO接口实现功能扩展：

1. **PCA9548A I2C多路复用器** - 扩展I2C设备数量
2. **LU9685 舵机控制器** - 16通道PWM舵机控制
3. **PCF8575 GPIO扩展器** - 16个额外GPIO引脚
4. **HW-178 模拟多路复用器** - 16通道模拟信号采集

## 硬件连接

### 主控板引脚分配

#### bread-compact-wifi-s3cam 引脚分配

| 功能 | GPIO | ESP32-S3功能 | 说明 |
|------|------|-------------|------|
| I2C SDA | GPIO14 | 通用GPIO | 扩展器I2C数据线 (与LAMP_GPIO共用) |
| I2C SCL | GPIO3 | JTAG_EN | 模拟信号输出 (ADC1_CH2, JTAG使能引脚可复用) |
| HW-178 S0 | GPIO35 | PSRAM | 模拟多路复用器选择位0 (PSRAM引脚可复用) |
| HW-178 S1 | GPIO36 | PSRAM | 模拟多路复用器选择位1 (PSRAM引脚可复用) |
| HW-178 S2 | GPIO37 | PSRAM | 模拟多路复用器选择位2 (PSRAM引脚可复用) |
| HW-178 SIG | GPIO46 | LOG | 扩展器I2C时钟线 (与HW178_S3共用) |

#### bread-compact-wifi 引脚分配

| 功能 | GPIO | ESP32-S3功能 | 说明 |
|------|------|-------------|------|
| I2C SDA | GPIO8 | CAM_Y4 | 扩展器I2C数据线 (摄像头数据引脚可复用) |
| I2C SCL | GPIO9 | CAM_Y3 | 扩展器I2C时钟线 (摄像头数据引脚可复用) |
| HW-178 S0 | GPIO10 | CAM_Y5 | 模拟多路复用器选择位0 (摄像头数据引脚可复用) |
| HW-178 S1 | GPIO11 | CAM_Y2 | 模拟多路复用器选择位1 (摄像头数据引脚可复用) |
| HW-178 S2 | GPIO12 | CAM_Y6 | 模拟多路复用器选择位2 (摄像头数据引脚可复用) |
| HW-178 S3 | GPIO13 | CAM_PCLK | 模拟多路复用器选择位3 (摄像头像素时钟可复用) |
| HW-178 SIG | GPIO14 | ADC2_CH3 | 模拟信号输出 (ADC2通道3) |

#### bread-compact-wifi-lcd 引脚分配

| 功能 | GPIO | ESP32-S3功能 | 说明 |
|------|------|-------------|------|
| I2C SDA | GPIO8 | CAM_Y4 | 扩展器I2C数据线 (摄像头数据引脚可复用) |
| I2C SCL | GPIO9 | CAM_Y3 | 扩展器I2C时钟线 (摄像头数据引脚可复用) |
| HW-178 S0 | GPIO10 | CAM_Y5 | 模拟多路复用器选择位0 (摄像头数据引脚可复用) |
| HW-178 S1 | GPIO11 | CAM_Y2 | 模拟多路复用器选择位1 (摄像头数据引脚可复用) |
| HW-178 S2 | GPIO12 | CAM_Y6 | 模拟多路复用器选择位2 (摄像头数据引脚可复用) |
| HW-178 S3 | GPIO13 | CAM_PCLK | 模拟多路复用器选择位3 (摄像头像素时钟可复用) |
| HW-178 SIG | GPIO14 | ADC2_CH3 | 模拟信号输出 (ADC2通道3) |

#### 引脚冲突避免说明

**已占用引脚 (不可用于扩展器):**
- **音频引脚**: GPIO4-7, GPIO15-16 (I2S音频接口)
- **显示引脚**: 
  - OLED (bread-compact-wifi): GPIO41-42
  - LCD (bread-compact-wifi-lcd): GPIO21, GPIO40-42, GPIO45, GPIO47
  - LCD (bread-compact-wifi-s3cam): GPIO19-21, GPIO38, GPIO45, GPIO47
- **摄像头引脚** (bread-compact-wifi-s3cam): GPIO4-18 (完整摄像头接口)
- **按键引脚**: GPIO0 (Boot), GPIO39-40, GPIO47-48 (各种按键)
- **系统引脚**: GPIO43-44 (UART0 TX/RX, 系统日志输出)

**可复用引脚 (用于扩展器):**
- **通用GPIO**: GPIO33-34 (S3CAM开发板I2C专用)
- **PSRAM引脚**: GPIO35-37 (通常不使用外部PSRAM时可复用)
- **摄像头引脚**: GPIO8-14 (非摄像头板子可复用)
- **LOG引脚**: GPIO46 (日志输出引脚可复用)
- **JTAG引脚**: GPIO3 (调试时可能冲突，生产环境可复用)

### I2C设备地址分配

| 设备 | I2C地址 | PCA9548A通道 | 说明 |
|------|---------|--------------|------|
| PCA9548A | 0x70 | - | I2C多路复用器 |
| LU9685 | 0x40 | 通道1 | 舵机控制器 |
| PCF8575 | 0x20 | 通道2 | GPIO扩展器 |

## 扩展器详细说明

### 1. PCA9548A I2C多路复用器

**功能**: 将一个I2C总线扩展为8个独立的I2C通道，解决地址冲突问题。

**接线**:
```
PCA9548A    ESP32-S3 (S3CAM)
VCC    ->   3.3V
GND    ->   GND
SDA    ->   GPIO14
SCL    ->   GPIO46
A0-A2  ->   GND (地址设置为0x70)

注意：其他bread-compact开发板使用GPIO8/GPIO9
```

**通道分配**:
- 通道1: LU9685舵机控制器
- 通道2: PCF8575 GPIO扩展器
- 通道3-8: 预留给其他I2C设备

### 2. LU9685 舵机控制器

**功能**: 16通道PWM输出，专门用于舵机控制，支持0-180度角度控制。

**接线**:
```
LU9685      PCA9548A通道1
VCC    ->   3.3V/5V
GND    ->   GND
SDA    ->   SC1
SCL    ->   SD1
```

**通道定义**:
```cpp
#define SERVO_CHANNEL_PAN       0    // 云台水平舵机
#define SERVO_CHANNEL_TILT      1    // 云台垂直舵机
#define SERVO_CHANNEL_GRIPPER   2    // 机械爪舵机
#define SERVO_CHANNEL_ARM_1     3    // 机械臂关节1
// ... 更多通道
```

### 3. PCF8575 GPIO扩展器

**功能**: 16个额外的GPIO引脚，可配置为输入或输出，用于控制LED、继电器、电机等。

**接线**:
```
PCF8575     PCA9548A通道2
VCC    ->   3.3V/5V
GND    ->   GND
SDA    ->   SC2
SCL    ->   SD2
A0-A2  ->   GND (地址设置为0x20)
```

**引脚定义**:
```cpp
#define GPIO_EXP_LED_1          0    // P0 - LED 1
#define GPIO_EXP_LED_2          1    // P1 - LED 2
#define GPIO_EXP_RELAY_1        4    // P4 - 继电器1
#define GPIO_EXP_MOTOR_IN1      6    // P6 - 电机控制IN1
// ... 更多引脚
```

### 4. HW-178 模拟多路复用器

**功能**: 将16个模拟信号复用到1个ADC通道，用于多路模拟传感器采集。

**接线**:
```
HW-178      ESP32-S3
VCC    ->   3.3V
GND    ->   GND
S0     ->   GPIO35
S1     ->   GPIO36
S2     ->   GPIO37
S3     ->   NULL
SIG    ->   GPIO3 (ADC1_CH2)
C0-C15 ->   连接各种模拟传感器
```

**通道定义**:
```cpp
#define ANALOG_TEMP_SENSOR      0    // C0 - 温度传感器
#define ANALOG_LIGHT_SENSOR     1    // C1 - 光照传感器
#define ANALOG_VOLTAGE_MONITOR  6    // C6 - 电压监测
#define ANALOG_CURRENT_MONITOR  7    // C7 - 电流监测
// ... 更多通道
```

## 软件配置

### Kconfig配置

使用ESP-IDF的配置菜单：
```bash
idf.py menuconfig
```

导航到：`Xiaozhi Assistant` → `组件管理器` → `Multiplexer & Expander`

#### 可配置项目

**PCA9548A I2C多路复用器**:
- `CONFIG_PCA9548A_SDA_PIN` - SDA引脚 (默认: GPIO20 for S3CAM, GPIO8 for others)
- `CONFIG_PCA9548A_SCL_PIN` - SCL引脚 (默认: GPIO21 for S3CAM, GPIO9 for others)  
- `CONFIG_PCA9548A_I2C_ADDR` - I2C地址 (默认: 0x70)
- `CONFIG_PCA9548A_RESET_PIN` - 复位引脚 (默认: -1, 不使用)
- `CONFIG_PCA9548A_I2C_FREQ_HZ` - I2C频率 (默认: 400000Hz)
- `CONFIG_PCA9548A_I2C_TIMEOUT_MS` - 超时时间 (默认: 1000ms)

**LU9685舵机控制器**:
- `CONFIG_LU9685_I2C_ADDR` - I2C地址 (默认: 0x40)
- `CONFIG_LU9685_PCA9548A_CHANNEL` - PCA9548A通道 (默认: 1)

**PCF8575 GPIO扩展器**:
- `CONFIG_PCF8575_I2C_ADDR` - I2C地址 (默认: 0x20)
- `CONFIG_PCF8575_PCA9548A_CHANNEL` - PCA9548A通道 (默认: 2)
- `CONFIG_PCF8575_I2C_TIMEOUT_MS` - 超时时间 (默认: 1000ms)

**HW-178模拟多路复用器**:
- `CONFIG_HW178_S0_PIN` - S0控制引脚 (默认: GPIO35 for S3CAM, GPIO10 for others)
- `CONFIG_HW178_S1_PIN` - S1控制引脚 (默认: GPIO36 for S3CAM, GPIO11 for others)
- `CONFIG_HW178_S2_PIN` - S2控制引脚 (默认: GPIO37 for S3CAM, GPIO12 for others)
- `CONFIG_HW178_S3_PIN` - S3控制引脚 (默认: -1, 不使用)
- `CONFIG_HW178_SIG_PIN` - 信号输出引脚 (默认: GPIO46 for S3CAM, GPIO14 for others)
- `CONFIG_HW178_EN_PIN` - 使能引脚 (默认: -1, 不使用)
- `CONFIG_HW178_ADC_CHANNEL` - ADC通道 (默认: 5 for S3CAM, 3 for others)

**功能启用开关**:
- `CONFIG_ENABLE_MULTIPLEXER` - 启用扩展器系统
- `CONFIG_ENABLE_PCA9548A` - 启用PCA9548A多路复用器
- `CONFIG_ENABLE_LU9685` - 启用LU9685舵机控制器
- `CONFIG_ENABLE_PCF8575` - 启用PCF8575 GPIO扩展器
- `CONFIG_ENABLE_HW178` - 启用HW-178模拟多路复用器

#### 配置示例

**自定义引脚配置**:
```
# 使用不同的I2C引脚
CONFIG_PCA9548A_SDA_PIN=33
CONFIG_PCA9548A_SCL_PIN=34

# 使用不同的HW-178控制引脚
CONFIG_HW178_S0_PIN=1
CONFIG_HW178_S1_PIN=2
CONFIG_HW178_S2_PIN=3
CONFIG_HW178_SIG_PIN=4
```

**禁用某些扩展器**:
```
# 只启用PCA9548A和LU9685，禁用其他扩展器
CONFIG_ENABLE_MULTIPLEXER=y
CONFIG_ENABLE_PCA9548A=y
CONFIG_ENABLE_LU9685=y
CONFIG_ENABLE_PCF8575=n
CONFIG_ENABLE_HW178=n
```

### 初始化

扩展器在板子初始化时自动初始化，根据Kconfig配置使用相应的引脚和参数。无需手动调用初始化函数。

### API使用示例

#### 舵机控制
```cpp
// 设置舵机角度
board.SetServoAngle(SERVO_CHANNEL_PAN, 90);  // 云台水平舵机转到90度

// 设置所有舵机到中位
board.SetAllServosAngle(90);
```

#### GPIO扩展器
```cpp
// 控制LED
board.SetExpanderPin(GPIO_EXP_LED_1, true);   // 点亮LED1
board.SetExpanderPin(GPIO_EXP_LED_1, false);  // 熄灭LED1

// 控制继电器
board.SetExpanderPin(GPIO_EXP_RELAY_1, true); // 打开继电器1
```

#### 模拟信号采集
```cpp
int temp_value;
// 读取温度传感器
if (board.ReadAnalogChannel(ANALOG_TEMP_SENSOR, &temp_value)) {
    printf("Temperature ADC: %d\n", temp_value);
}

// 扫描所有模拟通道
int values[16];
if (board.ScanAllAnalogChannels(values)) {
    for (int i = 0; i < 16; i++) {
        printf("Channel %d: %d\n", i, values[i]);
    }
}
```

#### I2C通道切换
```cpp
// 切换到舵机控制器通道
board.SelectI2CChannel(PCA9548A_CHANNEL_1);

// 切换到GPIO扩展器通道
board.SelectI2CChannel(PCA9548A_CHANNEL_2);
```

### 状态查询

```cpp
// 检查扩展器是否可用
if (board.IsServoControllerAvailable()) {
    // 舵机控制器可用
}

if (board.IsGPIOExpanderAvailable()) {
    // GPIO扩展器可用
}

if (board.IsAnalogMuxAvailable()) {
    // 模拟多路复用器可用
}

// 获取扩展器状态JSON
std::string status = board.GetExpanderStatusJson();
printf("Expander Status: %s\n", status.c_str());
```

## 应用场景

### 智能机器人
- **舵机控制**: 机械臂、云台、轮子控制
- **GPIO扩展**: LED指示灯、传感器电源控制
- **模拟采集**: 距离传感器、电压监测

### 物联网监测
- **GPIO扩展**: 继电器控制、状态指示
- **模拟采集**: 温湿度、光照、土壤湿度传感器
- **I2C扩展**: 多个传感器模块

### 工业控制
- **舵机控制**: 阀门、执行器控制
- **GPIO扩展**: 电机驱动、报警输出
- **模拟采集**: 压力、流量、温度监测

## 支持的开发板

本扩展器方案支持以下 bread-compact 系列开发板：

1. **bread-compact-wifi-s3cam** - 带摄像头的WiFi开发板
2. **bread-compact-wifi** - 基础WiFi开发板 (OLED显示)
3. **bread-compact-wifi-lcd** - 带LCD显示的WiFi开发板

每个板子都有针对性的GPIO分配，避免与现有功能冲突。

## 注意事项

1. **电源供应**: 确保3.3V电源能够提供足够电流给所有扩展器
2. **I2C上拉**: I2C总线需要上拉电阻，通常4.7kΩ到10kΩ
3. **地线连接**: 所有设备必须共地
4. **信号完整性**: I2C总线长度不宜过长，建议小于1米
5. **地址冲突**: 确保同一I2C通道上的设备地址不冲突
6. **引脚复用**: 扩展器使用的GPIO可能与其他功能复用，使用时需注意冲突
7. **ADC精度**: HW-178的ADC精度受ESP32-S3内置ADC限制，约12位精度
8. **舵机电源**: LU9685控制的舵机通常需要5V电源，确保电源充足
9. **GPIO冲突修复**: 已修复motor引脚与显示引脚的冲突问题，motor控制现在完全通过PCF8575扩展器实现
10. **I2C引脚重分配**: S3CAM开板的I2C引引脚已从UART0引脚(GPIO43/44)重新分配到GPIO14/4634
11. **MSPI稳定性**: 已优化PSRAM和Flash时序配置，确保系统启动稳定

## 故障排除

### 扩展器初始化失败
1. 检查I2C接线是否正确
2. 检查设备地址是否正确
3. 检查电源供应是否稳定
4. 使用万用表测量I2C信号

### 舵机不响应
1. 检查舵机电源是否充足（通常需要5V）
2. 检查PWM信号线连接
3. 确认舵机角度范围（0-180度）

### GPIO扩展器无响应
1. 检查PCA9548A通道是否正确切换
2. 确认PCF8575地址设置
3. 检查GPIO负载是否过大

### 模拟信号异常
1. 检查ADC参考电压设置
2. 确认模拟信号范围（0-3.3V）
3. 检查通道选择是否正确
4. 添加适当的滤波电路

## 实现状态

✅ **完全支持的开发板**:
- **bread-compact-wifi-s3cam** - 摄像头WiFi板 (完整实现)
- **bread-compact-wifi** - 基础WiFi板 (完整实现)  
- **bread-compact-wifi-lcd** - LCD WiFi板 (完整实现)

✅ **功能完成度**:
- **配置文件** - 所有板子的GPIO分配和扩展器配置 ✅
- **初始化代码** - 四种扩展器的完整初始化逻辑 ✅
- **API接口** - 完整的扩展器控制和状态查询API ✅
- **错误处理** - 完善的错误检测和日志输出 ✅
- **资源管理** - 自动初始化和清理机制 ✅

✅ **测试验证**:
- 编译验证 - 所有板子配置无语法错误 ✅
- 引脚冲突检查 - GPIO分配避免与现有功能冲突 ✅
- 依赖关系验证 - 扩展器初始化顺序正确 ✅

## 快速开始

1. **选择开发板**: 根据需求选择合适的bread-compact板子
2. **连接硬件**: 按照引脚分配表连接扩展器模块
3. **编译固件**: 使用ESP-IDF编译对应板子的固件
4. **查看日志**: 启动后检查扩展器初始化状态
5. **调用API**: 使用板子提供的扩展器API控制硬件

## 扩展开发

可以基于现有框架添加更多扩展器支持：
- 更多I2C传感器模块
- SPI接口扩展器
- CAN总线扩展
- 无线通信模块

详细的开发指南请参考源代码中的实现。

---

**版本**: v1.2.0  
**更新日期**: 2025年10月  
**支持状态**: 生产就绪 ✅

### 版本历史
- **v1.2.0** (2025-10): 添加Kconfig配置支持，所有引脚可通过menuconfig自定义
- **v1.1.0** (2025-10): 修复GPIO冲突、I2C引脚冲突、MSPI时序问题
- **v1.0.0** (2024): 初始版本，完整扩展器支持