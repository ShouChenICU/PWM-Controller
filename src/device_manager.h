/**
 * @file device_manager.h
 * @brief 设备管理模块
 *
 * 负责 PWM 设备的运行管理，包括 PWM 输出控制、
 * 转速读取（基于中断计数）、设备列表的增删改查。
 * 设备数据在内存中维护，需手动触发持久化到 NVS。
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

/// 转速计算间隔（毫秒），每秒计算一次
static const unsigned long RPM_CALC_INTERVAL_MS = 1000;

/// 风扇每转脉冲数（通常为 2）
static const uint8_t PULSES_PER_REVOLUTION = 2;

/// 最大支持转速读取的设备数量（受中断引脚限制）
static const uint8_t MAX_RPM_DEVICES = 8;

// ==================== 运行时设备数据 ====================

/**
 * @brief 运行时设备结构体
 *
 * 在 DeviceConfig 基础上增加运行时数据（转速等）。
 */
struct Device
{
    DeviceConfig config;          ///< 设备配置（来自 NVS）
    uint16_t rpm;                 ///< 当前转速（RPM），仅 rpmPin != -1 时有效
    uint8_t pwmChannel;           ///< 分配的 LEDC PWM 通道号
    volatile uint32_t pulseCount; ///< 转速脉冲计数（ISR 中递增）
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
     * @return 新设备的 ID，失败返回 0
     */
    uint8_t addDevice(const String &name, uint8_t pwmPin, int8_t rpmPin);

    /**
     * @brief 更新设备配置
     * @param id 设备ID
     * @param name 新名称
     * @param pwmPin 新 PWM 引脚
     * @param rpmPin 新转速引脚
     * @return true 更新成功，false 未找到设备
     */
    bool updateDevice(uint8_t id, const String &name, uint8_t pwmPin, int8_t rpmPin);

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

} // namespace DeviceManager
