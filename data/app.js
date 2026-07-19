/**
 * PWM Controller - 前端逻辑
 *
 * 负责与后端 RESTful API 通信，渲染设备卡片列表，
 * 处理用户交互（占空比调节、设备增删、WiFi 配置等）。
 */

// ==================== 全局状态 ====================

/** 设备列表缓存 */
let devices = []

/** 当前选中的设备ID（用于详情弹窗） */
let selectedDeviceId = null

/** 正在编辑的设备 ID；null 表示添加模式 */
let editingDeviceId = null

/**
 * 已保存的占空比缓存（key: 设备ID, value: 已持久化的占空比）
 * 初次加载时与 API 返回值一致；用户点击「保存占空比」后更新。
 */
const savedDuties = new Map()

/** 刷新定时器 */
let refreshTimer = null

/** 防止定时刷新请求重叠 */
let refreshInProgress = false

/** 串行执行占空比写入，避免旧请求后到覆盖新值 */
let dutyRequestChain = Promise.resolve(true)

/** 数据刷新间隔（毫秒） */
const REFRESH_INTERVAL = 2000

/** RPM 趋势图展示窗口（毫秒） */
const RPM_HISTORY_WINDOW_MS = 2 * 60 * 1000

/** RPM 趋势图逻辑尺寸 */
const RPM_CHART_WIDTH = 300
const RPM_CHART_HEIGHT = 60

/** RPM 趋势图纵轴取整步长及最小上限 */
const RPM_CHART_SCALE_STEP = 500
const RPM_CHART_MIN_SCALE = 1000

/** 各设备的浏览器端 RPM 历史，刷新页面后重新开始采样 */
const rpmHistories = new Map()

// ==================== DOM 元素引用 ====================

const $deviceList = document.getElementById('deviceList')
const $emptyState = document.getElementById('emptyState')
const $toast = document.getElementById('toast')
const $firmwareVersion = document.getElementById('firmwareVersion')

// 设备详情弹窗
const $modalDetail = document.getElementById('modalDeviceDetail')
const $detailTitle = document.getElementById('detailTitle')
const $detailDutyValue = document.getElementById('detailDutyValue')
const $detailDutySlider = document.getElementById('detailDutySlider')
const $detailInfo = document.getElementById('detailInfo')

// 设备详情弹窗新增元素
const $detailSavedDutyValue = document.getElementById('detailSavedDutyValue')
const $btnDutyMinus = document.getElementById('btnDutyMinus')
const $btnDutyPlus = document.getElementById('btnDutyPlus')

// 添加设备弹窗
const $modalAdd = document.getElementById('modalAddDevice')
const $addDeviceTitle = document.getElementById('addDeviceTitle')
const $inputName = document.getElementById('inputDeviceName')
const $inputPwmPin = document.getElementById('inputPwmPin')
const $inputRpmPin = document.getElementById('inputRpmPin')
const $inputInverted = document.getElementById('inputInverted')
const $inputPulsesPerRevolution = document.getElementById('inputPulsesPerRevolution')

// 系统设置弹窗
const $modalSettings = document.getElementById('modalSettings')
const $inputWifiSsid = document.getElementById('inputWifiSsid')
const $inputWifiPass = document.getElementById('inputWifiPass')
const $wifiStatus = document.getElementById('wifiStatus')
const $systemInfo = document.getElementById('systemInfo')
const $inputWebUsername = document.getElementById('inputWebUsername')
const $inputWebPassword = document.getElementById('inputWebPassword')
const $inputWebPasswordConfirm = document.getElementById('inputWebPasswordConfirm')

// ==================== API 封装 ====================

/**
 * 通用 fetch 请求封装
 * @param {string} url 请求地址
 * @param {object} options fetch 选项
 * @returns {Promise<object>} 响应 JSON
 */
