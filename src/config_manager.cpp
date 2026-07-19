/**
 * @file config_manager.cpp
 * @brief NVS 配置管理模块实现
 *
 * WiFi 配置直接存储；设备配置采用 A/B 双区写入，只有在新数据完整写入后
 * 才切换活动区，从而避免保存过程中掉电破坏上一份有效配置。
 */

#include "config_manager.h"
#include <ArduinoJson.h>
#include <Preferences.h>

namespace
{
    constexpr char NVS_NAMESPACE[] = "pwm_ctrl";
    constexpr char NVS_KEY_WIFI_CONFIG[] = "wifi_cfg";
    constexpr char NVS_KEY_WEB_AUTH_CONFIG[] = "web_auth";
    constexpr char NVS_KEY_ACTIVE_BANK[] = "active_bank";
    constexpr char BANK_A = 'a';
    constexpr char BANK_B = 'b';

    /** 生成设备数量键名。 */
    String makeCountKey(char bank)
    {
        return String("cnt_") + bank;
    }

    /** 生成设备数据键名。 */
    String makeDeviceKey(char bank, uint8_t index)
    {
        return String("dev_") + bank + "_" + String(index);
    }

    /** 将设备配置序列化为紧凑 JSON。 */
    String serializeDevice(const DeviceConfig &device)
    {
        JsonDocument doc;
        doc["id"] = device.id;
        doc["name"] = device.name;
        doc["pwmPin"] = device.pwmPin;
        doc["rpmPin"] = device.rpmPin;
        doc["duty"] = device.dutyCycle;
        doc["inverted"] = device.inverted;
        doc["ppr"] = device.pulsesPerRevolution;

        String json;
        serializeJson(doc, json);
        return json;
    }

    /** 从 JSON 解析设备配置。 */
    bool deserializeDevice(const String &json, DeviceConfig &device)
    {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, json);
        if (error)
        {
            return false;
        }

        device.id = doc["id"] | 0;
        device.name = doc["name"] | "未命名";
        device.pwmPin = doc["pwmPin"] | 0;
        device.rpmPin = doc["rpmPin"] | -1;
        device.dutyCycle = doc["duty"] | DEFAULT_DUTY_CYCLE;
        device.inverted = doc["inverted"] | false;
        device.pulsesPerRevolution = doc["ppr"] | DEFAULT_PULSES_PER_REVOLUTION;
        return true;
    }
} // namespace

void ConfigManager::init()
{
    Serial.println("[Config] NVS 配置管理模块初始化");

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false))
    {
        Serial.println("[Config] NVS 命名空间创建失败");
        return;
    }

    prefs.end();
    Serial.println("[Config] NVS 命名空间就绪");
}

WiFiConfig ConfigManager::loadWiFiConfig()
{
    WiFiConfig config;
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true))
    {
        Serial.println("[Config] 打开 NVS 失败（读取 WiFi）");
        return config;
    }

    const String json = prefs.getString(NVS_KEY_WIFI_CONFIG, "");
    if (!json.isEmpty())
    {
        JsonDocument doc;
        if (!deserializeJson(doc, json) && doc.is<JsonObjectConst>())
        {
            config.ssid = doc["ssid"] | "";
            config.password = doc["password"] | "";
        }
        else
        {
            Serial.println("[Config] WiFi 配置 JSON 无效");
        }
    }
    prefs.end();

    Serial.printf("[Config] 读取 WiFi 配置: SSID=%s\n", config.ssid.c_str());
    return config;
}

bool ConfigManager::saveWiFiConfig(const WiFiConfig &config)
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false))
    {
        Serial.println("[Config] 打开 NVS 失败（保存 WiFi）");
        return false;
    }

    JsonDocument doc;
    doc["ssid"] = config.ssid;
    doc["password"] = config.password;
    String json;
    serializeJson(doc, json);

    const size_t bytesWritten = prefs.putString(NVS_KEY_WIFI_CONFIG, json);
    const bool success = bytesWritten == json.length() &&
                         prefs.getString(NVS_KEY_WIFI_CONFIG, "") == json;
    prefs.end();

    Serial.printf("[Config] WiFi 配置保存%s: SSID=%s\n",
                  success ? "成功" : "失败", config.ssid.c_str());
    return success;
}

WebAuthConfig ConfigManager::loadWebAuthConfig()
{
    WebAuthConfig config;
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true))
    {
        Serial.println("[Config] 打开 NVS 失败（读取登录认证）");
        return config;
    }

    const String json = prefs.getString(NVS_KEY_WEB_AUTH_CONFIG, "");
    if (!json.isEmpty())
    {
        JsonDocument doc;
        if (!deserializeJson(doc, json) && doc.is<JsonObjectConst>())
        {
            config.username = doc["username"] | "";
            config.passwordHash = doc["passwordHash"] | "";
            if (config.username.isEmpty() || config.passwordHash.length() != 32)
            {
                config = {};
                Serial.println("[Config] 登录认证配置内容无效");
            }
        }
        else
        {
            Serial.println("[Config] 登录认证配置 JSON 无效");
        }
    }
    prefs.end();

    Serial.printf("[Config] 登录认证配置: %s\n",
                  config.username.isEmpty() ? "使用默认值" : config.username.c_str());
    return config;
}

