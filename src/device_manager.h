/**
 * @file device_manager.h
 * @brief PWM 与转速设备管理模块
 *
 * 负责设备配置、LEDC PWM 输出、转速中断采样、并发保护和持久化。
 */
#pragma once

#include <Arduino.h>
#include <vector>
#include "config_manager.h"

/// PWM 频率（标准 4 线风扇目标频率为 25kHz）
static const uint32_t PWM_FREQUENCY = 25000;

/// PWM 分辨率（8 位，对应 0～255）
static const uint8_t PWM_RESOLUTION = 8;

/// 转速脉冲周期缓冲区大小
static const uint8_t RPM_PULSE_BUFFER_SIZE = 8;

/// 合法转速上限
static const uint16_t RPM_MAX_VALID = 10000;

/// 无有效脉冲时的停转超时（微秒）
static const uint32_t RPM_STALL_TIMEOUT_US = 2000000UL;

/// RPM 更新间隔（毫秒）
static const uint32_t RPM_UPDATE_INTERVAL_MS = 200;

/// RPM EMA 平滑系数
static const float RPM_EMA_ALPHA = 0.3f;

/// 启动时至少需要的有效周期样本数
static const uint8_t RPM_MIN_STARTUP_SAMPLES = 3;

/** 运行时设备对象；对象地址在设备存续期间保持稳定，供 ISR 使用。 */
struct Device
{
    DeviceConfig config;
    uint8_t savedDutyCycle;
    uint16_t rpm;
    bool pwmAttached;
    bool rpmAttached;
    uint32_t minimumPulsePeriodUs;
    volatile uint32_t pulsePeriods[RPM_PULSE_BUFFER_SIZE];
    volatile uint32_t pulseWriteCount;
    volatile uint32_t lastEdgeTime;
    volatile uint32_t lastValidTime;
    float rpmEma;
};

/** 提供给 Web 层的不可变设备快照。 */
struct DeviceStatus
{
    DeviceConfig config;
    uint8_t savedDutyCycle;
    uint16_t rpm;
};

/** 设备操作错误码，供 Web 层生成稳定的 API 响应。 */
enum class DeviceError : uint8_t
{
    NONE,
    INVALID_ID,
    INVALID_NAME,
    PWM_PIN_UNAVAILABLE,
    RPM_PIN_UNAVAILABLE,
    PIN_CONFLICT,
    INVALID_DUTY,
    INVALID_PULSES_PER_REVOLUTION,
    DUPLICATE_ID,
    GPIO_IN_USE,
    MANAGER_UNAVAILABLE,
    DEVICE_LIMIT_REACHED,
    PWM_ATTACH_FAILED,
    NVS_ADD_ROLLBACK,
    DEVICE_NOT_FOUND,
    PWM_RECONFIGURE_FAILED,
    NVS_UPDATE_ROLLBACK,
    NVS_DELETE_FAILED,
    PWM_WRITE_FAILED,
    NVS_SAVE_FAILED
};

namespace DeviceManager
{
    /** 从 NVS 加载、验证并初始化全部设备。 */
    void init();

    /** 周期计算转速，在主循环调用。 */
    void update();

    /** 获取线程安全的设备状态快照。 */
    void getStatuses(std::vector<DeviceStatus> &statuses);

    /** 添加设备；成功返回新 ID，失败返回 0 并填写错误码。 */
    uint8_t addDevice(const String &name, uint8_t pwmPin, int8_t rpmPin,
                      bool inverted, uint8_t pulsesPerRevolution, DeviceError &error);

    /** 更新设备配置并持久化。 */
    bool updateDevice(uint8_t id, const String &name, uint8_t pwmPin, int8_t rpmPin,
                      bool inverted, uint8_t pulsesPerRevolution, DeviceError &error);

    /** 删除设备并持久化。 */
    bool removeDevice(uint8_t id, DeviceError &error);

    /** 设置实时占空比，仅修改内存与 PWM 输出。 */
    bool setDutyCycle(uint8_t id, uint8_t dutyCycle, DeviceError &error);

    /** 持久化全部设备当前配置。 */
    bool saveToNVS();

    /** 仅确认指定设备的当前占空比为已保存值，底层采用整表原子保存。 */
    bool saveDutyToNVS(uint8_t id, DeviceError &error);

    /** 获取稳定的 API 错误码。 */
    const char *getErrorCode(DeviceError error);

    /** 获取默认英文错误消息。 */
    const char *getErrorMessage(DeviceError error);
} // namespace DeviceManager
