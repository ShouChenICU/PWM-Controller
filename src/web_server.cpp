/**
 * @file web_server.cpp
 * @brief Web 服务器模块实现
 *
 * 提供 RESTful API 接口和 LittleFS 静态资源托管。
 * API 路由：
 *   GET    /api/devices           获取设备列表
 *   POST   /api/devices           添加新设备
 *   PUT    /api/devices/{id}      更新设备配置
 *   DELETE /api/devices/{id}      删除设备
 *   POST   /api/devices/{id}/duty 设置占空比（仅内存）
 *   POST   /api/devices/save      持久化所有设备到 NVS
 *   GET    /api/wifi              获取 WiFi 配置
 *   POST   /api/wifi              更新 WiFi 配置并重连
 *   GET    /api/system/info       获取系统信息
 */

#include "web_server.h"
#include "device_manager.h"
#include "wifi_manager.h"
#include "config_manager.h"

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

/// Web 服务器实例
static AsyncWebServer server(WEB_SERVER_PORT);

// ==================== 辅助函数 ====================

/**
 * @brief 返回 JSON 错误响应
 * @param request 请求对象
 * @param code HTTP 状态码
 * @param message 错误信息
 */
static void sendError(AsyncWebServerRequest *request, int code, const String &message)
{
    JsonDocument doc;
    doc["error"] = message;
    String json;
    serializeJson(doc, json);
    request->send(code, "application/json", json);
}

/**
 * @brief 返回 JSON 成功响应
 * @param request 请求对象
 * @param message 成功信息
 */
static void sendOk(AsyncWebServerRequest *request, const String &message)
{
    JsonDocument doc;
    doc["message"] = message;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

/**
 * @brief 将设备数据序列化为 JSON 对象
 * @param dev 设备引用
 * @param obj JSON 对象引用
 */
static void deviceToJson(const Device &dev, JsonObject obj)
{
    obj["id"] = dev.config.id;
    obj["name"] = dev.config.name;
    obj["pwmPin"] = dev.config.pwmPin;
    obj["rpmPin"] = dev.config.rpmPin;
    obj["dutyCycle"] = dev.config.dutyCycle;
    obj["rpm"] = dev.rpm;
}

// ==================== API 路由注册 ====================

/**
 * @brief 注册设备相关 API
 */
static void registerDeviceAPI()
{

    // ---- GET /api/devices : 获取所有设备列表 ----
    server.on("/api/devices", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        const auto& devices = DeviceManager::getDevices();

        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (const auto& dev : devices) {
            JsonObject obj = arr.add<JsonObject>();
            deviceToJson(dev, obj);
        }

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json); });

    // ---- POST /api/devices : 添加新设备 ----
    server.on("/api/devices", HTTP_POST,
              // 请求完成回调（处理 body 数据在 onBody 中）
              [](AsyncWebServerRequest *request) {}, nullptr,
              // onBody 回调：接收请求体
              [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
              {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err) {
                sendError(request, 400, "JSON 解析失败");
                return;
            }

            String name = doc["name"] | "未命名";
            uint8_t pwmPin = doc["pwmPin"] | 0;
            int8_t rpmPin = doc["rpmPin"] | -1;

            uint8_t id = DeviceManager::addDevice(name, pwmPin, rpmPin);
            if (id == 0) {
                sendError(request, 500, "添加设备失败");
                return;
            }

            // 返回新设备信息
            JsonDocument respDoc;
            respDoc["message"] = "设备添加成功";
            respDoc["id"] = id;
            String json;
            serializeJson(respDoc, json);
            request->send(201, "application/json", json); });

    // ---- POST /api/devices/save : 持久化所有设备到 NVS ----
    server.on("/api/devices/save", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        if (DeviceManager::saveToNVS()) {
            sendOk(request, "设备配置已保存");
        } else {
            sendError(request, 500, "保存失败");
        } });
}

/**
 * @brief 注册需要路径参数的设备 API（使用 onRequestBody）
 *
 * ESPAsyncWebServer 对路径参数的支持有限，
 * 这里使用统一入口解析 URL 中的设备 ID。
 */
