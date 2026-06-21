package com.waterquality.app.data.model

/**
 * Web 后端 API 通用响应模型
 */
data class ApiResponse<T>(
    val code: Int = 0,
    val message: String? = null,
    val data: T? = null,
    val total: Int? = null,
    val stats: HistoryStats? = null
)

data class HistoryStats(
    val max: String = "--",
    val min: String = "--",
    val avg: String = "--",
    val count: Int = 0
)

/** 历史数据条目 */
data class HistoryEntry(
    val time: String = "",
    val tds: Double = 0.0,
    val ph: Double = 0.0,
    val temp: Double = 0.0
)

/** 历史数据响应 */
data class HistoryResponse(
    val data: List<HistoryEntry> = emptyList(),
    val stats: HistoryStats = HistoryStats()
)

/** 设备列表响应 (Map形式) */
data class DevicesResponse(
    val data: Map<String, Device> = emptyMap()
)

/** 单设备响应 */
data class DeviceResponse(
    val data: Device? = null
)

/** 删除设备响应 */
data class DeleteResponse(
    val deletedId: String = ""
)
