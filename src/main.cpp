/**
 * @file main.cpp
 * @brief PWM Controller 主程序入口
 *
 * 负责初始化各功能模块并启动系统。
 * 初始化顺序：串口 → NVS配置 → WiFi → 设备管理 → Web服务器
 */

#include <Arduino.h>
#include "config_manager.h"
#include "wifi_manager.h"
#include "device_manager.h"
#include "web_server.h"

/// 串口波特率
static const uint32_t SERIAL_BAUD_RATE = 115200;

void setup()
{
  // 初始化串口
  Serial.begin(SERIAL_BAUD_RATE);
  delay(500); // 等待串口稳定
  Serial.println("\n========== PWM Controller 启动 ==========");

  // 1. 初始化 NVS 配置管理
  ConfigManager::init();

  // 2. 初始化 WiFi（自动尝试 STA，失败回退 AP）
  WiFiManager::init();

  // 3. 初始化设备管理（从 NVS 加载设备并启动 PWM）
  DeviceManager::init();

  // 4. 初始化并启动 Web 服务器
  WebServer::init();

  Serial.println("========== 系统初始化完成 ==========");
  Serial.printf("[System] IP 地址: %s\n", WiFiManager::getIP().c_str());
  Serial.printf("[System] 模式: %s\n", WiFiManager::isAPMode() ? "AP" : "STA");
}

void loop()
{
  // 周期性更新设备状态（转速计算等）
  DeviceManager::update();
}
