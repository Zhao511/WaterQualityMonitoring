/**
 * 智慧水质监测系统 - 主应用逻辑
 */

// ==================== 客户端标识（每个标签页唯一） ====================
var CLIENT_ID = (function() {
    var stored = sessionStorage.getItem('_wq_clientId');
    if (!stored) {
        stored = 'wq_' + Date.now() + '_' + Math.random().toString(36).slice(2, 9);
        sessionStorage.setItem('_wq_clientId', stored);
    }
    return stored;
})();

// ==================== 工具函数 ====================
function showToast(msg, type) {
    type = type || 'info';
    var bg = { success: '#4CAF50', error: '#F44336', info: '#2196F3', warning: '#FF9800' }[type] || '#333';
    var el = document.createElement('div');
    el.textContent = msg;
    el.style.cssText = 'position:fixed;top:20px;right:20px;z-index:99999;padding:12px 24px;color:#fff;background:' + bg + ';border-radius:4px;font-size:14px;max-width:400px;box-shadow:0 4px 12px rgba(0,0,0,.3);animation:fadeIn 0.3s';
    document.body.appendChild(el);
    setTimeout(function() { el.style.opacity = '0'; el.style.transition = 'opacity 0.3s'; }, 2500);
    setTimeout(function() { if (el.parentNode) el.parentNode.removeChild(el); }, 3000);
}

// ==================== 设备列表管理 ====================
function initDeviceList() {
    // 从后端拉取设备列表并渲染侧边栏和设备管理页面
    refreshDeviceList(true);
}

// refresh: false=读缓存, true=实时从华为云拉取
function refreshDeviceList(refresh) {
    var promise = refresh ? fetchRefresh() : fetchDevices();
    promise.then(function(res) {
        var devices = res && res.data ? res.data : {};
        var ids = Object.keys(devices);

        // 用后端数据完全替换 CONFIG.devices（移除不存在的旧设备）
        var isFirstLoad = Object.keys(CONFIG.devices).length === 0;
        CONFIG.devices = {};
        ids.forEach(function(id) {
            var d = devices[id];
            CONFIG.devices[id] = {
                id: id, name: d.name || id, location: d.location || '',
                status: d.status || 'OFFLINE',
                statusOnline: d.statusOnline || false,
                battery: d.battery || 0, signal: d.signal || 0,
                power: d.power || '', workState: d.workState || '', lastReport: d.lastReport || '',
                rfid: d.rfid || '', longitude: d.longitude || 0, latitude: d.latitude || 0,
                gpsStatus: d.gpsStatus || '', gpsRaw: d.gpsRaw || '',
                thresholds: d.thresholds || { Tds_threshold: 1000, Ph_min: 6, Ph_max: 9, Temp_threshold: 50 },
                data: d.data || { tds: 0, ph: 0, temp: 0 },
                lastUpdate: d.lastUpdate || new Date().toISOString()
            };
        });

        // 自动选择当前设备：优先 ONLINE
        if (isFirstLoad || !CONFIG.devices[CONFIG.currentDeviceId]) {
            var allIds = Object.keys(CONFIG.devices);
            var pickId = allIds[0];
            for (var i = 0; i < allIds.length; i++) {
                if (CONFIG.devices[allIds[i]].status === 'ONLINE') { pickId = allIds[i]; break; }
            }
            if (pickId) CONFIG.currentDeviceId = pickId;
        }

        // 渲染UI
        renderDeviceSidebar();
        renderDeviceSelector();
        renderDeviceTable();

        // 实时监测页刷新当前设备和阈值滑块
        if (AppRouter.currentPage && AppRouter.currentPage.id === 'page-main') {
            var dev = getCurrentDevice();
            if (dev) { updateWaterQualityData(dev); initThresholdSliders(); }
        }
        saveConfig();
    }).catch(function() {
        renderDeviceSidebar();
        renderDeviceSelector();
        renderDeviceTable();
        if (getCurrentDevice()) initThresholdSliders();
    });
}

function renderDeviceSidebar() {
    var el = document.getElementById('device-list');
    if (!el) return;
    var devices = getAllDevices();
    if (devices.length === 0) {
        el.innerHTML = '<div style=\"padding:10px;color:#999;text-align:center;\">暂无设备<br><small>请先连接华为云平台</small></div>';
        return;
    }
    var html = '';
    devices.forEach(function(d) {
        var activeClass = d.id === CONFIG.currentDeviceId ? ' active' : '';
        var statusClass = d.status === 'ONLINE' ? 'online' : 'offline';
        html += '<div class=\"device-item' + activeClass + '\" onclick=\"switchDevice(\'' + d.id + '\')\">';
        html += '<span class=\"device-status-dot ' + statusClass + '\"></span>';
        // 优先显示 RFID 作为设备名称
        html += '<span class=\"device-name\">' + (d.rfid || d.name || d.id) + '</span>';
        html += '</div>';
    });
    el.innerHTML = html;
}

function renderDeviceSelector() {
    var sel = document.getElementById('device-select');
    if (!sel) return;
    var devices = getAllDevices();
    var html = '';
    devices.forEach(function(d) {
        var selected = d.id === CONFIG.currentDeviceId ? ' selected' : '';
        html += '<option value=\"' + d.id + '\"' + selected + '>' + (d.rfid || d.name || d.id) + '</option>';
    });
    sel.innerHTML = html;
}

function renderDeviceTable() {
    var tbody = document.getElementById('device-table-body');
    if (!tbody) return;
    var devices = getAllDevices();
    if (devices.length === 0) {
        tbody.innerHTML = '<tr><td colspan=\"7\" style=\"text-align:center;color:#999;\">暂无设备，请先连接华为云平台</td></tr>';
        return;
    }
    var html = '';
    devices.forEach(function(d) {
        var statusLabel = d.status === 'ONLINE' ? '<span class=\"label label-success\">在线</span>' :
                          d.status === 'OFFLINE' ? '<span class=\"label label-warning\">离线</span>' :
                          d.status === 'UNKNOWN' ? '<span class=\"label label-default\">未知</span>' :
                          '<span class=\"label label-default\">未知</span>';
        html += '<tr>';
        // 设备标识（RFID 优先）
        html += '<td>' + (d.rfid || d.name || d.id) + '</td>';
        html += '<td>' + (d.name || '-') + '</td>';
        html += '<td>' + (d.location || '-') + '</td>';
        html += '<td><code>' + (d.sourceDeviceId || d.id) + '</code></td>';
        html += '<td>' + statusLabel + '</td>';
        html += '<td>' + (d.lastUpdate ? new Date(d.lastUpdate).toLocaleString() : '-') + '</td>';
        html += '<td><button class=\"btn btn-xs btn-info\" onclick=\"switchDevice(\'' + d.id + '\')\">查看</button></td>';
        html += '</tr>';
    });
    tbody.innerHTML = html;
}

// ==================== 设备切换 ====================
function switchDevice(deviceId) {
    if (!CONFIG.devices[deviceId]) return;
    CONFIG.currentDeviceId = deviceId;
    saveConfig();
    updateWaterQualityData(getCurrentDevice());
    initThresholdSliders();   // 切换设备后更新阈值滑块
    renderDeviceSidebar();
    renderDeviceSelector();
    fetchAlarms(deviceId);
}

