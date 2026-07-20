/**
 * @file config_manager.h
 * @brief NVS 配置管理模块
 *
 * 负责将设备列表、WiFi 配置等数据持久化到 NVS 中，
 * 以及从 NVS 中读取配置数据。使用命名空间 "pwm_ctrl"。
 */
#pragma once

#include <Arduino.h>
#include <vector>

// ==================== 常量定义 ====================

/// 设备默认占空比 (0-100)
static const uint8_t DEFAULT_DUTY_CYCLE = 10;

/// 最大设备数量（ESP32-C3 LEDC 只有 6 个独立通道）
static const uint8_t MAX_DEVICE_COUNT = 6;

/// 默认每转脉冲数
static const uint8_t DEFAULT_PULSES_PER_REVOLUTION = 2;

/// 每转脉冲数允许范围
static const uint8_t MIN_PULSES_PER_REVOLUTION = 1;
static const uint8_t MAX_PULSES_PER_REVOLUTION = 8;

// ==================== 数据结构 ====================

/**
 * @brief 设备配置结构体
 *
 * 表示一个 PWM 控制设备的完整配置信息。
 * id 为设备唯一标识，pwmPin 为 PWM 输出引脚，
 * rpmPin 为转速读取引脚（-1 表示无此功能）。
 */
struct DeviceConfig
{
    uint8_t id;                  ///< 设备唯一ID
    String name;                 ///< 设备名称
    uint8_t pwmPin;              ///< PWM 输出引脚
    int8_t rpmPin;               ///< 转速读取引脚（-1 表示无）
    uint8_t dutyCycle;           ///< 当前占空比 (0-100)
    bool inverted;               ///< 是否反转 PWM 信号
    uint8_t pulsesPerRevolution; ///< 转速信号每转脉冲数
};

/**
 * @brief WiFi 配置结构体
 */
struct WiFiConfig
{
    String ssid;     ///< WiFi SSID
    String password; ///< WiFi 密码
};

/**
 * @brief Web 登录认证配置结构体
 *
 * passwordHash 保存摘要认证 HA1 哈希，不在 NVS 中存储明文密码。
 */
struct WebAuthConfig
{
    String username;     ///< 登录用户名
    String passwordHash; ///< 摘要认证 HA1 哈希
};

// ==================== 模块接口 ====================

namespace ConfigManager
{

    /**
     * @brief 初始化配置管理模块
     *
     * 初始化 NVS 存储，必须在其他操作前调用。
     */
    void init();

    // ---------- WiFi 配置 ----------

    /**
     * @brief 从 NVS 读取 WiFi 配置
     * @return WiFiConfig 结构体，如果未配置则返回空字符串
     */
    WiFiConfig loadWiFiConfig();

    /**
     * @brief 保存 WiFi 配置到 NVS
     * @param config WiFi 配置信息
     * @return true 保存成功，false 保存失败
     */
    bool saveWiFiConfig(const WiFiConfig &config);

    // ---------- Web 登录认证 ----------

    /**
     * @brief 从 NVS 读取 Web 登录认证配置
     * @return WebAuthConfig 结构体，未配置或数据无效时返回空字符串
     */
    WebAuthConfig loadWebAuthConfig();

    /**
     * @brief 保存 Web 登录认证配置
     * @param config 用户名与摘要认证哈希
     * @return true 保存成功，false 保存失败
     */
    bool saveWebAuthConfig(const WebAuthConfig &config);

    // ---------- 设备配置 ----------

    /**
     * @brief 从 NVS 加载所有设备配置
     * @return 设备配置列表
     */
    std::vector<DeviceConfig> loadDevices();

    /**
     * @brief 保存所有设备配置到 NVS
     * @param devices 设备配置列表
     * @return true 保存成功，false 保存失败
     */
    bool saveDevices(const std::vector<DeviceConfig> &devices);

    /**
     * @brief 清空本项目命名空间内的全部 NVS 配置
     * @return true 清空成功，false 清空失败
     */
    bool clearAll();

    /**
     * @brief 生成下一个可用的设备ID
     * @param devices 当前设备列表
     * @return 新的唯一设备ID
     */
    uint8_t nextDeviceId(const std::vector<DeviceConfig> &devices);

} // namespace ConfigManager
