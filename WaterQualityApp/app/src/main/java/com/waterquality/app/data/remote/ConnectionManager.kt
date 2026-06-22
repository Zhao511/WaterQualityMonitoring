package com.waterquality.app.data.remote

import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import com.waterquality.app.WaterQualityApp
import com.waterquality.app.data.local.PreferencesManager
import com.waterquality.app.data.model.Alarm
import com.waterquality.app.data.model.Device
import com.waterquality.app.data.model.Thresholds
import com.waterquality.app.util.LogEntry
import com.waterquality.app.util.LogManager
import kotlinx.coroutines.*
import android.util.Log

object ConnectionManager {
    // 数据
    private val _devices = MutableLiveData<List<Device>>(emptyList())
    val devices: LiveData<List<Device>> = _devices

    private val _currentDevice = MutableLiveData<Device?>(null)
    val currentDevice: LiveData<Device?> = _currentDevice

    private val _alarmActive = MutableLiveData(false)
    val alarmActive: LiveData<Boolean> = _alarmActive

    private val _alarmRecords = MutableLiveData<List<String>>(emptyList())
    val alarmRecords: LiveData<List<String>> = _alarmRecords

    // 结构化告警记录
    private val _alarmList = MutableLiveData<List<Alarm>>(emptyList())
    val alarmList: LiveData<List<Alarm>> = _alarmList

    // 日志
    private val _logs = MutableLiveData<List<LogEntry>>(emptyList())
    val logs: LiveData<List<LogEntry>> = _logs

    // 内部状态 — 共用同一个 api 实例！
    private val api = IoTDAApi()
    private val repository = IoTDARepository(api)
    private val prefs = PreferencesManager(WaterQualityApp.instance)
    private var pollingJob: Job? = null
    private var alarmSent = false
    private var pollingPausedForBackground = false
    private var isActivityDestroyed = false
    val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    // 连接状态
    private val _connecting = MutableLiveData(false)
    val connecting: LiveData<Boolean> = _connecting

    private val _connectionError = MutableLiveData<String?>(null)
    val connectionError: LiveData<String?> = _connectionError

    val isConnected: Boolean get() = api.isConnected
    val isAutoMode: Boolean get() = prefs.isAutoMode

    fun connect(ak: String, sk: String, projectId: String, productId: String = "", endpoint: String = "", onResult: ((Boolean, String) -> Unit)? = null) {
        if (_connecting.value == true) {
            onResult?.invoke(false, "正在连接中，请稍候")
            return
        }
        _connecting.postValue(true)
        _connectionError.postValue(null)
        scope.launch(Dispatchers.IO) {
            try {
                api.disconnect()
                api.configure(ak, sk, projectId, productId, endpoint)
                val result = api.verify()
                if (result.isSuccess) {
                    prefs.ak = ak; prefs.sk = sk; prefs.projectId = projectId
                    prefs.productId = productId; prefs.endpoint = endpoint
                    // 立即同步设备列表到 LiveData
                    val devices = repository.syncDevices()
                    _devices.postValue(devices)
                    prefs.saveDeviceList(devices)
                    val current = devices.find { it.status == "ONLINE" } ?: devices.firstOrNull()
                    if (current != null) {
                        _currentDevice.postValue(current)
                        prefs.currentDeviceId = current.id
                    }
                    _connecting.postValue(false)
                    withContext(Dispatchers.Main) {
                        startPolling()
                        onResult?.invoke(true, "已连接, ${devices.size} 台设备")
                    }
                } else {
                    throw result.exceptionOrNull() ?: Exception("验证失败")
                }
            } catch (e: Exception) {
                api.disconnect()
                _connecting.postValue(false)
                _connectionError.postValue(e.message ?: "连接失败")
                withContext(Dispatchers.Main) {
                    onResult?.invoke(false, e.message ?: "连接失败")
                }
            }
        }
    }

    fun autoConnect(onResult: ((Boolean, String) -> Unit)? = null) {
        if (prefs.ak.isEmpty() || prefs.sk.isEmpty()) return
        if (isConnected || _connecting.value == true) return  // 防止重复连接
        connect(prefs.ak, prefs.sk, prefs.projectId, prefs.productId, prefs.endpoint, onResult)
    }