// ==================== 路由配置 ====================
var AppRouter = {
    // 页面定义：key = 路由path, value = 页面配置
    pages: {
        '/home/main': {
            id: 'page-main',
            navGroup: 'home',
            subNav: 'home-subnav',
            title: '实时监测',
            sectionTitle: '运营首页',
            showDeviceSidebar: true,
            showDeviceBar: true,
            init: function() { /* 仪表盘已在initGauges初始化 */ }
        },
        '/home/history': {
            id: 'page-history',
            navGroup: 'home',
            subNav: 'home-subnav',
            title: '历史曲线',
            sectionTitle: '运营首页',
            showDeviceSidebar: false,
            showDeviceBar: true,
            init: function() { loadHistoryChart(); }
        },
        '/home/device-list': {
            id: 'page-device-list',
            navGroup: 'home',
            subNav: 'home-subnav',
            title: '设备管理',
            sectionTitle: '运营首页',
            showDeviceSidebar: false,
            showDeviceBar: true,
            init: function() { refreshDeviceList(); }
        },
        '/home/log': {
            id: 'page-log',
            navGroup: 'home',
            subNav: 'home-subnav',
            title: '工作日志',
            sectionTitle: '运营首页',
            showDeviceSidebar: false,
            showDeviceBar: false,
            init: function() { initLogDeviceFilter(); renderLogs(); }
        },
        '/set/IDKEY': {
            id: 'page-IDKEY',
            navGroup: 'set',
            subNav: 'set-subnav',
            title: 'IDKey',
            sectionTitle: '更多信息',
            showDeviceSidebar: false,
            showDeviceBar: false,
            init: function() { /* IDKey页面静态内容 */ }
        },
        '/set/about': {
            id: 'page-about',
            navGroup: 'set',
            subNav: 'set-subnav',
            title: '版本信息',
            sectionTitle: '更多信息',
            showDeviceSidebar: false,
            showDeviceBar: false,
            init: function() { /* About页面静态内容 */ }
        }
    },

    // 当前页面
    currentPage: null,

    // 解析路由
    resolve: function(path) {
        path = path || '/';
        // 默认重定向到主页
        if (path === '/' || path === '/home') {
            path = '/home/main';
        } else if (path === '/set') {
            path = '/set/IDKEY';
        }
        return this.pages[path] || this.pages['/home/main'];
    },

    // 切换页面
    navigate: function(path) {
        var pageConfig = this.resolve(path);
        if (!pageConfig) return;

        this.currentPage = pageConfig;

        // 1. 隐藏所有页面内容
        document.querySelectorAll('.page-content').forEach(function(el) {
            el.classList.remove('active');
        });

        // 2. 显示目标页面
        var targetPage = document.getElementById(pageConfig.id);
        if (targetPage) {
            targetPage.classList.add('active');
        }

        // 3. 更新一级导航
        this.updateMainNav(pageConfig.navGroup);

        // 4. 更新二级导航显示/隐藏和内容
        this.updateSubNav(pageConfig);

        // 5. 更新设备侧边栏显示/隐藏
        this.updateDeviceSidebar(pageConfig.showDeviceSidebar);

        // 6. 更新设备信息栏显示/隐藏
        this.updateDeviceBar(pageConfig.showDeviceBar);

        // 7. 更新顶部标题
        this.updateTitle(pageConfig.sectionTitle);

        // 8. 执行页面初始化
        if (pageConfig.init) {
            pageConfig.init();
        }

        // 9. 更新浏览器标题
        document.title = pageConfig.sectionTitle + ' - ' + pageConfig.title + ' | 智慧水质监测';
    },

    // 更新一级导航
    updateMainNav: function(activeGroup) {
        document.querySelectorAll('.sidebar-nav a').forEach(function(link) {
            link.classList.remove('active');
            var href = link.getAttribute('href') || '';
            // 精确匹配当前路径分组
            var group = href.indexOf('#/home') !== -1 ? 'home' :
                       href.indexOf('#/set') !== -1 ? 'set' : '';
            if (group === activeGroup) {
                link.classList.add('active');
            }
        });
    },

    // 更新二级导航
    updateSubNav: function(pageConfig) {
        // 先隐藏所有二级导航
        document.querySelectorAll('.sub-nav').forEach(function(nav) {
            nav.style.display = 'none';
            nav.querySelectorAll('a').forEach(function(a) {
                a.classList.remove('active');
            });
        });

        // 显示当前分组的二级导航
        if (pageConfig.subNav) {
            var subNav = document.getElementById(pageConfig.subNav);
            if (subNav) {
                subNav.style.display = 'flex';
                // 高亮当前标签
                var currentHash = window.location.hash.slice(1) || '/';
                subNav.querySelectorAll('a').forEach(function(a) {
                    if (a.getAttribute('href') === '#' + currentHash) {
                        a.classList.add('active');
                    }
                });
            }
        }
    },

    // 更新设备侧边栏
    updateDeviceSidebar: function(show) {
        var sidebar = document.getElementById('device-sidebar');
        if (sidebar) {
            sidebar.style.display = show ? 'block' : 'none';
        }
        // 调整主内容区边距
        var mainContent = document.getElementById('main-content');
        if (mainContent) {
            mainContent.style.marginLeft = show ? '420px' : '220px';
        }
    },

    // 更新设备信息栏
    updateDeviceBar: function(show) {
        var bar = document.getElementById('device-info-bar');
        if (bar) {
            bar.style.display = show ? 'flex' : 'none';
        }
    },

    // 更新标题
    updateTitle: function(title) {
        var titleEl = document.getElementById('section-title');
        if (titleEl) {
            titleEl.textContent = title;
        }
    },

    // 初始化
    init: function() {
        var self = this;
        window.addEventListener('hashchange', function() {
            self.navigate(window.location.hash.slice(1));
        });
        // 初始导航
        this.navigate(window.location.hash.slice(1) || '/');
    }
};

// ==================== 全局变量 ====================
var gauges = {};
var alarmEnabled = false;
var saveConfigTimer = null;
var pollingTimer = null;
var POLLING_INTERVAL = 3000; // 3秒轮询一次

// ==================== 客户端心跳 ====================
var heartbeatTimer = null;
var HEARTBEAT_INTERVAL = 5000; // 5秒发一次心跳

function sendHeartbeat() {
    api('/api/heartbeat', {
        method: 'POST',
        body: { clientId: CLIENT_ID }
    }).catch(function(err) {
        console.warn('[HEARTBEAT] 失败:', err.message);
    });
}

function startHeartbeat() {
    if (heartbeatTimer) return;
    sendHeartbeat();  // 立即发一次
    heartbeatTimer = setInterval(sendHeartbeat, HEARTBEAT_INTERVAL);
}

function stopHeartbeat() {
    if (heartbeatTimer) {
        clearInterval(heartbeatTimer);
        heartbeatTimer = null;
    }
    // fire-and-forget 注销客户端
    api('/api/heartbeat', {
        method: 'DELETE',
        body: { clientId: CLIENT_ID }
    }).catch(function() {});
}

