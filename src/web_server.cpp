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

    /** 发送带稳定机器码的统一 JSON 消息。 */
    void sendMessage(AsyncWebServerRequest *request, int status, const char *messageCode,
                     const String &message)
    {
        JsonDocument doc;
        doc["code"] = messageCode;
        if (status >= 400)
        {
            doc["error"] = message;
        }
        else
        {
            doc["message"] = message;
        }
        String body;
        serializeJson(doc, body);
        request->send(status, "application/json", body);
    }

    /** 根据设备错误类别选择 HTTP 状态码。 */
    int deviceErrorStatus(DeviceError error)
    {
        switch (error)
        {
        case DeviceError::DEVICE_NOT_FOUND:
            return 404;
        case DeviceError::DUPLICATE_ID:
        case DeviceError::GPIO_IN_USE:
        case DeviceError::DEVICE_LIMIT_REACHED:
            return 409;
        case DeviceError::INVALID_ID:
        case DeviceError::INVALID_NAME:
        case DeviceError::PWM_PIN_UNAVAILABLE:
        case DeviceError::RPM_PIN_UNAVAILABLE:
        case DeviceError::PIN_CONFLICT:
        case DeviceError::INVALID_DUTY:
        case DeviceError::INVALID_PULSES_PER_REVOLUTION:
            return 400;
        default:
            return 500;
        }
    }

    /** 将设备管理错误转换为 API 响应。 */
    void sendDeviceError(AsyncWebServerRequest *request, DeviceError error)
    {
        sendMessage(request, deviceErrorStatus(error), DeviceManager::getErrorCode(error),
                    DeviceManager::getErrorMessage(error));
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
                           bool &inverted, uint8_t &pulsesPerRevolution,
                           const char *&errorCode, const char *&errorMessage)
    {
        if (!json.is<JsonObjectConst>())
        {
            errorCode = "INVALID_JSON_OBJECT";
            errorMessage = "The request body must be a JSON object";
            return false;
        }

        name = json["name"] | "";
        name.trim();
        if (name.isEmpty() || name.length() > 20)
        {
            errorCode = "INVALID_DEVICE_NAME";
            errorMessage = "The device name must contain 1 to 20 characters";
            return false;
        }

        long pwmValue;
        long rpmValue = -1;
        long pprValue = DEFAULT_PULSES_PER_REVOLUTION;
        if (!readInteger(json, "pwmPin", 0, 21, pwmValue))
        {
            errorCode = "INVALID_PWM_PIN_FORMAT";
            errorMessage = "The PWM pin format is invalid";
            return false;
        }
        if (!json["rpmPin"].isNull() && !readInteger(json, "rpmPin", -1, 21, rpmValue))
        {
            errorCode = "INVALID_RPM_PIN_FORMAT";
            errorMessage = "The RPM pin format is invalid";
            return false;
        }
        if (!json["pulsesPerRevolution"].isNull() &&
            !readInteger(json, "pulsesPerRevolution", MIN_PULSES_PER_REVOLUTION,
                         MAX_PULSES_PER_REVOLUTION, pprValue))
        {
            errorCode = "INVALID_PULSES_PER_REVOLUTION";
            errorMessage = "Pulses per revolution must be from 1 to 8";
            return false;
        }
        if (!json["inverted"].isNull() && !json["inverted"].is<bool>())
        {
            errorCode = "INVALID_INVERTED_FLAG";
            errorMessage = "The inverted flag must be a boolean";
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
            sendMessage(request, 404, "NOT_FOUND", "Not found");
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
            sendMessage(request, 404, "NOT_FOUND", "Not found");
            return;
        }
        if (DeviceManager::saveToNVS()) {
            sendMessage(request, 200, "DEVICES_SAVED", "All device settings were saved");
        } else {
            sendMessage(request, 500, "NVS_SAVE_FAILED", "Failed to save to NVS");
        } });

        auto *postJsonHandler = new AsyncCallbackJsonWebHandler(
            "/api/devices", [](AsyncWebServerRequest *request, JsonVariant &json)
            {
            const String url = request->url();
            if (url == "/api/devices") {
                String name;
                const char *errorCode = nullptr;
                const char *errorMessage = nullptr;
                uint8_t pwmPin;
                int8_t rpmPin;
                bool inverted;
                uint8_t ppr;
                if (!readDeviceRequest(json, name, pwmPin, rpmPin, inverted, ppr,
                                       errorCode, errorMessage)) {
                    sendMessage(request, 400, errorCode, errorMessage);
                    return;
                }
                DeviceError error = DeviceError::NONE;
                const uint8_t id = DeviceManager::addDevice(name, pwmPin, rpmPin, inverted, ppr, error);
                if (id == 0) {
                    sendDeviceError(request, error);
                    return;
                }
                JsonDocument response;
                response["code"] = "DEVICE_ADDED";
                response["message"] = "Device added";
                response["id"] = id;
                String body;
                serializeJson(response, body);
                request->send(201, "application/json", body);
                return;
            }

            uint8_t id;
            if (!parseDeviceId(url, "/duty", id)) {
                sendMessage(request, 404, "NOT_FOUND", "Not found");
                return;
            }
            long duty;
            if (!readInteger(json, "dutyCycle", 0, 100, duty)) {
                sendMessage(request, 400, "INVALID_DUTY_CYCLE", "Duty cycle must be an integer from 0 to 100");
                return;
            }
            DeviceError error = DeviceError::NONE;
            if (!DeviceManager::setDutyCycle(id, static_cast<uint8_t>(duty), error)) {
                sendDeviceError(request, error);
                return;
            }
            sendMessage(request, 200, "DUTY_CYCLE_SET", "Duty cycle updated"); });
        postJsonHandler->setMethod(HTTP_POST);
        server.addHandler(postJsonHandler);

        // 无 body 的单设备占空比保存；JSON duty 请求会优先命中上方处理器。
        server.on("/api/devices/*", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
        uint8_t id;
        if (!parseDeviceId(request->url(), "/save", id)) {
            sendMessage(request, 404, "NOT_FOUND", "Not found");
            return;
        }
        DeviceError error = DeviceError::NONE;
        if (!DeviceManager::saveDutyToNVS(id, error)) {
            sendDeviceError(request, error);
            return;
        }
        sendMessage(request, 200, "DUTY_CYCLE_SAVED", "Duty cycle saved"); });

        auto *putJsonHandler = new AsyncCallbackJsonWebHandler(
            "/api/devices", [](AsyncWebServerRequest *request, JsonVariant &json)
            {
            uint8_t id;
            if (!parseDeviceId(request->url(), "", id)) {
                sendMessage(request, 404, "NOT_FOUND", "Not found");
                return;
            }
            String name;
            const char *errorCode = nullptr;
            const char *errorMessage = nullptr;
            uint8_t pwmPin;
            int8_t rpmPin;
            bool inverted;
            uint8_t ppr;
            if (!readDeviceRequest(json, name, pwmPin, rpmPin, inverted, ppr,
                                   errorCode, errorMessage)) {
                sendMessage(request, 400, errorCode, errorMessage);
                return;
            }
            DeviceError error = DeviceError::NONE;
            if (!DeviceManager::updateDevice(id, name, pwmPin, rpmPin, inverted, ppr, error)) {
                sendDeviceError(request, error);
                return;
            }
            sendMessage(request, 200, "DEVICE_UPDATED", "Device updated and saved"); });
        putJsonHandler->setMethod(HTTP_PUT);
        server.addHandler(putJsonHandler);

        server.on("/api/devices/*", HTTP_DELETE, [](AsyncWebServerRequest *request)
                  {
        uint8_t id;
        if (!parseDeviceId(request->url(), "", id)) {
            sendMessage(request, 404, "NOT_FOUND", "Not found");
            return;
        }
        DeviceError error = DeviceError::NONE;
        if (!DeviceManager::removeDevice(id, error)) {
            sendDeviceError(request, error);
            return;
        }
        sendMessage(request, 200, "DEVICE_DELETED", "Device deleted"); });
    }

    /** 注册 WiFi API。 */
    void registerWiFiAPI()
    {
        server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
        if (request->url() != "/api/wifi") {
            sendMessage(request, 404, "NOT_FOUND", "Not found");
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
                sendMessage(request, 400, "INVALID_REQUEST", "Invalid request format");
                return;
            }
            const String ssid = json["ssid"] | "";
            const String password = json["password"] | "";
            if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 64) {
                sendMessage(request, 400, "INVALID_WIFI_CREDENTIALS", "The SSID or password length is invalid");
                return;
            }
            if (!WiFiManager::requestConnect(ssid, password)) {
                sendMessage(request, 500, "WIFI_CONNECT_FAILED", "Unable to start the WiFi connection");
                return;
            }
            sendMessage(request, 202, "WIFI_CONNECTING", "Validating the new WiFi connection; the rescue AP remains active"); });
        wifiJsonHandler->setMethod(HTTP_POST);
        server.addHandler(wifiJsonHandler);
    }

    /** 注册系统信息 API。 */
    void registerSystemAPI()
    {
        server.on("/api/system/info", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
        if (request->url() != "/api/system/info") {
            sendMessage(request, 404, "NOT_FOUND", "Not found");
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
            sendMessage(request, 404, "NOT_FOUND", "Not found");
            return;
        }
        if (!ConfigManager::clearAll()) {
            sendMessage(request, 500, "NVS_CLEAR_FAILED", "Failed to clear NVS settings");
            return;
        }
        scheduleRestart(RestartReason::RESET_SETTINGS);
        sendMessage(request, 200, "SETTINGS_RESET", "Settings cleared; the controller will restart"); });
    }

    /** 注册 Web 登录认证配置 API。 */
    void registerWebAuthAPI()
    {
        server.on("/api/system/auth", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
        if (request->url() != "/api/system/auth") {
            sendMessage(request, 404, "NOT_FOUND", "Not found");
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
                sendMessage(request, 400, "INVALID_REQUEST", "Invalid request format");
                return;
            }

            String username = json["username"] | "";
            const String password = json["password"] | "";
            username.trim();
            if (!isValidWebUsername(username)) {
                sendMessage(request, 400, "INVALID_USERNAME", "Username must contain 1 to 32 letters, numbers, dots, underscores, or hyphens");
                return;
            }
            if (password.length() < MIN_WEB_PASSWORD_LENGTH ||
                password.length() > MAX_WEB_PASSWORD_LENGTH) {
                sendMessage(request, 400, "INVALID_PASSWORD_LENGTH", "Password must contain 8 to 64 characters");
                return;
            }

            const String passwordHash = generateDigestHash(username.c_str(), password.c_str(), WEB_REALM);
            if (passwordHash.length() != 32) {
                sendMessage(request, 500, "PASSWORD_HASH_FAILED", "Failed to generate the login password hash");
                return;
            }
            if (!ConfigManager::saveWebAuthConfig({username, passwordHash})) {
                sendMessage(request, 500, "AUTH_SAVE_FAILED", "Failed to save web login settings");
                return;
            }

            scheduleRestart(RestartReason::AUTH_CHANGED);
            sendMessage(request, 200, "AUTH_SAVED", "Web login settings saved; the controller will restart"); });
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
    authentication.setAuthFailureMessage("Administrator authentication required");
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
        // 中文入口复用同一个页面，前端根据路径选择语言。
        server.on("/zh", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(LittleFS, "/index.html", "text/html"); });
        server.on("/zh/", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(LittleFS, "/index.html", "text/html"); });
        server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    }

    server.onNotFound([](AsyncWebServerRequest *request)
                      { sendMessage(request, 404, "NOT_FOUND", "Not found"); });
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
