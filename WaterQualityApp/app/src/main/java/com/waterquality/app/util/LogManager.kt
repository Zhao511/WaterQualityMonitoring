package com.waterquality.app.util

/**
 * 工作日志系统（纯内存）
 * 与 Web 端 js/app.js 中 LOG 系统功能一致
 */
object LogManager {

    private val logs = mutableListOf<LogEntry>()
    private const val MAX_LOGS = 500

    @Synchronized
    fun add(type: String, deviceId: String, message: String) {
        logs.add(LogEntry(
            time = System.currentTimeMillis(),
            type = type,
            deviceId = deviceId,
            message = message
        ))
        if (logs.size > MAX_LOGS) {
            logs.removeAt(0)
        }
    }

    fun getAll(): List<LogEntry> = logs.toList().reversed()

    fun getFiltered(
        types: List<String>? = null,
        deviceId: String? = null,
        timeRangeHours: Int? = null
    ): List<LogEntry> {
        var filtered = logs.toList()
        if (!types.isNullOrEmpty()) {
            filtered = filtered.filter { it.type in types }
        }
        if (!deviceId.isNullOrBlank()) {
            filtered = filtered.filter { it.deviceId == deviceId }
        }
        if (timeRangeHours != null && timeRangeHours > 0) {
            val cutoff = System.currentTimeMillis() - timeRangeHours * 3600_000L
            filtered = filtered.filter { it.time >= cutoff }
        }
        return filtered.reversed()
    }

    fun exportCsv(): String {
        val sb = StringBuilder()
        sb.appendLine("时间,类型,设备,消息")
        for (entry in logs.reversed()) {
            val time = java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", java.util.Locale.US).format(java.util.Date(entry.time))
            val msg = entry.message.replace("\"", "\"\"")
            sb.appendLine("$time,${entry.type},${entry.deviceId},\"$msg\"")
        }
        return sb.toString()
    }

    @Synchronized
    fun clear() {
        logs.clear()
    }
}

data class LogEntry(
    val time: Long,
    val type: String,       // info | success | warning | error
    val deviceId: String,
    val message: String
)
