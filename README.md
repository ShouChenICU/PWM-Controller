# PWM Controller - 基于 ESP32-C3 的 Web PWM 控制器

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Arduino](https://img.shields.io/badge/Arduino-ESP32--C3-00878F?logo=arduino&logoColor=white)](https://www.arduino.cc/)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-pioarduino-F5822A?logo=platformio&logoColor=white)](https://platformio.org/)
[![GitHub stars](https://img.shields.io/github/stars/ShouChenICU/PWM-Controller.svg)](https://github.com/ShouChenICU/PWM-Controller/stargazers)

这是一个专为 **ESP32-C3** 设计的开源 PWM 控制器项目。它通过极简的 Web 界面提供友好的交互体验，支持实时调节占空比、查看风扇转速，并管理多个 PWM 设备。特别适合用于 DIY 机箱散热控制、智能家居通风系统等场景。

## ✨ 主要特性

- 🚀 **高性能 PWM**：默认 25kHz 频率，专为 4 线 PC 风扇优化，消除电机啸叫。
- 📊 **转速监测**：实时读取风扇 Tacho (RPM) 信号。采用按 PPR 动态限频、停转复位、中位值滤波和 EMA 平滑，并在设备卡片展示最近 2 分钟的转速趋势。
- 🔧 **设备管理**：支持最多 6 路独立 PWM，动态添加、修改、删除并自动校验引脚冲突。
- 🔄 **信号反转**：支持 PWM 信号反序，适配特殊驱动电路。
- 💾 **掉电保存**：设备配置与 WiFi 参数持久化存储在 NVS 中。
- 🌐 **双模式 WiFi**：非阻塞 STA/AP 状态机；重配和断线时保留带密码的救援热点。
- 📱 **响应式 UI**：移动端优先设计，支持黑暗/明亮感官体验。
- 🏷️ **固件版本**：标题栏显示固件版本，由 `platformio.ini` 统一配置。

## 🛠️ 硬件连接

### 典型 4 线风扇接线建议

| 风扇线色/功能 | ESP32-C3 引脚 (示例) | 说明                 |
| :------------ | :------------------- | :------------------- |
| VCC (12V)     | 外接 12V 电源正极    | **切勿**直接接 ESP32 |
| GND           | ESP32 GND            | 共地连接             |
| PWM           | 外部 NPN/NMOS 驱动   | GPIO1 等输出 25kHz   |
| Tacho (RPM)   | GPIO3（示例）        | 外部上拉到 3.3V      |

> [!CAUTION]
> 请确保 ESP32 和 12V 电源共地 (GND)，但**绝不能**将 12V 直接接入 ESP32 的任何引脚。

### PWM 与转速接口要求

- 标准 4 线风扇 PWM 输入可能内部上拉到 5V，ESP32-C3 GPIO 并非 5V 容忍。
  请使用 NPN 三极管或小信号 NMOS 构成开集电极/开漏驱动，并配置基极/栅极下拉电阻，
  不要把风扇 PWM 线直接接到 ESP32。
- 外部低端驱动会反相信号，应在设备配置中勾选“外部驱动反相”。
- Tach 通常是开集电极输出，建议使用 4.7kΩ～10kΩ 电阻上拉到 ESP32 的 3.3V，
  禁止上拉到 12V。默认按每转 2 个脉冲计算，也可在页面配置为 1～8。
- PWM 默认允许 GPIO0、1、3～8、10、18、19；RPM 额外允许 GPIO2 输入。
  GPIO9 影响下载模式、GPIO20/21 用于串口，因此固件仍会拒绝这些必要风险引脚。
- GPIO8 同时连接板载 RGB LED，作为 PWM/RPM 使用时板载灯可能异常闪烁；
  GPIO2 仅建议作为外部上拉到 3.3V 的 RPM 输入。
- GPIO18/19 默认承担 USB-JTAG；将其用于 PWM/RPM 会关闭对应 USB-JTAG 功能，
  如依赖原生 USB 调试，请优先使用其他允许引脚。
- ESP32-C3 只有 6 个 LEDC 通道，所以最多支持 6 路独立 PWM。

## 🚀 快速开始

### 编译与烧录 (PlatformIO)

本项目使用 [PlatformIO](https://platformio.org/) 构建。

1. 克隆仓库：

    ```bash
    git clone https://github.com/ShouChenICU/PWM-Controller.git
    cd PWM-Controller
    ```

2. 在 VS Code 中打开项目。
3. 点击 **Build** (✔️) 编译固件。
4. 连接 ESP32-C3，点击 **Upload** (➡️) 烧录代码。
5. 点击 **Upload Filesystem Image** 将 `data/` 目录下的 Web 资源上传至 LittleFS，或执行`pio run --target uploadfs`。

### 初次配置

1. 上电后会开启 `PWM-Controller-XXXXXX` 救援热点，默认密码为 `pwm-controller`。
2. 手机/电脑连接热点，访问 `http://192.168.4.1`。
3. 浏览器认证用户名为 `admin`、密码为 `pwm-controller`；认证覆盖页面和全部 API。
4. 在“设置”页面修改网页登录凭据，并配置您的 WiFi SSID 和密码。
5. 在主界面点击“添加设备”，配置风扇的名称及对应的 PWM/RPM 引脚。

> 摘要认证可阻止一般的局域网未授权操作，但 HTTP 本身不提供传输加密；
> 不要把该管理端口直接暴露到互联网。首次登录后建议立即在“设置”中修改默认凭据。

## 📡 API 文档

后端提供 RESTful API 接口，可用于自动化集成：

| 路径                     | 方法   | 描述                             |
| :----------------------- | :----- | :------------------------------- |
| `/api/devices`           | GET    | 获取所有设备列表及实时转速       |
| `/api/devices`           | POST   | 添加新设备并持久化               |
| `/api/devices/{id}`      | PUT    | 更新指定设备配置并持久化         |
| `/api/devices/{id}`      | DELETE | 删除设备并持久化                 |
| `/api/devices/{id}/duty` | POST   | 实时设置占空比 (0-100)           |
| `/api/devices/save`      | POST   | 持久化当前设备配置到 NVS         |
| `/api/wifi`              | POST   | 后台验证新 WiFi，成功后持久化    |
| `/api/system/auth`       | GET    | 获取当前网页登录用户名           |
| `/api/system/auth`       | POST   | 更新网页登录凭据并重启           |
| `/api/system/info`       | GET    | 获取 IP 地址、运行时间等系统信息 |
| `/api/system/reset`      | POST   | 清空设备、WiFi 与登录配置并重启  |

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
