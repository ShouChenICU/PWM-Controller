/**
 * @file device_manager.cpp
 * @brief PWM 与转速设备管理模块实现
 */

#include "device_manager.h"
#include <algorithm>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace
{
    std::vector<std::unique_ptr<Device>> devices;
    SemaphoreHandle_t devicesMutex = nullptr;
    portMUX_TYPE rpmMux = portMUX_INITIALIZER_UNLOCKED;

    /** 自动获取和释放设备互斥锁。 */
    class DeviceLock
    {
    public:
        DeviceLock() : locked(devicesMutex && xSemaphoreTake(devicesMutex, portMAX_DELAY) == pdTRUE) {}
        ~DeviceLock()
        {
            if (locked)
            {
                xSemaphoreGive(devicesMutex);
            }
        }
        explicit operator bool() const { return locked; }

    private:
        bool locked;
    };

/** DevKitM-1 上允许用于 PWM 输出的 GPIO。 */
bool isAllowedPwmPin(int pin)
{
    // GPIO8 虽连接板载 RGB LED，但仍可作为普通 GPIO 使用。
    // GPIO9 影响下载模式，GPIO20/21 用于串口，GPIO11～17 未安全引出。
    static const uint8_t ALLOWED_PINS[] = {0, 1, 3, 4, 5, 6, 7, 8, 10, 18, 19};
    for (uint8_t allowed : ALLOWED_PINS)
        {
            if (pin == allowed)
            {
                return true;
            }
        }
    return false;
}

/** DevKitM-1 上允许用于 RPM 输入的 GPIO。 */
bool isAllowedRpmPin(int pin)
{
    // GPIO2 仅作为带上拉的输入使用；GPIO8 可能导致板载 RGB LED 闪烁。
    static const uint8_t ALLOWED_PINS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 18, 19};
    for (uint8_t allowed : ALLOWED_PINS)
    {
        if (pin == allowed)
        {
            return true;
        }
    }
    return false;
}

    /** 根据 ID 查找设备，调用者必须持有 devicesMutex。 */
    Device *findDeviceLocked(uint8_t id)
    {
        for (auto &device : devices)
        {
            if (device->config.id == id)
            {
                return device.get();
            }
        }
        return nullptr;
    }

    /** 验证配置与引脚冲突，ignoreId 用于更新现有设备。 */
    bool validateConfigLocked(const DeviceConfig &config, uint8_t ignoreId, String &error)
    {
        if (config.id == 0)
        {
            error = "设备 ID 无效";
            return false;
        }
        if (config.name.isEmpty() || config.name.length() > 20)
        {
            error = "设备名称长度必须为 1～20 个字符";
            return false;
        }
    if (!isAllowedPwmPin(config.pwmPin))
    {
        error = "PWM 引脚不可用，请使用 GPIO0、1、3～8、10、18 或 19";
        return false;
    }
    if (config.rpmPin >= 0 && !isAllowedRpmPin(config.rpmPin))
    {
        error = "转速引脚不可用，请使用 GPIO0～8、10、18 或 19（不含 GPIO9）";
            return false;
        }
        if (config.rpmPin >= 0 && config.rpmPin == config.pwmPin)
        {
            error = "PWM 与转速引脚不能相同";
            return false;
        }
        if (config.dutyCycle > 100)
        {
            error = "占空比必须为 0～100";
            return false;
        }
        if (config.pulsesPerRevolution < MIN_PULSES_PER_REVOLUTION ||
            config.pulsesPerRevolution > MAX_PULSES_PER_REVOLUTION)
        {
            error = "每转脉冲数必须为 1～8";
            return false;
        }

        for (const auto &device : devices)
        {
            if (device->config.id == ignoreId)
            {
                continue;
            }
            if (device->config.id == config.id)
            {
                error = "设备 ID 重复";
                return false;
            }

            const int existingPins[] = {device->config.pwmPin, device->config.rpmPin};
            const int newPins[] = {config.pwmPin, config.rpmPin};
            for (int newPin : newPins)
            {
                if (newPin < 0)
                {
                    continue;
                }
                for (int existingPin : existingPins)
                {
                    if (newPin == existingPin)
                    {
                        error = String("GPIO") + newPin + " 已被其他设备占用";
                        return false;
                    }
                }
            }
        }
        return true;
    }

    /** 将逻辑占空比写入 PWM。 */
    bool applyDuty(Device &device, uint8_t logicalDuty)
    {
        const uint8_t effectiveDuty = device.config.inverted ? 100 - logicalDuty : logicalDuty;
        const uint32_t rawDuty = map(effectiveDuty, 0, 100, 0, 255);
        return ledcWrite(device.config.pwmPin, rawDuty);
    }

    /** 初始化 PWM，失败时不保留半初始化状态。 */
    bool initPWM(Device &device)
    {
        if (!ledcAttach(device.config.pwmPin, PWM_FREQUENCY, PWM_RESOLUTION))
        {
            Serial.printf("[Device] PWM 初始化失败: pin=%u\n", device.config.pwmPin);
            return false;
        }
        device.pwmAttached = true;
        if (!applyDuty(device, device.config.dutyCycle))
        {
            ledcDetach(device.config.pwmPin);
            device.pwmAttached = false;
            Serial.printf("[Device] PWM 初始占空比写入失败: pin=%u\n", device.config.pwmPin);
            return false;
        }

        Serial.printf("[Device] PWM 初始化: pin=%u, duty=%u%%, inverted=%s\n",
                      device.config.pwmPin, device.config.dutyCycle,
                      device.config.inverted ? "是" : "否");
        return true;
    }

    /** RPM 下降沿 ISR：持续高频噪声会被全部拒绝，避免混叠成虚假转速。 */
    void IRAM_ATTR rpmISR(void *argument)
    {
        Device *device = static_cast<Device *>(argument);
        if (!device)
        {
            return;
        }

        uint32_t now = micros();
        if (now == 0)
        {
            now = 1;
        }

        portENTER_CRITICAL_ISR(&rpmMux);
        if (device->lastEdgeTime != 0)
        {
            const uint32_t sinceEdge = now - device->lastEdgeTime;
            device->lastEdgeTime = now;
            if (sinceEdge < device->minimumPulsePeriodUs)
            {
                portEXIT_CRITICAL_ISR(&rpmMux);
                return;
            }
        }
        else
        {
            device->lastEdgeTime = now;
        }

        if (device->lastValidTime != 0)
        {
            const uint32_t period = now - device->lastValidTime;
            const uint32_t position = device->pulseWriteCount % RPM_PULSE_BUFFER_SIZE;
            device->pulsePeriods[position] = period;
            device->pulseWriteCount = device->pulseWriteCount + 1U;
        }
        device->lastValidTime = now;
        portEXIT_CRITICAL_ISR(&rpmMux);
    }

    /** 清空 RPM 采样状态。 */
    void resetRpmState(Device &device)
    {
        portENTER_CRITICAL(&rpmMux);
        device.pulseWriteCount = 0;
        device.lastEdgeTime = 0;
        device.lastValidTime = 0;
        memset((void *)device.pulsePeriods, 0, sizeof(device.pulsePeriods));
        portEXIT_CRITICAL(&rpmMux);
        device.rpm = 0;
        device.rpmEma = 0.0f;
    }

    /** 初始化转速中断。 */
    void initRPM(Device &device)
    {
        // 阈值按 PPR 预计算，既拒绝超过合法转速的毛刺，也不压掉高 PPR 有效脉冲。
        device.minimumPulsePeriodUs =
            60000000UL /
            (static_cast<uint32_t>(device.config.pulsesPerRevolution) * RPM_MAX_VALID);
        resetRpmState(device);
        if (device.config.rpmPin < 0)
        {
            device.rpmAttached = false;
            return;
        }

        pinMode(device.config.rpmPin, INPUT_PULLUP);
        attachInterruptArg(device.config.rpmPin, rpmISR, &device, FALLING);
        device.rpmAttached = true;
        Serial.printf("[Device] 转速读取初始化: pin=%d, ppr=%u\n",
                      device.config.rpmPin, device.config.pulsesPerRevolution);
    }

    /** 停止设备；PWM 先切到风扇全速的故障安全状态，再释放引脚。 */
    void deinitDevice(Device &device)
    {
        if (device.rpmAttached && device.config.rpmPin >= 0)
        {
            detachInterrupt(device.config.rpmPin);
            device.rpmAttached = false;
            pinMode(device.config.rpmPin, INPUT);
        }

        if (device.pwmAttached)
        {
            applyDuty(device, 100);
            ledcDetach(device.config.pwmPin);
            device.pwmAttached = false;
            pinMode(device.config.pwmPin, INPUT);
        }
        resetRpmState(device);
    }

    /** 提取持久化配置；currentDuty=false 时保留各设备上次保存的占空比。 */
    std::vector<DeviceConfig> extractConfigsLocked(bool currentDuty)
    {
        std::vector<DeviceConfig> configs;
        configs.reserve(devices.size());
        for (const auto &device : devices)
        {
            DeviceConfig config = device->config;
            if (!currentDuty)
            {
                config.dutyCycle = device->savedDutyCycle;
            }
            configs.push_back(config);
        }
        return configs;
    }

    /** 小数组插入排序。 */
    void insertionSort(uint32_t *values, uint8_t count)
    {
        for (uint8_t i = 1; i < count; ++i)
        {
            const uint32_t key = values[i];
            int8_t j = static_cast<int8_t>(i) - 1;
            while (j >= 0 && values[j] > key)
            {
                values[j + 1] = values[j];
                --j;
            }
            values[j + 1] = key;
        }
    }
} // namespace

