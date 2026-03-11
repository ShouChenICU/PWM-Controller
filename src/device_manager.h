/**
 * @file device_manager.h
 * @brief 设备管理模块
 *
 * 负责 PWM 设备的运行管理，包括 PWM 输出控制、
 * 转速读取（ISR 毛刺过滤 + 脉冲间隔测量 + 中位值滤波 + EMA 平滑）、
 * 设备列表的增删改查。设备数据在内存中维护，需手动触发持久化到 NVS。
 */
#pragma once

#include <Arduino.h>
#include <vector>
#include "config_manager.h"

// ==================== 常量定义 ====================

/// PWM 频率（Hz），25kHz 适合 4 线 PWM 风扇
static const uint32_t PWM_FREQUENCY = 25000;

/// PWM 分辨率（位数），8 位对应 0-255
static const uint8_t PWM_RESOLUTION = 8;

/// 最大支持转速读取的设备数量（受中断引脚限制）
static const uint8_t MAX_RPM_DEVICES = 8;

/// 风扇每转脉冲数（通常为 2）
static const uint8_t PULSES_PER_REVOLUTION = 2;

/// 脉冲间隔环形缓冲区大小（保留最近 N 个间隔用于中位值滤波）
static const uint8_t RPM_PULSE_BUFFER_SIZE = 8;

/// ISR 去抖阈值（微秒）：距上次任意边沿小于此值的中断直接丢弃
/// 该值控制的是连续边沿间的最短间隔，防止抖动/噪声混叠
/// 1000 us 即可过滤大部分电气噪声和信号抖动
static const uint32_t RPM_DEBOUNCE_US = 1000UL;

/// 合法 RPM 上限：超过此值的计算结果视为噪声，钳位到 0
/// 普通 PC/服务器风扇一般不超过 6000 RPM
static const uint16_t RPM_MAX_VALID = 10000;

/// 风扇停止判定超时（微秒）：超过此时间无脉冲则 RPM 置 0
/// 2,000,000 us = 2 秒，对应可检测最低转速约 15 RPM（2 脉冲/转）
static const uint32_t RPM_STALL_TIMEOUT_US = 2000000UL;

/// 转速更新间隔（毫秒）：降低计算频率，减少 CPU 占用
static const uint32_t RPM_UPDATE_INTERVAL_MS = 200;

/// 转速 EMA 平滑系数（0.0～1.0），值越大响应越快，值越小读数越平滑
static const float RPM_EMA_ALPHA = 0.3f;

// ==================== 运行时设备数据 ====================

/**
 * @brief 运行时设备结构体
 *
 * 在 DeviceConfig 基础上增加运行时数据（转速等）。
 */
struct Device
{
    DeviceConfig config;                                   ///< 设备配置（来自 NVS）
    uint16_t rpm;                                          ///< 当前转速（RPM），仅 rpmPin != -1 时有效
    uint8_t pwmChannel;                                    ///< 分配的 LEDC PWM 通道号
    volatile uint32_t pulsePeriods[RPM_PULSE_BUFFER_SIZE]; ///< 脉冲间隔环形缓冲区（微秒，ISR 计算相邻有效脉冲差值后写入）
    volatile uint32_t pulseWriteCount;                     ///< 已写入的有效间隔数（单调递增，用于寻址缓冲区）
    volatile uint32_t lastEdgeTime;                        ///< 上次任意中断边沿时刻（微秒，每次 ISR 触发都更新，防混叠）
    volatile uint32_t lastValidTime;                       ///< 上次通过去抖的有效脉冲时刻（微秒，用于计算周期）
    float rpmEma;                                          ///< EMA 内部平滑状态
};

// ==================== 模块接口 ====================

namespace DeviceManager
{

    /**
     * @brief 初始化设备管理模块
     *
     * 从 NVS 加载设备列表，初始化各设备的 PWM 输出和转速读取。
     */
    void init();

    /**
     * @brief 周期性更新（在 loop 中调用）
     *
     * 计算各设备的转速值（基于中断脉冲计数）。
     */
    void update();

    /**
     * @brief 获取所有设备列表的引用
     * @return 设备列表的常量引用
     */
    const std::vector<Device> &getDevices();

    /**
     * @brief 根据 ID 查找设备
     * @param id 设备ID
     * @return 指向设备的指针，未找到返回 nullptr
     */
    Device *findDevice(uint8_t id);

    /**
     * @brief 添加新设备
     * @param name 设备名称
     * @param pwmPin PWM 输出引脚
     * @param rpmPin 转速读取引脚（-1 表示无）
     * @param inverted 是否反转信号
     * @return 新设备的 ID，失败返回 0
     */
    uint8_t addDevice(const String &name, uint8_t pwmPin, int8_t rpmPin, bool inverted = false);

    /**
     * @brief 更新设备配置
     * @param id 设备ID
     * @param name 新名称
     * @param pwmPin 新 PWM 引脚
     * @param rpmPin 新转速引脚
     * @param inverted 是否反转信号
     * @return true 更新成功，false 未找到设备
     */
    bool updateDevice(uint8_t id, const String &name, uint8_t pwmPin, int8_t rpmPin, bool inverted = false);

    /**
     * @brief 删除设备
     * @param id 设备ID
     * @return true 删除成功，false 未找到设备
     */
    bool removeDevice(uint8_t id);

    /**
     * @brief 设置设备占空比（仅内存，不持久化）
     * @param id 设备ID
     * @param dutyCycle 占空比 (0-100)
     * @return true 设置成功，false 未找到设备
     */
    bool setDutyCycle(uint8_t id, uint8_t dutyCycle);

    /**
     * @brief 将当前所有设备配置持久化到 NVS
     * @return true 保存成功，false 保存失败
     */
    bool saveToNVS();

    /**
     * @brief 仅将指定设备的当前占空比持久化到 NVS（其他设备不受影响）
     * @param id 设备ID
     * @return true 保存成功，false 未找到设备或保存失败
     */
    bool saveDutyToNVS(uint8_t id);

    /**
     * @brief 获取指定设备上一次保存到 NVS 的占空比
     * @param id 设备ID
     * @return 已保存的占空比 (0-100)
     */
    uint8_t getSavedDuty(uint8_t id);

} // namespace DeviceManager