// 防抖保存配置（避免滑块拖动时频繁写 localStorage）
function saveConfigDebounced() {
    if (saveConfigTimer) clearTimeout(saveConfigTimer);
    saveConfigTimer = setTimeout(function() {
        saveConfig();
    }, 500);
}

// 立即保存并清除防抖
function saveConfigFlush() {
    if (saveConfigTimer) {
        clearTimeout(saveConfigTimer);
        saveConfigTimer = null;
    }
    saveConfig();
}

// ==================== 仪表盘初始化 ====================
function initGauges() {
    if (typeof FusionCharts === 'undefined') {
        console.warn('[GAUGE] FusionCharts 未加载，跳过仪表盘');
        gauges = { tds: null, ph: null, temp: null };
        return;
    }
    try {
        gauges.tds = new FusionCharts({
        type: 'angulargauge',
        renderAt: 'chart-tds',
        width: '250', height: '250',
        dataFormat: 'json',
        dataSource: {
            chart: { caption: 'TDS', lowerLimit: '0', upperLimit: '2000', showValue: '1', theme: 'fusion' },
            colorRange: { color: [
                { minValue: '0', maxValue: '800', color: '#27AE60' },
                { minValue: '800', maxValue: '1500', color: '#F39C12' },
                { minValue: '1500', maxValue: '2000', color: '#E74C3C' }
            ]},
            dials: { dial: [{ value: '0' }] }
        }
    });
    gauges.tds.render();

    gauges.ph = new FusionCharts({
        type: 'angulargauge',
        renderAt: 'chart-ph',
        width: '250', height: '250',
        dataFormat: 'json',
        dataSource: {
            chart: { caption: 'pH值', lowerLimit: '0', upperLimit: '14', showValue: '1', theme: 'fusion' },
            colorRange: { color: [
                { minValue: '0', maxValue: '4', color: '#E74C3C' },
                { minValue: '4', maxValue: '6', color: '#F39C12' },
                { minValue: '6', maxValue: '9', color: '#27AE60' },
                { minValue: '9', maxValue: '14', color: '#E74C3C' }
            ]},
            dials: { dial: [{ value: '7' }] }
        }
    });
    gauges.ph.render();

    gauges.temp = new FusionCharts({
        type: 'angulargauge',
        renderAt: 'chart-temp',
        width: '250', height: '250',
        dataFormat: 'json',
        dataSource: {
            chart: { caption: '水温', lowerLimit: '-20', upperLimit: '100', showValue: '1', theme: 'fusion' },
            colorRange: { color: [
                { minValue: '-20', maxValue: '0', color: '#5B9BD5' },
                { minValue: '0', maxValue: '50', color: '#62B58F' },
                { minValue: '50', maxValue: '100', color: '#FC6211' }
            ]},
            dials: { dial: [{ value: '25' }] }
        }
    });
    gauges.temp.render();
    } catch (e) {
        console.error('[GAUGE] 仪表盘初始化失败:', e.message);
        gauges = { tds: null, ph: null, temp: null };
    }
}

// 更新仪表盘数据
function updateGauge(gaugeName, value) {
    var g = gauges[gaugeName];
    if (g && typeof g.feedData === 'function') {
        try { g.feedData('&value=' + value); } catch (e) {}
    }
}

// 更新水质数据显示
function updateWaterQualityData(device) {
    if (!device) return;

    // 水质数据
    document.getElementById('tds-value').textContent = (device.data.tds || 0).toFixed(0) + ' mg/L';
    document.getElementById('ph-value').textContent = (device.data.ph || 0).toFixed(1);
    document.getElementById('temp-value').textContent = (device.data.temp || 0).toFixed(1) + '℃';

    updateGauge('tds', device.data.tds || 0);
    updateGauge('ph', device.data.ph || 0);
    updateGauge('temp', device.data.temp || 0);

    // 设备信息
    document.getElementById('rfid-value').textContent = device.rfid || '未设置';
    document.getElementById('gps-value').textContent =
        '经度: ' + (device.longitude || 0).toFixed(4) + ' | 纬度: ' + (device.latitude || 0).toFixed(4);

    // 设备运行状态
    document.getElementById('device-battery').textContent = (device.battery || 0).toFixed(0) + '%';
    document.getElementById('device-signal').textContent = (device.signal || 0) + ' dBm';
    document.getElementById('device-power').textContent = device.power || '--';
    document.getElementById('device-workstate').textContent = device.workState || '--';
    document.getElementById('device-lastreport').textContent = device.lastReport || '--';
    document.getElementById('gps-status').textContent = device.gpsStatus || '--';

    document.getElementById('current-device-name').textContent = device.rfid || device.name || device.id;

    // 在线状态 - 以华为云 listDevices 返回的 status 为准
    var isOnline = device.status === 'ONLINE';
    var isUnknown = device.status === 'UNKNOWN';
    var statusEl = document.getElementById('current-device-status');
    if (statusEl) {
        statusEl.className = 'label ' + (isOnline ? 'label-success' : (isUnknown ? 'label-warning' : 'label-default'));
        statusEl.textContent = isOnline ? '在线' : (isUnknown ? '状态未知' : '离线');
    }

    checkThresholds(device);
}

// 检查阈值
function checkThresholds(device) {
    if (!device) return;

    var statusLight = document.getElementById('device-status-light');
    if (!statusLight) return;

    var isOnline = device.status === 'ONLINE';

    console.log('[CHECK] mode=' + getCurrentMode() + ' online=' + isOnline + ' connected=' + CONFIG.cloudConnected);
    console.log('[CHECK] data:', JSON.stringify(device.data), 'thresholds:', JSON.stringify(device.thresholds));

    if (!CONFIG.cloudConnected || !isOnline) {
        console.log('[CHECK] 退出: 设备离线或未连接');
        statusLight.className = 'status-light status-offline';
        document.getElementById('device-status-text').textContent = '离线';
        var els = document.querySelectorAll('.gauge-status');
        els.forEach(function(el) { el.className = 'gauge-status offline'; el.textContent = '设备离线'; });
        return;
    }

    var els2 = document.querySelectorAll('.gauge-status');
    els2.forEach(function(el) { el.className = 'gauge-status online'; el.textContent = '设备在线'; });

    var curMode = getCurrentMode();
    if (curMode === 'auto-mode') {
        var isNormal = true;
        var th = device.thresholds;
        var d = device.data;

        if (d.temp > th.Temp_threshold) { console.log('[CHECK] 水温超限: ' + d.temp + ' > ' + th.Temp_threshold); isNormal = false; }
        if (d.ph < th.Ph_min || d.ph > th.Ph_max) { console.log('[CHECK] pH超限: ' + d.ph + ' 范围[' + th.Ph_min + ',' + th.Ph_max + ']'); isNormal = false; }
        if (d.tds > th.Tds_threshold) { console.log('[CHECK] TDS超限: ' + d.tds + ' > ' + th.Tds_threshold); isNormal = false; }

        console.log('[CHECK] isNormal=' + isNormal + ' curMode=' + curMode);
        if (isNormal) {
            statusLight.className = 'status-light status-normal';
            document.getElementById('device-status-text').textContent = '正常';
            // 恢复正常后清除告警标记，更新面板
            if (device._alarmSent) {
                device._alarmSent = false;
                setAlarmPanelUI(false);
                document.getElementById('alarm-list').innerHTML = '<div class=\"alarm-empty\">暂无告警记录</div>';
            }
        } else {
            statusLight.className = 'status-light status-warning';
            document.getElementById('device-status-text').textContent = '预警';
            var alarmDetails = [];
            if (d.temp > th.Temp_threshold) alarmDetails.push('水温超限(' + d.temp.toFixed(1) + '>' + th.Temp_threshold + ')');
            if (d.ph < th.Ph_min || d.ph > th.Ph_max) alarmDetails.push('pH超限(' + d.ph.toFixed(1) + ')');
            if (d.tds > th.Tds_threshold) alarmDetails.push('TDS超限(' + d.tds.toFixed(0) + '>' + th.Tds_threshold + ')');
            addLog('warning', device.id, '自动告警触发: ' + alarmDetails.join(', '));
            // 更新告警面板
            setAlarmPanelUI(true, '告警中');
            renderAlarms(alarmDetails.map(function(detail) {
                return { alarmType: detail, alarmLevel: '警告', alarmTime: new Date().toLocaleString() };
            }));
            // 自动模式下超阈值 → 下发 set_alarm_mode 命令（避免重复发送）
            if (!device._alarmSent) {
                device._alarmSent = true;
                sendAlarm(device.id, alarmDetails.join(','),
                    Math.max(d.temp || 0, d.ph || 0, d.tds || 0),
                    Math.max(th.Temp_threshold || 50, th.Ph_max || 9, th.Tds_threshold || 1000),
                    '警告');
            }
        }
    } else {
        statusLight.className = 'status-light status-normal';
        document.getElementById('device-status-text').textContent = '正常';
    }
}

