/**
 * @file wifi_manager.h
 * @brief WiFi 连接管理模块
 *
 * 负责 WiFi 连接管理，支持 STA 模式（连接到路由器）
 * 和 AP 模式（作为热点，当无法连接 WiFi 时自动切换）。
 */
#pragma once

#include <Arduino.h>

// ==================== 常量定义 ====================

/// AP 模式默认 SSID
static const char *AP_SSID = "PWM-Controller";

/// AP 模式默认密码（空字符串表示无密码）
static const char *AP_PASS = "";

/// STA 模式连接超时时间（毫秒）
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

// ==================== 模块接口 ====================

namespace WiFiManager
{

    /**
     * @brief 初始化 WiFi 管理模块并尝试连接
     *
     * 从 NVS 读取 WiFi 配置，尝试以 STA 模式连接。
     * 如果连接失败或未配置，则自动切换到 AP 模式。
     */
    void init();

    /**
     * @brief 尝试使用指定配置连接 WiFi
     * @param ssid WiFi SSID
     * @param password WiFi 密码
     * @return true 连接成功，false 连接失败（会自动启动 AP）
     */
    bool connect(const String &ssid, const String &password);

    /**
     * @brief 启动 AP 模式
     *
     * 以无密码热点方式启动，供用户连接配置 WiFi。
     */
    void startAP();

    /**
     * @brief 获取当前 IP 地址字符串
     * @return 当前 IP 地址（STA 或 AP 模式的 IP）
     */
    String getIP();

    /**
     * @brief 当前是否处于 AP 模式
     * @return true 处于 AP 模式，false 处于 STA 模式
     */
    bool isAPMode();

} // namespace WiFiManager