void DeviceManager::init()
{
    devicesMutex = xSemaphoreCreateMutex();
    if (!devicesMutex)
    {
        Serial.println("[Device] 创建设备互斥锁失败");
        return;
    }

    DeviceLock lock;
    if (!lock)
    {
        return;
    }

    const std::vector<DeviceConfig> configs = ConfigManager::loadDevices();
    for (const auto &config : configs)
    {
        if (devices.size() >= MAX_DEVICE_COUNT)
        {
            Serial.println("[Device] 超出硬件上限的配置已保留在 NVS，但本次不启用");
            break;
        }

        String error;
        if (!validateConfigLocked(config, 0, error))
        {
            Serial.printf("[Device] 跳过无效配置 id=%u: %s\n", config.id, error.c_str());
            continue;
        }

        auto device = std::make_unique<Device>();
        device->config = config;
        device->savedDutyCycle = config.dutyCycle;
        device->pwmAttached = false;
        device->rpmAttached = false;
        resetRpmState(*device);
        if (!initPWM(*device))
        {
            Serial.printf("[Device] 配置 id=%u 初始化失败，NVS 数据保持不变\n", config.id);
            continue;
        }
        initRPM(*device);
        devices.push_back(std::move(device));
    }

    Serial.printf("[Device] 共加载 %u 个设备\n", static_cast<unsigned>(devices.size()));
}