// 初始化阈值滑块
function initThresholdSliders() {
    if (typeof noUiSlider === 'undefined') {
        console.warn('[SLIDER] noUiSlider 未加载，延迟重试');
        setTimeout(initThresholdSliders, 500);
        return;
    }

    var device = getCurrentDevice();
    if (!device) return;

    // 先销毁已存在的滑块
    ['temp-threshold-slider', 'ph-threshold-slider', 'tds-threshold-slider'].forEach(function(id) {
        var el = document.getElementById(id);
        if (el && el.noUiSlider) {
            el.noUiSlider.destroy();
        }
    });

    var thresholds = device.thresholds;

    function createSlider(elId, options, updateFn, changeFn) {
        var el = document.getElementById(elId);
        if (!el) { console.warn('[SLIDER] 元素不存在: ' + elId); return; }
        try {
            var slider = noUiSlider.create(el, options);
            slider.on('update', updateFn);
            slider.on('change', changeFn);
        } catch (e) {
            console.error('[SLIDER] 创建失败: ' + elId, e.message);
        }
    }

    createSlider('temp-threshold-slider',
        { start: [thresholds.Temp_threshold], range: { min: 0, max: 100 }, step: 1 },
        function(values) { document.getElementById('temp-threshold-value').textContent = parseFloat(values[0]).toFixed(0) + '℃'; },
        function(values) {
            device.thresholds.Temp_threshold = parseFloat(values[0]);
            addLog('info', device.id, '水温阈值更新为 ' + values[0] + '℃');
            saveConfig();
        }
    );

    createSlider('ph-threshold-slider',
        { start: [thresholds.Ph_min, thresholds.Ph_max], range: { min: 0, max: 14 }, step: 0.1, connect: true },
        function(values) { document.getElementById('ph-threshold-value').textContent = parseFloat(values[0]).toFixed(1) + ' - ' + parseFloat(values[1]).toFixed(1); },
        function(values) {
            device.thresholds.Ph_min = parseFloat(values[0]);
            device.thresholds.Ph_max = parseFloat(values[1]);
            addLog('info', device.id, 'pH阈值更新为 ' + values[0] + ' - ' + values[1]);
            saveConfig();
        }
    );

    createSlider('tds-threshold-slider',
        { start: [thresholds.Tds_threshold], range: { min: 0, max: 2000 }, step: 10 },
        function(values) { document.getElementById('tds-threshold-value').textContent = parseFloat(values[0]).toFixed(0) + ' mg/L'; },
        function(values) {
            device.thresholds.Tds_threshold = parseFloat(values[0]);
            addLog('info', device.id, 'TDS阈值更新为 ' + values[0] + ' mg/L');
            saveConfig();
        }
    );
}

// 初始化模式切换
function initModeSwitch() {
    document.querySelectorAll("input[name='mode']").forEach(function(radio) {
        radio.addEventListener('change', function() {
            CONFIG.curMode = this.value;
            saveConfig();
            var modeName = CONFIG.curMode === 'auto-mode' ? '自动模式' : '手动模式';
            addLog('info', '', '运行模式切换为: ' + modeName);
            showToast('运行模式已切换为: ' + modeName, 'success');
        });
    });

    document.querySelector("input[name='mode'][value='" + CONFIG.curMode + "']").checked = true;
}

// 获取当前运行模式（从 radio 读取，避免 CONFIG 不同步）
function getCurrentMode() {
    var checkedRadio = document.querySelector("input[name='mode']:checked");
    return checkedRadio ? checkedRadio.value : CONFIG.curMode;
}

// 更新告警面板 UI（自动/手动模式共用）
function setAlarmPanelUI(active, msg) {
    var icon = document.getElementById('alarm-icon');
    var status = document.getElementById('alarm-status');
    if (icon) icon.className = 'alarm-icon ' + (active ? 'alarm' : 'normal');
    if (status) status.innerHTML = active
        ? '<span class=\"label label-danger\">' + (msg || '告警中') + '</span>'
        : '<span class=\"label label-success\">告警关闭</span>';
}

// 切换告警状态
function toggleAlarm(enabled) {
    var curMode = getCurrentMode();
    console.log('[ALARM] 当前模式:', curMode, 'CONFIG存储:', CONFIG.curMode);

    if (enabled) {
        // 检查条件：自动模式不能手动启动告警
        if (curMode === 'auto-mode') {
            showToast('自动模式下，告警由系统自动控制，请先切换到手动模式', 'error');
            return;
        }
        var dev = getCurrentDevice();
        if (!dev) { showToast('请先选择设备', 'error'); return; }
        if (!CONFIG.cloudConnected || dev.status !== 'ONLINE') {
            showToast('设备离线状态下无法启用手动告警', 'error');
            return;
        }
        setAlarmPanelUI(true, '告警已下发');
        addLog('warning', dev.id, '手动触发水质告警 → 下发命令到云端');
        // 下发 set_alarm_mode 命令到设备
        sendAlarm(dev.id, '手动触发', 0, 0, '警告');
        showToast('报警命令已下发到云端', 'info');
    } else {
        var dev2 = getCurrentDevice();
        setAlarmPanelUI(false);
        if (dev2) {
            addLog('info', dev2.id, '手动关闭水质告警 → 下发停止命令');
            // 下发停止命令
            stopAlarm(dev2.id);
        }
        showToast('停止命令已下发到云端', 'info');
    }
}