async function api(url, options = {}) {
  try {
    const headers = { ...(options.headers || {}) }
    if (options.body !== undefined) {
      headers['Content-Type'] = 'application/json'
    }
    const resp = await fetch(url, {
      ...options,
      headers
    })
    const contentType = resp.headers.get('content-type') || ''
    const data = contentType.includes('application/json') ? await resp.json() : null
    if (!resp.ok) {
      throw new Error(data?.error || `请求失败 (${resp.status})`)
    }
    return data || {}
  } catch (err) {
    console.error('API 请求失败:', url, err)
    showToast(err.message || '网络请求失败')
    return null
  }
}

/** 获取设备列表 */
async function fetchDevices() {
  if (refreshInProgress) return
  refreshInProgress = true
  try {
    const data = await api('/api/devices')
    if (Array.isArray(data)) {
      updateRpmHistories(data, performance.now())
      devices = data
      // 从后端返回的 savedDutyCycle 更新已保存值缓存
      savedDuties.clear()
      devices.forEach((dev) => {
        savedDuties.set(dev.id, dev.savedDutyCycle)
      })
      renderDevices()
      // 如果详情弹窗正在展示，同步刷新已保存占空比和 RPM
      if (selectedDeviceId !== null && $modalDetail.classList.contains('active')) {
        const $saved = document.getElementById('detailSavedDutyValue')
        if ($saved) $saved.textContent = savedDuties.get(selectedDeviceId) ?? '-'
        const current = devices.find((dev) => dev.id === selectedDeviceId)
        if (current) {
          $detailTitle.textContent = current.name
          renderDetailInfo(current)
        }
      }
    }
  } finally {
    refreshInProgress = false
  }
}

/** 添加设备（后端会自动持久化，无需前端额外保存） */
async function addDevice(name, pwmPin, rpmPin, inverted, pulsesPerRevolution) {
  const data = await api('/api/devices', {
    method: 'POST',
    body: JSON.stringify({
      name,
      pwmPin,
      rpmPin,
      inverted,
      pulsesPerRevolution
    })
  })
  if (data && data.id) {
    showToast('设备添加成功')
    await fetchDevices()
  }
  return data
}

/** 更新设备配置 */
async function updateDevice(id, name, pwmPin, rpmPin, inverted, pulsesPerRevolution) {
  const data = await api(`/api/devices/${id}`, {
    method: 'PUT',
    body: JSON.stringify({
      name,
      pwmPin,
      rpmPin,
      inverted,
      pulsesPerRevolution
    })
  })
  if (data) {
    showToast('设备已更新')
    await fetchDevices()
    return true
  }
  return false
}

/** 删除设备 */
async function deleteDevice(id) {
  const data = await api(`/api/devices/${id}`, { method: 'DELETE' })
  if (data) {
    showToast('设备已删除')
    await fetchDevices()
    return true
  }
  return false
}

/** 设置占空比（仅内存） */
async function setDutyCycle(id, dutyCycle) {
  const device = devices.find((item) => item.id === id)
  if (device) device.dutyCycle = dutyCycle
  renderDevices()

  dutyRequestChain = dutyRequestChain.then(async () => {
    const result = await api(`/api/devices/${id}/duty`, {
      method: 'POST',
      body: JSON.stringify({ dutyCycle })
    })
    return result !== null
  })
  return dutyRequestChain
}

/** 持久化所有设备到 NVS */
async function saveDevicesToNVS() {
  const data = await api('/api/devices/save', { method: 'POST' })
  if (data) {
    showToast('设备配置已保存到设备')
    // 更新所有设备的已保存占空比为当前实时值
    devices.forEach((dev) => {
      savedDuties.set(dev.id, dev.dutyCycle)
    })
    // 如果详情页正在显示，刷新其已保存值的文本
    if (selectedDeviceId !== null) {
      const $saved = document.getElementById('detailSavedDutyValue')
      if ($saved) $saved.textContent = savedDuties.get(selectedDeviceId)
    }
  }
}

/** 获取 WiFi 配置 */
async function fetchWiFiConfig() {
  return await api('/api/wifi')
}

/** 保存 WiFi 配置 */
async function saveWiFiConfig(ssid, password) {
  const data = await api('/api/wifi', {
    method: 'POST',
    body: JSON.stringify({ ssid, password })
  })
  if (data) {
    showToast('正在后台验证新 WiFi...')
    return true
  }
  return false
}

