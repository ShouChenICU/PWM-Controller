/**
 * PWM Controller - 前端逻辑
 *
 * 负责与后端 RESTful API 通信，渲染设备卡片列表，
 * 处理用户交互（占空比调节、设备增删、WiFi 配置等）。
 */

// ==================== 全局状态 ====================

/** 设备列表缓存 */
let devices = [];

/** 当前选中的设备ID（用于详情弹窗） */
let selectedDeviceId = null;

/** 刷新定时器 */
let refreshTimer = null;

/** 数据刷新间隔（毫秒） */
const REFRESH_INTERVAL = 2000;

// ==================== DOM 元素引用 ====================

const $deviceList = document.getElementById("deviceList");
const $emptyState = document.getElementById("emptyState");
const $toast = document.getElementById("toast");

// 设备详情弹窗
const $modalDetail = document.getElementById("modalDeviceDetail");
const $detailTitle = document.getElementById("detailTitle");
const $detailDutyValue = document.getElementById("detailDutyValue");
const $detailDutySlider = document.getElementById("detailDutySlider");
const $detailInfo = document.getElementById("detailInfo");

// 添加设备弹窗
const $modalAdd = document.getElementById("modalAddDevice");
const $addDeviceTitle = document.getElementById("addDeviceTitle");
const $inputName = document.getElementById("inputDeviceName");
const $inputPwmPin = document.getElementById("inputPwmPin");
const $inputRpmPin = document.getElementById("inputRpmPin");

// 系统设置弹窗
const $modalSettings = document.getElementById("modalSettings");
const $inputWifiSsid = document.getElementById("inputWifiSsid");
const $inputWifiPass = document.getElementById("inputWifiPass");
const $wifiStatus = document.getElementById("wifiStatus");
const $systemInfo = document.getElementById("systemInfo");

// ==================== API 封装 ====================

/**
 * 通用 fetch 请求封装
 * @param {string} url 请求地址
 * @param {object} options fetch 选项
 * @returns {Promise<object>} 响应 JSON
 */
async function api(url, options = {}) {
  try {
    const resp = await fetch(url, {
      headers: { "Content-Type": "application/json" },
      ...options,
    });
    return await resp.json();
  } catch (err) {
    console.error("API 请求失败:", url, err);
    showToast("网络请求失败");
    return null;
  }
}

/** 获取设备列表 */
async function fetchDevices() {
  const data = await api("/api/devices");
  if (data) {
    devices = data;
    renderDevices();
  }
}

/** 添加设备 */
async function addDevice(name, pwmPin, rpmPin) {
  const data = await api("/api/devices", {
    method: "POST",
    body: JSON.stringify({ name, pwmPin, rpmPin }),
  });
  if (data && data.id) {
    showToast("设备添加成功");
    await fetchDevices();
  }
  return data;
}

/** 更新设备配置 */
async function updateDevice(id, name, pwmPin, rpmPin) {
  const data = await api(`/api/devices/${id}`, {
    method: "PUT",
    body: JSON.stringify({ name, pwmPin, rpmPin }),
  });
  if (data) {
    showToast("设备已更新");
    await fetchDevices();
  }
}

/** 删除设备 */
async function deleteDevice(id) {
  const data = await api(`/api/devices/${id}`, { method: "DELETE" });
  if (data) {
    showToast("设备已删除");
    await fetchDevices();
  }
}

/** 设置占空比（仅内存） */
async function setDutyCycle(id, dutyCycle) {
  await api(`/api/devices/${id}/duty`, {
    method: "POST",
    body: JSON.stringify({ dutyCycle }),
  });
}

/** 持久化所有设备到 NVS */
async function saveDevicesToNVS() {
  const data = await api("/api/devices/save", { method: "POST" });
  if (data) {
    showToast("设备配置已保存到设备");
  }
}

/** 获取 WiFi 配置 */
async function fetchWiFiConfig() {
  return await api("/api/wifi");
}

/** 保存 WiFi 配置 */
async function saveWiFiConfig(ssid, password) {
  const data = await api("/api/wifi", {
    method: "POST",
    body: JSON.stringify({ ssid, password }),
  });
  if (data) {
    showToast("WiFi 配置已保存，正在重连...");
  }
}