void DeviceManager::update()
{
    static uint32_t lastUpdateMs = 0;
    const uint32_t nowMs = millis();
    if (nowMs - lastUpdateMs < RPM_UPDATE_INTERVAL_MS)
    {
        return;
    }
    lastUpdateMs = nowMs;

    DeviceLock lock;
    if (!lock)
    {
        return;
    }

    const uint32_t nowUs = micros();
    for (auto &devicePtr : devices)
    {
        Device &device = *devicePtr;
        if (!device.rpmAttached || device.config.rpmPin < 0)
        {
            continue;
        }

        uint32_t writeCount;
        uint32_t lastValid;
        uint32_t periods[RPM_PULSE_BUFFER_SIZE];
        portENTER_CRITICAL(&rpmMux);
        writeCount = device.pulseWriteCount;
        lastValid = device.lastValidTime;
        memcpy(periods, (const void *)device.pulsePeriods, sizeof(periods));
        portEXIT_CRITICAL(&rpmMux);

        if (lastValid != 0 && nowUs - lastValid > RPM_STALL_TIMEOUT_US)
        {
            // 停转后清除旧样本，防止再次启动时复用历史转速。
            resetRpmState(device);
            continue;
        }

        const uint8_t count = writeCount < RPM_PULSE_BUFFER_SIZE
                                  ? static_cast<uint8_t>(writeCount)
                                  : RPM_PULSE_BUFFER_SIZE;
        if (count < RPM_MIN_STARTUP_SAMPLES)
        {
            device.rpm = 0;
            device.rpmEma = 0.0f;
            continue;
        }

        const uint32_t minimumPeriod = device.minimumPulsePeriodUs;
        uint32_t validPeriods[RPM_PULSE_BUFFER_SIZE];
        uint8_t validCount = 0;
        for (uint8_t i = 0; i < count; ++i)
        {
            if (periods[i] >= minimumPeriod && periods[i] <= RPM_STALL_TIMEOUT_US)
            {
                validPeriods[validCount++] = periods[i];
            }
        }

        if (validCount < RPM_MIN_STARTUP_SAMPLES)
        {
            device.rpm = 0;
            device.rpmEma = 0.0f;
            continue;
        }

        insertionSort(validPeriods, validCount);
        uint32_t medianPeriod;
        if ((validCount & 1U) == 0)
        {
            medianPeriod = static_cast<uint32_t>(
                (static_cast<uint64_t>(validPeriods[validCount / 2 - 1]) +
                 validPeriods[validCount / 2]) /
                2ULL);
        }
        else
        {
            medianPeriod = validPeriods[validCount / 2];
        }

        const uint64_t denominator =
            static_cast<uint64_t>(device.config.pulsesPerRevolution) * medianPeriod;
        const uint32_t calculatedRpm = denominator == 0 ? 0 : 60000000ULL / denominator;
        const uint16_t rawRpm = calculatedRpm <= RPM_MAX_VALID
                                    ? static_cast<uint16_t>(calculatedRpm)
                                    : 0;

        if (rawRpm == 0)
        {
            device.rpmEma = 0.0f;
        }
        else if (device.rpmEma < 1.0f)
        {
            device.rpmEma = rawRpm;
        }
        else
        {
            device.rpmEma = RPM_EMA_ALPHA * rawRpm +
                            (1.0f - RPM_EMA_ALPHA) * device.rpmEma;
        }
        device.rpm = static_cast<uint16_t>(device.rpmEma + 0.5f);
    }
}