/** 获取网页登录配置（后端不会返回密码或哈希）。 */
async function fetchWebAuthConfig() {
  return await api('/api/system/auth')
}

/** 保存网页登录配置并等待控制器重启。 */
async function saveWebAuthConfig(username, password) {
  return await api('/api/system/auth', {
    method: 'POST',
    body: JSON.stringify({ username, password })
  })
}

/** 获取系统信息 */
async function fetchSystemInfo() {
  return await api('/api/system/info')
}

/** 更新标题右侧的固件版本号。 */
function updateFirmwareVersion(systemInfo) {
  if (systemInfo?.version) {
    $firmwareVersion.textContent = `v${systemInfo.version}`
  }
}

/**
 * 记录一次成功获取的 RPM 快照，并清理过期或已删除设备的数据。
 * @param {Array<object>} snapshots 设备状态快照
 * @param {number} sampledAt 采样时间戳
 */
function updateRpmHistories(snapshots, sampledAt) {
  const activeDeviceIds = new Set()
  const cutoff = sampledAt - RPM_HISTORY_WINDOW_MS

  snapshots.forEach((device) => {
    if (device.rpmPin < 0) {
      rpmHistories.delete(device.id)
      return
    }

    activeDeviceIds.add(device.id)
    const parsedRpm = Number(device.rpm)
    const rpm = Number.isFinite(parsedRpm) ? Math.max(0, parsedRpm) : 0
    const history = rpmHistories.get(device.id) || []
    history.push({ timestamp: sampledAt, rpm })

    let firstValidIndex = 0
    while (
      firstValidIndex < history.length &&
      history[firstValidIndex].timestamp < cutoff
    ) {
      firstValidIndex++
    }
    if (firstValidIndex > 0) {
      history.splice(0, firstValidIndex)
    }
    rpmHistories.set(device.id, history)
  })

  for (const deviceId of rpmHistories.keys()) {
    if (!activeDeviceIds.has(deviceId)) {
      rpmHistories.delete(deviceId)
    }
  }
}

/** 将纵轴上限向上取整，避免折线贴近图表顶部。 */
function calculateRpmChartScale(history) {
  const maximumRpm = history.reduce((maximum, sample) => Math.max(maximum, sample.rpm), 0)
  const paddedMaximum = maximumRpm * 1.1
  return Math.max(
    RPM_CHART_MIN_SCALE,
    Math.ceil(paddedMaximum / RPM_CHART_SCALE_STEP) * RPM_CHART_SCALE_STEP
  )
}

/**
 * 生成设备卡片内的 RPM SVG 趋势图。
 * @param {object} device 设备状态
 * @returns {string} 图表 HTML；无转速引脚时返回空字符串
 */