// ==================== REST API 数据层 ====================

// 通用 fetch 封装
function api(url, options) {
    options = options || {};
    return fetch(url, {
        method: options.method || 'GET',
        headers: { 'Content-Type': 'application/json' },
        body: options.body ? JSON.stringify(options.body) : undefined
    }).then(function(res) {
        if (!res.ok) throw new Error('HTTP ' + res.status);
        return res.json();
    });
}

// 从后端缓存拉取设备数据
function fetchDevices() {
    return api('/api/devices');
}

// 从后端实时向华为云刷新设备列表
function fetchRefresh() {
    return api('/api/refresh', { method: 'POST' });
}

// 向华为云下发报警命令（使用 Alarm 服务的 set_alarm_mode 命令）
function sendAlarm(deviceId, alarmType, currentValue, threshold, alarmLevel) {
    console.log('[ALARM] 下发报警:', deviceId, alarmType);
    api('/api/alarm', {
        method: 'POST',
        body: {
            deviceId: deviceId, alarmType: alarmType || '手动触发',
            currentValue: currentValue || 0, threshold: threshold || 0,
            alarmLevel: alarmLevel || '警告'
        }
    }).then(function(res) {
        if (res.code === 200) {
            console.log('[ALARM] 报警命令已下发到云端');
        } else {
            console.error('[ALARM] 下发失败:', res.message);
        }
    }).catch(function(err) {
        console.error('[ALARM] 请求失败:', err.message);
    });
}

// 向华为云下发停止报警命令
function stopAlarm(deviceId) {
    console.log('[ALARM] 停止报警:', deviceId);
    api('/api/alarm/stop', {
        method: 'POST',
        body: { deviceId: deviceId }
    }).then(function(res) {
        if (res.code === 200) {
            console.log('[ALARM] 停止命令已下发到云端');
        }
    }).catch(function(err) {
        console.error('[ALARM] 请求失败:', err.message);
    });
}

// 获取设备告警列表
function fetchAlarms(deviceId) {
    if (!deviceId) return;
    api('/api/devices/' + deviceId + '/alarms?limit=5').then(function(res) {
        if (res && res.data) {
            renderAlarms(res.data);
        }
    }).catch(function() {});
}

// 渲染告警面板
function renderAlarms(alarms) {
    var el = document.getElementById('alarm-list');
    if (!el) return;
    if (!alarms || alarms.length === 0) {
        el.innerHTML = '<div class=\"alarm-empty\">暂无告警记录</div>';
        return;
    }
    var levelClass = { '普通': 'label-info', '警告': 'label-warning', '紧急': 'label-danger' };
    var html = '';
    alarms.forEach(function(a) {
        var cls = levelClass[a.alarmLevel] || 'label-default';
        html += '<div class=\"alarm-item\">';
        html += '<span class=\"label ' + cls + '\">' + (a.alarmLevel || '--') + '</span> ';
        html += '<span class=\"alarm-type\">' + (a.alarmType || '--') + '</span> ';
        html += '<span class=\"alarm-value\">值:' + (a.currentValue || 0) + '/阈值:' + (a.alarmThreshold || 0) + '</span> ';
        html += '<span class=\"alarm-time\">' + (a.alarmTime || '--') + '</span> ';
        html += '</div>';
    });
    el.innerHTML = html;
}

// 从后端拉取历史数据
function fetchHistory(deviceId, param, range) {
    return api('/api/devices/' + deviceId + '/history?param=' + param + '&range=' + range);
}

// 连接云端（提交华为云AK/SK到后端，启动轮询）
function connectCloud() {
    var ak = document.getElementById('cloud-ak').value.trim();
    var sk = document.getElementById('cloud-sk').value.trim();
    var projectId = document.getElementById('cloud-projectid').value.trim();
    var productId = document.getElementById('cloud-productid').value.trim();
    var endpoint = document.getElementById('cloud-endpoint').value.trim();

    if (!ak || !sk || !projectId) {
        showToast('请填写 AK、SK 和项目ID', 'error');
        return;
    }

    CONFIG.ak = ak;
    CONFIG.sk = sk;
    CONFIG.projectId = projectId;
    CONFIG.productId = productId;
    CONFIG.endpoint = endpoint;
    saveConfig();

    document.getElementById('cloud-status').innerHTML =
        '<span class="label label-warning">认证中...</span>';

    // 发送到后端
    api('/api/config', {
        method: 'POST',
        body: { ak: ak, sk: sk, projectId: projectId, productId: productId, endpoint: endpoint, clientId: CLIENT_ID }
    }).then(function(res) {
        if (res.code === 200) {
            CONFIG.cloudConnected = true;
            CONFIG.realConnection = true;
            saveConfig();
            document.getElementById('cloud-status').innerHTML =
                '<span class="label label-success">已连接</span>';
            addLog('success', '', '华为云API认证成功');
            showToast('认证成功，开始拉取设备数据', 'success');

            // 启动心跳（通知后端此客户端活跃）
            startHeartbeat();

            // 前端开始轮询
            if (pollingTimer) clearInterval(pollingTimer);
            pollData();
            pollingTimer = setInterval(pollData, POLLING_INTERVAL);
        } else {
            document.getElementById('cloud-status').innerHTML =
                '<span class="label label-danger">认证失败</span>';
            showToast('认证失败: ' + (res.message || '未知错误'), 'error');
        }
    }).catch(function(err) {
        document.getElementById('cloud-status').innerHTML =
            '<span class="label label-danger">连接失败</span>';
        showToast('连接后端失败，请确认 node server.js 已启动', 'error');
    });
}

