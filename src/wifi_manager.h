/**
 * @file wifi_manager.h
 * @brief 非阻塞 WiFi STA/AP 状态机
 */
#pragma once

#include <Arduino.h>

namespace WiFiManager
{
    /** 初始化 AP，并在存在已保存配置时异步尝试 STA。 */
    void init();

    /** 推进非阻塞连接与断线恢复状态机。 */
    void update();

    /** 请求连接新 WiFi；连接成功后才持久化。 */
    bool requestConnect(const String &ssid, const String &password);

    /** 启动或保持救援 AP。 */
    bool startAP();

    /** 获取当前可访问 IP。 */
    String getIP();

    /** 当前是否开启 AP。 */
    bool isAPMode();

    /** 获取连接状态：ap、connecting、connected、failed。 */
    String getState();

    /** 获取实际 AP SSID。 */
    String getAPSSID();
} // namespace WiFiManager