function renderRpmChart(device) {
  if (device.rpmPin < 0) return ''

  const history = rpmHistories.get(device.id) || []
  if (history.length === 0) return ''

  const chartTop = 4
  const chartBottom = RPM_CHART_HEIGHT - 3
  const chartHeight = chartBottom - chartTop
  const latestTimestamp = history[history.length - 1].timestamp
  const earliestTimestamp = Math.max(
    latestTimestamp - RPM_HISTORY_WINDOW_MS,
    history[0].timestamp
  )
  const timeSpan = Math.max(latestTimestamp - earliestTimestamp, 1)
  const scaleMaximum = calculateRpmChartScale(history)

  const pointCoordinates = history.map((sample) => {
    const x =
      history.length === 1
        ? RPM_CHART_WIDTH / 2
        : ((sample.timestamp - earliestTimestamp) / timeSpan) * RPM_CHART_WIDTH
    const boundedRpm = Math.min(sample.rpm, scaleMaximum)
    const y = chartBottom - (boundedRpm / scaleMaximum) * chartHeight
    return { x: x.toFixed(1), y: y.toFixed(1) }
  })

  const points = pointCoordinates.map((point) => `${point.x},${point.y}`).join(' ')
  const latestPoint = pointCoordinates[pointCoordinates.length - 1]
  const latestLeft = ((Number(latestPoint.x) / RPM_CHART_WIDTH) * 100).toFixed(2)
  const latestTop = ((Number(latestPoint.y) / RPM_CHART_HEIGHT) * 100).toFixed(2)

  return `
    <div class="device-rpm-chart">
      <div class="rpm-chart-caption">
        <span>最近 2 分钟</span>
        <span>0–${scaleMaximum} RPM</span>
      </div>
      <div class="rpm-chart-plot">
        <svg class="rpm-chart-svg" viewBox="0 0 ${RPM_CHART_WIDTH} ${RPM_CHART_HEIGHT}"
             preserveAspectRatio="none" role="img" aria-label="最近两分钟转速趋势">
          <line class="rpm-chart-grid" x1="0" y1="${chartTop}" x2="${RPM_CHART_WIDTH}" y2="${chartTop}" />
          <line class="rpm-chart-grid" x1="0" y1="${((chartTop + chartBottom) / 2).toFixed(1)}"
                x2="${RPM_CHART_WIDTH}" y2="${((chartTop + chartBottom) / 2).toFixed(1)}" />
          <line class="rpm-chart-grid" x1="0" y1="${chartBottom}" x2="${RPM_CHART_WIDTH}" y2="${chartBottom}" />
          <polyline class="rpm-chart-line" points="${points}" />
        </svg>
        <span class="rpm-chart-point" style="left:${latestLeft}%;top:${latestTop}%" aria-hidden="true"></span>
      </div>
    </div>
  `
}

/** 清空 NVS 配置并请求设备重启。 */
async function resetSettings() {
  const confirmed = confirm(
    '确定要清空全部设备、WiFi 和网页登录配置吗？控制器将自动重启，此操作无法撤销。'
  )
  if (!confirmed) return

  const button = document.getElementById('btnResetSettings')
  button.disabled = true
  const data = await api('/api/system/reset', { method: 'POST' })
  if (data) {
    if (refreshTimer) clearInterval(refreshTimer)
    showToast('设置已清空，控制器正在重启...', 5000)
    closeModal($modalSettings)
    return
  }
  button.disabled = false
}

// ==================== 渲染函数 ====================

/**
 * 渲染设备卡片列表
 */
function renderDevices() {
  if (devices.length === 0) {
    $deviceList.innerHTML = ''
    $emptyState.style.display = 'block'
    return
  }

  $emptyState.style.display = 'none'

  $deviceList.innerHTML = devices
    .map((dev) => {
      // 计算圆环 SVG 参数
      const radius = 24
      const circumference = 2 * Math.PI * radius
      const offset = circumference * (1 - dev.dutyCycle / 100)

      // 转速显示
      const rpmHtml = dev.rpmPin >= 0 ? `<span class="device-rpm">${dev.rpm} RPM</span>` : ''
      const rpmChartHtml = renderRpmChart(dev)

      return `
            <div class="device-card" data-id="${dev.id}" onclick="openDeviceDetail(${dev.id})">
                <div class="device-card-main">
                    <div class="device-duty-ring">
                        <svg width="60" height="60" viewBox="0 0 60 60">
                            <circle class="ring-bg" cx="30" cy="30" r="${radius}" />
                            <circle class="ring-fg" cx="30" cy="30" r="${radius}"
                                    stroke-dasharray="${circumference}"
                                    stroke-dashoffset="${offset}" />
                        </svg>
                        <span class="device-duty-text">${dev.dutyCycle}%</span>
                    </div>
                    <div class="device-info">
                        <div class="device-name">${escapeHtml(dev.name)}</div>
                        <div class="device-meta">
                            <span>PWM: GPIO${dev.pwmPin}</span>
                            ${dev.rpmPin >= 0 ? `<span>转速: GPIO${dev.rpmPin}</span>` : ''}
                            ${rpmHtml}
                        </div>
                    </div>
                </div>
                ${rpmChartHtml}
            </div>
        `
    })
    .join('')
}

// ==================== 弹窗控制 ====================

/**
 * 打开设备详情弹窗
 * @param {number} id 设备ID
 */