void DeviceManager::getStatuses(std::vector<DeviceStatus> &statuses)
{
    statuses.clear();
    DeviceLock lock;
    if (!lock)
    {
        return;
    }

    statuses.reserve(devices.size());
    for (const auto &device : devices)
    {
        statuses.push_back({device->config, device->savedDutyCycle, device->rpm});
    }
}

uint8_t DeviceManager::addDevice(const String &name, uint8_t pwmPin, int8_t rpmPin,
                                 bool inverted, uint8_t pulsesPerRevolution, String &error)
{
    DeviceLock lock;
    if (!lock)
    {
        error = "设备管理器不可用";
        return 0;
    }
    if (devices.size() >= MAX_DEVICE_COUNT)
    {
        error = "ESP32-C3 最多支持 6 路独立 PWM";
        return 0;
    }

    const std::vector<DeviceConfig> currentConfigs = extractConfigsLocked(false);
    DeviceConfig config;
    config.id = ConfigManager::nextDeviceId(currentConfigs);
    config.name = name;
    config.pwmPin = pwmPin;
    config.rpmPin = rpmPin;
    config.dutyCycle = DEFAULT_DUTY_CYCLE;
    config.inverted = inverted;
    config.pulsesPerRevolution = pulsesPerRevolution;

    if (!validateConfigLocked(config, 0, error))
    {
        return 0;
    }

    auto device = std::make_unique<Device>();
    device->config = config;
    device->savedDutyCycle = config.dutyCycle;
    device->pwmAttached = false;
    device->rpmAttached = false;
    resetRpmState(*device);
    if (!initPWM(*device))
    {
        error = "PWM 通道分配失败";
        return 0;
    }
    initRPM(*device);
    devices.push_back(std::move(device));

    if (!ConfigManager::saveDevices(extractConfigsLocked(false)))
    {
        deinitDevice(*devices.back());
        devices.pop_back();
        error = "NVS 保存失败，添加操作已回滚";
        return 0;
    }

    Serial.printf("[Device] 添加设备: id=%u, name=%s\n", config.id, config.name.c_str());
    return config.id;
}