    fun disconnect() {
        pollingJob?.cancel()
        pollingJob = null
        pollingPausedForBackground = false
        isActivityDestroyed = true
        api.disconnect()
        _devices.postValue(emptyList())
        _currentDevice.postValue(null)
        _alarmActive.postValue(false)
        _connecting.postValue(false)
    }

    // ==================== 前后台生命周期 ====================

    // 暂停轮询（后台时调用，保持 SDK 连接不断开）
    fun pausePolling() {
        if (pollingJob != null) {
            pollingJob?.cancel()
            pollingJob = null
            pollingPausedForBackground = true
            Log.d("ConnectionManager", "轮询已暂停（进入后台）")
        }
    }

    // 恢复轮询（前台时调用）
    fun resumePolling() {
        if (pollingPausedForBackground && isConnected && !isActivityDestroyed) {
            pollingPausedForBackground = false
            startPolling()
            Log.d("ConnectionManager", "轮询已恢复（回到前台）")
        }
    }

    // 前后台状态切换
    fun setForegroundState(inForeground: Boolean) {
        if (inForeground) {
            resumePolling()
        } else {
            pausePolling()
        }
    }

    private fun startPolling() {
        pollingJob?.cancel()
        pollingJob = scope.launch {
            while (isActive) {
                try {
                    val devices = repository.syncDevices()
                    if (devices.isNotEmpty()) {
                        _devices.postValue(devices)
                        prefs.saveDeviceList(devices)

                        val currentId = prefs.currentDeviceId
                        val current = devices.find { it.id == currentId }
                            ?: devices.find { it.status == "ONLINE" }
                            ?: devices.firstOrNull()

                        if (current != null) {
                            val savedTh = prefs.getThresholds(current.id)
                            val dev = current.copy(thresholds = if (savedTh.Temp_threshold > 0) savedTh else current.thresholds)
                            _currentDevice.postValue(dev)
                            prefs.currentDeviceId = current.id
                            checkAutoAlarm(dev)
                        }
                    }
                } catch (e: Exception) {
                    val cached = prefs.getDeviceList()
                    if (cached.isNotEmpty()) _devices.postValue(cached)
                }
                delay(5000)
            }
        }
    }

    fun switchDevice(deviceId: String) {
        prefs.currentDeviceId = deviceId
        _devices.value?.find { it.id == deviceId }?.let { dev ->
            _currentDevice.value = dev.copy(thresholds = prefs.getThresholds(deviceId))
            alarmSent = false
        }
    }

    fun setAutoMode(enabled: Boolean) {
        prefs.isAutoMode = enabled
        if (!enabled) { _alarmActive.value = false }
    }

    fun updateThreshold(deviceId: String, t: Thresholds) {
        prefs.saveThresholds(deviceId, t)
        addLog("info", deviceId, "阈值已更新: 水温<=${t.Temp_threshold}℃, pH=${t.Ph_min}-${t.Ph_max}, TDS<=${t.Tds_threshold}")
    }

    fun getThresholds(deviceId: String) = prefs.getThresholds(deviceId)

    fun triggerAlarm() {
        if (prefs.isAutoMode) {
            _alarmRecords.postValue(listOf("自动模式下由系统自动控制，请先切换到手动模式"))
            return
        }
        val dev = _currentDevice.value
        if (dev == null) {
            _alarmRecords.postValue(listOf("错误：未选择设备"))
            return
        }
        _alarmActive.postValue(true)
        _alarmRecords.postValue(listOf("手动触发告警 - ${dev.id}"))
        scope.launch(Dispatchers.IO) {
            repository.sendAlarmCommand(dev.id, "alert")
            // 同时通知 Web 后端
            WebApiService.triggerAlarm(dev.id, "手动触发")
        }
        addLog("warning", dev.id, "手动触发告警")
    }

    fun stopAlarm() {
        if (prefs.isAutoMode) return
        val dev = _currentDevice.value
        if (dev == null) return
        _alarmActive.postValue(false)
        _alarmRecords.postValue(emptyList())
        scope.launch(Dispatchers.IO) {
            repository.sendAlarmCommand(dev.id, "normal")
            // 同时通知 Web 后端
            WebApiService.stopAlarm(dev.id)
        }
        addLog("success", dev.id, "手动关闭告警")
    }

