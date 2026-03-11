/**
 * @file main.cpp
 * @brief PWM Controller 主程序入口
 *
 * 负责初始化各功能模块并启动系统。
 * 初始化顺序：串口 → NVS配置 → 设备管理 → WiFi → Web服务器
 *
 * 设备管理优先于 WiFi 初始化，确保 PWM 信号在 WiFi 连接等待期间已输出，
 * 避免风扇等设备因缺少 PWM 信号而出现异常行为。
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
  delay(100); // 等待串口稳定
  Serial.println("\n========== PWM Controller 启动 ==========");

  // 1. 初始化 NVS 配置管理
  ConfigManager::init();

  // 2. 优先初始化设备管理（从 NVS 加载设备并立即输出 PWM 信号）
  //    必须在 WiFi 连接之前完成，防止风扇等设备因等待 WiFi 期间
  //    缺少 PWM 信号而进入异常状态（如全速或失控）
  DeviceManager::init();

  // 3. 初始化 WiFi（自动尝试 STA，失败回退 AP）
  //    STA 连接可能阻塞数秒，PWM 已提前输出，设备状态正常
  WiFiManager::init();

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
