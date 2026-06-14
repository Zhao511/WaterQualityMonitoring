package com.waterquality.app.data.local

import android.content.Context
import android.content.SharedPreferences
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken
import com.waterquality.app.data.model.Device
import com.waterquality.app.data.model.Thresholds

class PreferencesManager(context: Context) {
    private val prefs: SharedPreferences = context.getSharedPreferences("water_quality", Context.MODE_PRIVATE)
    private val gson = Gson()

    var ak: String
        get() = prefs.getString("ak", "") ?: ""
        set(v) = prefs.edit().putString("ak", v).apply()

    var sk: String
        get() = prefs.getString("sk", "") ?: ""
        set(v) = prefs.edit().putString("sk", v).apply()

    var projectId: String
        get() = prefs.getString("projectId", "") ?: ""
        set(v) = prefs.edit().putString("projectId", v).apply()

    var isAutoMode: Boolean
        get() = prefs.getBoolean("autoMode", false)
        set(v) = prefs.edit().putBoolean("autoMode", v).apply()

    var currentDeviceId: String
        get() = prefs.getString("currentDeviceId", "") ?: ""
        set(v) = prefs.edit().putString("currentDeviceId", v).apply()

    fun saveThresholds(deviceId: String, t: Thresholds) {
        val json = gson.toJson(t)
        prefs.edit().putString("thresholds_$deviceId", json).apply()
    }

    fun getThresholds(deviceId: String): Thresholds {
        val json = prefs.getString("thresholds_$deviceId", null) ?: return Thresholds()
        return try { gson.fromJson(json, Thresholds::class.java) } catch (e: Exception) { Thresholds() }
    }

    fun saveDeviceList(devices: List<Device>) {
        val json = gson.toJson(devices)
        prefs.edit().putString("deviceList", json).apply()
    }

    fun getDeviceList(): List<Device> {
        val json = prefs.getString("deviceList", null) ?: return emptyList()
        return try {
            val type = object : TypeToken<List<Device>>() {}.type
            gson.fromJson(json, type)
        } catch (e: Exception) { emptyList() }
    }
}