bool DeviceManager::updateDevice(uint8_t id, const String &name, uint8_t pwmPin, int8_t rpmPin,
                                 bool inverted, uint8_t pulsesPerRevolution, String &error)
{
    DeviceLock lock;
    if (!lock)
    {
        error = "设备管理器不可用";
        return false;
    }

    Device *device = findDeviceLocked(id);
    if (!device)
    {
        error = "设备不存在";
        return false;
    }

    const DeviceConfig oldConfig = device->config;
    DeviceConfig newConfig = oldConfig;
    newConfig.name = name;
    newConfig.pwmPin = pwmPin;
    newConfig.rpmPin = rpmPin;
    newConfig.inverted = inverted;
    newConfig.pulsesPerRevolution = pulsesPerRevolution;
    if (!validateConfigLocked(newConfig, id, error))
    {
        return false;
    }

    deinitDevice(*device);
    device->config = newConfig;
    if (!initPWM(*device))
    {
        device->config = oldConfig;
        initPWM(*device);
        initRPM(*device);
        error = "新 PWM 配置初始化失败，已恢复旧配置";
        return false;
    }
    initRPM(*device);

    if (!ConfigManager::saveDevices(extractConfigsLocked(false)))
    {
        deinitDevice(*device);
        device->config = oldConfig;
        initPWM(*device);
        initRPM(*device);
        error = "NVS 保存失败，更新操作已回滚";
        return false;
    }

    Serial.printf("[Device] 更新设备: id=%u, name=%s\n", id, name.c_str());
    return true;
}

bool DeviceManager::removeDevice(uint8_t id, String &error)
{
    DeviceLock lock;
    if (!lock)
    {
        error = "设备管理器不可用";
        return false;
    }

    auto iterator = std::find_if(devices.begin(), devices.end(),
                                 [id](const std::unique_ptr<Device> &device)
                                 { return device->config.id == id; });
    if (iterator == devices.end())
    {
        error = "设备不存在";
        return false;
    }

    std::vector<DeviceConfig> remainingConfigs;
    remainingConfigs.reserve(devices.size() - 1);
    for (const auto &device : devices)
    {
        if (device->config.id != id)
        {
            DeviceConfig config = device->config;
            config.dutyCycle = device->savedDutyCycle;
            remainingConfigs.push_back(config);
        }
    }
    if (!ConfigManager::saveDevices(remainingConfigs))
    {
        error = "NVS 保存失败，未删除设备";
        return false;
    }

    deinitDevice(**iterator);
    devices.erase(iterator);
    Serial.printf("[Device] 删除设备: id=%u\n", id);
    return true;
}

bool DeviceManager::setDutyCycle(uint8_t id, uint8_t dutyCycle, String &error)
{
    if (dutyCycle > 100)
    {
        error = "占空比必须为 0～100";
        return false;
    }

    DeviceLock lock;
    if (!lock)
    {
        error = "设备管理器不可用";
        return false;
    }
    Device *device = findDeviceLocked(id);
    if (!device)
    {
        error = "设备不存在";
        return false;
    }
    if (!applyDuty(*device, dutyCycle))
    {
        error = "PWM 写入失败";
        return false;
    }

    device->config.dutyCycle = dutyCycle;
    return true;
}

bool DeviceManager::saveToNVS()
{
    DeviceLock lock;
    if (!lock)
    {
        return false;
    }
    if (!ConfigManager::saveDevices(extractConfigsLocked(true)))
    {
        return false;
    }
    for (auto &device : devices)
    {
        device->savedDutyCycle = device->config.dutyCycle;
    }
    return true;
}

bool DeviceManager::saveDutyToNVS(uint8_t id, String &error)
{
    DeviceLock lock;
    if (!lock)
    {
        error = "设备管理器不可用";
        return false;
    }
    Device *target = findDeviceLocked(id);
    if (!target)
    {
        error = "设备不存在";
        return false;
    }

    std::vector<DeviceConfig> configs;
    configs.reserve(devices.size());
    for (const auto &device : devices)
    {
        DeviceConfig config = device->config;
        if (device.get() != target)
        {
            config.dutyCycle = device->savedDutyCycle;
        }
        configs.push_back(config);
    }
    if (!ConfigManager::saveDevices(configs))
    {
        error = "NVS 保存失败";
        return false;
    }

    target->savedDutyCycle = target->config.dutyCycle;
    return true;
}