bool ConfigManager::saveWebAuthConfig(const WebAuthConfig &config)
{
    if (config.username.isEmpty() || config.passwordHash.length() != 32)
    {
        Serial.println("[Config] 拒绝保存无效的登录认证配置");
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false))
    {
        Serial.println("[Config] 打开 NVS 失败（保存登录认证）");
        return false;
    }

    JsonDocument doc;
    doc["username"] = config.username;
    doc["passwordHash"] = config.passwordHash;
    String json;
    serializeJson(doc, json);

    const size_t bytesWritten = prefs.putString(NVS_KEY_WEB_AUTH_CONFIG, json);
    const bool success = bytesWritten == json.length() &&
                         prefs.getString(NVS_KEY_WEB_AUTH_CONFIG, "") == json;
    prefs.end();

    Serial.printf("[Config] 登录认证配置保存%s: 用户名=%s\n",
                  success ? "成功" : "失败", config.username.c_str());
    return success;
}

std::vector<DeviceConfig> ConfigManager::loadDevices()
{
    std::vector<DeviceConfig> devices;
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true))
    {
        Serial.println("[Config] 打开 NVS 失败（读取设备）");
        return devices;
    }

    const String activeValue = prefs.getString(NVS_KEY_ACTIVE_BANK, "");
    if (activeValue.isEmpty())
    {
        prefs.end();
        Serial.println("[Config] 未找到当前格式的设备配置");
        return devices;
    }

    const char bank = activeValue[0] == BANK_B ? BANK_B : BANK_A;
    const uint8_t count = min(prefs.getUChar(makeCountKey(bank).c_str(), 0),
                              MAX_DEVICE_COUNT);
    for (uint8_t i = 0; i < count; ++i)
    {
        const String json = prefs.getString(makeDeviceKey(bank, i).c_str(), "");
        DeviceConfig device;
        if (json.isEmpty() || !deserializeDevice(json, device))
        {
            Serial.printf("[Config] 设备区 %c 索引 %u 数据无效，已跳过\n", bank, i);
            continue;
        }
        devices.push_back(device);
    }

    prefs.end();
    Serial.printf("[Config] 已加载 %u 个设备\n", static_cast<unsigned>(devices.size()));
    return devices;
}

bool ConfigManager::saveDevices(const std::vector<DeviceConfig> &devices)
{
    if (devices.size() > MAX_DEVICE_COUNT)
    {
        Serial.println("[Config] 设备数量超过硬件上限");
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false))
    {
        Serial.println("[Config] 打开 NVS 失败（保存设备）");
        return false;
    }

    const String activeValue = prefs.getString(NVS_KEY_ACTIVE_BANK, String(BANK_B));
    const char targetBank = activeValue[0] == BANK_A ? BANK_B : BANK_A;
    const String countKey = makeCountKey(targetBank);

    bool success = true;
    for (uint8_t i = 0; i < devices.size(); ++i)
    {
        const String json = serializeDevice(devices[i]);
        const String key = makeDeviceKey(targetBank, i);
        if (prefs.putString(key.c_str(), json) != json.length())
        {
            success = false;
            break;
        }
    }

    if (success)
    {
        success = prefs.putUChar(countKey.c_str(), static_cast<uint8_t>(devices.size())) == 1;
    }

    // 活动区标记最后写入；此前任何失败都仍可读取旧区。
    if (success)
    {
        success = prefs.putString(NVS_KEY_ACTIVE_BANK, String(targetBank)) == 1;
    }

    prefs.end();
    Serial.printf("[Config] 设备配置保存%s，活动区=%c\n",
                  success ? "成功" : "失败", targetBank);
    return success;
}

bool ConfigManager::clearAll()
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false))
    {
        Serial.println("[Config] 打开 NVS 失败（恢复出厂设置）");
        return false;
    }

    const bool success = prefs.clear();
    prefs.end();
    Serial.printf("[Config] 恢复出厂设置%s\n", success ? "成功" : "失败");
    return success;
}

uint8_t ConfigManager::nextDeviceId(const std::vector<DeviceConfig> &devices)
{
    // 从最小正整数开始查找空闲 ID，避免 uint8_t 最大值回绕。
    for (uint16_t candidate = 1; candidate <= UINT8_MAX; ++candidate)
    {
        bool used = false;
        for (const auto &device : devices)
        {
            if (device.id == candidate)
            {
                used = true;
                break;
            }
        }
        if (!used)
        {
            return static_cast<uint8_t>(candidate);
        }
    }
    return 0;
}
