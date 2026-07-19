/**
 * @file web_server.cpp
 * @brief Web 静态资源与 REST API 实现
 *
 * JSON API 使用 AsyncCallbackJsonWebHandler，由库负责完整接收分片 body 后解析，
 * 避免直接解析单个 TCP 分片。所有动态路径均进行严格格式校验。
 */

#include "web_server.h"
#include "config_manager.h"
#include "device_manager.h"
#include "project_version.h"
#include "wifi_manager.h"

#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WebAuthentication.h>
#include <atomic>

namespace
{
    AsyncWebServer server(WEB_SERVER_PORT);
    AsyncAuthenticationMiddleware authentication;
    constexpr char DEFAULT_WEB_USERNAME[] = "admin";
    constexpr char DEFAULT_WEB_PASSWORD[] = "pwm-controller";
    constexpr char WEB_REALM[] = "PWM Controller";
    constexpr uint8_t MIN_WEB_PASSWORD_LENGTH = 8;
    constexpr uint8_t MAX_WEB_PASSWORD_LENGTH = 64;
    constexpr uint8_t MAX_WEB_USERNAME_LENGTH = 32;
    constexpr uint32_t RESTART_DELAY_MS = 1000;

    enum class RestartReason : uint8_t
    {
        NONE,
        RESET_SETTINGS,
        AUTH_CHANGED
    };

    std::atomic<RestartReason> restartReason{RestartReason::NONE};
    std::atomic<uint32_t> restartAt{0};

    /** 安排响应发送完成后的延迟重启。 */
    void scheduleRestart(RestartReason reason)
    {
        restartAt.store(millis() + RESTART_DELAY_MS, std::memory_order_relaxed);
        restartReason.store(reason, std::memory_order_release);
    }

    /** 验证摘要认证用户名，避免请求头中的特殊字符产生歧义。 */
    bool isValidWebUsername(const String &username)
    {
        if (username.isEmpty() || username.length() > MAX_WEB_USERNAME_LENGTH)
        {
            return false;
        }
        for (size_t i = 0; i < username.length(); ++i)
        {
            const char character = username[i];
            const bool valid = (character >= 'a' && character <= 'z') ||
                               (character >= 'A' && character <= 'Z') ||
                               (character >= '0' && character <= '9') ||
                               character == '.' || character == '_' || character == '-';
            if (!valid)
            {
                return false;
            }
        }
        return true;
    }

    /** 发送统一 JSON 消息。 */
    void sendMessage(AsyncWebServerRequest *request, int code, const String &message)
    {
        JsonDocument doc;
        if (code >= 400)
        {
            doc["error"] = message;
        }
        else
        {
            doc["message"] = message;
        }
        String body;
        serializeJson(doc, body);
        request->send(code, "application/json", body);
    }

    /** 严格解析 /api/devices/{id}{suffix}。 */
    bool parseDeviceId(const String &url, const char *suffix, uint8_t &id)
    {
        const String prefix = "/api/devices/";
        if (!url.startsWith(prefix))
        {
            return false;
        }

        String value = url.substring(prefix.length());
        const String suffixValue = suffix;
        if (!suffixValue.isEmpty())
        {
            if (!value.endsWith(suffixValue))
            {
                return false;
            }
            value.remove(value.length() - suffixValue.length());
        }
        if (value.isEmpty() || value.indexOf('/') >= 0)
        {
            return false;
        }
        for (size_t i = 0; i < value.length(); ++i)
        {
            if (!isDigit(value[i]))
            {
                return false;
            }
        }

        const long parsed = value.toInt();
        if (parsed <= 0 || parsed > UINT8_MAX)
        {
            return false;
        }
        id = static_cast<uint8_t>(parsed);
        return true;
    }

    /** 读取并验证 JSON 整数。 */
    bool readInteger(JsonVariantConst json, const char *key, long minimum, long maximum, long &value)
    {
        if (!json[key].is<long>())
        {
            return false;
        }
        value = json[key].as<long>();
        return value >= minimum && value <= maximum;
    }

