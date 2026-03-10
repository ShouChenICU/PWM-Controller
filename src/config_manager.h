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

/// NVS 命名空间
static const char *NVS_NAMESPACE = "pwm_ctrl";

/// NVS 键名
static const char *NVS_KEY_WIFI_SSID = "wifi_ssid";
static const char *NVS_KEY_WIFI_PASS = "wifi_pass";
static const char *NVS_KEY_DEV_COUNT = "dev_count";
static const char *NVS_KEY_DEV_PREFIX = "dev_"; // 设备键前缀，完整键名为 dev_{id}

/// 设备默认占空比 (0-100)
static const uint8_t DEFAULT_DUTY_CYCLE = 10;

/// 最大设备数量
static const uint8_t MAX_DEVICE_COUNT = 16;

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
    uint8_t id;        ///< 设备唯一ID
    String name;       ///< 设备名称
    uint8_t pwmPin;    ///< PWM 输出引脚
    int8_t rpmPin;     ///< 转速读取引脚（-1 表示无）
    uint8_t dutyCycle; ///< 当前占空比 (0-100)
};

/**
 * @brief WiFi 配置结构体
 */
struct WiFiConfig
{
    String ssid;     ///< WiFi SSID
    String password; ///< WiFi 密码
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
     * @brief 生成下一个可用的设备ID
     * @param devices 当前设备列表
     * @return 新的唯一设备ID
     */
    uint8_t nextDeviceId(const std::vector<DeviceConfig> &devices);

} // namespace ConfigManager