// 轮询后端数据
function pollData() {
    fetchDevices().then(function(res) {
        if (!res || !res.data) return;
        var devices = res.data;
        var ids = Object.keys(devices);
        if (ids.length === 0) return;

        // 用后端数据同步：新增/更新，并删除后端已经不再返回的设备
        var backendIds = {};
        ids.forEach(function(id) { backendIds[id] = true; });
        // 删除不在后端的旧设备
        var changed = false;
        Object.keys(CONFIG.devices).forEach(function(oldId) {
            if (!backendIds[oldId]) { delete CONFIG.devices[oldId]; changed = true; }
        });
        ids.forEach(function(id) {
            var dev = devices[id];
            if (!CONFIG.devices[id]) {
                CONFIG.devices[id] = {
                    id: id, name: dev.name || id, location: dev.location || '',
                    status: dev.status || 'OFFLINE',
                    statusOnline: dev.statusOnline || false,
                    battery: dev.battery || 0, signal: dev.signal || 0,
                    power: dev.power || '', workState: dev.workState || '', lastReport: dev.lastReport || '',
                    rfid: dev.rfid || '', longitude: dev.longitude || 0, latitude: dev.latitude || 0,
                    gpsStatus: dev.gpsStatus || '', gpsRaw: dev.gpsRaw || '',
                    thresholds: dev.thresholds || { Tds_threshold: 1000, Ph_min: 6, Ph_max: 9, Temp_threshold: 50 },
                    data: dev.data || { tds: 0, ph: 0, temp: 0 },
                    lastUpdate: Date.now()
                };
                changed = true;
            } else {
                var oldData = JSON.stringify(CONFIG.devices[id].data);
                CONFIG.devices[id].data = dev.data || CONFIG.devices[id].data;
                // 用 !== undefined 判断，防止 false/0/'' 等假值被跳过
                if (dev.status !== undefined) CONFIG.devices[id].status = dev.status;
                if (dev.statusOnline !== undefined) CONFIG.devices[id].statusOnline = dev.statusOnline;
                if (dev.battery !== undefined) CONFIG.devices[id].battery = dev.battery;
                if (dev.signal !== undefined) CONFIG.devices[id].signal = dev.signal;
                if (dev.power !== undefined) CONFIG.devices[id].power = dev.power;
                if (dev.workState !== undefined) CONFIG.devices[id].workState = dev.workState;
                if (dev.lastReport !== undefined) CONFIG.devices[id].lastReport = dev.lastReport;
                if (dev.rfid !== undefined) CONFIG.devices[id].rfid = dev.rfid;
                if (dev.longitude !== undefined) CONFIG.devices[id].longitude = dev.longitude;
                if (dev.latitude !== undefined) CONFIG.devices[id].latitude = dev.latitude;
                if (dev.gpsStatus !== undefined) CONFIG.devices[id].gpsStatus = dev.gpsStatus;
                if (oldData !== JSON.stringify(CONFIG.devices[id].data)) changed = true;
            }
        });

        if (changed) refreshAll();
        // 自动选择当前设备：优先 ONLINE，其次第一个
        if (!getCurrentDevice()) {
            var allIds = Object.keys(CONFIG.devices);
            var pickId = allIds[0];
            for (var i = 0; i < allIds.length; i++) {
                if (CONFIG.devices[allIds[i]].status === 'ONLINE') { pickId = allIds[i]; break; }
            }
            if (pickId) { CONFIG.currentDeviceId = pickId; saveConfig(); }
        }
        // 实时监测页始终刷新当前设备
        if (AppRouter.currentPage && AppRouter.currentPage.id === 'page-main') {
            var dev = getCurrentDevice();
            if (dev) updateWaterQualityData(dev);
        }
    }).catch(function(e) { console.warn('[API] 轮询失败:', e.message); });
}

function refreshAll() {
    renderDeviceSidebar();
    renderDeviceSelector();
    // 连接平台后设备已加载，补初始化滑块
    if (getCurrentDevice()) initThresholdSliders();
}

// 停止数据接收
function disconnectCloud() {
    if (pollingTimer) { clearInterval(pollingTimer); pollingTimer = null; }
    stopHeartbeat();  // 发送离开通知并清除心跳
    CONFIG.cloudConnected = false;
    CONFIG.realConnection = false;
    saveConfig();
    // 保留旧 /api/stop 调用以兼容旧版服务端，附带 clientId
    api('/api/stop', { method: 'POST', body: { clientId: CLIENT_ID } }).catch(function(){});
    document.getElementById('cloud-status').innerHTML = '<span class="label label-default">未配置</span>';
    addLog('info', '', '停止数据接收');
    showToast('已停止数据接收', 'info');
}


// ==================== 工作日志系统 ====================
var LOG = [];
var LOG_MAX = 500; // 最多保留500条

// 日志类型中文名
var LOG_TYPE_CN = {
    info: '信息',
    success: '成功',
    warning: '警告',
    error: '错误'
};

// 添加日志
function addLog(type, deviceId, message) {
    var entry = {
        time: new Date().toISOString(),
        type: type || 'info',
        deviceId: deviceId || '',
        deviceName: '',
        message: message || ''
    };

    // 关联设备名称
    if (entry.deviceId && CONFIG.devices[entry.deviceId]) {
        entry.deviceName = CONFIG.devices[entry.deviceId].name;
    } else if (entry.deviceId && entry.deviceId === CONFIG.id) {
        entry.deviceName = '云平台网关';
    }

    LOG.unshift(entry);

    // 超出上限裁剪旧日志
    if (LOG.length > LOG_MAX) {
        LOG.length = LOG_MAX;
    }

    // 如果当前在日志页面，实时刷新
    if (AppRouter.currentPage && AppRouter.currentPage.id === 'page-log') {
        renderLogs();
    }
}

// 初始化日志页面的设备筛选下拉框
function initLogDeviceFilter() {
    var select = document.getElementById('log-device-filter');
    if (!select) return;

    // 保留 "全部设备" 选项
    select.innerHTML = '<option value="all">全部设备</option>';

    Object.values(CONFIG.devices).forEach(function(dev) {
        var opt = document.createElement('option');
        opt.value = dev.id;
        opt.textContent = dev.name + ' (' + dev.id + ')';
        select.appendChild(opt);
    });

    // 添加 "系统" 选项（无设备关联的日志）
    var sysOpt = document.createElement('option');
    sysOpt.value = '_system';
    sysOpt.textContent = '系统';
    select.appendChild(sysOpt);
}

// 获取过滤后的日志
function getFilteredLogs() {
    var typeFilter = document.getElementById('log-type-filter') ?
        document.getElementById('log-type-filter').value : 'all';
    var deviceFilter = document.getElementById('log-device-filter') ?
        document.getElementById('log-device-filter').value : 'all';
    var timeFilter = document.getElementById('log-time-filter') ?
        document.getElementById('log-time-filter').value : 'all';

    var now = Date.now();
    var timeCutoff = 0;
    var timeMap = { '1h': 3600000, '6h': 21600000, '24h': 86400000, '7d': 604800000 };
    if (timeMap[timeFilter]) {
        timeCutoff = now - timeMap[timeFilter];
    }

    return LOG.filter(function(entry) {
        // 类型筛选
        if (typeFilter !== 'all' && entry.type !== typeFilter) return false;

        // 设备筛选
        if (deviceFilter !== 'all') {
            if (deviceFilter === '_system' && entry.deviceId !== '') return false;
            if (deviceFilter !== '_system' && entry.deviceId !== deviceFilter) return false;
        }

        // 时间筛选
        if (timeCutoff > 0) {
            if (new Date(entry.time).getTime() < timeCutoff) return false;
        }

        return true;
    });
}

// 筛选日志
function filterLogs() {
    renderLogs();
}

// 渲染日志列表
function renderLogs() {
    var container = document.getElementById('log-list');
    var totalCount = document.getElementById('log-total-count');
    var filteredCount = document.getElementById('log-filtered-count');

    if (!container) return;

    var filtered = getFilteredLogs();

    if (totalCount) totalCount.textContent = LOG.length;
    if (filteredCount) filteredCount.textContent = filtered.length;

    if (filtered.length === 0) {
        container.innerHTML = '<div class="log-empty">暂无日志记录</div>';
        return;
    }

    var html = '';
    filtered.forEach(function(entry) {
        var time = new Date(entry.time);
        var timeStr = time.getFullYear() + '-' +
            String(time.getMonth() + 1).padStart(2, '0') + '-' +
            String(time.getDate()).padStart(2, '0') + ' ' +
            String(time.getHours()).padStart(2, '0') + ':' +
            String(time.getMinutes()).padStart(2, '0') + ':' +
            String(time.getSeconds()).padStart(2, '0');

        var typeClass = 'log-type-' + entry.type;
        var typeLabel = LOG_TYPE_CN[entry.type] || entry.type;
        var sourceStr = entry.deviceName || entry.deviceId || '系统';

        html += '<div class="log-entry ' + typeClass + '">' +
            '<span class="log-time">' + timeStr + '</span>' +
            '<span class="log-type-badge badge-' + entry.type + '">' + typeLabel + '</span>' +
            '<span class="log-source">[' + sourceStr + ']</span>' +
            '<span class="log-message">' + escapeHtml(entry.message) + '</span>' +
            '</div>';
    });

    container.innerHTML = html;
}

