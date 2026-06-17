/**
 * 智慧水质监测系统 - 后端服务
 * 使用华为云官方SDK调用IoTDA API拉取设备数据
 *
 * 启动: node server.js
 * 端口: 3000
 */

var core = require('@huaweicloud/huaweicloud-sdk-core');
var iotda = require('@huaweicloud/huaweicloud-sdk-iotda');
var fs = require('fs');
var path = require('path');

var PORT = 3000;
var DATA_DIR = path.join(__dirname, 'data');
var DEVICES_FILE = path.join(DATA_DIR, 'devices.json');
var HISTORY_DIR = path.join(DATA_DIR, 'history');
var ALARMS_DIR = path.join(DATA_DIR, 'alarms');

// ==================== 初始化 ====================
[DATA_DIR, HISTORY_DIR, ALARMS_DIR].forEach(function(dir) {
    if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
});
if (!fs.existsSync(DEVICES_FILE)) fs.writeFileSync(DEVICES_FILE, JSON.stringify({}));

// ==================== 华为云配置 ====================
var hwConfig = {
    ak: 'YOUR_AK',
    sk: 'YOUR_SK',
    projectId: 'YOUR_PROJECT_ID',
    productId: 'YOUR_PRODUCT_ID',    /* TODO: 华为云产品ID */
    endpoint: 'YOUR_IOTDA_ENDPOINT'  /* TODO */
};
// 物模型属性映射（按服务组织，IoTDA属性名 --> 系统内部字段）
var SERVICE_PROPS = {
    DeviceStatus: {
        online: 'online', battery: 'battery', signal: 'signal',
        power: 'power', work_state: 'workState', last_report: 'lastReport'
    },
    Water_status: {
        tds: 'tds', pH: 'ph', temp: 'temp',
        rfid: 'rfid', gps: 'gps'
    },
    gps: {
        longitude: 'longitude', latitude: 'latitude', gps_status: 'gpsStatus'
    },
    Alarm: {
        alarm_id: 'alarmId', alarm_type: 'alarmType', device_id: 'alarmDeviceId',
        rfid: 'alarmRfid', current_value: 'currentValue', threshold: 'alarmThreshold',
        alarm_level: 'alarmLevel', alarm_time: 'alarmTime', status: 'alarmStatus'
    }
};
// 任一 Water_status 属性匹配即视为水质设备
var WATER_QUALITY_KEYS = Object.keys(SERVICE_PROPS.Water_status);
var pollingTimer = null;
var consecutivePollFailures = 0;
var MAX_POLL_FAILURES = 3;          // 连续失败 3 次 (15s) 标记所有设备状态陈旧

// ==================== 客户端连接管理（引用计数 + 心跳） ====================
var clients = {};            // { clientId: { lastHeartbeat: timestamp } }
var heartbeatTimeoutMs = 15000;  // 15秒无心跳视为断开
var heartbeatCheckInterval = null;

// 有活跃客户端时才启动轮询
function ensurePolling() {
    if (pollingTimer) return;
    console.log('[POLL] 启动轮询 (活跃客户端: ' + Object.keys(clients).length + ')');
    pollOnce();
    pollingTimer = setInterval(pollOnce, 5000);
}

// 无客户端时停止轮询
function maybeStopPolling() {
    if (Object.keys(clients).length > 0) return;
    if (pollingTimer) {
        clearInterval(pollingTimer);
        pollingTimer = null;
        console.log('[POLL] 停止轮询 (无活跃客户端)');
    }
}

// 定期清理超时的客户端心跳
function heartbeatCleanup() {
    var now = Date.now();
    var staleIds = [];
    Object.keys(clients).forEach(function(id) {
        if (now - clients[id].lastHeartbeat > heartbeatTimeoutMs) {
            staleIds.push(id);
        }
    });
    if (staleIds.length > 0) {
        staleIds.forEach(function(id) {
            console.log('[CLIENT] 心跳超时: ' + id);
            delete clients[id];
        });
        console.log('[CLIENT] 剩余客户端: ' + Object.keys(clients).length);
        maybeStopPolling();
    }
}

