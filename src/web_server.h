/**
 * @file web_server.h
 * @brief Web 服务器模块
 *
 * 基于 ESPAsyncWebServer 提供 RESTful API 和静态资源托管。
 * 负责处理前端页面请求和设备控制 API。
 */
#pragma once

#include <Arduino.h>

// ==================== 常量定义 ====================

/// Web 服务器端口
static const uint16_t WEB_SERVER_PORT = 80;

// ==================== 模块接口 ====================

namespace WebServer
{

    /**
     * @brief 初始化并启动 Web 服务器
     *
     * 注册所有 API 路由和静态资源服务，然后启动监听。
     */
    void init();

} // namespace WebServer
