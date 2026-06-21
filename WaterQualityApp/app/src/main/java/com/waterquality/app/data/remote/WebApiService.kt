package com.waterquality.app.data.remote

import com.google.gson.Gson
import com.google.gson.reflect.TypeToken
import com.waterquality.app.data.model.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.IOException
import java.util.UUID

/**
 * Web 后端 REST API 服务 (OkHttp 封装)
 * 用于获取持久化数据（历史、告警）和执行管理操作（编辑/删除设备）
 */
object WebApiService {

    var baseUrl: String = "http://10.0.2.2:3000"

    private val clientId: String = UUID.randomUUID().toString().take(8)
    private val client = OkHttpClient.Builder()
        .connectTimeout(10, java.util.concurrent.TimeUnit.SECONDS)
        .readTimeout(15, java.util.concurrent.TimeUnit.SECONDS)
        .build()
    private val gson = Gson()
    private val jsonMediaType = "application/json; charset=utf-8".toMediaType()

    // ==================== 设备相关 ====================

    /** 获取所有设备 */
    suspend fun getDevices(): Result<Map<String, Device>> = withContext(Dispatchers.IO) {
        try {
            val resp = get("/api/devices")
            val type = object : TypeToken<ApiResponse<Map<String, Device>>>() {}.type
            val apiResp: ApiResponse<Map<String, Device>> = gson.fromJson(resp, type)
            if (apiResp.code == 200 && apiResp.data != null) {
                Result.success(apiResp.data)
            } else {
                Result.failure(IOException(apiResp.message ?: "获取设备列表失败"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    /** 从华为云强制刷新设备列表 */
    suspend fun refreshDevices(): Result<Map<String, Device>> = withContext(Dispatchers.IO) {
        try {
            val resp = post("/api/refresh", null)
            val type = object : TypeToken<ApiResponse<Map<String, Device>>>() {}.type
            val apiResp: ApiResponse<Map<String, Device>> = gson.fromJson(resp, type)
            if (apiResp.code == 200 && apiResp.data != null) {
                Result.success(apiResp.data)
            } else {
                Result.failure(IOException(apiResp.message ?: "刷新设备列表失败"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    /** 获取单个设备 */
    suspend fun getDevice(id: String): Result<Device> = withContext(Dispatchers.IO) {
        try {
            val resp = get("/api/devices/${java.net.URLEncoder.encode(id, "UTF-8")}")
            val type = object : TypeToken<ApiResponse<Device>>() {}.type
            val apiResp: ApiResponse<Device> = gson.fromJson(resp, type)
            if (apiResp.code == 200 && apiResp.data != null) {
                Result.success(apiResp.data)
            } else {
                Result.failure(IOException(apiResp.message ?: "设备不存在"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    /** 更新设备信息（昵称、位置） */
    suspend fun updateDevice(id: String, name: String, location: String): Result<Device> = withContext(Dispatchers.IO) {
        try {
            val body = mapOf("name" to name, "location" to location)
            val resp = put("/api/devices/${java.net.URLEncoder.encode(id, "UTF-8")}", body)
            val type = object : TypeToken<ApiResponse<Device>>() {}.type
            val apiResp: ApiResponse<Device> = gson.fromJson(resp, type)
            if (apiResp.code == 200 && apiResp.data != null) {
                Result.success(apiResp.data)
            } else {
                Result.failure(IOException(apiResp.message ?: "更新设备失败"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    /** 删除设备（同时删除历史数据和告警记录） */
    suspend fun deleteDevice(id: String): Result<String> = withContext(Dispatchers.IO) {
        try {
            val resp = delete("/api/devices/${java.net.URLEncoder.encode(id, "UTF-8")}")
            val type = object : TypeToken<ApiResponse<DeleteResponse>>() {}.type
            val apiResp: ApiResponse<DeleteResponse> = gson.fromJson(resp, type)
            if (apiResp.code == 200) {
                Result.success(apiResp.message ?: "设备已删除")
            } else {
                Result.failure(IOException(apiResp.message ?: "删除设备失败"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    // ==================== 历史数据 ====================

    /** 获取设备历史水质数据 */
    suspend fun getDeviceHistory(id: String, param: String = "temp", range: String = "24h"): Result<HistoryResponse> = withContext(Dispatchers.IO) {
        try {
            val resp = get("/api/devices/${java.net.URLEncoder.encode(id, "UTF-8")}/history?param=$param&range=$range")
            val type = object : TypeToken<ApiResponse<List<HistoryEntry>>>() {}.type
            val apiResp: ApiResponse<List<HistoryEntry>> = gson.fromJson(resp, type)
            if (apiResp.code == 200) {
                Result.success(HistoryResponse(
                    data = apiResp.data ?: emptyList(),
                    stats = apiResp.stats ?: HistoryStats()
                ))
            } else {
                Result.failure(IOException(apiResp.message ?: "获取历史数据失败"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    // ==================== 告警相关 ====================

    /** 获取设备告警记录 */
    suspend fun getDeviceAlarms(id: String, limit: Int = 50): Result<Pair<List<Alarm>, Int>> = withContext(Dispatchers.IO) {
        try {
            val resp = get("/api/devices/${java.net.URLEncoder.encode(id, "UTF-8")}/alarms?limit=$limit")
            val type = object : TypeToken<ApiResponse<List<Alarm>>>() {}.type
            val apiResp: ApiResponse<List<Alarm>> = gson.fromJson(resp, type)
            if (apiResp.code == 200) {
                Result.success(Pair(apiResp.data ?: emptyList(), apiResp.total ?: 0))
            } else {
                Result.failure(IOException(apiResp.message ?: "获取告警记录失败"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    /** 手动触发告警 */
    suspend fun triggerAlarm(deviceId: String, alarmType: String = "手动触发"): Result<String> = withContext(Dispatchers.IO) {
        try {
            val body = mapOf(
                "deviceId" to deviceId,
                "alarmType" to alarmType,
                "alarmLevel" to "警告"
            )
            val resp = post("/api/alarm", body)
            val type = object : TypeToken<ApiResponse<Any>>() {}.type
            val apiResp: ApiResponse<Any> = gson.fromJson(resp, type)
            if (apiResp.code == 200) {
                Result.success(apiResp.message ?: "告警已触发")
            } else {
                Result.failure(IOException(apiResp.message ?: "触发告警失败"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    /** 停止告警 */
    suspend fun stopAlarm(deviceId: String): Result<String> = withContext(Dispatchers.IO) {
        try {
            val body = mapOf("deviceId" to deviceId)
            val resp = post("/api/alarm/stop", body)
            val type = object : TypeToken<ApiResponse<Any>>() {}.type
            val apiResp: ApiResponse<Any> = gson.fromJson(resp, type)
            if (apiResp.code == 200) {
                Result.success(apiResp.message ?: "告警已停止")
            } else {
                Result.failure(IOException(apiResp.message ?: "停止告警失败"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    // ==================== 配置与心跳 ====================

    /** 发送云端配置（注册客户端并触发云端同步） */
    suspend fun sendConfig(ak: String, sk: String, projectId: String, productId: String, endpoint: String): Result<String> = withContext(Dispatchers.IO) {
        try {
            val body = mapOf(
                "ak" to ak, "sk" to sk,
                "projectId" to projectId, "productId" to productId,
                "endpoint" to endpoint, "clientId" to clientId
            )
            val resp = post("/api/config", body)
            val type = object : TypeToken<ApiResponse<Any>>() {}.type
            val apiResp: ApiResponse<Any> = gson.fromJson(resp, type)
            if (apiResp.code == 200) {
                Result.success(apiResp.message ?: "配置成功")
            } else {
                Result.failure(IOException(apiResp.message ?: "配置失败"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    /** 发送心跳 */
    suspend fun sendHeartbeat(): Result<Boolean> = withContext(Dispatchers.IO) {
        try {
            val body = mapOf("clientId" to clientId)
            post("/api/heartbeat", body)
            Result.success(true)
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    /** 发送离开通知 */
    suspend fun sendHeartbeatLeave(): Result<Boolean> = withContext(Dispatchers.IO) {
        try {
            val body = mapOf("clientId" to clientId)
            post("/api/heartbeat/leave", body)
            Result.success(true)
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    // ==================== HTTP 基础方法 ====================

    private fun get(path: String): String {
        val request = Request.Builder().url("$baseUrl$path").get().build()
        val response = client.newCall(request).execute()
        val body = response.body?.string() ?: "{}"
        if (!response.isSuccessful) throw IOException("HTTP ${response.code}: $body")
        return body
    }

    private fun post(path: String, bodyMap: Map<String, Any?>?): String {
        val bodyStr = if (bodyMap != null) gson.toJson(bodyMap) else "{}"
        val requestBody = bodyStr.toRequestBody(jsonMediaType)
        val request = Request.Builder().url("$baseUrl$path").post(requestBody).build()
        val response = client.newCall(request).execute()
        val body = response.body?.string() ?: "{}"
        if (!response.isSuccessful) throw IOException("HTTP ${response.code}: $body")
        return body
    }

    private fun put(path: String, bodyMap: Map<String, Any?>): String {
        val bodyStr = gson.toJson(bodyMap)
        val requestBody = bodyStr.toRequestBody(jsonMediaType)
        val request = Request.Builder().url("$baseUrl$path").put(requestBody).build()
        val response = client.newCall(request).execute()
        val body = response.body?.string() ?: "{}"
        if (!response.isSuccessful) throw IOException("HTTP ${response.code}: $body")
        return body
    }

    private fun delete(path: String): String {
        val request = Request.Builder().url("$baseUrl$path").delete().build()
        val response = client.newCall(request).execute()
        val body = response.body?.string() ?: "{}"
        if (!response.isSuccessful) throw IOException("HTTP ${response.code}: $body")
        return body
    }
}