static void registerDeviceParamAPI()
{

    // 处理 /api/devices/xxx 的统一回调
    // 使用 server.onRequestBody 来处理带 body 的请求
    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
                         {
        String url = request->url();

        // ---- PUT /api/devices/{id} : 更新设备配置 ----
        if (request->method() == HTTP_PUT && url.startsWith("/api/devices/")) {
            String idStr = url.substring(13);  // 截取 /api/devices/ 之后的部分
            uint8_t id = idStr.toInt();

            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err) {
                sendError(request, 400, "JSON 解析失败");
                return;
            }

            String name = doc["name"] | "";
            uint8_t pwmPin = doc["pwmPin"] | 0;
            int8_t rpmPin = doc["rpmPin"] | -1;

            if (DeviceManager::updateDevice(id, name, pwmPin, rpmPin)) {
                sendOk(request, "设备已更新");
            } else {
                sendError(request, 404, "设备不存在");
            }
        }

        // ---- POST /api/devices/{id}/duty : 设置占空比 ----
        if (request->method() == HTTP_POST && url.indexOf("/duty") > 0 && url.startsWith("/api/devices/")) {
            // 解析 URL: /api/devices/{id}/duty
            String sub = url.substring(13);  // {id}/duty
            int slashPos = sub.indexOf('/');
            if (slashPos < 0) {
                sendError(request, 400, "URL 格式错误");
                return;
            }
            uint8_t id = sub.substring(0, slashPos).toInt();

            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err) {
                sendError(request, 400, "JSON 解析失败");
                return;
            }

            uint8_t dutyCycle = doc["dutyCycle"] | 0;
            if (DeviceManager::setDutyCycle(id, dutyCycle)) {
                sendOk(request, "占空比已设置");
            } else {
                sendError(request, 404, "设备不存在");
            }
        } });

    // ---- DELETE /api/devices/{id} : 删除设备 ----
    // DELETE 请求通常无 body，使用普通路由匹配
    server.onNotFound([](AsyncWebServerRequest *request)
                      {
        String url = request->url();

        if (request->method() == HTTP_DELETE && url.startsWith("/api/devices/")) {
            String idStr = url.substring(13);
            uint8_t id = idStr.toInt();

            if (DeviceManager::removeDevice(id)) {
                sendOk(request, "设备已删除");
            } else {
                sendError(request, 404, "设备不存在");
            }
            return;
        }

        // 其他未匹配的请求返回 404
        sendError(request, 404, "未找到"); });
}

/**
 * @brief 注册 WiFi 相关 API
 */
static void registerWiFiAPI()
{

    // ---- GET /api/wifi : 获取 WiFi 配置 ----
    server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        WiFiConfig config = ConfigManager::loadWiFiConfig();

        JsonDocument doc;
        doc["ssid"] = config.ssid;
        doc["isAP"] = WiFiManager::isAPMode();
        doc["ip"] = WiFiManager::getIP();
        // 不返回密码，安全考虑
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json); });

    // ---- POST /api/wifi : 更新 WiFi 配置并重连 ----
    server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
              {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err) {
                sendError(request, 400, "JSON 解析失败");
                return;
            }

            WiFiConfig config;
            config.ssid = doc["ssid"] | "";
            config.password = doc["password"] | "";

            if (config.ssid.isEmpty()) {
                sendError(request, 400, "SSID 不能为空");
                return;
            }

            // 保存到 NVS
            ConfigManager::saveWiFiConfig(config);

            // 先返回响应，再尝试重连（异步）
            sendOk(request, "WiFi 配置已保存，正在重连...");

            // 延迟重连，确保响应发送完成
            // 注意：实际重连会在 main loop 中触发，或使用定时器
            // 这里简单延迟后重连
            delay(1000);
            WiFiManager::connect(config.ssid, config.password); });
}

/**
 * @brief 注册系统信息 API
 */
static void registerSystemAPI()
{

    // ---- GET /api/system/info : 获取系统信息 ----
    server.on("/api/system/info", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        JsonDocument doc;
        doc["ip"] = WiFiManager::getIP();
        doc["isAP"] = WiFiManager::isAPMode();
        doc["uptime"] = millis() / 1000;  // 运行时间（秒）
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["chipModel"] = ESP.getChipModel();

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json); });
}

// ==================== 模块接口实现 ====================

void WebServer::init()
{
    Serial.println("[Web] Web 服务器模块初始化");

    // 初始化 LittleFS 文件系统
    if (!LittleFS.begin(true))
    {
        Serial.println("[Web] LittleFS 挂载失败!");
    }
    else
    {
        Serial.println("[Web] LittleFS 挂载成功");
    }

    // 注册 API 路由
    registerDeviceAPI();
    registerWiFiAPI();
    registerSystemAPI();
    registerDeviceParamAPI(); // 放在最后（包含 onNotFound）

    // 注册静态资源服务（LittleFS 根目录映射到 /）
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // 允许跨域请求（开发调试用）
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    // 启动服务器
    server.begin();
    Serial.printf("[Web] 服务器已启动，端口: %d\n", WEB_SERVER_PORT);
}
