# MCP摄像头控制接口文档

## 概述

MCP（Model Context Protocol）摄像头控制接口为AI提供了完整的摄像头操作能力。通过这些接口，AI可以直接控制摄像头进行拍照、调整参数、控制闪光灯等操作。

**注意**：摄像头控制功能仅在 `bread-compact-wifi-s3cam` 开发板上可用。

## 支持的MCP工具

### 1. 拍照工具 (`self.camera.take_photo`)

#### 功能描述
拍摄照片并返回base64编码的图像数据，AI可以直接分析图像内容。

#### 参数说明

| 参数名 | 类型 | 必需 | 默认值 | 范围 | 说明 |
|--------|------|------|--------|------|------|
| `width` | 整数 | 否 | 800 | 96-1600 | 图像宽度（像素） |
| `height` | 整数 | 否 | 600 | 96-1200 | 图像高度（像素） |
| `quality` | 整数 | 否 | 80 | 1-100 | JPEG压缩质量 |

#### 调用示例

**基本拍照**：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.take_photo",
    "arguments": {}
  },
  "id": 1
}
```

**高质量拍照**：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.take_photo",
    "arguments": {
      "width": 1024,
      "height": 768,
      "quality": 95
    }
  },
  "id": 2
}
```

**快速预览**：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.take_photo",
    "arguments": {
      "width": 320,
      "height": 240,
      "quality": 60
    }
  },
  "id": 3
}
```

#### 返回值

成功响应：
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "data:image/jpeg;base64,/9j/4AAQSkZJRgABAQEAYABgAAD..."
      }
    ],
    "isError": false
  }
}
```

错误响应：
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": {
    "code": -32000,
    "message": "Camera not enabled or initialization failed"
  }
}
```

#### 使用场景

1. **物体识别**：AI拍照后分析图像中的物体
2. **文字识别**：拍摄文档或标识进行OCR
3. **环境监控**：定期拍照监控环境变化
4. **用户交互**：响应用户"拍张照片"的请求

### 2. 闪光灯控制 (`self.camera.flash`)

#### 功能描述
控制LED闪光灯的开关和亮度，用于改善拍照光照条件。

#### 参数说明

| 参数名 | 类型 | 必需 | 默认值 | 范围 | 说明 |
|--------|------|------|--------|------|------|
| `enabled` | 布尔 | 是 | - | true/false | 是否启用闪光灯 |
| `intensity` | 整数 | 否 | 50 | 0-100 | 闪光灯强度百分比 |

#### 调用示例

**开启闪光灯**：
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
  "id": 4
}
```

**关闭闪光灯**：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.flash",
    "arguments": {
      "enabled": false
    }
  },
  "id": 5
}
```

#### 返回值

成功响应：
```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "{\"success\": true, \"enabled\": true, \"intensity\": 75}"
      }
    ],
    "isError": false
  }
}
```

#### 使用场景

1. **低光拍照**：在光线不足时自动开启闪光灯
2. **补光拍照**：提高图像质量和清晰度
3. **信号指示**：使用闪光灯作为视觉信号
4. **夜间模式**：夜间拍照时的照明

### 3. 参数配置 (`self.camera.set_config`)

#### 功能描述
调整摄像头的各种参数，优化图像质量和效果。

#### 参数说明

| 参数名 | 类型 | 必需 | 默认值 | 范围 | 说明 |
|--------|------|------|--------|------|------|
| `brightness` | 整数 | 否 | 0 | -2到2 | 亮度调节 |
| `contrast` | 整数 | 否 | 0 | -2到2 | 对比度调节 |
| `saturation` | 整数 | 否 | 0 | -2到2 | 饱和度调节 |
| `special_effect` | 整数 | 否 | 0 | 0-6 | 特殊效果 |
| `wb_mode` | 整数 | 否 | 0 | 0-4 | 白平衡模式 |
| `ae_level` | 整数 | 否 | 0 | -2到2 | 自动曝光级别 |

#### 特殊效果值说明

| 值 | 效果 | 说明 |
|----|------|------|
| 0 | 无效果 | 正常模式 |
| 1 | 负片 | 颜色反转 |
| 2 | 黑白 | 灰度模式 |
| 3 | 红色调 | 红色滤镜 |
| 4 | 绿色调 | 绿色滤镜 |
| 5 | 蓝色调 | 蓝色滤镜 |
| 6 | 复古 | 复古效果 |

#### 白平衡模式说明

| 值 | 模式 | 说明 |
|----|------|------|
| 0 | 自动 | 自动白平衡 |
| 1 | 日光 | 日光模式 |
| 2 | 阴天 | 阴天模式 |
| 3 | 办公室 | 办公室荧光灯 |
| 4 | 家庭 | 家庭白炽灯 |

#### 调用示例

**基本参数调整**：
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
  "id": 6
}
```

