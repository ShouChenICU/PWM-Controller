/**
 * @file device_manager.cpp
 * @brief 设备管理模块实现
 *
 * 管理设备列表、PWM 输出控制和转速读取。
 * 使用 ESP32 LEDC 外设输出 PWM，使用 GPIO 中断统计转速脉冲。
 */

#include "device_manager.h"
#include "config_manager.h"

// ==================== 内部数据 ====================

/// 设备列表（运行时数据）
static std::vector<Device> devices;

/// 下一个可用的 PWM 通道号
static uint8_t nextChannel = 0;

// ==================== 中断服务程序 ====================

/**
 * @brief 转速脉冲中断回调表
 *
 * ESP32 的 GPIO 中断需要 C 风格的函数指针，
 * 这里为每个可能的转速设备创建独立的 ISR。
 * 通过查找表将中断回调映射到对应的设备。
 */

/// 用于 ISR 的设备指针数组（最多 MAX_RPM_DEVICES 个）
static Device *rpmDevicePtrs[MAX_RPM_DEVICES] = {nullptr};
static uint8_t rpmDeviceCount = 0;

/**
 * @brief ISR 内计算相邻有效脉冲间隔并写入环形缓冲区
 *
 * 去抖/抗混叠策略：
 *   - lastEdgeTime 在每次中断触发时始终更新，此举显著为关键：
 *     若仅在脉冲通过过滤时才更新，高频噪声（如 25kHz PWM 泄漏）
 *     会在 ~DEBOUNCE_US 后积累成假的“有效间隔”，产生混叠假读数。
 *   - 始终更新后，连续噪声的每个边沿只看到 ~40μs，永远被拒绝。
 *   - lastValidTime 单独追踪最近一次通过去抖的脉冲，用于计算真实周期。
 */
#define DEFINE_RPM_ISR(idx)                                 \
    static void IRAM_ATTR rpmISR_##idx()                    \
    {                                                       \
        Device *d = rpmDevicePtrs[idx];                     \
        if (!d)                                             \
            return;                                         \
        uint32_t now = micros();                            \
        if (now == 0)                                       \
            now = 1; /* 避免与哨兵值 0 混淆 */              \
        /* 去抖：距上次任意边沿太近则丢弃 */                \
        if (d->lastEdgeTime != 0)                           \
        {                                                   \
            uint32_t sinceEdge = now - d->lastEdgeTime;     \
            d->lastEdgeTime = now; /* 始终更新，防混叠 */   \
            if (sinceEdge < RPM_DEBOUNCE_US)                \
                return;                                     \
        }                                                   \
        else                                                \
        {                                                   \
            d->lastEdgeTime = now;                          \
        }                                                   \
        /* 通过去抖，视为有效脉冲 */                        \
        if (d->lastValidTime != 0)                          \
        {                                                   \
            uint32_t period = now - d->lastValidTime;       \
            uint32_t pos =                                  \
                d->pulseWriteCount % RPM_PULSE_BUFFER_SIZE; \
            d->pulsePeriods[pos] = period;                  \
            d->pulseWriteCount++;                           \
        }                                                   \
        d->lastValidTime = now;                             \
    }

// 定义 8 个 ISR（对应最多 8 个转速设备）
DEFINE_RPM_ISR(0)
DEFINE_RPM_ISR(1)
DEFINE_RPM_ISR(2)
DEFINE_RPM_ISR(3)
DEFINE_RPM_ISR(4)
DEFINE_RPM_ISR(5)
DEFINE_RPM_ISR(6)
DEFINE_RPM_ISR(7)

/// ISR 函数指针表
static void (*rpmISRTable[MAX_RPM_DEVICES])() = {
    rpmISR_0, rpmISR_1, rpmISR_2, rpmISR_3,
    rpmISR_4, rpmISR_5, rpmISR_6, rpmISR_7};

// ==================== 内部辅助函数 ====================