    /** 读取设备配置请求。 */
    bool readDeviceRequest(JsonVariantConst json, String &name, uint8_t &pwmPin, int8_t &rpmPin,
                           bool &inverted, uint8_t &pulsesPerRevolution, String &error)
    {
        if (!json.is<JsonObjectConst>())
        {
            error = "请求必须是 JSON 对象";
            return false;
        }

        name = json["name"] | "";
        name.trim();
        if (name.isEmpty() || name.length() > 20)
        {
            error = "设备名称长度必须为 1～20 个字符";
            return false;
        }

        long pwmValue;
        long rpmValue = -1;
        long pprValue = DEFAULT_PULSES_PER_REVOLUTION;
        if (!readInteger(json, "pwmPin", 0, 21, pwmValue))
        {
            error = "PWM 引脚格式无效";
            return false;
        }
        if (!json["rpmPin"].isNull() && !readInteger(json, "rpmPin", -1, 21, rpmValue))
        {
            error = "转速引脚格式无效";
            return false;
        }
        if (!json["pulsesPerRevolution"].isNull() &&
            !readInteger(json, "pulsesPerRevolution", MIN_PULSES_PER_REVOLUTION,
                         MAX_PULSES_PER_REVOLUTION, pprValue))
        {
            error = "每转脉冲数必须为 1～8";
            return false;
        }
        if (!json["inverted"].isNull() && !json["inverted"].is<bool>())
        {
            error = "反转标志格式无效";
            return false;
        }

        pwmPin = static_cast<uint8_t>(pwmValue);
        rpmPin = static_cast<int8_t>(rpmValue);
        inverted = json["inverted"] | false;
        pulsesPerRevolution = static_cast<uint8_t>(pprValue);
        return true;
    }

    /** 将状态快照写入 JSON。 */
    void statusToJson(const DeviceStatus &status, JsonObject object)
    {
        object["id"] = status.config.id;
        object["name"] = status.config.name;
        object["pwmPin"] = status.config.pwmPin;
        object["rpmPin"] = status.config.rpmPin;
        object["dutyCycle"] = status.config.dutyCycle;
        object["savedDutyCycle"] = status.savedDutyCycle;
        object["inverted"] = status.config.inverted;
        object["pulsesPerRevolution"] = status.config.pulsesPerRevolution;
        object["rpm"] = status.rpm;
    }

    /** 注册设备 API。 */
    void registerDeviceAPI()
    {
        server.on("/api/devices", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
        if (request->url() != "/api/devices") {
            sendMessage(request, 404, "未找到");
            return;
        }
        std::vector<DeviceStatus> statuses;
        DeviceManager::getStatuses(statuses);
        JsonDocument doc;
        JsonArray array = doc.to<JsonArray>();
        for (const auto &status : statuses) {
            statusToJson(status, array.add<JsonObject>());
        }
        String body;
        serializeJson(doc, body);
        request->send(200, "application/json", body); });

