# 面包板紧凑型WiFi+LCD+摄像头开发板

这是一个基于ESP32-S3的高级开发板，集成了WiFi、LCD显示屏和摄像头功能，专为需要视觉处理能力的AI应用设计。

## 特性

- 基于ESP32-S3模块（支持AI加速）
- 支持WiFi连接
- 内置LCD显示屏
- **增强型摄像头系统**：
  - 支持多种摄像头型号（OV2640、OV3660、OV5640）
  - 自动检测摄像头型号
  - 智能资源管理系统
  - 摄像头开关功能
- **视觉处理集成**：
  - 实时图像处理
  - Web流媒体支持
  - AI视觉分析
- **MCP摄像头控制**：
  - AI可直接控制摄像头
  - 拍照、参数调节、闪光灯控制
- 引出所有GPIO引脚到面包板友好的接口
- USB供电和编程接口

## 重要说明

**摄像头功能集中化**：从v2.0版本开始，所有摄像头和视觉处理功能都集中到此开发板。其他开发板（如 `bread-compact-wifi` 和 `bread-compact-wifi-lcd`）不再支持摄像头功能。

**资源管理**：由于摄像头和音频系统共享某些GPIO资源，本开发板实现了智能资源管理系统，可以动态切换摄像头和音频功能，避免资源冲突。

## 硬件配置

### 摄像头连接

| 功能 | GPIO 引脚 | 说明 |
|------|-----------|------|
| PWDN | 32 | 摄像头电源控制 |
| RESET | -1 | 摄像头复位（未使用） |
| XCLK | 0 | 摄像头时钟 |
| SIOD | 26 | I2C数据线 |
| SIOC | 27 | I2C时钟线 |
| Y9 | 35 | 数据线9 |
| Y8 | 34 | 数据线8 |
| Y7 | 39 | 数据线7 |
| Y6 | 36 | 数据线6 |
| Y5 | 21 | 数据线5 |
| Y4 | 19 | 数据线4 |
| Y3 | 18 | 数据线3 |
| Y2 | 5 | 数据线2 |
| VSYNC | 25 | 垂直同步 |
| HREF | 23 | 水平参考 |
| PCLK | 22 | 像素时钟 |

### LCD显示屏连接

| 功能 | GPIO 引脚 | 说明 |
|------|-----------|------|
| SCK | 14 | SPI时钟 |
| MOSI | 13 | SPI数据 |
| DC | 2 | 数据/命令选择 |
| CS | 15 | 片选 |
| RST | 12 | 复位 |
| BL | 4 | 背光控制 |

### 音频连接

| 功能 | GPIO 引脚 | 说明 |
|------|-----------|------|
| I2S_WS | 42 | 字选择 |
| I2S_SCK | 41 | 串行时钟 |
| I2S_DIN | 2 | 数据输入（麦克风） |
| I2S_DOUT | 1 | 数据输出（扬声器） |

**注意**：由于摄像头占用了大量GPIO引脚，包括ESP32-S3的USB引脚（19、20），在使用摄像头时USB功能会受到影响。

## 摄像头功能详解

### 支持的摄像头型号

1. **OV2640**（默认）
   - 分辨率：最高1600x1200
   - 格式：JPEG、YUV422
   - 特性：低功耗、成本效益高

2. **OV3660**
   - 分辨率：最高2048x1536
   - 格式：JPEG、YUV422、RGB565
   - 特性：更高分辨率、更好的图像质量

3. **OV5640**
   - 分辨率：最高2592x1944
   - 格式：JPEG、YUV422、RGB565
   - 特性：自动对焦、高分辨率

### 摄像头开关功能

开发板支持动态启用/禁用摄像头：

```cpp
// 启用摄像头
camera_resource_manager->SetCameraEnabled(true);

// 禁用摄像头（释放资源给音频使用）
camera_resource_manager->SetCameraEnabled(false);

// 检查摄像头状态
bool enabled = camera_resource_manager->IsCameraEnabled();
```

### 自动检测功能

系统会自动检测连接的摄像头型号：

```cpp
// 获取检测到的摄像头型号
camera_model_t model = enhanced_camera->GetDetectedModel();
```

## MCP摄像头控制接口

AI可以通过以下MCP工具控制摄像头：

### 1. 拍照工具 (`self.camera.take_photo`)

**功能**：拍摄照片并返回base64编码的图像数据

**参数**：
- `width` (可选): 图像宽度，默认800
- `height` (可选): 图像高度，默认600
- `quality` (可选): JPEG质量，范围1-100，默认80

**示例调用**：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.take_photo",
    "arguments": {
      "width": 640,
      "height": 480,
      "quality": 90
    }
  },
  "id": 1
}
```

### 2. 闪光灯控制 (`self.camera.flash`)

**功能**：控制LED闪光灯

**参数**：
- `enabled`: 布尔值，true为开启，false为关闭
- `intensity` (可选): 强度，范围0-100，默认50

**示例调用**：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.flash",
    "arguments": {
      "enabled": true,
      "intensity": 75
    }
  },
  "id": 2
}
```

### 3. 摄像头参数配置 (`self.camera.set_config`)

**功能**：调整摄像头参数

