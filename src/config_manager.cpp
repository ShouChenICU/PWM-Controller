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

    // 以读写模式打开一次，确保命名空间存在（首次启动时自动创建）
    if (!prefs.begin(NVS_NAMESPACE, false))
    {
        Serial.println("[Config] NVS 命名空间创建失败!");
    }
    else
    {
        Serial.println("[Config] NVS 命名空间就绪");
        prefs.end();
    }
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
        dev.inverted = doc["inverted"] | false;

        devices.push_back(dev);
        Serial.printf("[Config] 加载设备: id=%d, name=%s, pwm=%d, rpm=%d, duty=%d%%, inverted=%s\n",
                      dev.id, dev.name.c_str(), dev.pwmPin, dev.rpmPin, dev.dutyCycle, dev.inverted ? "是" : "否");
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
        doc["inverted"] = dev.inverted;

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

bool ConfigManager::saveOneDevice(uint8_t nvsIndex, const DeviceConfig &config)
{
    if (!prefs.begin(NVS_NAMESPACE, false))
    {
        Serial.println("[Config] 打开 NVS 失败（保存单设备）");
        return false;
    }

    // 序列化为 JSON 并写入对应索引的键
    JsonDocument doc;
    doc["id"] = config.id;
    doc["name"] = config.name;
    doc["pwmPin"] = config.pwmPin;
    doc["rpmPin"] = config.rpmPin;
    doc["duty"] = config.dutyCycle;
    doc["inverted"] = config.inverted;

    String json;
    serializeJson(doc, json);

    String key = String(NVS_KEY_DEV_PREFIX) + String(nvsIndex);
    prefs.putString(key.c_str(), json);
    prefs.end();

    Serial.printf("[Config] 单设备已保存: id=%d, key=%s, duty=%d%%\n",
                  config.id, key.c_str(), config.dutyCycle);
    return true;
}

uint8_t ConfigManager::getSavedDuty(uint8_t nvsIndex, uint8_t defaultDuty)
{
    if (!prefs.begin(NVS_NAMESPACE, true))
    {
        return defaultDuty;
    }

    String key = String(NVS_KEY_DEV_PREFIX) + String(nvsIndex);
    String json = prefs.getString(key.c_str(), "");
    prefs.end();

    if (json.isEmpty())
    {
        return defaultDuty;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err)
    {
        return defaultDuty;
    }

    return doc["duty"] | defaultDuty;
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