// HTML转义
function escapeHtml(str) {
    var div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
}

// 导出日志
function exportLogs() {
    var filtered = getFilteredLogs();
    if (filtered.length === 0) {
        showToast('没有可导出的日志', 'info');
        return;
    }

    var headers = ['时间', '类型', '来源设备', '消息'];
    var rows = filtered.map(function(entry) {
        return [
            entry.time,
            LOG_TYPE_CN[entry.type] || entry.type,
            entry.deviceName || entry.deviceId || '系统',
            entry.message
        ];
    });

    var csvContent = headers.join(',') + '\n' +
        rows.map(function(row) { return row.map(function(cell) { return '"' + cell + '"'; }).join(','); }).join('\n');

    // 添加 BOM 确保 Excel 正确识别 UTF-8 中文
    var blob = new Blob(['\uFEFF' + csvContent], { type: 'text/csv;charset=utf-8;' });
    var link = document.createElement('a');
    var url = URL.createObjectURL(blob);
    link.href = url;
    link.download = '工作日志_' + new Date().toISOString().slice(0, 10) + '.csv';
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);

    showToast('日志导出成功', 'success');
}

// 清空日志
function clearLogs() {
    if (!confirm('确认清空所有工作日志？此操作不可恢复。')) return;
    LOG = [];
    renderLogs();
    showToast('日志已清空', 'info');
}

// ==================== 页面初始化 ====================
function initPage() {
    loadConfig();

    // 系统启动日志
    addLog('info', '', '智慧水质监测系统启动 v' + CONFIG.version);

    AppRouter.init();
    initGauges();
    initModeSwitch();
    initDeviceList();

    // 更新当前设备数据
    updateWaterQualityData(getCurrentDevice());

    // 回填华为云API配置
    document.getElementById('cloud-ak').value = CONFIG.ak || '';
    document.getElementById('cloud-sk').value = CONFIG.sk || '';
    document.getElementById('cloud-projectid').value = CONFIG.projectId || '';
    document.getElementById('cloud-productid').value = CONFIG.productId || '';
    document.getElementById('cloud-endpoint').value = CONFIG.endpoint || '';
    document.getElementById('cloud-status').innerHTML =
        CONFIG.cloudConnected ? '<span class="label label-success">已连接</span>' : '<span class="label label-default">未配置</span>';

    // 设置版本信息
    document.getElementById('current-version').textContent = '当前版本: ' + CONFIG.version;

    // 模拟数据更新（仅当无真实云端连接时才运行，用于离线演示）
    setInterval(function() {
        if (CONFIG.cloudConnected && !CONFIG.realConnection) {
            var hasOnlineDevice = false;
            Object.values(CONFIG.devices).forEach(function(device) {
                if (device.status === 'online') {
                    device.data.temperature += (Math.random() - 0.5) * 0.5;
                    device.data.ph += (Math.random() - 0.5) * 0.1;
                    device.data.ec += (Math.random() - 0.5) * 10;
                    device.data.turbidity += (Math.random() - 0.5) * 5;

                    device.data.temperature = Math.max(-20, Math.min(100, device.data.temperature));
                    device.data.ph = Math.max(0, Math.min(14, device.data.ph));
                    device.data.ec = Math.max(0, Math.min(2000, device.data.ec));
                    device.data.turbidity = Math.max(0, Math.min(1000, device.data.turbidity));

                    device.lastUpdate = Date.now();
                    hasOnlineDevice = true;
                }
            });

            if (hasOnlineDevice && AppRouter.currentPage && AppRouter.currentPage.id === 'page-main') {
                updateWaterQualityData(getCurrentDevice());
            }
        }
    }, 3000);
}

// ==================== 历史曲线相关功能 ====================
var historyChart = null;

// 生成模拟历史数据
function generateMockHistoryData(hours, intervalMinutes) {
    var data = [];
    var now = Date.now();
    var intervalMs = intervalMinutes * 60 * 1000;
    var totalPoints = Math.floor((hours * 60) / intervalMinutes);
    
    var device = getCurrentDevice();
    if (!device) return data;
    
    var baseTemp = device.data.temperature || 25;
    var basePh = device.data.ph || 7;
    var baseEc = device.data.ec || 500;
    var baseTurb = device.data.turbidity || 100;
    
    for (var i = totalPoints; i >= 0; i--) {
        var timestamp = now - (i * intervalMs);
        var hourOfDay = new Date(timestamp).getHours();
        
        // 添加时间变化趋势
        var tempVariation = Math.sin((hourOfDay / 24) * Math.PI * 2) * 5;
        
        data.push({
            timestamp: timestamp,
            temperature: baseTemp + tempVariation + (Math.random() - 0.5) * 3,
            ph: basePh + (Math.random() - 0.5) * 0.5,
            ec: baseEc + (Math.random() - 0.5) * 50,
            turbidity: baseTurb + (Math.random() - 0.5) * 30
        });
    }
    
    return data;
}

// 获取时间范围对应的小时数
function getHoursFromRange(range) {
    var map = {
        '1h': 1,
        '6h': 6,
        '24h': 24,
        '7d': 7 * 24,
        '30d': 30 * 24
    };
    return map[range] || 24;
}

// 获取数据间隔对应的分钟数
function getIntervalMinutes(interval) {
    var map = {
        '1m': 1,
        '5m': 5,
        '15m': 15,
        '1h': 60
    };
    return map[interval] || 5;
}

// 获取参数单位
function getParamUnit(param) {
    var map = {
        'temperature': '℃',
        'ph': '',
        'ec': 'μS/cm',
        'turbidity': 'NTU'
    };
    return map[param] || '';
}

// 获取参数名称
function getParamName(param) {
    var map = {
        'temperature': '水温',
        'ph': 'pH值',
        'ec': '电导率',
        'turbidity': '浊度'
    };
    return map[param] || param;
}

// 计算统计数据
function calculateStats(data, param) {
    if (!data || data.length === 0) {
        return { max: '--', min: '--', avg: '--', count: 0 };
    }
    
    var values = data.map(function(d) { return d[param]; });
    var max = Math.max.apply(null, values);
    var min = Math.min.apply(null, values);
    var avg = values.reduce(function(a, b) { return a + b; }, 0) / values.length;
    
    return {
        max: max.toFixed(2),
        min: min.toFixed(2),
        avg: avg.toFixed(2),
        count: values.length
    };
}