// ==================== IoTDA SDK ====================
function getClient() {
    var cred = new core.BasicCredentials()
        .withAk(hwConfig.ak)
        .withSk(hwConfig.sk)
        .withProjectId(hwConfig.projectId);
    // 实例专用端点需要 V11-HMAC-SHA256 派生签名
    cred.withDerivedPredicate(function() { return true; });
    cred.withRegionId('cn-south-1');
    cred.processDerivedAuthParams('iotda', 'cn-south-1');
    return iotda.IoTDAClient.newBuilder()
        .withCredential(cred)
        .withEndpoint(hwConfig.endpoint)
        .build();
}

function queryDevices() {
    var client = getClient();
    var req = new iotda.ListDevicesRequest();
    if (hwConfig.productId) req.withProductId(hwConfig.productId);
    return client.listDevices(req).then(function(res) {
        return res.devices || [];
    });
}

// 校验设备影子中是否包含水质监测属性
function isWaterQualityShadow(shadowEntries) {
    if (!shadowEntries || !shadowEntries.length) return false;
    return shadowEntries.some(function(entry) {
        if (entry.service_id === 'Water_status' && entry.reported && entry.reported.properties) {
            var props = entry.reported.properties;
            return WATER_QUALITY_KEYS.some(function(k) { return props[k] !== undefined; });
        }
        return false;
    });
}

function queryDeviceShadow(deviceId) {
    var client = getClient();
    var req = new iotda.ShowDeviceShadowRequest().withDeviceId(deviceId);
    return client.showDeviceShadow(req).then(function(res) {
        if (res && res.shadow && res.shadow.length > 0) return res.shadow;
        return [];
    }).catch(function(err) {
        console.warn('[SDK] showDeviceShadow error:', err.message);
        return [];
    });
}

// ==================== 数据存储 ====================
function readJSON(file) { try { return JSON.parse(fs.readFileSync(file, 'utf-8')); } catch(e) { return {}; } }
function writeJSON(file, data) { fs.writeFileSync(file, JSON.stringify(data, null, 2)); }

// 按服务类型处理设备影子属性
function applyServiceProps(dev, serviceId, props) {
    var map = SERVICE_PROPS[serviceId];
    if (!map) return;
    Object.keys(map).forEach(function(srcKey) {
        if (props[srcKey] !== undefined) {
            dev[map[srcKey]] = props[srcKey];
        }
    });
}

// 以下函数操作共享的 dev 对象引用，不独立读写文件
function saveDeviceStatusData(dev, props) {
    if (props.online !== undefined) dev.statusOnline = props.online;
    if (props.battery !== undefined) dev.battery = Number(props.battery);
    if (props.signal !== undefined) dev.signal = Number(props.signal);
    if (props.power !== undefined) dev.power = String(props.power);
    if (props.work_state !== undefined) dev.workState = String(props.work_state);
    if (props.last_report !== undefined) dev.lastReport = String(props.last_report);
}

function saveWaterStatusData(deviceId, dev, props) {
    if (props.tds !== undefined) dev.data.tds = Number(props.tds);
    if (props.pH !== undefined) dev.data.ph = Number(props.pH);
    if (props.temp !== undefined) dev.data.temp = Number(props.temp);
    if (props.rfid !== undefined) dev.rfid = String(props.rfid);
    if (props.gps !== undefined) dev.gpsRaw = String(props.gps);

    // 写入历史数据
    var hFile = path.join(HISTORY_DIR, deviceId + '.json');
    var history = [];
    try { history = JSON.parse(fs.readFileSync(hFile, 'utf-8')); } catch(e) {}
    history.push({ time: new Date().toISOString(), tds: dev.data.tds, ph: dev.data.ph, temp: dev.data.temp });
    var cutoff = Date.now() - 30 * 24 * 3600 * 1000;
    history = history.filter(function(e) { return new Date(e.time).getTime() > cutoff; });
    if (history.length > 10000) history = history.slice(-10000);
    fs.writeFileSync(hFile, JSON.stringify(history));
}

function saveGpsData(dev, props) {
    if (props.longitude !== undefined) dev.longitude = Number(props.longitude);
    if (props.latitude !== undefined) dev.latitude = Number(props.latitude);
    if (props.gps_status !== undefined) dev.gpsStatus = String(props.gps_status);
}