        // 无 body 的全量保存必须先于 /api/devices 前缀 JSON 处理器注册。
        server.on("/api/devices/save", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
        if (request->url() != "/api/devices/save") {
            sendMessage(request, 404, "未找到");
            return;
        }
        if (DeviceManager::saveToNVS()) {
            sendMessage(request, 200, "全部设备配置已保存");
        } else {
            sendMessage(request, 500, "NVS 保存失败");
        } });

        auto *postJsonHandler = new AsyncCallbackJsonWebHandler(
            "/api/devices", [](AsyncWebServerRequest *request, JsonVariant &json)
            {
            const String url = request->url();
            if (url == "/api/devices") {
                String name;
                String error;
                uint8_t pwmPin;
                int8_t rpmPin;
                bool inverted;
                uint8_t ppr;
                if (!readDeviceRequest(json, name, pwmPin, rpmPin, inverted, ppr, error)) {
                    sendMessage(request, 400, error);
                    return;
                }
                const uint8_t id = DeviceManager::addDevice(name, pwmPin, rpmPin, inverted, ppr, error);
                if (id == 0) {
                    sendMessage(request, 409, error);
                    return;
                }
                JsonDocument response;
                response["message"] = "设备添加成功";
                response["id"] = id;
                String body;
                serializeJson(response, body);
                request->send(201, "application/json", body);
                return;
            }

            uint8_t id;
            if (!parseDeviceId(url, "/duty", id)) {
                sendMessage(request, 404, "未找到");
                return;
            }
            long duty;
            if (!readInteger(json, "dutyCycle", 0, 100, duty)) {
                sendMessage(request, 400, "占空比必须为 0～100 的整数");
                return;
            }
            String error;
            if (!DeviceManager::setDutyCycle(id, static_cast<uint8_t>(duty), error)) {
                sendMessage(request, error == "设备不存在" ? 404 : 500, error);
                return;
            }
            sendMessage(request, 200, "占空比已设置"); });
        postJsonHandler->setMethod(HTTP_POST);
        server.addHandler(postJsonHandler);

        // 无 body 的单设备占空比保存；JSON duty 请求会优先命中上方处理器。
        server.on("/api/devices/*", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
        uint8_t id;
        if (!parseDeviceId(request->url(), "/save", id)) {
            sendMessage(request, 404, "未找到");
            return;
        }
        String error;
        if (!DeviceManager::saveDutyToNVS(id, error)) {
            sendMessage(request, error == "设备不存在" ? 404 : 500, error);
            return;
        }
        sendMessage(request, 200, "占空比已保存"); });

        auto *putJsonHandler = new AsyncCallbackJsonWebHandler(
            "/api/devices", [](AsyncWebServerRequest *request, JsonVariant &json)
            {
            uint8_t id;
            if (!parseDeviceId(request->url(), "", id)) {
                sendMessage(request, 404, "未找到");
                return;
            }
            String name;
            String error;
            uint8_t pwmPin;
            int8_t rpmPin;
            bool inverted;
            uint8_t ppr;
            if (!readDeviceRequest(json, name, pwmPin, rpmPin, inverted, ppr, error)) {
                sendMessage(request, 400, error);
                return;
            }
            if (!DeviceManager::updateDevice(id, name, pwmPin, rpmPin, inverted, ppr, error)) {
                sendMessage(request, error == "设备不存在" ? 404 : 409, error);
                return;
            }
            sendMessage(request, 200, "设备已更新并保存"); });
        putJsonHandler->setMethod(HTTP_PUT);
        server.addHandler(putJsonHandler);

        server.on("/api/devices/*", HTTP_DELETE, [](AsyncWebServerRequest *request)
                  {
        uint8_t id;
        if (!parseDeviceId(request->url(), "", id)) {
            sendMessage(request, 404, "未找到");
            return;
        }
        String error;
        if (!DeviceManager::removeDevice(id, error)) {
            sendMessage(request, error == "设备不存在" ? 404 : 500, error);
            return;
        }
        sendMessage(request, 200, "设备已删除"); });
    }

    /** 注册 WiFi API。 */
    void registerWiFiAPI()
    {
        server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
        if (request->url() != "/api/wifi") {
            sendMessage(request, 404, "未找到");
            return;
        }
        const WiFiConfig config = ConfigManager::loadWiFiConfig();
        JsonDocument doc;
        doc["ssid"] = config.ssid;
        doc["isAP"] = WiFiManager::isAPMode();
        doc["ip"] = WiFiManager::getIP();
        doc["state"] = WiFiManager::getState();
        doc["apSsid"] = WiFiManager::getAPSSID();
        String body;
        serializeJson(doc, body);
        request->send(200, "application/json", body); });

        auto *wifiJsonHandler = new AsyncCallbackJsonWebHandler(
            "/api/wifi", [](AsyncWebServerRequest *request, JsonVariant &json)
            {
            if (request->url() != "/api/wifi" || !json.is<JsonObjectConst>()) {
                sendMessage(request, 400, "请求格式错误");
                return;
            }
            const String ssid = json["ssid"] | "";
            const String password = json["password"] | "";
            if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 64) {
                sendMessage(request, 400, "SSID 或密码长度无效");
                return;
            }
            if (!WiFiManager::requestConnect(ssid, password)) {
                sendMessage(request, 500, "无法启动 WiFi 连接");
                return;
            }
            sendMessage(request, 202, "正在后台验证新 WiFi，救援 AP 将保持开启"); });
        wifiJsonHandler->setMethod(HTTP_POST);
        server.addHandler(wifiJsonHandler);
    }

    /** 注册系统信息 API。 */
    void registerSystemAPI()
    {
        server.on("/api/system/info", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
        if (request->url() != "/api/system/info") {
            sendMessage(request, 404, "未找到");
            return;
        }
        JsonDocument doc;
        doc["ip"] = WiFiManager::getIP();
        doc["isAP"] = WiFiManager::isAPMode();
        doc["wifiState"] = WiFiManager::getState();
        doc["uptime"] = millis() / 1000;
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["chipModel"] = ESP.getChipModel();
        doc["version"] = PROJECT_VERSION;
        String body;
        serializeJson(doc, body);
        request->send(200, "application/json", body); });

        server.on("/api/system/reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
        if (request->url() != "/api/system/reset") {
            sendMessage(request, 404, "未找到");
            return;
        }
        if (!ConfigManager::clearAll()) {
            sendMessage(request, 500, "清空 NVS 失败");
            return;
        }
        scheduleRestart(RestartReason::RESET_SETTINGS);
        sendMessage(request, 200, "设置已清空，设备即将重启"); });
    }

    /** 注册 Web 登录认证配置 API。 */
    void registerWebAuthAPI()
    {
        server.on("/api/system/auth", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
        if (request->url() != "/api/system/auth") {
            sendMessage(request, 404, "未找到");
            return;
        }
        JsonDocument doc;
        doc["username"] = authentication.username();
        String body;
        serializeJson(doc, body);
        request->send(200, "application/json", body); });

        auto *authJsonHandler = new AsyncCallbackJsonWebHandler(
            "/api/system/auth", [](AsyncWebServerRequest *request, JsonVariant &json)
            {
            if (request->url() != "/api/system/auth" || !json.is<JsonObjectConst>()) {
                sendMessage(request, 400, "请求格式错误");
                return;
            }

            String username = json["username"] | "";
            const String password = json["password"] | "";
            username.trim();
            if (!isValidWebUsername(username)) {
                sendMessage(request, 400, "用户名只能包含 1～32 位字母、数字、点、下划线或连字符");
                return;
            }
            if (password.length() < MIN_WEB_PASSWORD_LENGTH ||
                password.length() > MAX_WEB_PASSWORD_LENGTH) {
                sendMessage(request, 400, "密码长度必须为 8～64 位");
                return;
            }

            const String passwordHash = generateDigestHash(username.c_str(), password.c_str(), WEB_REALM);
            if (passwordHash.length() != 32) {
                sendMessage(request, 500, "登录密码哈希生成失败");
                return;
            }
            if (!ConfigManager::saveWebAuthConfig({username, passwordHash})) {
                sendMessage(request, 500, "登录配置保存失败");
                return;
            }

            scheduleRestart(RestartReason::AUTH_CHANGED);
            sendMessage(request, 200, "登录配置已保存，设备即将重启"); });
        authJsonHandler->setMethod(HTTP_POST);
        server.addHandler(authJsonHandler);
    }
} // namespace