function openDeviceDetail(id) {
  const dev = devices.find((d) => d.id === id)
  if (!dev) return

  selectedDeviceId = id

  // 填充详情
  $detailTitle.textContent = dev.name
  $detailDutyValue.textContent = dev.dutyCycle
  $detailDutySlider.value = dev.dutyCycle
  // 显示已持久化的占空比
  $detailSavedDutyValue.textContent = savedDuties.get(id) ?? dev.dutyCycle

  renderDetailInfo(dev)

  showModal($modalDetail)
}

/** 更新设备详情中的静态配置与实时 RPM。 */
function renderDetailInfo(dev) {
  let infoHtml = `<p><strong>设备ID:</strong> ${dev.id}</p>`
  infoHtml += `<p><strong>PWM 引脚:</strong> GPIO${dev.pwmPin}</p>`
  if (dev.inverted) {
    infoHtml += `<p><strong>PWM 信号:</strong> 反转 (10% -> 90%)</p>`
  }
  if (dev.rpmPin >= 0) {
    infoHtml += `<p><strong>转速引脚:</strong> GPIO${dev.rpmPin}</p>`
    infoHtml += `<p><strong>每转脉冲:</strong> ${dev.pulsesPerRevolution}</p>`
    infoHtml += `<p><strong>当前转速:</strong> ${dev.rpm} RPM</p>`
  } else {
    infoHtml += `<p><strong>转速引脚:</strong> 无</p>`
  }
  $detailInfo.innerHTML = infoHtml
}

/** 打开添加设备弹窗 */
function openAddDevice() {
  editingDeviceId = null
  $addDeviceTitle.textContent = '添加设备'
  $inputName.value = ''
  $inputPwmPin.value = ''
  $inputRpmPin.value = ''
  $inputInverted.checked = false
  $inputPulsesPerRevolution.value = '2'
  showModal($modalAdd)
}

/** 使用添加弹窗编辑当前设备配置。 */
function openEditDevice() {
  const dev = devices.find((item) => item.id === selectedDeviceId)
  if (!dev) return
  editingDeviceId = dev.id
  $addDeviceTitle.textContent = '编辑设备'
  $inputName.value = dev.name
  $inputPwmPin.value = dev.pwmPin
  $inputRpmPin.value = dev.rpmPin >= 0 ? dev.rpmPin : ''
  $inputInverted.checked = dev.inverted
  $inputPulsesPerRevolution.value = dev.pulsesPerRevolution || 2
  showModal($modalAdd)
}

/** 打开系统设置弹窗 */
async function openSettings() {
  showModal($modalSettings)

  // 并行加载 WiFi、登录配置和系统信息。
  const [wifiData, authData, sysData] = await Promise.all([
    fetchWiFiConfig(),
    fetchWebAuthConfig(),
    fetchSystemInfo()
  ])

  if (wifiData) {
    $inputWifiSsid.value = wifiData.ssid || ''
    $inputWifiPass.value = ''
    $wifiStatus.innerHTML = `
            <p><span class="label">当前状态:</span> ${wifiData.state === 'connecting' ? '正在连接 STA（救援 AP 保持开启）' : wifiData.isAP ? 'AP 模式（热点）' : 'STA 模式（已连接）'}</p>
            <p><span class="label">IP 地址:</span> ${wifiData.ip}</p>
            ${wifiData.isAP ? `<p><span class="label">救援热点:</span> ${escapeHtml(wifiData.apSsid || '-')}</p>` : ''}
            ${wifiData.ssid ? `<p><span class="label">已配置 SSID:</span> ${escapeHtml(wifiData.ssid)}</p>` : ''}
        `
  }

  if (authData) {
    $inputWebUsername.value = authData.username || ''
    $inputWebPassword.value = ''
    $inputWebPasswordConfirm.value = ''
  }

  if (sysData) {
    updateFirmwareVersion(sysData)
    const uptimeMin = Math.floor(sysData.uptime / 60)
    const uptimeSec = sysData.uptime % 60
    $systemInfo.innerHTML = `
            <p><strong>芯片:</strong> ${sysData.chipModel}</p>
            <p><strong>运行时间:</strong> ${uptimeMin}分${uptimeSec}秒</p>
            <p><strong>可用内存:</strong> ${(sysData.freeHeap / 1024).toFixed(1)} KB</p>
            <p><strong>IP 地址:</strong> ${sysData.ip}</p>
        `
  }
}