/** 获取系统信息 */
async function fetchSystemInfo() {
  return await api("/api/system/info");
}

// ==================== 渲染函数 ====================

/**
 * 渲染设备卡片列表
 */
function renderDevices() {
  if (devices.length === 0) {
    $deviceList.innerHTML = "";
    $emptyState.style.display = "block";
    return;
  }

  $emptyState.style.display = "none";

  $deviceList.innerHTML = devices
    .map((dev) => {
      // 计算圆环 SVG 参数
      const radius = 24;
      const circumference = 2 * Math.PI * radius;
      const offset = circumference * (1 - dev.dutyCycle / 100);

      // 转速显示
      const rpmHtml =
        dev.rpmPin >= 0 ? `<span class="device-rpm">${dev.rpm} RPM</span>` : "";

      return `
            <div class="device-card" data-id="${dev.id}" onclick="openDeviceDetail(${dev.id})">
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
                        ${dev.rpmPin >= 0 ? `<span>转速: GPIO${dev.rpmPin}</span>` : ""}
                        ${rpmHtml}
                    </div>
                </div>
            </div>
        `;
    })
    .join("");
}

// ==================== 弹窗控制 ====================

/**
 * 打开设备详情弹窗
 * @param {number} id 设备ID
 */
function openDeviceDetail(id) {
  const dev = devices.find((d) => d.id === id);
  if (!dev) return;

  selectedDeviceId = id;

  // 填充详情
  $detailTitle.textContent = dev.name;
  $detailDutyValue.textContent = dev.dutyCycle;
  $detailDutySlider.value = dev.dutyCycle;

  // 设备信息
  let infoHtml = `<p><strong>设备ID:</strong> ${dev.id}</p>`;
  infoHtml += `<p><strong>PWM 引脚:</strong> GPIO${dev.pwmPin}</p>`;
  if (dev.rpmPin >= 0) {
    infoHtml += `<p><strong>转速引脚:</strong> GPIO${dev.rpmPin}</p>`;
    infoHtml += `<p><strong>当前转速:</strong> ${dev.rpm} RPM</p>`;
  } else {
    infoHtml += `<p><strong>转速引脚:</strong> 无</p>`;
  }
  $detailInfo.innerHTML = infoHtml;

  showModal($modalDetail);
}

/** 打开添加设备弹窗 */
function openAddDevice() {
  $addDeviceTitle.textContent = "添加设备";
  $inputName.value = "";
  $inputPwmPin.value = "";
  $inputRpmPin.value = "";
  showModal($modalAdd);
}

/** 打开系统设置弹窗 */
async function openSettings() {
  showModal($modalSettings);

  // 并行加载 WiFi 配置和系统信息
  const [wifiData, sysData] = await Promise.all([
    fetchWiFiConfig(),
    fetchSystemInfo(),
  ]);

  if (wifiData) {
    $inputWifiSsid.value = wifiData.ssid || "";
    $inputWifiPass.value = "";
    $wifiStatus.innerHTML = `
            <p><span class="label">当前状态:</span> ${wifiData.isAP ? "AP 模式（热点）" : "STA 模式（已连接）"}</p>
            <p><span class="label">IP 地址:</span> ${wifiData.ip}</p>
            ${wifiData.ssid ? `<p><span class="label">已配置 SSID:</span> ${escapeHtml(wifiData.ssid)}</p>` : ""}
        `;
  }

  if (sysData) {
    const uptimeMin = Math.floor(sysData.uptime / 60);
    const uptimeSec = sysData.uptime % 60;
    $systemInfo.innerHTML = `
            <p><strong>芯片:</strong> ${sysData.chipModel}</p>
            <p><strong>运行时间:</strong> ${uptimeMin}分${uptimeSec}秒</p>
            <p><strong>可用内存:</strong> ${(sysData.freeHeap / 1024).toFixed(1)} KB</p>
            <p><strong>IP 地址:</strong> ${sysData.ip}</p>
        `;
  }
}

/** 显示弹窗 */
function showModal(modal) {
  modal.classList.add("active");
}

/** 关闭弹窗 */
function closeModal(modal) {
  modal.classList.remove("active");
}

