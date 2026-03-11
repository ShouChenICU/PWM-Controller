# PWM Controller - 基于 ESP32-C3 的 Web PWM 控制器

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Build Status](https://img.shields.io/github/actions/workflow/status/ShouChenICU/PWM-Controller/build.yml?branch=main)](https://github.com/ShouChenICU/PWM-Controller/actions)
[![GitHub stars](https://img.shields.io/github/stars/ShouChenICU/PWM-Controller.svg)](https://github.com/ShouChenICU/PWM-Controller/stargazers)

这是一个专为 **ESP32-C3** 设计的开源 PWM 控制器项目。它通过极简的 Web 界面提供友好的交互体验，支持实时调节占空比、查看风扇转速，并管理多个 PWM 设备。特别适合用于 DIY 机箱散热控制、智能家居通风系统等场景。

## ✨ 主要特性

- 🚀 **高性能 PWM**：默认 25kHz 频率，专为 4 线 PC 风扇优化，消除电机啸叫。
- 📊 **转速监测**：实时读取风扇 Tacho (RPM) 信号。采用**双时间戳抗混叠去抖** + **中位值滤波**算法，有效抑制 PWM 信号耦合干扰，读数稳定准确。
- 🔧 **设备管理**：支持动态添加、修改、删除多个设备，引脚灵活配置。
- 🔄 **信号反转**：支持 PWM 信号反序，适配特殊驱动电路。
- 💾 **掉电保存**：设备配置与 WiFi 参数持久化存储在 NVS 中。
- 🌐 **双模式 WiFi**：支持 STA 模式连接家中的 WiFi，连接失败则自动开启 AP 热点 (`PWM-Controller`)。
- 📱 **响应式 UI**：移动端优先设计，支持黑暗/明亮感官体验。

## 🛠️ 硬件连接

### 典型 4 线风扇接线建议

| 风扇线色/功能 | ESP32-C3 引脚 (示例) | 说明                 |
| :------------ | :------------------- | :------------------- |
| VCC (12V)     | 外接 12V 电源正极    | **切勿**直接接 ESP32 |
| GND           | ESP32 GND            | 共地连接             |
| PWM           | GPIO 1 (可配置)      | 默认 25kHz 频率      |
| Tacho (RPM)   | GPIO 2 (可配置)      | 支持转速反馈         |

> [!CAUTION]
> 请确保 ESP32 和 12V 电源共地 (GND)，但**绝不能**将 12V 直接接入 ESP32 的任何引脚。

## 🚀 快速开始

### 编译与烧录 (PlatformIO)

本项目使用 [PlatformIO](https://platformio.org/) 构建。

1.  克隆仓库：
    ```bash
    git clone https://github.com/ShouChenICU/PWM-Controller.git
    cd PWM-Controller
    ```
2.  在 VS Code 中打开项目。
3.  点击 **Build** (✔️) 编译固件。
4.  连接 ESP32-C3，点击 **Upload** (➡️) 烧录代码。
5.  点击 **Upload Filesystem Image** 将 `data/` 目录下的 Web 资源上传至 LittleFS，或执行`pio run --target uploadfs`。

### 初次配置

1.  上电后，若未配置 WiFi，设备将开启名为 `PWM-Controller` 的无密码热点。
2.  手机/电脑连接热点，访问 `http://192.168.4.1`。
3.  在“设置”页面配置您的 WiFi SSID 和密码。
4.  在主界面点击“添加设备”，配置风扇的名称及对应的 PWM/RPM 引脚。

## 📡 API 文档

后端提供 RESTful API 接口，可用于自动化集成：

| 路径                     | 方法   | 描述                             |
| :----------------------- | :----- | :------------------------------- |
| `/api/devices`           | GET    | 获取所有设备列表及实时转速       |
| `/api/devices`           | POST   | 添加新设备                       |
| `/api/devices/{id}`      | PUT    | 更新指定设备配置                 |
| `/api/devices/{id}`      | DELETE | 删除指定设备                     |
| `/api/devices/{id}/duty` | POST   | 实时设置占空比 (0-100)           |
| `/api/devices/save`      | POST   | 持久化当前设备配置到 NVS         |
| `/api/wifi`              | POST   | 更新 WiFi 配置并重启设备         |
| `/api/system/info`       | GET    | 获取 IP 地址、运行时间等系统信息 |

## 🏗️ 项目结构

- `src/`: C++ 源代码 (Arduino 框架)。
- `data/`: Web 静态资源 (HTML/CSS/JS)。
- `platformio.ini`: 项目配置文件。

## 📄 开源协议

本项目采用 **[MIT](LICENSE)** 协议开源。

## 🤝 贡献与反馈

欢迎提交 PR 或在 [Issue](https://github.com/ShouChenICU/PWM-Controller/issues) 中反馈 bug。

- **作者**: [ShouChen](https://github.com/ShouChenICU)
- **开源地址**: [https://github.com/ShouChenICU/PWM-Controller](https://github.com/ShouChenICU/PWM-Controller)

---

_Made with ❤️ for the open source community._
