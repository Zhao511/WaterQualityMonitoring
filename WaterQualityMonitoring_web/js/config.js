/**
 * 智慧水质监测系统 - 配置文件
 * 物模型：DeviceStatus / Water_status / gps / Alarm
 */
var CONFIG = {
    // 华为云API配置
    ak: '',
    sk: '',
    projectId: '',
    productId: '',
    endpoint: '',  /* 从华为云控制台获取 */
    region: 'cn-south-1',

    // 当前选中的设备ID
    currentDeviceId: '',

    // 数据接收状态
    cloudConnected: false,
    realConnection: false,

    // 版本信息
    version: '1.0.0',

    // 系统运行模式 (全局)
    curMode: 'manual-mode',

    // 历史数据存储（用于曲线查询）
    history: {},

    // 设备列表（从华为云IoTDA同步，RFID为主键，无RFID时以device_id为key）
    devices: {}
};

// 获取当前设备配置
function getCurrentDevice() {
    return CONFIG.devices[CONFIG.currentDeviceId] || null;
}

// 获取所有设备列表
function getAllDevices() {
    return Object.values(CONFIG.devices);
}

// 删除设备（调用后端API）
function deleteDeviceFromServer(deviceId) {
    var dev = CONFIG.devices[deviceId];
    if (!dev) return Promise.reject(new Error('设备不存在'));
    return api('/api/devices/' + encodeURIComponent(deviceId), { method: 'DELETE' });
}

// 更新设备信息（调用后端API）
function updateDeviceOnServer(deviceId, updates) {
    var dev = CONFIG.devices[deviceId];
    if (!dev) return Promise.reject(new Error('设备不存在'));
    return api('/api/devices/' + encodeURIComponent(deviceId), {
        method: 'PUT',
        body: updates
    });
}

// 从localStorage加载配置
function loadConfig() {
    var savedConfig = localStorage.getItem('waterQualityConfig');
    if (savedConfig) {
        try {
            var parsed = JSON.parse(savedConfig);
            for (var key in parsed) {
                if (CONFIG.hasOwnProperty(key)) {
                    CONFIG[key] = parsed[key];
                }
            }
        } catch (e) {
            console.error('Failed to load config:', e);
        }
    }
}

// 保存配置到localStorage
function saveConfig() {
    localStorage.setItem('waterQualityConfig', JSON.stringify(CONFIG));
}

// 清除本地缓存
function clearLocalStorage() {
    localStorage.removeItem('waterQualityConfig');
    showToast('本地缓存已清除', 'info');
}