**夜间模式配置**：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.set_config",
    "arguments": {
      "brightness": 2,
      "contrast": 1,
      "ae_level": 2,
      "wb_mode": 0
    }
  },
  "id": 7
}
```

**艺术效果配置**：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.set_config",
    "arguments": {
      "special_effect": 2,
      "contrast": 1,
      "saturation": -1
    }
  },
  "id": 8
}
```

#### 返回值

成功响应：
```json
{
  "jsonrpc": "2.0",
  "id": 6,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "{\"success\": true, \"applied_settings\": {\"brightness\": 1, \"contrast\": 0, \"saturation\": 1}}"
      }
    ],
    "isError": false
  }
}
```

#### 使用场景

1. **环境适应**：根据光照条件自动调整参数
2. **用户偏好**：根据用户喜好调整图像效果
3. **特定用途**：为OCR、物体识别等优化参数
4. **艺术创作**：应用特殊效果创造艺术图像

### 4. 摄像头开关 (`self.camera.switch`)

#### 功能描述
启用或禁用摄像头，管理系统资源分配。

#### 参数说明

| 参数名 | 类型 | 必需 | 默认值 | 范围 | 说明 |
|--------|------|------|--------|------|------|
| `enabled` | 布尔 | 是 | - | true/false | 是否启用摄像头 |

#### 调用示例

**启用摄像头**：
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
  "id": 9
}
```

**禁用摄像头**：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.switch",
    "arguments": {
      "enabled": false
    }
  },
  "id": 10
}
```

#### 返回值

启用成功：
```json
{
  "jsonrpc": "2.0",
  "id": 9,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "{\"success\": true, \"message\": \"Camera enabled successfully\", \"current_state\": \"enabled\"}"
      }
    ],
    "isError": false
  }
}
```

禁用成功：
```json
{
  "jsonrpc": "2.0",
  "id": 10,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "{\"success\": true, \"message\": \"Camera disabled successfully\", \"current_state\": \"disabled\"}"
      }
    ],
    "isError": false
  }
}
```

#### 使用场景

1. **节能管理**：不需要时禁用摄像头节省电力
2. **资源优化**：为音频处理释放共享资源
3. **隐私保护**：在敏感场景下禁用摄像头
4. **功能切换**：根据应用需求动态切换功能

### 5. 状态查询 (`self.camera.get_status`)

#### 功能描述
获取摄像头系统的详细状态信息，包括硬件状态、配置参数等。

#### 参数说明
无参数

#### 调用示例

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.get_status",
    "arguments": {}
  },
  "id": 11
}
```

#### 返回值

```json
{
  "jsonrpc": "2.0",
  "id": 11,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "{\"camera_enabled\": true, \"resource_state\": \"camera_active\", \"camera_model\": \"OV2640\", \"initialization_status\": \"success\", \"current_config\": {\"brightness\": 1, \"contrast\": 0, \"saturation\": 1, \"special_effect\": 0, \"wb_mode\": 0, \"ae_level\": 0}, \"flash_status\": {\"enabled\": false, \"intensity\": 50}, \"supported_resolutions\": [\"160x120\", \"320x240\", \"640x480\", \"800x600\", \"1024x768\", \"1280x1024\", \"1600x1200\"]}"
      }
    ],
    "isError": false
  }
}
```

#### 状态字段说明

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `camera_enabled` | 布尔 | 摄像头是否启用 |
| `resource_state` | 字符串 | 资源管理器状态 |
| `camera_model` | 字符串 | 检测到的摄像头型号 |
| `initialization_status` | 字符串 | 初始化状态 |
| `current_config` | 对象 | 当前配置参数 |
| `flash_status` | 对象 | 闪光灯状态 |
| `supported_resolutions` | 数组 | 支持的分辨率列表 |

#### 使用场景

1. **系统诊断**：检查摄像头系统是否正常工作
2. **配置查询**：获取当前配置以便调整
3. **能力发现**：了解摄像头支持的功能和分辨率
4. **故障排除**：诊断摄像头相关问题

## 工作流程示例

### 智能拍照流程

```json
// 1. 检查摄像头状态
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.get_status",
    "arguments": {}
  },
  "id": 1
}

