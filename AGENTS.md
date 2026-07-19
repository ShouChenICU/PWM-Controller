# PWM Controller 项目 - Copilot 编程指令

## 项目概述

本项目是一个基于 ESP32-C3 的 Web PWM 控制器，通过浏览器页面控制 PWM 输出，用于驱动 PWM 风扇等设备。

## 技术栈

- **硬件平台**: ESP32-C3 (Arduino 框架)
- **构建工具**: PlatformIO
- **HTTP 服务**: ESPAsyncWebServer
- **文件系统**: LittleFS（存放 Web 静态资源）
- **持久化存储**: NVS（存储设备列表、WiFi 配置等）
- **前端**: 原生 HTML/CSS/JS，移动端优先响应式设计

## 代码规范

### 通用规范

- 所有代码注释使用**中文**
- 函数、变量命名使用**英文**，遵循 camelCase 风格
- 常量使用 UPPER_SNAKE_CASE 风格
- 每个模块（.h/.cpp）职责单一，文件头部写明模块用途
- 函数应有简要注释说明功能、参数和返回值
- 避免魔法数字，使用常量或宏定义

### C++ 代码规范

- 头文件使用 `#pragma once` 防重复包含
- 类和结构体使用 PascalCase 命名
- 模块化设计：配置管理、WiFi 管理、设备管理、Web 服务各自独立
- 优先使用 ArduinoJson 进行 JSON 序列化/反序列化
- 串口日志使用 `Serial.printf()` 格式化输出，便于调试
- 日志格式统一为 `[模块名] 消息内容`，如 `[WiFi] 连接成功`

### 前端代码规范

- HTML/CSS/JS 分离，各司其职
- CSS 采用移动端优先的响应式设计（Mobile First）
- JS 使用 fetch API 与后端 RESTful 接口通信
- 页面状态变化应有视觉反馈（加载动画、提示消息等）
- 所有用户可见文本使用中文

## 架构设计

### 模块划分

```
src/
├── main.cpp           // 主程序入口，初始化各模块并启动
├── config_manager.h/cpp  // NVS 配置管理（设备列表、WiFi 配置读写）
├── wifi_manager.h/cpp    // WiFi 连接管理（STA/AP 模式切换）
├── device_manager.h/cpp  // 设备管理（PWM 输出控制、转速读取）
└── web_server.h/cpp      // Web 服务器（静态资源托管、RESTful API）

data/
├── index.html         // 首页（设备卡片列表）
├── style.css          // 全局样式
└── app.js             // 前端逻辑
```

### 数据模型

```
设备 (Device):
- id: uint8_t          // 设备唯一ID
- name: String          // 设备名称
- pwmPin: uint8_t       // PWM 输出引脚
- rpmPin: int8_t        // 转速读取引脚（-1 表示无）
- dutyCycle: uint8_t    // 当前占空比 (0-100)，默认 10%
- inverted: bool        // 是否反转 PWM 信号
- rpm: uint16_t         // 当前转速（仅运行时有效）
```

### API 设计

- `GET  /api/devices`        — 获取所有设备列表
- `POST /api/devices`        — 添加新设备（含 inverted 标志）
- `PUT  /api/devices/{id}`   — 更新设备配置（含 inverted 标志）
- `DELETE /api/devices/{id}` — 删除设备
- `POST /api/devices/{id}/duty` — 设置占空比（仅内存）
- `POST /api/devices/save`   — 持久化所有设备配置到 NVS
- `GET  /api/wifi`           — 获取当前 WiFi 配置
- `POST /api/wifi`           — 更新 WiFi 配置并重连
- `GET  /api/system/info`    — 获取系统信息（IP、运行时间等）

### NVS 存储结构

- 命名空间 `pwm_ctrl`：
  - `wifi_ssid`: WiFi SSID
  - `wifi_pass`: WiFi 密码
  - `dev_count`: 设备数量
  - `dev_{id}`: 各设备的 JSON 配置

## 注意事项

- ESP32-C3 可用 GPIO 有限，注意引脚冲突
- PWM 频率建议使用 25kHz（适合 4 线 PWM 风扇）
- 转速读取使用中断计数方式，注意 ISR 中不可使用阻塞操作
- Web 页面文件通过 `pio run -t uploadfs` 上传到 LittleFS
- NVS 空间有限，设备数据应紧凑存储