/** 显示弹窗 */
function showModal(modal) {
  modal.classList.add('active')
}

/** 关闭弹窗 */
function closeModal(modal) {
  modal.classList.remove('active')
}

// ==================== 提示消息 ====================

/** Toast 定时器 */
let toastTimer = null

/**
 * 显示提示消息
 * @param {string} message 消息内容
 * @param {number} duration 持续时间（毫秒）
 */
function showToast(message, duration = 2000) {
  $toast.textContent = message
  $toast.classList.add('show')

  if (toastTimer) clearTimeout(toastTimer)
  toastTimer = setTimeout(() => {
    $toast.classList.remove('show')
  }, duration)
}

// ==================== 工具函数 ====================

/**
 * HTML 转义，防止 XSS
 * @param {string} str 原始字符串
 * @returns {string} 转义后的字符串
 */
function escapeHtml(str) {
  const div = document.createElement('div')
  div.textContent = str
  return div.innerHTML
}

// ==================== 事件绑定 ====================

/** 占空比滑块实时更新 */
$detailDutySlider.addEventListener('input', function () {
  $detailDutyValue.textContent = this.value
})

/** 占空比滑块松开时发送到设备（仅内存） */
$detailDutySlider.addEventListener('change', function () {
  if (selectedDeviceId !== null) {
    setDutyCycle(selectedDeviceId, parseInt(this.value))
  }
})

/** 占空比微调按钮 - 减少 1% */
$btnDutyMinus.addEventListener('click', function () {
  let val = parseInt($detailDutySlider.value)
  if (val > 0) {
    val--
    $detailDutySlider.value = val
    $detailDutyValue.textContent = val
    if (selectedDeviceId !== null) {
      setDutyCycle(selectedDeviceId, val)
    }
  }
})

/** 占空比微调按钮 - 增加 1% */
$btnDutyPlus.addEventListener('click', function () {
  let val = parseInt($detailDutySlider.value)
  if (val < 100) {
    val++
    $detailDutySlider.value = val
    $detailDutyValue.textContent = val
    if (selectedDeviceId !== null) {
      setDutyCycle(selectedDeviceId, val)
    }
  }
})

/** 设置按钮 → 打开系统设置 */
document.getElementById('btnSettings').addEventListener('click', openSettings)

/** 添加设备按钮 */
document.getElementById('btnAddDevice').addEventListener('click', openAddDevice)

/** 编辑设备按钮 */
document.getElementById('btnEditDevice').addEventListener('click', openEditDevice)

/** 关闭详情弹窗 */
document.getElementById('btnCloseDetail').addEventListener('click', () => closeModal($modalDetail))

/** 关闭添加弹窗 */
document.getElementById('btnCloseAdd').addEventListener('click', () => closeModal($modalAdd))
document.getElementById('btnCancelAdd').addEventListener('click', () => closeModal($modalAdd))

/** 关闭设置弹窗 */
document
  .getElementById('btnCloseSettings')
  .addEventListener('click', () => closeModal($modalSettings))
document
  .getElementById('btnCancelSettings')
  .addEventListener('click', () => closeModal($modalSettings))