/**
 * @brief 初始化单个设备的 PWM 输出
 * @param dev 设备引用
 */
static void initPWM(Device &dev)
{
    dev.pwmChannel = nextChannel++;

    // 配置 LEDC 通道
    ledcAttach(dev.config.pwmPin, PWM_FREQUENCY, PWM_RESOLUTION);

    // 设置初始占空比（将百分比映射到 0-255）
    uint8_t effectiveDuty = dev.config.inverted ? (100 - dev.config.dutyCycle) : dev.config.dutyCycle;
    uint32_t duty = map(effectiveDuty, 0, 100, 0, 255);
    ledcWrite(dev.config.pwmPin, duty);

    Serial.printf("[Device] PWM 初始化: pin=%d, channel=%d, duty=%d%%, inverted=%s\n",
                  dev.config.pwmPin, dev.pwmChannel, dev.config.dutyCycle, dev.config.inverted ? "是" : "否");
}

/**
 * @brief 初始化单个设备的转速读取
 * @param dev 设备引用
 */
static void initRPM(Device &dev)
{
    if (dev.config.rpmPin < 0)
        return;
    if (rpmDeviceCount >= MAX_RPM_DEVICES)
    {
        Serial.printf("[Device] 转速设备数量已满，无法为 pin=%d 注册中断\n", dev.config.rpmPin);
        return;
    }

    // 配置引脚为输入上拉
    pinMode(dev.config.rpmPin, INPUT_PULLUP);

    // 注册中断
    uint8_t idx = rpmDeviceCount;
    rpmDevicePtrs[idx] = &dev;
    dev.pulseWriteCount = 0;
    dev.lastEdgeTime = 0;
    dev.lastValidTime = 0;
    memset((void *)dev.pulsePeriods, 0, sizeof(dev.pulsePeriods));
    dev.rpmEma = 0.0f;
    dev.rpm = 0;
    attachInterrupt(digitalPinToInterrupt(dev.config.rpmPin), rpmISRTable[idx], FALLING);
    rpmDeviceCount++;

    Serial.printf("[Device] 转速读取初始化: pin=%d, ISR索引=%d\n", dev.config.rpmPin, idx);
}

/**
 * @brief 停止单个设备的 PWM 和中断
 * @param dev 设备引用
 */
static void deinitDevice(Device &dev)
{
    // 先将占空比置 0，使引脚输出低电平
    ledcWrite(dev.config.pwmPin, 0);

    // 停止 PWM 并重置 GPIO 状态
    ledcDetach(dev.config.pwmPin);
    pinMode(dev.config.pwmPin, INPUT);

    // 解除转速中断
    if (dev.config.rpmPin >= 0)
    {
        detachInterrupt(digitalPinToInterrupt(dev.config.rpmPin));
        // 从 ISR 表中移除
        for (uint8_t i = 0; i < rpmDeviceCount; i++)
        {
            if (rpmDevicePtrs[i] == &dev)
            {
                rpmDevicePtrs[i] = nullptr;
                break;
            }
        }
    }

    Serial.printf("[Device] 设备已停止: id=%d\n", dev.config.id);
}

// ==================== 模块接口实现 ====================

void DeviceManager::init()
{
    Serial.println("[Device] 设备管理模块初始化");

    // 从 NVS 加载设备配置
    std::vector<DeviceConfig> configs = ConfigManager::loadDevices();

    // 初始化各设备
    for (auto &cfg : configs)
    {
        Device dev;
        dev.config = cfg;
        dev.rpm = 0;
        dev.pulseWriteCount = 0;
        dev.lastEdgeTime = 0;
        dev.lastValidTime = 0;
        dev.rpmEma = 0.0f;
        dev.pwmChannel = 0;

        devices.push_back(dev);
    }

    // 初始化 PWM 和转速读取（在 push_back 之后，地址稳定）
    for (auto &dev : devices)
    {
        initPWM(dev);
        initRPM(dev);
    }

    Serial.printf("[Device] 共加载 %d 个设备\n", devices.size());
}

