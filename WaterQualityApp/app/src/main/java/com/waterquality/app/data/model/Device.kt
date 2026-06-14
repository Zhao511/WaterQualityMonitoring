package com.waterquality.app.data.model

data class Device(
    val id: String = "",
    val name: String = "",
    val location: String = "",
    val status: String = "OFFLINE",
    val statusOnline: Boolean = false,
    val battery: Double = 0.0,
    val signal: Int = 0,
    val power: String = "",
    val workState: String = "",
    val lastReport: String = "",
    val rfid: String = "",
    val longitude: Double = 0.0,
    val latitude: Double = 0.0,
    val gpsStatus: String = "",
    val gpsRaw: String = "",
    val thresholds: Thresholds = Thresholds(),
    val data: WaterData = WaterData(),
    val lastUpdate: String = ""
)

data class Thresholds(
    val Temp_threshold: Double = 50.0,
    val Ph_min: Double = 6.0,
    val Ph_max: Double = 9.0,
    val Tds_threshold: Double = 1000.0
)

data class WaterData(
    val tds: Double = 0.0,
    val ph: Double = 0.0,
    val temp: Double = 0.0
)
