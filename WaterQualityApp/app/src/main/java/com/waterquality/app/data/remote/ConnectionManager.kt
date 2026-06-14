package com.waterquality.app.data.remote

import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import com.waterquality.app.WaterQualityApp
import com.waterquality.app.data.local.PreferencesManager
import com.waterquality.app.data.model.Device
import com.waterquality.app.data.model.Thresholds
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

    fun connect(ak: String, sk: String, projectId: String, onResult: ((Boolean, String) -> Unit)? = null) {
        if (_connecting.value == true) {
            onResult?.invoke(false, "正在连接中，请稍候")
            return
        }
        _connecting.postValue(true)
        _connectionError.postValue(null)
        scope.launch(Dispatchers.IO) {
            try {
                api.disconnect()
                api.configure(ak, sk, projectId)
                val result = api.verify()
                if (result.isSuccess) {
                    prefs.ak = ak; prefs.sk = sk; prefs.projectId = projectId
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
        connect(prefs.ak, prefs.sk, prefs.projectId, onResult)
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
        }
    }

    fun stopAlarm() {
        if (prefs.isAutoMode) return
        val dev = _currentDevice.value
        if (dev == null) return
        _alarmActive.postValue(false)
        _alarmRecords.postValue(emptyList())
        scope.launch(Dispatchers.IO) {
            repository.sendAlarmCommand(dev.id, "normal")
        }
    }

    private fun checkAutoAlarm(device: Device) {
        if (!prefs.isAutoMode) return

        val d = device.data; val th = device.thresholds
        val triggered = d.temp > th.Temp_threshold || d.ph < th.Ph_min || d.ph > th.Ph_max || d.tds > th.Tds_threshold

        if (triggered) {
            val details = mutableListOf<String>()
            if (d.temp > th.Temp_threshold) details.add("水温超限(${d.temp}>${th.Temp_threshold})")
            if (d.ph < th.Ph_min || d.ph > th.Ph_max) details.add("pH超限(${d.ph})")
            if (d.tds > th.Tds_threshold) details.add("TDS超限(${d.tds}>${th.Tds_threshold})")
            _alarmActive.postValue(true)
            _alarmRecords.postValue(details)
            android.util.Log.d("ALARM", "Auto alarm triggered: $details")

            if (!alarmSent) {
                alarmSent = true
                scope.launch { repository.sendAlarmCommand(device.id, "alert") }
            }
        } else {
            if (alarmSent) {
                alarmSent = false
                _alarmActive.postValue(false)
                _alarmRecords.postValue(emptyList())
            }
        }
    }
}