**参数**：
- `brightness` (可选): 亮度，范围-2到2
- `contrast` (可选): 对比度，范围-2到2
- `saturation` (可选): 饱和度，范围-2到2
- `special_effect` (可选): 特效，0-6
- `wb_mode` (可选): 白平衡模式，0-4
- `ae_level` (可选): 自动曝光级别，-2到2

**示例调用**：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.set_config",
    "arguments": {
      "brightness": 1,
      "contrast": 0,
      "saturation": 1
    }
  },
  "id": 3
}
```

### 4. 摄像头开关 (`self.camera.switch`)

**功能**：启用或禁用摄像头

**参数**：
- `enabled`: 布尔值，true为启用，false为禁用

**示例调用**：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.switch",
    "arguments": {
      "enabled": true
    }
  },
  "id": 4
}
```

### 5. 摄像头状态查询 (`self.camera.get_status`)

**功能**：获取摄像头当前状态

**参数**：无

**返回信息**：
- 摄像头是否启用
- 当前摄像头型号
- 资源状态
- 配置参数

**示例调用**：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.get_status",
    "arguments": {}
  },
  "id": 5
}
```

## 视觉处理集成

### Web流媒体

摄像头图像可以通过Web界面实时查看：
- 访问 `http://[设备IP]/camera` 查看实时画面
- 支持MJPEG流格式
- 可调整分辨率和帧率

### AI视觉分析

集成的视觉系统支持：
- 物体检测和识别
- 人脸检测
- 图像分类
- 自定义视觉模型

## 编译和配置

### 快速开始

使用自动化脚本编译：

```bash
python scripts/release.py bread-compact-wifi-s3cam
```

### 手动配置

**1. 配置编译目标为 ESP32-S3：**

```bash
idf.py set-target esp32s3
```

**2. 清理旧配置：**

```bash
idf.py fullclean
```

**3. 打开配置菜单：**

```bash
idf.py menuconfig
```

**4. 选择开发板：**

导航到：`Xiaozhi Assistant` -> `Board Type` -> `面包板新版接线（WiFi）+ LCD + Camera`

**5. 摄像头配置（可选）：**

在 `Camera Configuration` 中可以配置：
- 摄像头型号选择
- 是否启用自动检测
- 默认分辨率和质量设置
- 资源管理选项

**6. 编译和烧录：**

```bash
idf.py build flash monitor
```

## 使用指南

### 基本操作

1. **启动设备**：上电后设备会自动初始化摄像头系统
2. **连接WiFi**：通过Web界面或按钮配置WiFi连接
3. **访问Web界面**：在浏览器中输入设备IP地址
4. **摄像头控制**：通过Web界面或MCP命令控制摄像头

### 按键功能

- **Boot按键**：短按进入/退出聊天模式，长按重置WiFi配置
- **触摸按键**（如果有）：按住开始语音识别

### 资源管理

由于摄像头和音频共享资源，系统会智能管理：

- **摄像头优先模式**：摄像头启用时，音频功能受限
- **音频优先模式**：摄像头禁用时，音频功能完全可用
- **自动切换**：根据应用需求自动切换资源分配

### 故障排除

**摄像头无法初始化**：
1. 检查摄像头连接是否正确
2. 确认摄像头型号是否支持
3. 检查GPIO引脚配置
4. 查看串口日志获取详细错误信息

**图像质量问题**：
1. 调整摄像头参数（亮度、对比度等）
2. 检查光照条件
3. 尝试不同的分辨率设置

**资源冲突**：
1. 确保同时只启用摄像头或音频功能
2. 使用摄像头开关功能切换资源分配
3. 检查资源管理器状态

## 开发参考

### 添加自定义摄像头功能

```cpp
#include "camera/camera_components.h"

// 获取摄像头组件
auto* enhanced_camera = CameraComponentFactory::GetEnhancedCamera();
auto* resource_manager = CameraComponentFactory::GetResourceManager();

// 自定义拍照功能
if (resource_manager->IsCameraEnabled()) {
    camera_fb_t* fb = enhanced_camera->TakePhoto();
    if (fb) {
        // 处理图像数据
        ProcessImage(fb->buf, fb->len);
        enhanced_camera->ReturnFrameBuffer(fb);
    }
}
```

### 集成自定义视觉算法

```cpp
#include "camera/vision_integration.h"

// 获取视觉集成组件
auto* vision_integration = CameraComponentFactory::GetVisionIntegration();

// 注册自定义视觉处理回调
vision_integration->RegisterProcessingCallback([](camera_fb_t* fb) {
    // 自定义视觉处理逻辑
    return ProcessCustomVision(fb);
});
```

## 技术规格

- **处理器**：ESP32-S3 (双核 Xtensa LX7, 240MHz)
- **内存**：512KB SRAM, 384KB ROM
- **Flash**：4MB/8MB/16MB (根据具体型号)
- **WiFi**：802.11 b/g/n (2.4GHz)
- **摄像头接口**：DVP (Digital Video Port)
- **显示接口**：SPI
- **音频接口**：I2S
- **工作电压**：3.3V
- **工作温度**：-40°C ~ +85°C

## 更新日志

### v2.0
- 集成所有摄像头和视觉功能
- 新增多摄像头型号支持
- 实现智能资源管理系统
- 添加MCP摄像头控制接口
- 优化视觉处理性能

### v1.0
- 基础摄像头功能
- LCD显示支持
- WiFi连接功能