function saveAlarmData(deviceId, props) {
    var alarmFile = path.join(ALARMS_DIR, deviceId + '.json');
    var alarms = [];
    try { alarms = JSON.parse(fs.readFileSync(alarmFile, 'utf-8')); } catch(e) {}
    alarms.push({
        alarmId: String(props.alarm_id || ''),
        alarmType: String(props.alarm_type || ''),
        alarmDeviceId: String(props.device_id || deviceId),
        alarmRfid: String(props.rfid || ''),
        currentValue: Number(props.current_value || 0),
        alarmThreshold: Number(props.threshold || 0),
        alarmLevel: String(props.alarm_level || ''),
        alarmTime: String(props.alarm_time || ''),
        alarmStatus: String(props.status || '未处理')
    });
    if (alarms.length > 500) alarms = alarms.slice(-500);
    fs.writeFileSync(alarmFile, JSON.stringify(alarms));
}

function saveDeviceData(deviceId, deviceInfo, shadowEntries) {
    var devices = readJSON(DEVICES_FILE);

    // 1. 从 shadow 中提取 RFID，优先作为设备标识
    var rfid = '';
    if (shadowEntries && shadowEntries.length > 0) {
        shadowEntries.forEach(function(entry) {
            if (entry.service_id === 'Water_status' && entry.reported && entry.reported.properties) {
                if (entry.reported.properties.rfid !== undefined && entry.reported.properties.rfid !== '') {
                    rfid = String(entry.reported.properties.rfid);
                }
            }
        });
    }

    // 2. 确定存储 key：优先 RFID，没有则用 device_id
    var storageKey = rfid || deviceId;

    // 3. 如果 RFID 变化（同一 device_id 上报了不同 RFID），清理旧条目
    if (rfid) {
        Object.keys(devices).forEach(function(key) {
            if (key !== storageKey && devices[key].sourceDeviceId === deviceId && devices[key].rfid) {
                // 旧 RFID 条目: 迁移历史/告警数据到新 key，然后移除
                migrateDeviceFiles(key, storageKey);
                delete devices[key];
                console.log('[DEVICE] RFID 变化: ' + key + ' -> ' + storageKey);
            }
        });
    }

    // 4. 新设备必须有水质属性才创建
    var isNewDevice = !devices[storageKey];
    if (isNewDevice && shadowEntries && shadowEntries.length > 0 && !isWaterQualityShadow(shadowEntries)) {
        return;
    }

    if (isNewDevice) {
        devices[storageKey] = {
            id: storageKey,
            name: (deviceInfo && deviceInfo.device_name) || storageKey,
            location: '',
            status: deviceInfo ? (deviceInfo.status || 'OFFLINE') : 'OFFLINE',
            statusOnline: false,
            battery: 0, signal: 0,
            power: '', workState: '', lastReport: '',
            rfid: rfid, sourceDeviceId: deviceId,
            gpsRaw: '', longitude: 0, latitude: 0, gpsStatus: '',
            thresholds: { Tds_threshold: 1000, Ph_min: 6, Ph_max: 9, Temp_threshold: 50 },
            data: { tds: 0, ph: 0, temp: 0 },
            lastUpdate: new Date().toISOString()
        };
    }
    var dev = devices[storageKey];
    // 更新关联字段（如果 RFID 后来才上报，补齐 rfid 和迁移 key）
    dev.sourceDeviceId = deviceId;
    if (rfid) dev.rfid = rfid;

    if (deviceInfo) {
        dev.name = deviceInfo.device_name || dev.name;
        // 直接使用云端返回的 status，不回退到旧值，避免保留过期在线状态
        dev.status = deviceInfo.status || 'OFFLINE';
    }

    // 5. 遍历所有服务影子条目（历史/告警写入使用 storageKey）
    if (shadowEntries && shadowEntries.length > 0) {
        shadowEntries.forEach(function(entry) {
            if (!entry.reported || !entry.reported.properties) return;
            var props = entry.reported.properties;
            switch (entry.service_id) {
                case 'DeviceStatus':
                    saveDeviceStatusData(dev, props);
                    break;
                case 'Water_status':
                    saveWaterStatusData(storageKey, dev, props);
                    break;
                case 'gps':
                    saveGpsData(dev, props);
                    break;
                case 'Alarm':
                    saveAlarmData(storageKey, props);
                    break;
            }
        });
    }

    dev.lastUpdate = new Date().toISOString();
    writeJSON(DEVICES_FILE, devices);
}

