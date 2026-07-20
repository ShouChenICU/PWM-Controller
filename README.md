# PWM Controller - ESP32-C3 Web PWM Controller

English | [简体中文](README.zh-CN.md)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Arduino](https://img.shields.io/badge/Arduino-ESP32--C3-00878F?logo=arduino&logoColor=white)](https://www.arduino.cc/)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-pioarduino-F5822A?logo=platformio&logoColor=white)](https://platformio.org/)
[![GitHub stars](https://img.shields.io/github/stars/ShouChenICU/PWM-Controller.svg)](https://github.com/ShouChenICU/PWM-Controller/stargazers)

An open-source PWM controller designed for the **ESP32-C3**. Its responsive web interface lets you adjust duty cycles in real time, monitor fan speed, and manage multiple PWM devices. It is suitable for DIY PC cooling, home ventilation, and similar projects.

## ✨ Features

- 🚀 **High-frequency PWM**: Uses 25 kHz by default, optimized for 4-wire PC fans to avoid audible motor noise.
- 📊 **RPM monitoring**: Reads fan tachometer signals in real time, with PPR-aware pulse filtering, stall reset, median filtering, EMA smoothing, and a two-minute RPM trend chart on each device card.
- 🔧 **Device management**: Manages up to six independent PWM outputs with add, edit, delete, persistence, and GPIO conflict validation.
- 🔄 **Signal inversion**: Supports inverted PWM for external low-side driver circuits.
- 💾 **Persistent settings**: Stores device, WiFi, and web login settings in NVS.
- 🌐 **Resilient WiFi**: Uses a non-blocking STA/AP state machine and keeps a password-protected rescue AP available while reconnecting.
- 🌍 **Bilingual UI**: English is available at `/`; Simplified Chinese is available at `/zh`.
- 📱 **Responsive UI**: Mobile-first layout with light and dark system appearance support.
- 🏷️ **Firmware version**: Displays the firmware version configured in `platformio.ini`.

## 🛠️ Hardware Connections

### Typical 4-wire fan wiring

| Fan wire/function | ESP32-C3 connection (example) | Notes |
| :---------------- | :---------------------------- | :---- |
| VCC (12 V) | External 12 V supply | **Never** connect directly to the ESP32 |
| GND | ESP32 GND | The grounds must be shared |
| PWM | External NPN/NMOS driver | GPIO1 or another allowed 25 kHz output |
| Tacho (RPM) | GPIO3 (example) | External pull-up to 3.3 V |

> [!CAUTION]
> The ESP32 and the 12 V supply must share a ground, but 12 V must **never** be connected to an ESP32 GPIO.

### PWM and tachometer requirements

- A standard 4-wire fan PWM input may be internally pulled up to 5 V. ESP32-C3 GPIOs are not 5 V tolerant. Use an NPN transistor or small-signal NMOS as an open-collector/open-drain driver with a suitable base/gate pull-down resistor. Do not connect the fan PWM wire directly to the ESP32.
- A low-side external driver inverts the signal. Enable **External driver inversion** in the device settings.
- Tachometer outputs are normally open collector. Use a 4.7 kΩ to 10 kΩ pull-up to 3.3 V, never to 12 V. The default is two pulses per revolution and can be configured from 1 to 8.
- PWM allows GPIO0, 1, 3-8, 10, 18, and 19. RPM input additionally allows GPIO2.
- GPIO9 affects download mode, while GPIO20/21 are used by the serial port; the firmware rejects these risk-sensitive pins.
- GPIO8 is also connected to the onboard RGB LED, so using it for PWM/RPM may make the LED flicker. GPIO2 should only be used as an RPM input with an external 3.3 V pull-up.
- GPIO18/19 normally provide USB-JTAG. Using either for PWM/RPM disables the corresponding USB-JTAG function, so use other pins if native USB debugging is required.
- The ESP32-C3 has six LEDC channels and therefore supports up to six independent PWM outputs.

## 🚀 Getting Started

### Build and upload with PlatformIO

This project uses [PlatformIO](https://platformio.org/).

1. Clone the repository:

    ```bash
    git clone https://github.com/ShouChenICU/PWM-Controller.git
    cd PWM-Controller
    ```

2. Open the project in VS Code.
3. Select **Build** (✔️) to compile the firmware.
4. Connect the ESP32-C3 and select **Upload** (➡️).
5. Select **Upload Filesystem Image** to upload the web assets from `data/` to LittleFS, or run:

    ```bash
    pio run --target uploadfs
    ```

> [!IMPORTANT]
> Firmware and LittleFS are uploaded separately. After UI or translation changes, upload the filesystem image again.

### First-time setup

1. On startup, the controller creates a `PWM-Controller-XXXXXX` rescue access point with the default password `pwm-controller`.
2. Connect a phone or computer to it and open `http://192.168.4.1` for English or `http://192.168.4.1/zh` for Simplified Chinese.
3. Sign in with username `admin` and password `pwm-controller`. Digest authentication protects the page and every API endpoint.
4. Open **System Settings** to change the web login credentials and configure a WiFi SSID and password.
5. Select **Add Device** and enter the fan name and its PWM/RPM pins.

> Digest authentication prevents typical unauthorized access on a trusted LAN, but HTTP does not encrypt traffic. Do not expose the management port directly to the internet, and change the default credentials after the first sign-in.

## 📡 API

The controller provides RESTful endpoints for automation. JSON status responses include a stable `code` and an English `message` or `error` string.

| Path | Method | Description |
| :--- | :----- | :---------- |
| `/api/devices` | GET | List devices and live RPM values |
| `/api/devices` | POST | Add and persist a device |
| `/api/devices/{id}` | PUT | Update and persist a device |
| `/api/devices/{id}` | DELETE | Delete and persist a device |
| `/api/devices/{id}/duty` | POST | Set the live duty cycle (0-100) |
| `/api/devices/save` | POST | Persist all current device settings to NVS |
| `/api/wifi` | POST | Validate new WiFi credentials in the background and persist them on success |
| `/api/system/auth` | GET | Get the current web login username |
| `/api/system/auth` | POST | Update web login credentials and restart |
| `/api/system/info` | GET | Get the IP address, uptime, firmware version, and other system information |
| `/api/system/reset` | POST | Clear device, WiFi, and login settings, then restart |

## 🏗️ Project Structure

- `src/`: C++ firmware sources using the Arduino framework.
- `data/`: HTML, CSS, JavaScript, and translation assets stored in LittleFS.
- `platformio.ini`: PlatformIO build configuration and project version.

## 📄 License

Licensed under the **[MIT License](LICENSE)**.

## 🤝 Contributing and Feedback

Pull requests and bug reports are welcome through [GitHub Issues](https://github.com/ShouChenICU/PWM-Controller/issues).

- **Author**: [ShouChen](https://github.com/ShouChenICU)
- **Repository**: [https://github.com/ShouChenICU/PWM-Controller](https://github.com/ShouChenICU/PWM-Controller)

---

_Made with ❤️ for the open source community._