// 更新统计显示
function updateStatsDisplay(stats, unit) {
    document.getElementById('stat-max').textContent = stats.max + ' ' + unit;
    document.getElementById('stat-min').textContent = stats.min + ' ' + unit;
    document.getElementById('stat-avg').textContent = stats.avg + ' ' + unit;
    document.getElementById('stat-count').textContent = stats.count;
}

// 加载历史曲线图表
function loadHistoryChart() {
    var param = document.getElementById('history-param').value;
    var range = document.getElementById('time-range').value;
    var showThreshold = document.getElementById('show-threshold').checked;
    var unit = getParamUnit(param);

    var device = getCurrentDevice();
    if (!device || !device.id) {
        showToast('请先选择设备', 'info');
        return;
    }

    // 从后端API拉取真实历史数据
    fetchHistory(device.id, param, range).then(function(res) {
        if (!res || !res.data || res.data.length === 0) {
            showToast('该时段暂无历史数据', 'info');
            updateStatsDisplay({ max: '--', min: '--', avg: '--', count: 0 }, unit);
            return;
        }

        var rawData = res.data;
        var stats = res.stats || calculateStats(rawData, param);
        updateStatsDisplay(stats, unit);

        // 格式化图表数据
        var hours = getHoursFromRange(range);
        var chartData = rawData.map(function(d) {
            var date = new Date(d.time);
            var timeStr;
            if (hours <= 24) {
                timeStr = String(date.getHours()).padStart(2, '0') + ':' +
                          String(date.getMinutes()).padStart(2, '0');
            } else {
                timeStr = (date.getMonth() + 1) + '/' + date.getDate() + ' ' +
                          String(date.getHours()).padStart(2, '0') + ':00';
            }
            return [timeStr, parseFloat((d[param] || 0).toFixed(2))];
        });

        // 获取阈值
        var thresholds = device.thresholds;
        var thresholdValue = null;
        if (showThreshold && thresholds) {
            if (param === 'temperature') thresholdValue = thresholds.Tem_threshold;
            else if (param === 'ph') thresholdValue = thresholds.Ph_max;
            else if (param === 'ec') thresholdValue = thresholds.Ec_threshold;
            else if (param === 'turbidity') thresholdValue = thresholds.Turb_threshold;
        }

        // 创建图表
        var chartConfig = {
            type: 'timeseries',
            renderAt: 'history-chart',
            width: '100%',
            height: '400',
            dataFormat: 'json',
            dataSource: {
                chart: {
                    caption: getParamName(param) + '历史曲线（真实数据）',
                    subCaption: '时间范围: ' + range,
                    xAxisName: '时间',
                    yAxisName: getParamName(param) + ' (' + unit + ')',
                    theme: 'fusion',
                    showLegend: false,
                    showValues: false,
                    lineThickness: 2,
                    chartLeftMargin: 50,
                    chartRightMargin: 50
                },
                categories: [{
                    category: chartData.map(function(d) { return { label: d[0] }; })
                }],
                dataset: [{
                    seriesname: getParamName(param),
                    data: chartData.map(function(d) { return { value: d[1] }; })
                }]
            }
        };

        if (thresholdValue !== null) {
            chartConfig.dataSource.trendlines = [{
                line: [{
                    startValue: thresholdValue,
                    endValue: thresholdValue,
                    color: '#E74C3C',
                    thickness: 2,
                    showOnTop: true,
                    valueOnRight: true,
                    label: '阈值: ' + thresholdValue + unit
                }]
            }];
        }

        if (historyChart) historyChart.dispose();
        historyChart = new FusionCharts(chartConfig);
        historyChart.render();

    }).catch(function(err) {
        console.error('[API] 加载历史数据失败:', err);
        showToast('加载历史数据失败，请确认后端已启动', 'error');
    });
}

// 导出历史数据
function exportHistoryData() {
    var param = document.getElementById('history-param').value;
    var range = document.getElementById('time-range').value;
    var interval = document.getElementById('data-interval').value;
    
    var hours = getHoursFromRange(range);
    var intervalMinutes = getIntervalMinutes(interval);
    
    var data = generateMockHistoryData(hours, intervalMinutes);
    
    // 生成CSV内容
    var headers = ['时间', '水温(℃)', 'pH值', '电导率(μS/cm)', '浊度(NTU)'];
    var rows = data.map(function(d) {
        var date = new Date(d.timestamp);
        var timeStr = date.toLocaleString('zh-CN');
        return [
            timeStr,
            d.temperature.toFixed(2),
            d.ph.toFixed(2),
            d.ec.toFixed(2),
            d.turbidity.toFixed(2)
        ];
    });
    
    var csvContent = headers.join(',') + '\n' + rows.map(function(row) { return row.join(','); }).join('\n');
    
    // 创建下载链接
    var blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
    var link = document.createElement('a');
    var url = URL.createObjectURL(blob);
    link.setAttribute('href', url);
    link.setAttribute('download', '水质历史数据_' + new Date().toISOString().slice(0, 10) + '.csv');
    link.style.visibility = 'hidden';
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    
    showToast('数据导出成功', 'success');
}

// 刷新历史数据
function refreshHistoryData() {
    loadHistoryChart();
    showToast('数据已刷新', 'info');
}

// 页面加载完成后初始化
document.addEventListener('DOMContentLoaded', function() {
    initPage();
});

// ==================== 页面生命周期 ====================

// 页面可见性切换 — 暂停/恢复前端本地 UI 轮询
document.addEventListener('visibilitychange', function() {
    if (!CONFIG.cloudConnected) return;
    if (document.hidden) {
        // 切到后台 → 暂停本地轮询（心跳继续）
        if (pollingTimer) {
            clearInterval(pollingTimer);
            pollingTimer = null;
        }
        console.log('[VISIBILITY] 页面隐藏，本地轮询暂停');
    } else {
        // 回到前台 → 立即补发心跳，恢复轮询
        sendHeartbeat();
        if (!pollingTimer) {
            pollData();
            pollingTimer = setInterval(pollData, POLLING_INTERVAL);
        }
        console.log('[VISIBILITY] 页面恢复，本地轮询继续');
    }
});

// 页面即将关闭（PC 端可靠）
window.addEventListener('beforeunload', function() {
    if (CONFIG.cloudConnected && navigator.sendBeacon) {
        var blob = new Blob([JSON.stringify({ clientId: CLIENT_ID })],
            { type: 'application/json' });
        navigator.sendBeacon('/api/heartbeat/leave', blob);
    }
    if (heartbeatTimer) clearInterval(heartbeatTimer);
    if (pollingTimer) clearInterval(pollingTimer);
});

// 页面隐藏（移动端更可靠）
window.addEventListener('pagehide', function() {
    if (CONFIG.cloudConnected && navigator.sendBeacon) {
        var blob = new Blob([JSON.stringify({ clientId: CLIENT_ID })],
            { type: 'application/json' });
        navigator.sendBeacon('/api/heartbeat/leave', blob);
    }
    if (heartbeatTimer) clearInterval(heartbeatTimer);
    if (pollingTimer) clearInterval(pollingTimer);
});