// 当 RFID 变化时，迁移历史数据和告警数据文件
function migrateDeviceFiles(oldKey, newKey) {
    var oldHistory = path.join(HISTORY_DIR, oldKey + '.json');
    var newHistory = path.join(HISTORY_DIR, newKey + '.json');
    var oldAlarms = path.join(ALARMS_DIR, oldKey + '.json');
    var newAlarms = path.join(ALARMS_DIR, newKey + '.json');

    try {
        if (fs.existsSync(oldHistory)) {
            var oldData = JSON.parse(fs.readFileSync(oldHistory, 'utf-8'));
            var newData = [];
            try { newData = JSON.parse(fs.readFileSync(newHistory, 'utf-8')); } catch(e) {}
            var merged = oldData.concat(newData).sort(function(a, b) {
                return new Date(a.time).getTime() - new Date(b.time).getTime();
            });
            fs.writeFileSync(newHistory, JSON.stringify(merged));
            fs.unlinkSync(oldHistory);
        }
    } catch(e) { console.warn('[MIGRATE] 历史数据迁移失败:', e.message); }

    try {
        if (fs.existsSync(oldAlarms)) {
            var oldA = JSON.parse(fs.readFileSync(oldAlarms, 'utf-8'));
            var newA = [];
            try { newA = JSON.parse(fs.readFileSync(newAlarms, 'utf-8')); } catch(e) {}
            var mergedA = oldA.concat(newA);
            if (mergedA.length > 500) mergedA = mergedA.slice(-500);
            fs.writeFileSync(newAlarms, JSON.stringify(mergedA));
            fs.unlinkSync(oldAlarms);
        }
    } catch(e) { console.warn('[MIGRATE] 告警数据迁移失败:', e.message); }
}

// ==================== 轮询 ====================
function startPolling() {
    if (pollingTimer) return;
    console.log('[POLL] 开始轮询');
    pollOnce();
    pollingTimer = setInterval(pollOnce, 5000);
}

function stopPolling() {
    if (pollingTimer) { clearInterval(pollingTimer); pollingTimer = null; }
    console.log('[POLL] 停止');
}

function pollOnce() {
    if (!hwConfig.ak || !hwConfig.sk || !hwConfig.projectId) return;
    syncFromCloud().then(function() {
        consecutivePollFailures = 0;
        console.log('[POLL] OK, ' + Object.keys(readJSON(DEVICES_FILE)).length + ' 设备');
    }).catch(function(err) {
        consecutivePollFailures++;
        console.error('[POLL] 失败 (' + consecutivePollFailures + '/' + MAX_POLL_FAILURES + '):', err.message);
        if (consecutivePollFailures >= MAX_POLL_FAILURES) {
            markAllDevicesStale();
        }
    });
}

// 连续轮询失败时将所有 ONLINE 设备标记为 UNKNOWN，防止前端显示过期在线状态
function markAllDevicesStale() {
    var devices = readJSON(DEVICES_FILE);
    var changed = false;
    Object.keys(devices).forEach(function(id) {
        if (devices[id].status === 'ONLINE') {
            devices[id].status = 'UNKNOWN';
            devices[id].statusOnline = false;
            changed = true;
            console.log('[POLL] 标记设备状态为 UNKNOWN: ' + id);
        }
    });
    if (changed) {
        writeJSON(DEVICES_FILE, devices);
        console.log('[POLL] 连续 ' + MAX_POLL_FAILURES + ' 次失败, 所有 ONLINE 设备已标记为 UNKNOWN');
    }
}

// 从华为云同步设备数据
function syncFromCloud() {
    return queryDevices().then(function(devices) {
        return Promise.all(devices.map(function(dev) {
            return queryDeviceShadow(dev.device_id).then(function(s) {
                saveDeviceData(dev.device_id, dev, s);
            }).catch(function() {
                saveDeviceData(dev.device_id, dev, null);
            });
        }));
    });
}