void WebServer::init()
{
    Serial.println("[Web] Web 服务器模块初始化");
    const bool fsReady = LittleFS.begin(false);
    Serial.println(fsReady ? "[Web] LittleFS 挂载成功" : "[Web] LittleFS 挂载失败，未自动格式化");

    // 全站使用摘要认证；NVS 未配置时使用默认登录凭据。
    const WebAuthConfig savedAuth = ConfigManager::loadWebAuthConfig();
    const String authUsername = savedAuth.username.isEmpty() ? DEFAULT_WEB_USERNAME : savedAuth.username;
    String authPasswordHash = savedAuth.passwordHash;
    if (authPasswordHash.isEmpty())
    {
        authPasswordHash = generateDigestHash(DEFAULT_WEB_USERNAME, DEFAULT_WEB_PASSWORD, WEB_REALM);
    }

    authentication.setUsername(authUsername.c_str());
    authentication.setPasswordHash(authPasswordHash.c_str());
    authentication.setRealm(WEB_REALM);
    authentication.setAuthFailureMessage("需要管理员认证");
    authentication.setAuthType(AsyncAuthType::AUTH_DIGEST);
    if (!authentication.hasCredentials())
    {
        Serial.println("[Web] 摘要认证初始化失败");
    }
    server.addMiddleware(&authentication);

    registerDeviceAPI();
    registerWiFiAPI();
    registerSystemAPI();
    registerWebAuthAPI();

    if (fsReady)
    {
        server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    }

    server.onNotFound([](AsyncWebServerRequest *request)
                      { sendMessage(request, 404, "未找到"); });
    server.begin();
    Serial.printf("[Web] 服务器已启动，端口=%u\n", WEB_SERVER_PORT);
}

void WebServer::update()
{
    const RestartReason reason = restartReason.load(std::memory_order_acquire);
    if (reason == RestartReason::NONE)
    {
        return;
    }

    const uint32_t scheduledAt = restartAt.load(std::memory_order_relaxed);
    if (static_cast<int32_t>(millis() - scheduledAt) >= 0)
    {
        restartReason.store(RestartReason::NONE, std::memory_order_relaxed);
        Serial.println(reason == RestartReason::RESET_SETTINGS
                           ? "[System] NVS 已清空，正在重启"
                           : "[System] 登录配置已更新，正在重启");
        Serial.flush();
        ESP.restart();
    }
}