// ==================== 提示消息 ====================

/** Toast 定时器 */
let toastTimer = null;

/**
 * 显示提示消息
 * @param {string} message 消息内容
 * @param {number} duration 持续时间（毫秒）
 */
function showToast(message, duration = 2000) {
  $toast.textContent = message;
  $toast.classList.add("show");

  if (toastTimer) clearTimeout(toastTimer);
  toastTimer = setTimeout(() => {
    $toast.classList.remove("show");
  }, duration);
}

// ==================== 工具函数 ====================

/**
 * HTML 转义，防止 XSS
 * @param {string} str 原始字符串
 * @returns {string} 转义后的字符串
 */
function escapeHtml(str) {
  const div = document.createElement("div");
  div.textContent = str;
  return div.innerHTML;
}

// ==================== 事件绑定 ====================

/** 占空比滑块实时更新 */
$detailDutySlider.addEventListener("input", function () {
  $detailDutyValue.textContent = this.value;
});

/** 占空比滑块松开时发送到设备（仅内存） */
$detailDutySlider.addEventListener("change", function () {
  if (selectedDeviceId !== null) {
    setDutyCycle(selectedDeviceId, parseInt(this.value));
  }
});

/** 设置按钮 → 打开系统设置 */
document.getElementById("btnSettings").addEventListener("click", openSettings);

/** 添加设备按钮 */
document
  .getElementById("btnAddDevice")
  .addEventListener("click", openAddDevice);

/** 关闭详情弹窗 */
document
  .getElementById("btnCloseDetail")
  .addEventListener("click", () => closeModal($modalDetail));

/** 关闭添加弹窗 */
document
  .getElementById("btnCloseAdd")
  .addEventListener("click", () => closeModal($modalAdd));
document
  .getElementById("btnCancelAdd")
  .addEventListener("click", () => closeModal($modalAdd));

/** 关闭设置弹窗 */
document
  .getElementById("btnCloseSettings")
  .addEventListener("click", () => closeModal($modalSettings));
document
  .getElementById("btnCancelSettings")
  .addEventListener("click", () => closeModal($modalSettings));

/** 确认添加设备 */
document.getElementById("btnConfirmAdd").addEventListener("click", async () => {
  const name = $inputName.value.trim();
  const pwmPin = parseInt($inputPwmPin.value);
  const rpmPin =
    $inputRpmPin.value.trim() === "" ? -1 : parseInt($inputRpmPin.value);

  if (!name) {
    showToast("请输入设备名称");
    return;
  }
  if (isNaN(pwmPin)) {
    showToast("请输入有效的 PWM 引脚");
    return;
  }

  await addDevice(name, pwmPin, rpmPin);
  closeModal($modalAdd);
});

/** 删除设备 */
document
  .getElementById("btnDeleteDevice")
  .addEventListener("click", async () => {
    if (selectedDeviceId === null) return;
    if (!confirm("确定要删除该设备吗？")) return;

    await deleteDevice(selectedDeviceId);
    closeModal($modalDetail);
    selectedDeviceId = null;
  });

/** 保存设备配置到 NVS */
document
  .getElementById("btnSaveDevices")
  .addEventListener("click", async () => {
    await saveDevicesToNVS();
  });

/** 保存 WiFi 配置 */
document.getElementById("btnSaveWifi").addEventListener("click", async () => {
  const ssid = $inputWifiSsid.value.trim();
  const password = $inputWifiPass.value;

  if (!ssid) {
    showToast("请输入 WiFi SSID");
    return;
  }

  await saveWiFiConfig(ssid, password);
  closeModal($modalSettings);
});

/** 点击弹窗背景关闭 */
document.querySelectorAll(".modal").forEach((modal) => {
  modal.addEventListener("click", function (e) {
    if (e.target === this) {
      closeModal(this);
    }
  });
});

// ==================== 初始化 ====================

/**
 * 页面加载完成后初始化
 */
async function init() {
  // 首次加载设备列表
  await fetchDevices();

  // 启动定时刷新（获取最新转速等数据）
  refreshTimer = setInterval(fetchDevices, REFRESH_INTERVAL);
}

// 启动
init();