// ==================== 报警命令下发 ====================
// 使用 Alarm 服务的 set_alarm_mode / clear_alarm 命令（云端→设备）
function sendAlarmCommand(deviceId, commandName, paras) {
    var client = getClient();
    var body = new iotda.DeviceCommandRequest();
    body.withServiceId('Alarm');
    body.withCommandName(commandName);
    body.withParas(paras || {});
    var req = new iotda.CreateCommandRequest();
    req.withDeviceId(deviceId);
    req.withBody(body);
    return client.createCommand(req).then(function(res) {
        console.log('[ALARM] 命令已下发: ' + deviceId + ' cmd=' + commandName, JSON.stringify(paras));
        return res;
    }).catch(function(err) {
        console.error('[ALARM] 下发失败: ' + deviceId, err.message);
        throw err;
    });
}

// ==================== REST API ====================
var express = require('express');
var app = express();
app.use(express.json());

app.post('/api/config', function(req, res) {
    var b = req.body;
    if (b.ak) hwConfig.ak = b.ak.trim();
    if (b.sk) hwConfig.sk = b.sk.trim();
    if (b.projectId) hwConfig.projectId = b.projectId.trim();
    if (b.productId) hwConfig.productId = b.productId.trim();
    if (b.endpoint) hwConfig.endpoint = b.endpoint.trim();
    var clientId = b.clientId || '';
    console.log('[CONFIG] AK长度:', hwConfig.ak.length, 'SK长度:', hwConfig.sk.length,
                'AK首字符:', hwConfig.ak.substring(0, 4), 'SK首字符:', hwConfig.sk.substring(0, 4),
                'Client:', clientId);

    var responded = false;
    function reply(code, msg) {
        if (!responded) { responded = true; res.json({ code: code, message: msg }); }
    }

    console.log('[CONFIG] 测试连接...');
    queryDevices().then(function(devices) {
        console.log('[CONFIG] 列出 ' + devices.length + ' 设备, 开始同步影子...');
        // 先同步一次影子数据再回复前端，确保首次 pollData 能读到数据
        return syncFromCloud().then(function() {
            // 注册客户端并启动轮询（引用计数管理）
            if (clientId) {
                var isNew = !clients[clientId];
                clients[clientId] = { lastHeartbeat: Date.now() };
                if (isNew) console.log('[CLIENT] 注册: ' + clientId + ' (总数: ' + Object.keys(clients).length + ')');
            }
            ensurePolling();
            var saved = readJSON(DEVICES_FILE);
            console.log('[CONFIG] 同步完成, ' + Object.keys(saved).length + ' 设备');
            reply(200, '成功, ' + devices.length + ' 设备');
        });
    }).catch(function(err) {
        console.error('[CONFIG] 失败:', err.message);
        reply(401, '失败: ' + err.message);
    });
    setTimeout(function() { reply(500, '超时'); }, 10000);
});

app.get('/api/config', function(req, res) {
    res.json({ code: 200, data: { connected: !!(pollingTimer), clientCount: Object.keys(clients).length } });
});

app.get('/api/devices', function(req, res) {
    res.json({ code: 200, data: readJSON(DEVICES_FILE) });
});

// 主动从华为云刷新设备列表
app.post('/api/refresh', function(req, res) {
    if (!hwConfig.ak) return res.json({ code: 401, message: '请先配置AK/SK' });
    console.log('[REFRESH] 手动刷新设备列表...');
    syncFromCloud().then(function() {
        var devices = readJSON(DEVICES_FILE);
        console.log('[REFRESH] 完成, ' + Object.keys(devices).length + ' 设备');
        res.json({ code: 200, data: devices, message: '刷新成功' });
    }).catch(function(err) {
        console.error('[REFRESH] 失败:', err.message);
        res.json({ code: 500, message: err.message });
    });
});

// 根据标识符查找设备条目（支持 RFID key 和 sourceDeviceId 两种查询方式）
function findDeviceByIdentifier(identifier) {
    var devices = readJSON(DEVICES_FILE);
    // 1. 直接匹配 key（RFID 或 device_id）
    if (devices[identifier]) return { entry: devices[identifier], targetId: identifier };
    // 2. 按 sourceDeviceId 查找（兼容旧 device_id 查询）
    var keys = Object.keys(devices);
    for (var i = 0; i < keys.length; i++) {
        if (devices[keys[i]].sourceDeviceId === identifier) {
            return { entry: devices[keys[i]], targetId: keys[i] };
        }
    }
    return null;
}