/** 确认添加设备 */
document.getElementById('btnConfirmAdd').addEventListener('click', async () => {
  const name = $inputName.value.trim()
  const pwmPin = parseInt($inputPwmPin.value)
  const rpmPin = $inputRpmPin.value.trim() === '' ? -1 : parseInt($inputRpmPin.value)
  const inverted = $inputInverted.checked
  const pulsesPerRevolution = parseInt($inputPulsesPerRevolution.value)

  if (!name) {
    showToast('请输入设备名称')
    return
  }
  if (isNaN(pwmPin)) {
    showToast('请输入有效的 PWM 引脚')
    return
  }
  if (rpmPin !== -1 && isNaN(rpmPin)) {
    showToast('请输入有效的转速引脚或留空')
    return
  }
  if (isNaN(pulsesPerRevolution) || pulsesPerRevolution < 1 || pulsesPerRevolution > 8) {
    showToast('每转脉冲数必须为 1～8')
    return
  }

  let success
  if (editingDeviceId === null) {
    const result = await addDevice(name, pwmPin, rpmPin, inverted, pulsesPerRevolution)
    success = Boolean(result?.id)
  } else {
    success = await updateDevice(
      editingDeviceId,
      name,
      pwmPin,
      rpmPin,
      inverted,
      pulsesPerRevolution
    )
  }
  if (success) {
    closeModal($modalAdd)
    editingDeviceId = null
  }
})

/** 删除设备 */
document.getElementById('btnDeleteDevice').addEventListener('click', async () => {
  if (selectedDeviceId === null) return
  if (!confirm('确定要删除该设备吗？')) return

  if (await deleteDevice(selectedDeviceId)) {
    closeModal($modalDetail)
    selectedDeviceId = null
  }
})

/** 保存当前设备的占空比到 NVS（仅针对该设备，不影响其他设备） */
document.getElementById('btnSaveDuty').addEventListener('click', async () => {
  if (selectedDeviceId === null) return
  const currentDuty = parseInt($detailDutySlider.value)
  const dutyApplied = await dutyRequestChain
  if (!dutyApplied) return
  // 仅持久化当前选中设备
  const data = await api(`/api/devices/${selectedDeviceId}/save`, {
    method: 'POST'
  })
  if (data) {
    savedDuties.set(selectedDeviceId, currentDuty)
    $detailSavedDutyValue.textContent = currentDuty
    showToast('占空比已保存')
  }
})

/** 保存 WiFi 配置 */
document.getElementById('btnSaveWifi').addEventListener('click', async () => {
  const ssid = $inputWifiSsid.value.trim()
  const password = $inputWifiPass.value

  if (!ssid) {
    showToast('请输入 WiFi SSID')
    return
  }

  if (await saveWiFiConfig(ssid, password)) {
    closeModal($modalSettings)
  }
})

/** 保存网页登录设置。 */
document.getElementById('btnSaveWebAuth').addEventListener('click', async () => {
  const username = $inputWebUsername.value.trim()
  const password = $inputWebPassword.value
  const passwordConfirm = $inputWebPasswordConfirm.value

  if (!/^[A-Za-z0-9_.-]{1,32}$/.test(username)) {
    showToast('用户名只能包含 1～32 位字母、数字、点、下划线或连字符')
    return
  }
  if (password.length < 8 || password.length > 64) {
    showToast('密码长度必须为 8～64 位')
    return
  }
  if (password !== passwordConfirm) {
    showToast('两次输入的密码不一致')
    return
  }

  const button = document.getElementById('btnSaveWebAuth')
  button.disabled = true
  const data = await saveWebAuthConfig(username, password)
  if (data) {
    if (refreshTimer) clearInterval(refreshTimer)
    showToast('登录设置已保存，控制器正在重启...', 5000)
    closeModal($modalSettings)
    return
  }
  button.disabled = false
})

/** 恢复出厂设置 */
document.getElementById('btnResetSettings').addEventListener('click', resetSettings)

/** 点击弹窗背景关闭 */
document.querySelectorAll('.modal').forEach((modal) => {
  modal.addEventListener('click', function (e) {
    if (e.target === this) {
      closeModal(this)
    }
  })
})

// ==================== 初始化 ====================

/**
 * 页面加载完成后初始化
 */
async function init() {
  // 首次并行加载设备列表与固件版本。
  const [, systemInfo] = await Promise.all([fetchDevices(), fetchSystemInfo()])
  updateFirmwareVersion(systemInfo)

  // 启动定时刷新（获取最新转速等数据）
  refreshTimer = setInterval(() => void fetchDevices(), REFRESH_INTERVAL)
}

// 启动
init()