    private fun checkAutoAlarm(device: Device) {
        if (!prefs.isAutoMode) return

        /* 告警状态由 STM32 终端的 alarm_active 决定, 不在 App 本地判断阈值 */
        val alarmActive = device.data["alarm_active"]?.let { it as? Boolean } ?: false

        if (alarmActive) {
            _alarmActive.postValue(true)
            if (!alarmSent) {
                alarmSent = true
                addLog("warning", device.id, "STM32 终端告警中")
            }
        } else {
            if (alarmSent) {
                alarmSent = false
                _alarmActive.postValue(false)
                _alarmRecords.postValue(emptyList())
                addLog("info", device.id, "STM32 终端告警已清除，恢复正常")
            }
        }
    }

    // ==================== 设备管理（通过 Web 后端） ====================

    /** 删除设备（调用 Web 后端 DELETE API） */
    fun deleteDevice(deviceId: String, onResult: ((Boolean, String) -> Unit)? = null) {
        val dev = _devices.value?.find { it.id == deviceId || it.sourceDeviceId == deviceId }
        val label = dev?.rfid ?: dev?.name ?: deviceId
        scope.launch {
            val result = WebApiService.deleteDevice(deviceId)
            result.onSuccess { msg ->
                // 从本地列表移除
                val updatedList = _devices.value?.filter { it.id != deviceId && it.sourceDeviceId != deviceId } ?: emptyList()
                _devices.postValue(updatedList)

                // 如果删除的是当前设备，自动切换
                if (_currentDevice.value?.id == deviceId || _currentDevice.value?.sourceDeviceId == deviceId) {
                    val next = updatedList.find { it.status == "ONLINE" } ?: updatedList.firstOrNull()
                    _currentDevice.postValue(next)
                    prefs.currentDeviceId = next?.id ?: ""
                }
                addLog("warning", deviceId, "设备已删除: $label")
                WebApiService.sendHeartbeat() // 保持后端心跳
                withContext(Dispatchers.Main) { onResult?.invoke(true, msg) }
            }
            result.onFailure { e ->
                withContext(Dispatchers.Main) { onResult?.invoke(false, e.message ?: "删除失败") }
            }
        }
    }

    /** 更新设备昵称/位置（调用 Web 后端 PUT API） */
    fun updateDevice(deviceId: String, name: String, location: String, onResult: ((Boolean, String) -> Unit)? = null) {
        scope.launch {
            val result = WebApiService.updateDevice(deviceId, name, location)
            result.onSuccess { updatedDev ->
                // 更新本地列表
                val updatedList = _devices.value?.map {
                    if (it.id == deviceId || it.sourceDeviceId == deviceId) it.copy(name = name, location = location)
                    else it
                } ?: emptyList()
                _devices.postValue(updatedList)

                // 更新当前设备
                if (_currentDevice.value?.id == deviceId || _currentDevice.value?.sourceDeviceId == deviceId) {
                    _currentDevice.postValue(_currentDevice.value?.copy(name = name, location = location))
                }
                addLog("info", deviceId, "设备信息已更新: 名称=$name, 位置=$location")
                withContext(Dispatchers.Main) { onResult?.invoke(true, "设备信息已更新") }
            }
            result.onFailure { e ->
                withContext(Dispatchers.Main) { onResult?.invoke(false, e.message ?: "更新失败") }
            }
        }
    }

    /** 从 Web 后端获取设备告警记录 */
    fun fetchAlarms(deviceId: String, limit: Int = 50) {
        scope.launch {
            val result = WebApiService.getDeviceAlarms(deviceId, limit)
            result.onSuccess { (alarms, _) ->
                _alarmList.postValue(alarms)
            }
            result.onFailure {
                _alarmList.postValue(emptyList())
            }
        }
    }

    // ==================== 日志 ====================

    fun addLog(type: String, deviceId: String, message: String) {
        LogManager.add(type, deviceId, message)
        _logs.postValue(LogManager.getAll())
    }

    fun refreshLogs() {
        _logs.postValue(LogManager.getAll())
    }

    fun clearLogs() {
        LogManager.clear()
        _logs.postValue(emptyList())
    }
}
