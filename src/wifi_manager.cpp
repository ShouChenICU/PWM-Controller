/**
 * @file wifi_manager.cpp
 * @brief 非阻塞 WiFi STA/AP 状态机实现
 */

#include "wifi_manager.h"
#include "config_manager.h"
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace
{
constexpr char AP_SSID_PREFIX[] = "PWM-Controller";
constexpr char AP_PASSWORD[] = "pwm-controller";
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_RECOVERY_TIMEOUT_MS = 30000;

enum class ConnectionState
{
    AP,
    CONNECTING,
    CONNECTED,
    FAILED
};

ConnectionState state = ConnectionState::AP;
WiFiConfig pendingConfig;
WiFiConfig activeConfig;
bool persistPendingConfig = false;
bool apMode = false;
uint32_t connectStartedAt = 0;
uint32_t disconnectedAt = 0;
String apSsid;
SemaphoreHandle_t wifiMutex = nullptr;

/** 自动获取和释放 WiFi 状态互斥锁。 */
class WiFiLock
{
public:
    WiFiLock() : locked(wifiMutex && xSemaphoreTake(wifiMutex, portMAX_DELAY) == pdTRUE) {}
    ~WiFiLock()
    {
        if (locked)
        {
            xSemaphoreGive(wifiMutex);
        }
    }
    explicit operator bool() const { return locked; }

private:
    bool locked;
};

/** 启动或保持救援 AP，调用者必须持有 wifiMutex。 */
bool startAPLocked()
{
    if (apSsid.isEmpty())
    {
        const uint32_t shortId = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFULL);
        char suffix[8];
        snprintf(suffix, sizeof(suffix), "%06lX", static_cast<unsigned long>(shortId));
        apSsid = String(AP_SSID_PREFIX) + "-" + suffix;
    }

    WiFi.mode(WIFI_AP_STA);
    if (!apMode)
    {
        if (!WiFi.softAP(apSsid.c_str(), AP_PASSWORD))
        {
            Serial.println("[WiFi] 救援 AP 启动失败");
            return false;
        }
        apMode = true;
    }

    if (state != ConnectionState::CONNECTING)
    {
        state = ConnectionState::AP;
    }
    Serial.printf("[WiFi] 救援 AP: SSID=%s, IP=%s\n",
                  apSsid.c_str(), WiFi.softAPIP().toString().c_str());
    return true;
}

/** 启动一次 STA 连接，同时保留 AP 可访问性。 */
bool beginConnection(const WiFiConfig &config, bool persistOnSuccess)
{
    if (config.ssid.isEmpty())
    {
        return false;
    }
    if (!apMode && !startAPLocked())
    {
        return false;
    }

    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect(false, false);
    pendingConfig = config;
    persistPendingConfig = persistOnSuccess;
    connectStartedAt = millis();
    disconnectedAt = 0;
    state = ConnectionState::CONNECTING;
    WiFi.begin(config.ssid.c_str(), config.password.c_str());
    Serial.printf("[WiFi] 异步连接开始: SSID=%s\n", config.ssid.c_str());
    return true;
}
} // namespace

void WiFiManager::init()
{
    Serial.println("[WiFi] WiFi 管理模块初始化");
    wifiMutex = xSemaphoreCreateMutex();
    if (!wifiMutex)
    {
        Serial.println("[WiFi] 创建状态互斥锁失败");
        return;
    }

    WiFiLock lock;
    if (!lock)
    {
        return;
    }
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    startAPLocked();

    const WiFiConfig config = ConfigManager::loadWiFiConfig();
    if (!config.ssid.isEmpty())
    {
        beginConnection(config, false);
    }
}

void WiFiManager::update()
{
    WiFiLock lock;
    if (!lock)
    {
        return;
    }
    const uint32_t now = millis();
    if (state == ConnectionState::CONNECTING)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            if (persistPendingConfig && !ConfigManager::saveWiFiConfig(pendingConfig))
            {
                Serial.println("[WiFi] 新配置连接成功但 NVS 保存失败，尝试恢复旧网络");
                if (!activeConfig.ssid.isEmpty())
                {
                    beginConnection(activeConfig, false);
                }
                else
                {
                    state = ConnectionState::FAILED;
                }
                return;
            }

            activeConfig = pendingConfig;
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            apMode = false;
            state = ConnectionState::CONNECTED;
            disconnectedAt = 0;
            Serial.printf("[WiFi] 连接成功: IP=%s\n", WiFi.localIP().toString().c_str());
        }
        else if (now - connectStartedAt >= WIFI_CONNECT_TIMEOUT_MS)
        {
            WiFi.disconnect(false, false);
            if (persistPendingConfig && !activeConfig.ssid.isEmpty())
            {
                Serial.println("[WiFi] 新网络连接超时，尝试恢复旧网络");
                beginConnection(activeConfig, false);
            }
            else
            {
                state = ConnectionState::FAILED;
                Serial.println("[WiFi] 连接超时，继续保留救援 AP");
            }
        }
        return;
    }

    if (state == ConnectionState::CONNECTED && WiFi.status() != WL_CONNECTED)
    {
        if (disconnectedAt == 0)
        {
            disconnectedAt = now;
            Serial.println("[WiFi] STA 已断线，等待自动重连");
        }
        else if (now - disconnectedAt >= WIFI_RECOVERY_TIMEOUT_MS)
        {
            Serial.println("[WiFi] 自动重连超时，启动救援 AP 并重新尝试");
            startAPLocked();
            beginConnection(activeConfig, false);
        }
    }
    else if (state == ConnectionState::CONNECTED)
    {
        disconnectedAt = 0;
    }
}

bool WiFiManager::requestConnect(const String &ssid, const String &password)
{
    if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 64)
    {
        return false;
    }
    WiFiLock lock;
    return lock && beginConnection({ssid, password}, true);
}

bool WiFiManager::startAP()
{
    WiFiLock lock;
    return lock && startAPLocked();
}

String WiFiManager::getIP()
{
    WiFiLock lock;
    if (!lock)
    {
        return "0.0.0.0";
    }
    if (state == ConnectionState::CONNECTED && WiFi.status() == WL_CONNECTED)
    {
        return WiFi.localIP().toString();
    }
    return apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

bool WiFiManager::isAPMode()
{
    WiFiLock lock;
    return lock && apMode;
}

String WiFiManager::getState()
{
    WiFiLock lock;
    if (!lock)
    {
        return "failed";
    }
    switch (state)
    {
    case ConnectionState::CONNECTING:
        return "connecting";
    case ConnectionState::CONNECTED:
        return "connected";
    case ConnectionState::FAILED:
        return "failed";
    case ConnectionState::AP:
    default:
        return "ap";
    }
}

String WiFiManager::getAPSSID()
{
    WiFiLock lock;
    return lock ? apSsid : String();
}
