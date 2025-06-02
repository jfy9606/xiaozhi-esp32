# 面包板紧凑型WiFi开发板

这是一个基于ESP32的紧凑型开发板，具有WiFi功能，专为面包板使用设计。

## 特性

- 基于ESP32-WROOM-32模块
- 支持WiFi连接
- 引出所有GPIO引脚到面包板友好的接口
- 支持多种摄像头模块(OV2640/OV3660/OV5640)
- 板载LED指示灯
- USB供电和编程接口

## 摄像头支持

开发板支持多种摄像头模块，视觉系统自动检测并适配以下摄像头型号：

- OV2640
- OV3660
- OV5640

摄像头引脚配置已在开发板配置中预设，无需额外配置。

## 视觉功能

开发板支持以下视觉功能：

1. **视频流**：通过Web界面实时查看摄像头画面
2. **拍照**：捕获高分辨率图像
3. **AI识别**：支持物体识别、人脸检测等AI功能
4. **摄像头参数调整**：可调整分辨率、亮度、对比度等参数

## 使用方法

1. 将摄像头模块连接到开发板的摄像头接口
2. 上电启动开发板
3. 连接到开发板创建的WiFi热点或将开发板连接到现有WiFi网络
4. 在浏览器中访问开发板IP地址
5. 点击"视觉"选项卡使用摄像头功能

## 注意事项

- 摄像头和WiFi同时使用时可能会影响WiFi性能，建议使用较低分辨率
- 视觉功能和AI识别可以并行工作，互不干扰
- LED闪光灯可用于低光环境拍摄

## 引脚定义

| 引脚 | 功能 |
|------|------|
| 5V   | 电源正极 |
| GND  | 电源负极 |
| IO0  | Boot/下载模式 |
| IO2  | 板载LED |
| ... | ... |

### 摄像头引脚

| 引脚 | 功能 |
|------|------|
| IO32 | CAMERA_PWDN |
| IO39 | CAMERA_RESET |
| IO27 | CAMERA_XCLK |
| IO35 | CAMERA_SIOD |
| IO17 | CAMERA_SIOC |
| IO34 | CAMERA_Y2 |
| IO36 | CAMERA_Y3 |
| IO21 | CAMERA_Y4 |
| IO19 | CAMERA_Y5 |
| IO18 | CAMERA_Y6 |
| IO5  | CAMERA_Y7 |
| IO25 | CAMERA_Y8 |
| IO22 | CAMERA_Y9 |
| IO26 | CAMERA_VSYNC |
| IO23 | CAMERA_HREF |
| IO33 | CAMERA_PCLK |
| IO4  | CAMERA_LED |

## 硬件连接

摄像头引脚连接方式：

| 功能      | GPIO 引脚 | 说明                    |
|----------|-----------|------------------------|
| PWDN     | -1        | 掉电引脚（不使用）        |
| RESET    | -1        | 复位引脚（不使用）        |
| XCLK     | 15        | 摄像头外部时钟           |
| SIOD     | 4         | I2C 数据引脚            |
| SIOC     | 5         | I2C 时钟引脚            |
| D0 ~ D7  | 11,9,8,10,12,18,17,16 | 数据引脚  |
| VSYNC    | 6         | 垂直同步               |
| HREF     | 7         | 水平同步               |
| PCLK     | 13        | 像素时钟               |
| 闪光灯    | 45        | LED 闪光灯（可选）      |

## 使用说明

1. **初始化**：摄像头会在开发板启动时自动初始化
2. **拍照**：短按触摸按钮可以拍照
3. **API 控制**：
   - 通过 MCP 协议可以控制摄像头参数
   - 可用的 MCP 命令:
     - `self.camera.take_photo`: 拍照
     - `self.camera.flash`: 控制闪光灯
     - `self.camera.set_config`: 设置摄像头参数（亮度、对比度等）

## 注意事项

1. 请确保使用兼容的摄像头模块
2. 摄像头和显示屏共享 I2C 总线，请确保正确配置
3. 高分辨率拍照可能会占用较多内存，建议使用默认 VGA 分辨率

## 代码示例

```cpp
// 获取摄像头实例
Camera* camera = board->GetCamera();
if (camera) {
    // 拍照
    camera->Capture();
    
    // 设置参数
    camera->SetBrightness(1);  // 范围 -2 到 2
    camera->SetContrast(1);    // 范围 -2 到 2
    camera->SetSaturation(0);  // 范围 -2 到 2
    
    // 镜像翻转
    camera->SetHMirror(true);  // 水平镜像
    camera->SetVFlip(true);    // 垂直翻转
}
``` 