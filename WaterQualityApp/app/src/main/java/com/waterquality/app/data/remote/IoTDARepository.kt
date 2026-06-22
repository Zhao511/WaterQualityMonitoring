package com.waterquality.app.data.remote

import com.huaweicloud.sdk.iotda.v5.model.DeviceShadowData
import com.huaweicloud.sdk.iotda.v5.model.QueryDeviceSimplify
import com.waterquality.app.data.model.Device
import com.waterquality.app.data.model.WaterData
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class IoTDARepository(private val api: IoTDAApi = IoTDAApi()) {

    suspend fun syncDevices(): List<Device> = withContext(Dispatchers.IO) {
        val sdkDevices = api.listDevices()
        sdkDevices.mapNotNull { info ->
            val id = info.deviceId ?: return@mapNotNull null
            val shadows = api.getDeviceShadow(id)
            buildDevice(id, info, shadows)
        }
    }

    @Suppress("UNCHECKED_CAST")
    private fun buildDevice(id: String, info: QueryDeviceSimplify, shadows: List<DeviceShadowData>): Device {
        var dev = Device(
            id = id,
            name = info.deviceName ?: id,
            status = info.status ?: "OFFLINE",
            sourceDeviceId = id,
            lastUpdate = java.text.SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'", java.util.Locale.US).format(java.util.Date())
        )
        for (entry in shadows) {
            val rawProps: Any? = entry.reported?.properties
            val props: Map<String, Any?> = when (rawProps) {
                is Map<*, *> -> rawProps as Map<String, Any?>
                else -> continue
            }
            dev = when (entry.serviceId) {
                "DeviceStatus" -> dev.copy(
                    statusOnline = props["online"] as? Boolean ?: dev.statusOnline,
                    battery = (props["battery"] as? Number)?.toDouble() ?: dev.battery,
                    signal = (props["signal"] as? Number)?.toInt() ?: dev.signal,
                    power = props["power"] as? String ?: dev.power,
                    workState = props["work_state"] as? String ?: dev.workState,
                    lastReport = props["last_report"] as? String ?: dev.lastReport
                )
                "Water_status" -> dev.copy(
                    data = WaterData(
                        tds = (props["tds"] as? Number)?.toDouble() ?: dev.data.tds,
                        ph = (props["pH"] as? Number)?.toDouble() ?: dev.data.ph,
                        temp = (props["temp"] as? Number)?.toDouble() ?: dev.data.temp
                    ),
                    rfid = props["rfid"] as? String ?: dev.rfid,
                    gpsRaw = props["gps"] as? String ?: dev.gpsRaw
                )
                "gps" -> dev.copy(
                    longitude = (props["longitude"] as? Number)?.toDouble() ?: dev.longitude,
                    latitude = (props["latitude"] as? Number)?.toDouble() ?: dev.latitude,
                    gpsStatus = props["gps_status"] as? String ?: dev.gpsStatus
                )
                else -> dev
            }
        }
        return dev
    }

    suspend fun sendAlarmCommand(deviceId: String, alarmMode: String, rfid: String? = null) = withContext(Dispatchers.IO) {
        val paras = mutableMapOf<String, Any>("mode" to alarmMode, "buffer_time" to 0)
        if (!rfid.isNullOrBlank()) {
            paras["rfid"] = rfid
        }
        api.createCommand(deviceId, "Alarm", "set_alarm_mode", paras)
    }
}