// 2. 如果摄像头未启用，先启用
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.switch",
    "arguments": {
      "enabled": true
    }
  },
  "id": 2
}

// 3. 根据环境调整参数（假设是低光环境）
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.set_config",
    "arguments": {
      "brightness": 2,
      "ae_level": 2
    }
  },
  "id": 3
}

// 4. 开启闪光灯
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.flash",
    "arguments": {
      "enabled": true,
      "intensity": 80
    }
  },
  "id": 4
}

// 5. 拍照
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.take_photo",
    "arguments": {
      "width": 800,
      "height": 600,
      "quality": 85
    }
  },
  "id": 5
}

// 6. 关闭闪光灯
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.flash",
    "arguments": {
      "enabled": false
    }
  },
  "id": 6
}
```

### 节能模式切换

```json
// 1. 禁用摄像头进入节能模式
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.switch",
    "arguments": {
      "enabled": false
    }
  },
  "id": 1
}

// 2. 需要时重新启用
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.switch",
    "arguments": {
      "enabled": true
    }
  },
  "id": 2
}
```

## 错误处理

### 常见错误代码

| 错误代码 | 错误信息 | 原因 | 解决方案 |
|----------|----------|------|----------|
| -32000 | Camera not enabled | 摄像头未启用 | 先调用switch工具启用摄像头 |
| -32001 | Camera initialization failed | 摄像头初始化失败 | 检查硬件连接和配置 |
| -32002 | Invalid parameter | 参数无效 | 检查参数范围和类型 |
| -32003 | Resource conflict | 资源冲突 | 等待资源释放或强制切换 |
| -32004 | Hardware error | 硬件错误 | 检查摄像头硬件状态 |
| -32005 | Memory allocation failed | 内存分配失败 | 释放内存或重启系统 |

### 错误处理最佳实践

1. **状态检查**：操作前先检查摄像头状态
2. **重试机制**：对于临时错误实现重试
3. **资源管理**：及时释放不需要的资源
4. **用户反馈**：提供清晰的错误信息给用户

## 性能优化建议

### 1. 分辨率选择
- **预览用途**：使用320x240或640x480
- **一般拍照**：使用800x600或1024x768
- **高质量拍照**：使用1280x1024或更高

### 2. 质量设置
- **快速预览**：质量50-60
- **一般用途**：质量70-80
- **高质量存储**：质量85-95

### 3. 参数调优
- **室内环境**：适当提高亮度和对比度
- **户外环境**：注意白平衡设置
- **特定用途**：根据识别需求调整参数

### 4. 资源管理
- **按需启用**：只在需要时启用摄像头
- **及时释放**：完成操作后及时释放资源
- **状态监控**：定期检查系统状态

## 安全考虑

### 1. 隐私保护
- 在敏感场景自动禁用摄像头
- 提供明确的摄像头状态指示
- 支持用户手动控制摄像头开关

### 2. 数据安全
- 图像数据仅在内存中处理
- 不自动保存或传输图像
- 支持本地图像处理

### 3. 访问控制
- 验证MCP调用的合法性
- 限制摄像头操作频率
- 记录摄像头使用日志

## 更新历史

### v2.0.0
- 初始版本发布
- 支持基本的拍照和参数控制功能
- 实现摄像头开关和状态查询

### v2.0.1
- 增加闪光灯控制功能
- 优化错误处理机制
- 改进参数验证逻辑

### v2.1.0
- 支持更多摄像头型号
- 增强图像质量控制
- 添加性能优化选项