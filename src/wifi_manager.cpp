/**
 * @file wifi_manager.cpp
 * @brief WiFi 连接管理模块实现
 *
 * 实现 STA/AP 模式切换逻辑。优先尝试 STA 连接，
 * 失败后自动回退到 AP 模式以保证设备始终可访问。
 */

#include "wifi_manager.h"
#include "config_manager.h"
#include <WiFi.h>

/// 当前是否处于 AP 模式的标志
static bool apMode = false;

// ==================== 模块接口实现 ====================

void WiFiManager::init()
{
    Serial.println("[WiFi] WiFi 管理模块初始化");

    // 从 NVS 读取 WiFi 配置
    WiFiConfig config = ConfigManager::loadWiFiConfig();

    if (config.ssid.isEmpty())
    {
        // 未配置 WiFi，直接启动 AP 模式
        Serial.println("[WiFi] 未找到 WiFi 配置，启动 AP 模式");
        startAP();
        return;
    }

    // 尝试 STA 连接
    if (!connect(config.ssid, config.password))
    {
        // 连接失败，回退到 AP 模式
        Serial.println("[WiFi] STA 连接失败，回退到 AP 模式");
        startAP();
    }
}

bool WiFiManager::connect(const String &ssid, const String &password)
{
    Serial.printf("[WiFi] 正在连接: %s\n", ssid.c_str());

    // 断开当前连接（如果有）
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    // 等待连接，带超时
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - startTime > WIFI_CONNECT_TIMEOUT_MS)
        {
            Serial.printf("[WiFi] 连接超时（%lu ms）\n", WIFI_CONNECT_TIMEOUT_MS);
            return false;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    apMode = false;
    Serial.printf("[WiFi] 连接成功! IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

void WiFiManager::startAP()
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);

    apMode = true;
    Serial.printf("[WiFi] AP 模式已启动, SSID: %s, IP: %s\n",
                  AP_SSID, WiFi.softAPIP().toString().c_str());
}

String WiFiManager::getIP()
{
    if (apMode)
    {
        return WiFi.softAPIP().toString();
    }
    return WiFi.localIP().toString();
}

bool WiFiManager::isAPMode()
{
    return apMode;
}
