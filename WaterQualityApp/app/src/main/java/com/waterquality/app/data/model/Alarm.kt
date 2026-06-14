package com.waterquality.app.data.model

data class Alarm(
    val alarmId: String = "",
    val alarmType: String = "",
    val alarmDeviceId: String = "",
    val alarmRfid: String = "",
    val currentValue: Double = 0.0,
    val alarmThreshold: Double = 0.0,
    val alarmLevel: String = "",
    val alarmTime: String = "",
    val alarmStatus: String = "未处理"
)

data class AlarmListResponse(
    val code: Int = 0,
    val data: List<Alarm>? = null,
    val total: Int = 0
)
