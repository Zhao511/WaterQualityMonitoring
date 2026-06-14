package com.waterquality.app.ui.home

import androidx.lifecycle.LiveData
import androidx.lifecycle.ViewModel
import com.waterquality.app.data.model.Device
import com.waterquality.app.data.model.Thresholds
import com.waterquality.app.data.remote.ConnectionManager

class HomeViewModel : ViewModel() {
    val devices: LiveData<List<Device>> = ConnectionManager.devices
    val currentDevice: LiveData<Device?> = ConnectionManager.currentDevice
    val alarmActive: LiveData<Boolean> = ConnectionManager.alarmActive
    val alarmRecords: LiveData<List<String>> = ConnectionManager.alarmRecords
    val connecting: LiveData<Boolean> = ConnectionManager.connecting
    val connectionError: LiveData<String?> = ConnectionManager.connectionError
    val isConnected get() = ConnectionManager.isConnected
    val isAutoMode get() = ConnectionManager.isAutoMode

    fun connect(ak: String, sk: String, pid: String, cb: ((Boolean, String) -> Unit)? = null) =
        ConnectionManager.connect(ak, sk, pid, cb)

    fun autoConnect(cb: ((Boolean, String) -> Unit)? = null) =
        ConnectionManager.autoConnect(cb)

    fun switchDevice(id: String) = ConnectionManager.switchDevice(id)
    fun setAutoMode(enabled: Boolean) = ConnectionManager.setAutoMode(enabled)
    fun updateThreshold(id: String, t: Thresholds) = ConnectionManager.updateThreshold(id, t)
    fun disconnect() = ConnectionManager.disconnect()
    fun triggerAlarm() = ConnectionManager.triggerAlarm()
    fun stopAlarm() = ConnectionManager.stopAlarm()
}