/**
 * @brief 对小数组进行插入排序（用于中位值滤波）
 * @param arr 数组指针
 * @param n 元素数量
 */
static void insertionSort(uint32_t *arr, uint8_t n)
{
    for (uint8_t i = 1; i < n; i++)
    {
        uint32_t key = arr[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && arr[j] > key)
        {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

void DeviceManager::update()
{
    // 限制更新频率，避免每次 loop 都计算
    static uint32_t lastUpdateMs = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastUpdateMs < RPM_UPDATE_INTERVAL_MS)
        return;
    lastUpdateMs = nowMs;

    uint32_t nowUs = micros();

    for (auto &dev : devices)
    {
        if (dev.config.rpmPin < 0)
            continue;

        // 原子读取脉冲间隔缓冲区副本
        noInterrupts();
        uint32_t writeCount = dev.pulseWriteCount;
        uint32_t lastValid = dev.lastValidTime;
        uint32_t periods[RPM_PULSE_BUFFER_SIZE];
        memcpy(periods, (const void *)dev.pulsePeriods, sizeof(periods));
        interrupts();

        uint16_t rawRpm = 0;

        if (lastValid == 0 || writeCount == 0)
        {
            // 从未收到有效脉冲间隔
            rawRpm = 0;
        }
        else if ((nowUs - lastValid) > RPM_STALL_TIMEOUT_US)
        {
            // 超时，风扇已停止（或信号全是噪声无法解读）
            rawRpm = 0;
        }
        else
        {
            // 取缓冲区中有效的间隔数量
            uint8_t count = (writeCount < RPM_PULSE_BUFFER_SIZE)
                                ? (uint8_t)writeCount
                                : RPM_PULSE_BUFFER_SIZE;

            // 复制有效数据并排序（中位值滤波）
            uint32_t sorted[RPM_PULSE_BUFFER_SIZE];
            memcpy(sorted, periods, count * sizeof(uint32_t));
            insertionSort(sorted, count);

            // 取中位值：对称地取中间元素，拒绝离群值干扰
            uint32_t medianPeriod = sorted[count / 2];

            if (medianPeriod > 0)
            {
                uint16_t calcRpm = (uint16_t)(60000000UL / ((uint32_t)PULSES_PER_REVOLUTION * medianPeriod));
                // 合法性钳位：超过上限视为噪声
                rawRpm = (calcRpm <= RPM_MAX_VALID) ? calcRpm : 0;
            }
        }

        // EMA 平滑：风扇停止时立刻归零，首次有效读数直接赋值避免从 0 慢慢爬升
        if (rawRpm == 0)
        {
            dev.rpmEma = 0.0f;
        }
        else if (dev.rpmEma < 1.0f)
        {
            dev.rpmEma = (float)rawRpm;
        }
        else
        {
            dev.rpmEma = RPM_EMA_ALPHA * (float)rawRpm + (1.0f - RPM_EMA_ALPHA) * dev.rpmEma;
        }

        dev.rpm = (uint16_t)(dev.rpmEma + 0.5f); // 四舍五入
    }
}

const std::vector<Device> &DeviceManager::getDevices()
{
    return devices;
}

Device *DeviceManager::findDevice(uint8_t id)
{
    for (auto &dev : devices)
    {
        if (dev.config.id == id)
        {
            return &dev;
        }
    }
    return nullptr;
}

uint8_t DeviceManager::addDevice(const String &name, uint8_t pwmPin, int8_t rpmPin, bool inverted)
{
    if (devices.size() >= MAX_DEVICE_COUNT)
    {
        Serial.println("[Device] 设备数量已达上限");
        return 0;
    }

    // 获取下一个 ID
    std::vector<DeviceConfig> configs;
    for (const auto &d : devices)
    {
        configs.push_back(d.config);
    }

    Device dev;
    dev.config.id = ConfigManager::nextDeviceId(configs);
    dev.config.name = name;
    dev.config.pwmPin = pwmPin;
    dev.config.rpmPin = rpmPin;
    dev.config.dutyCycle = DEFAULT_DUTY_CYCLE;
    dev.config.inverted = inverted;
    dev.rpm = 0;
    dev.pulseWriteCount = 0;
    dev.lastEdgeTime = 0;
    dev.lastValidTime = 0;
    dev.rpmEma = 0.0f;
    dev.pwmChannel = 0;

    devices.push_back(dev);

    // 初始化新设备
    Device &newDev = devices.back();
    initPWM(newDev);
    initRPM(newDev);

    Serial.printf("[Device] 添加设备: id=%d, name=%s, inverted=%s\n", dev.config.id, name.c_str(), inverted ? "是" : "否");
    return dev.config.id;
}

bool DeviceManager::updateDevice(uint8_t id, const String &name, uint8_t pwmPin, int8_t rpmPin, bool inverted)
{
    Device *dev = findDevice(id);
    if (!dev)
        return false;

    // 先停止旧的 PWM/中断
    deinitDevice(*dev);

    // 更新配置
    dev->config.name = name;
    dev->config.pwmPin = pwmPin;
    dev->config.rpmPin = rpmPin;
    dev->config.inverted = inverted;

    // 重新初始化
    initPWM(*dev);
    initRPM(*dev);

    Serial.printf("[Device] 更新设备: id=%d, name=%s, inverted=%s\n", id, name.c_str(), inverted ? "是" : "否");
    return true;
}

bool DeviceManager::removeDevice(uint8_t id)
{
    for (auto it = devices.begin(); it != devices.end(); ++it)
    {
        if (it->config.id == id)
        {
            deinitDevice(*it);
            devices.erase(it);
            Serial.printf("[Device] 删除设备: id=%d\n", id);
            return true;
        }
    }
    return false;
}

bool DeviceManager::setDutyCycle(uint8_t id, uint8_t dutyCycle)
{
    Device *dev = findDevice(id);
    if (!dev)
        return false;

    // 限制范围 0-100
    dutyCycle = min(dutyCycle, (uint8_t)100);
    dev->config.dutyCycle = dutyCycle;

    // 更新 PWM 输出（百分比映射到 0-255）
    uint8_t effectiveDuty = dev->config.inverted ? (100 - dutyCycle) : dutyCycle;
    uint32_t duty = map(effectiveDuty, 0, 100, 0, 255);
    ledcWrite(dev->config.pwmPin, duty);

    Serial.printf("[Device] 设置占空比: id=%d, duty=%d%% (有效=%d%%)\n", id, dutyCycle, effectiveDuty);
    return true;
}

bool DeviceManager::saveToNVS()
{
    // 提取设备配置列表
    std::vector<DeviceConfig> configs;
    for (const auto &dev : devices)
    {
        configs.push_back(dev.config);
    }
    return ConfigManager::saveDevices(configs);
}

bool DeviceManager::saveDutyToNVS(uint8_t id)
{
    // 遍历设备列表，找到目标设备及其在列表中的位置（即 NVS 索引）
    for (size_t i = 0; i < devices.size(); i++)
    {
        if (devices[i].config.id == id)
        {
            return ConfigManager::saveOneDevice(static_cast<uint8_t>(i), devices[i].config);
        }
    }
    Serial.printf("[Device] saveDutyToNVS: 未找到设备 id=%d\n", id);
    return false;
}

uint8_t DeviceManager::getSavedDuty(uint8_t id)
{
    for (size_t i = 0; i < devices.size(); i++)
    {
        if (devices[i].config.id == id)
        {
            return ConfigManager::getSavedDuty(static_cast<uint8_t>(i), devices[i].config.dutyCycle);
        }
    }
    return 0;
}