// 下发报警命令到设备
app.post('/api/alarm', function(req, res) {
    if (!hwConfig.ak) return res.json({ code: 401, message: '请先配置AK/SK' });
    var b = req.body;
    var deviceIdentifier = b.deviceId;  // 可能是 RFID key 或华为云 device_id
    var alarmType = b.alarmType || '自动触发';
    var currentValue = b.currentValue || 0;
    var threshold = b.threshold || 0;
    var alarmLevel = b.alarmLevel || '警告';
    if (!deviceIdentifier) return res.json({ code: 400, message: '缺少 deviceId' });

    // 查找设备：支持 RFID key 和 sourceDeviceId 两种查找方式
    var found = findDeviceByIdentifier(deviceIdentifier);
    var storageKey = found ? found.targetId : deviceIdentifier;
    var actualDeviceId = found ? found.entry.sourceDeviceId : deviceIdentifier;

    console.log('[ALARM] 触发告警: key=' + storageKey + ' device=' + actualDeviceId + ' type=' + alarmType);
    // 写入本地告警记录（使用 storageKey 路径）
    saveAlarmData(storageKey, {
        alarm_id: 'WEB-' + Date.now(),
        alarm_type: alarmType || '自动触发',
        device_id: actualDeviceId,
        rfid: found ? (found.entry.rfid || '') : '',
        current_value: currentValue || 0,
        threshold: threshold || 0,
        alarm_level: alarmLevel || '警告',
        alarm_time: new Date().toISOString(),
        status: '未处理'
    });
    sendAlarmCommand(actualDeviceId, 'set_alarm_mode', {
        mode: 'alert',
        buffer_time: 0
    }).then(function() {
        res.json({ code: 200, message: '报警命令已下发(set_alarm_mode)' });
    }).catch(function(err) {
        res.json({ code: 500, message: '下发失败: ' + err.message });
    });
});

// 停止报警命令
app.post('/api/alarm/stop', function(req, res) {
    if (!hwConfig.ak) return res.json({ code: 401, message: '请先配置AK/SK' });
    var deviceIdentifier = req.body.deviceId;
    if (!deviceIdentifier) return res.json({ code: 400, message: '缺少 deviceId' });

    var found = findDeviceByIdentifier(deviceIdentifier);
    var actualDeviceId = found ? found.entry.sourceDeviceId : deviceIdentifier;

    console.log('[ALARM] 停止告警: key=' + (found ? found.targetId : deviceIdentifier) + ' device=' + actualDeviceId);
    sendAlarmCommand(actualDeviceId, 'set_alarm_mode', {
        mode: 'normal',
        buffer_time: 0
    }).then(function() {
        res.json({ code: 200, message: '停止命令已下发(set_alarm_mode)' });
    }).catch(function(err) {
        res.json({ code: 500, message: '下发失败: ' + err.message });
    });
});

app.get('/api/devices/:id', function(req, res) {
    var found = findDeviceByIdentifier(req.params.id);
    if (found) return res.json({ code: 200, data: found.entry });
    res.json({ code: 404, message: 'not found' });
});

app.get('/api/devices/:id/history', function(req, res) {
    var hFile = path.join(HISTORY_DIR, req.params.id + '.json');
    var history = [];
    try { history = JSON.parse(fs.readFileSync(hFile, 'utf-8')); } catch(e) {}
    var param = req.query.param || 'temp';
    var range = req.query.range || '24h';
    var hours = { '1h': 1, '6h': 6, '24h': 24, '7d': 168, '30d': 720 }[range] || 24;
    var cutoff = Date.now() - hours * 3600 * 1000;
    history = history.filter(function(e) { return new Date(e.time).getTime() > cutoff; });
    var stats = { max: '--', min: '--', avg: '--', count: 0 };
    if (history.length > 0) {
        var vals = history.map(function(h) { return h[param] || 0; });
        stats = { max: Math.max.apply(null, vals).toFixed(2), min: Math.min.apply(null, vals).toFixed(2), avg: (vals.reduce(function(a,b){return a+b;},0)/vals.length).toFixed(2), count: vals.length };
    }
    res.json({ code: 200, data: history, stats: stats });
});

