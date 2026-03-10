/**
 * @file config_manager.cpp
 * @brief NVS 配置管理模块实现
 *
 * 使用 ESP32 的 Preferences 库操作 NVS，
 * 设备数据以 JSON 字符串形式存储。
 */

#include "config_manager.h"
#include <Preferences.h>
#include <ArduinoJson.h>

/// NVS 操作实例（模块内部使用）
static Preferences prefs;

// ==================== 初始化 ====================

void ConfigManager::init()
{
    Serial.println("[Config] NVS 配置管理模块初始化");
    // Preferences 在每次读写时打开/关闭，无需全局初始化
}

// ==================== WiFi 配置 ====================

WiFiConfig ConfigManager::loadWiFiConfig()
{
    WiFiConfig config;

    // 以只读模式打开 NVS 命名空间
    if (!prefs.begin(NVS_NAMESPACE, true))
    {
        Serial.println("[Config] 打开 NVS 失败（读取WiFi）");
        return config;
    }

    config.ssid = prefs.getString(NVS_KEY_WIFI_SSID, "");
    config.password = prefs.getString(NVS_KEY_WIFI_PASS, "");
    prefs.end();

    Serial.printf("[Config] 读取WiFi配置: SSID=%s\n", config.ssid.c_str());
    return config;
}

bool ConfigManager::saveWiFiConfig(const WiFiConfig &config)
{
    // 以读写模式打开 NVS 命名空间
    if (!prefs.begin(NVS_NAMESPACE, false))
    {
        Serial.println("[Config] 打开 NVS 失败（保存WiFi）");
        return false;
    }

    prefs.putString(NVS_KEY_WIFI_SSID, config.ssid);
    prefs.putString(NVS_KEY_WIFI_PASS, config.password);
    prefs.end();

    Serial.printf("[Config] WiFi配置已保存: SSID=%s\n", config.ssid.c_str());
    return true;
}

// ==================== 设备配置 ====================

std::vector<DeviceConfig> ConfigManager::loadDevices()
{
    std::vector<DeviceConfig> devices;

    if (!prefs.begin(NVS_NAMESPACE, true))
    {
        Serial.println("[Config] 打开 NVS 失败（读取设备）");
        return devices;
    }

    // 读取设备数量
    uint8_t count = prefs.getUChar(NVS_KEY_DEV_COUNT, 0);
    Serial.printf("[Config] NVS 中存储了 %d 个设备\n", count);

    for (uint8_t i = 0; i < count; i++)
    {
        // 构建设备键名：dev_0, dev_1, ...
        String key = String(NVS_KEY_DEV_PREFIX) + String(i);
        String json = prefs.getString(key.c_str(), "");

        if (json.isEmpty())
        {
            Serial.printf("[Config] 设备 %d 数据为空，跳过\n", i);
            continue;
        }

        // 解析 JSON 字符串
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, json);
        if (err)
        {
            Serial.printf("[Config] 设备 %d JSON 解析失败: %s\n", i, err.c_str());
            continue;
        }

        // 填充设备配置结构体
        DeviceConfig dev;
        dev.id = doc["id"] | 0;
        dev.name = doc["name"] | "未命名";
        dev.pwmPin = doc["pwmPin"] | 0;
        dev.rpmPin = doc["rpmPin"] | -1;
        dev.dutyCycle = doc["duty"] | DEFAULT_DUTY_CYCLE;

        devices.push_back(dev);
        Serial.printf("[Config] 加载设备: id=%d, name=%s, pwm=%d, rpm=%d, duty=%d%%\n",
                      dev.id, dev.name.c_str(), dev.pwmPin, dev.rpmPin, dev.dutyCycle);
    }

    prefs.end();
    return devices;
}

bool ConfigManager::saveDevices(const std::vector<DeviceConfig> &devices)
{
    if (!prefs.begin(NVS_NAMESPACE, false))
    {
        Serial.println("[Config] 打开 NVS 失败（保存设备）");
        return false;
    }

    // 先清除旧的设备数据（防止残留）
    uint8_t oldCount = prefs.getUChar(NVS_KEY_DEV_COUNT, 0);
    for (uint8_t i = 0; i < oldCount; i++)
    {
        String key = String(NVS_KEY_DEV_PREFIX) + String(i);
        prefs.remove(key.c_str());
    }

    // 保存设备数量
    uint8_t count = min((uint8_t)devices.size(), MAX_DEVICE_COUNT);
    prefs.putUChar(NVS_KEY_DEV_COUNT, count);

    // 逐个保存设备配置（序列化为 JSON）
    for (uint8_t i = 0; i < count; i++)
    {
        const DeviceConfig &dev = devices[i];

        JsonDocument doc;
        doc["id"] = dev.id;
        doc["name"] = dev.name;
        doc["pwmPin"] = dev.pwmPin;
        doc["rpmPin"] = dev.rpmPin;
        doc["duty"] = dev.dutyCycle;

        String json;
        serializeJson(doc, json);

        String key = String(NVS_KEY_DEV_PREFIX) + String(i);
        prefs.putString(key.c_str(), json);

        Serial.printf("[Config] 保存设备: id=%d, key=%s\n", dev.id, key.c_str());
    }

    prefs.end();
    Serial.printf("[Config] 已保存 %d 个设备到 NVS\n", count);
    return true;
}

uint8_t ConfigManager::nextDeviceId(const std::vector<DeviceConfig> &devices)
{
    // 找到当前最大ID，返回 maxId + 1
    uint8_t maxId = 0;
    for (const auto &dev : devices)
    {
        if (dev.id > maxId)
        {
            maxId = dev.id;
        }
    }
    return maxId + 1;
}