// 查询设备告警列表
app.get('/api/devices/:id/alarms', function(req, res) {
    var alarmFile = path.join(ALARMS_DIR, req.params.id + '.json');
    var alarms = [];
    try { alarms = JSON.parse(fs.readFileSync(alarmFile, 'utf-8')); } catch(e) {}
    // 支持 ?limit= 参数，默认最近20条
    var limit = parseInt(req.query.limit) || 20;
    var latest = alarms.slice(-limit).reverse();
    res.json({ code: 200, data: latest, total: alarms.length });
});

app.post('/api/stop', function(req, res) {
    var clientId = req.body.clientId;
    if (clientId && clients[clientId]) {
        delete clients[clientId];
        console.log('[CLIENT] 离开 (via /stop): ' + clientId + ' (剩余: ' + Object.keys(clients).length + ')');
    } else if (!clientId) {
        console.warn('[DEPRECATED] /api/stop 未提供 clientId');
    }
    maybeStopPolling();
    res.json({ code: 200, message: 'ok' });
});

// ==================== 客户端心跳 ====================
// 注册/刷新心跳
app.post('/api/heartbeat', function(req, res) {
    var clientId = req.body.clientId;
    if (!clientId) return res.json({ code: 400, message: '缺少 clientId' });
    var isNew = !clients[clientId];
    clients[clientId] = { lastHeartbeat: Date.now() };
    if (isNew) {
        console.log('[CLIENT] 心跳注册: ' + clientId + ' (总数: ' + Object.keys(clients).length + ')');
    }
    res.json({ code: 200, data: { clientCount: Object.keys(clients).length } });
});

// 客户端离开（用于 sendBeacon，仅支持 POST）
app.post('/api/heartbeat/leave', function(req, res) {
    var clientId = req.body.clientId;
    if (clientId && clients[clientId]) {
        delete clients[clientId];
        console.log('[CLIENT] 离开 (via beacon): ' + clientId + ' (剩余: ' + Object.keys(clients).length + ')');
        maybeStopPolling();
    }
    res.json({ code: 200, message: 'ok' });
});

// 客户端离开（fetch DELETE）
app.delete('/api/heartbeat', function(req, res) {
    var clientId = req.body.clientId;
    if (clientId && clients[clientId]) {
        delete clients[clientId];
        console.log('[CLIENT] 离开 (via DELETE): ' + clientId + ' (剩余: ' + Object.keys(clients).length + ')');
        maybeStopPolling();
    }
    res.json({ code: 200, message: 'ok' });
});

app.use(express.static(__dirname));

// ==================== 数据迁移：旧 device_id key → RFID key ====================
function migrateDevicesIfNeeded() {
    var devices = readJSON(DEVICES_FILE);
    var migrated = {};
    var needsMigration = false;
    Object.keys(devices).forEach(function(key) {
        var dev = devices[key];
        // 已有 sourceDeviceId 字段说明已经迁移过
        if (dev.sourceDeviceId) {
            migrated[key] = dev;
            return;
        }
        // 旧格式：key 是 device_id，检查是否有 rfid 可用作新 key
        if (dev.rfid && dev.rfid !== key && dev.rfid !== '') {
            dev.sourceDeviceId = key;
            dev.id = dev.rfid;
            migrated[dev.rfid] = dev;
            needsMigration = true;
            // 迁移历史/告警文件
            migrateDeviceFiles(key, dev.rfid);
            console.log('[MIGRATE] ' + key + ' -> ' + dev.rfid);
        } else {
            // 无 RFID: 补充 sourceDeviceId 字段但保留原 key
            dev.sourceDeviceId = key;
            migrated[key] = dev;
            needsMigration = true;
        }
    });
    if (needsMigration) {
        writeJSON(DEVICES_FILE, migrated);
        console.log('[MIGRATE] 设备数据迁移完成, ' + Object.keys(migrated).length + ' 设备');
    }
}

app.listen(PORT, function() {
    console.log('========================================');
    console.log('  智慧水质监测 - 后端服务 v2 (SDK)');
    console.log('  端口: ' + PORT);
    console.log('  前端: http://localhost:' + PORT);
    console.log('  心跳超时: ' + (heartbeatTimeoutMs / 1000) + 's');
    console.log('========================================');
    // 启动时自动迁移旧格式数据
    migrateDevicesIfNeeded();
    // 每 10 秒检查一次客户端心跳超时
    heartbeatCheckInterval = setInterval(heartbeatCleanup, 10000);
});
