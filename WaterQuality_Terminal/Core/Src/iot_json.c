/**
 * ============================================================
 * 物联网 JSON 编解码实现 — 手写轻量解析/序列化
 * ============================================================
 */

#include "iot_json.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
 * 内部辅助宏
 * ================================================================ */
#define SAFE_SNPRINTF(pos, remain, fmt, ...)  do { \
    int _w = snprintf((pos), (remain), (fmt), ##__VA_ARGS__); \
    if (_w > 0 && _w < (remain)) { (pos) += _w; (remain) -= _w; } \
    else if (_w >= (remain)) { (remain) = 0; } \
} while(0)

/* ================================================================
 * 枚举 → 字符串映射
 * ================================================================ */

static const char *power_source_str(PowerSource p) {
    return (p == POWER_SOURCE_MAINS) ? "mains" : "battery";
}

static const char *work_state_str(WorkState w) {
    switch (w) {
        case WORK_MODE_LOWPOWER: return "lowpower";
        case WORK_MODE_DEBUG:    return "debug";
        default:                 return "normal";
    }
}

static const char *alarm_type_str(AlarmType t) {
    switch (t) {
        case ALARM_TYPE_BATTERY: return "battery";
        case ALARM_TYPE_SIGNAL:  return "signal";
        case ALARM_TYPE_GPS:     return "gps";
        default:                 return "water";
    }
}

static const char *alarm_level_str(AlarmLevel l) {
    switch (l) {
        case ALARM_LEVEL_SERIOUS:  return "serious";
        case ALARM_LEVEL_CRITICAL: return "critical";
        default:                   return "normal";
    }
}

static const char *alarm_status_str(AlarmStatus s) {
    switch (s) {
        case ALARM_STATUS_CLEARED: return "cleared";
        case ALARM_STATUS_MUTED:   return "muted";
        default:                   return "unhandled";
    }
}

/* ================================================================
 * 上行序列化 — DeviceStatus
 * ================================================================ */
int iot_json_serialize_device_status(const DeviceStatus *s,
                                      char *buf, int max_len)
{
    char *p = buf;
    int remain = max_len;

    SAFE_SNPRINTF(p, remain,
        "{\"service_id\":\"DeviceStatus\",\"properties\":{"
        "\"online\":%s,"
        "\"battery\":%.1f,"
        "\"signal\":%d,"
        "\"power\":\"%s\","
        "\"work_state\":\"%s\","
        "\"alarm_active\":%s,"
        "\"last_report\":\"%s\","
        "\"addr\":%d"
        "}}",
        s->online ? "true" : "false",
        s->battery,
        s->signal,
        power_source_str(s->power),
        work_state_str(s->work_state),
        s->alarm_active ? "true" : "false",
        s->last_report,
        g_terminal_addr);

    return (int)(p - buf);
}

/* ================================================================
 * 上行序列化 — Water_status
 * ================================================================ */
int iot_json_serialize_water_status(const WaterStatus *s,
                                     char *buf, int max_len)
{
    char *p = buf;
    int remain = max_len;

    SAFE_SNPRINTF(p, remain,
        "{\"service_id\":\"Water_status\",\"properties\":{"
        "\"tds\":%.1f,"
        "\"pH\":%.2f,"
        "\"temp\":%.1f,"
        "\"rfid\":\"%s\","
        "\"gps\":\"%s\","
        "\"addr\":%d"
        "}}",
        s->tds,
        s->ph,
        s->temp,
        s->rfid,
        s->gps,
        g_terminal_addr);

    return (int)(p - buf);
}

/* ================================================================
 * 上行序列化 — Alarm
 * ================================================================ */
int iot_json_serialize_alarm(const Alarm *a,
                              char *buf, int max_len)
{
    char *p = buf;
    int remain = max_len;

    /* 去掉 device_id (MQTT topic 已知), 缩减 ~24 字节以适配 LoRa SF7 222B 上限 */
    SAFE_SNPRINTF(p, remain,
        "{\"service_id\":\"Alarm\",\"properties\":{"
        "\"alarm_id\":\"%s\","
        "\"alarm_type\":\"%s\","
        "\"rfid\":\"%s\","
        "\"current_value\":%.2f,"
        "\"threshold\":%.2f,"
        "\"alarm_level\":\"%s\","
        "\"alarm_time\":\"%s\","
        "\"status\":\"%s\","
        "\"addr\":%d"
        "}}",
        a->alarm_id,
        alarm_type_str(a->alarm_type),
        a->rfid,
        a->current_value,
        a->threshold,
        alarm_level_str(a->alarm_level),
        a->alarm_time,
        alarm_status_str(a->status),
        g_terminal_addr);

    return (int)(p - buf);
}

/* ================================================================
 * 上行序列化 — GPS
 * ================================================================ */
int iot_json_serialize_gps(const GPS *g,
                            char *buf, int max_len)
{
    char *p = buf;
    int remain = max_len;

    SAFE_SNPRINTF(p, remain,
        "{\"service_id\":\"gps\",\"properties\":{"
        "\"longitude\":%.6f,"
        "\"latitude\":%.6f,"
        "\"gps_status\":%d,"
        "\"addr\":%d"
        "}}",
        g->longitude,
        g->latitude,
        (int)g->gps_status,
        g_terminal_addr);

    return (int)(p - buf);
}

/* ================================================================
 * 命令响应序列化
 * ================================================================ */
int iot_json_serialize_response(const char *cmd_name,
                                 bool result, const char *msg,
                                 char *buf, int max_len)
{
    char *p = buf;
    int remain = max_len;

    SAFE_SNPRINTF(p, remain,
        "{\"rsp\":\"%s\",\"result\":%s,\"msg\":\"%s\"}",
        cmd_name,
        result ? "true" : "false",
        msg ? msg : "");

    return (int)(p - buf);
}

int iot_json_serialize_response_ex(const char *cmd_name,
                                    bool result, const char *msg,
                                    const char *extra_fmt, ...)
{
    /* 简化实现: 忽略 variadic, 仅返回基本响应。
     * 如需带额外参数 (如 request_location 返回坐标),
     * 调用方自行拼装 JSON。
     */
    (void)extra_fmt;
    static char buf[IOT_RESPONSE_MAX];
    return iot_json_serialize_response(cmd_name, result, msg,
                                        buf, sizeof(buf));
}

/* ================================================================
 * 下行 JSON 解析 — 提取命令
 * 格式: {"cmd":"xxx","svc":"yyy","params":{...}}
 * ================================================================ */
int iot_json_parse_cmd(const char *json,
                       char *svc_out, int svc_len,
                       char *cmd_out, int cmd_len,
                       char *params_out, int params_len)
{
    const char *p;
    const char *start, *end;
    int depth;

    if (!json || !svc_out || !cmd_out || !params_out) return -1;
    *svc_out = '\0';
    *cmd_out = '\0';
    *params_out = '\0';

    /* 提取 "svc" 值 */
    p = strstr(json, "\"svc\"");
    if (!p) return -1;
    p = strchr(p, ':');
    if (!p) return -1;
    p = strchr(p, '"');
    if (!p) return -1;
    start = p + 1;
    end = strchr(start, '"');
    if (!end) return -1;
    if ((end - start) < svc_len) {
        memcpy(svc_out, start, end - start);
        svc_out[end - start] = '\0';
    }

    /* 提取 "cmd" 值 */
    p = strstr(json, "\"cmd\"");
    if (!p) return -1;
    p = strchr(p, ':');
    if (!p) return -1;
    p = strchr(p, '"');
    if (!p) return -1;
    start = p + 1;
    end = strchr(start, '"');
    if (!end) return -1;
    if ((end - start) < cmd_len) {
        memcpy(cmd_out, start, end - start);
        cmd_out[end - start] = '\0';
    }

    /* 提取 "params" 对象 { ... } */
    p = strstr(json, "\"params\"");
    if (!p) return 0;  /* 无参数, 合法 */
    p = strchr(p, ':');
    if (!p) return -1;
    p = strchr(p, '{');
    if (!p) return -1;
    start = p;
    depth = 0;
    end = start;
    while (*end) {
        if (*end == '{') depth++;
        else if (*end == '}') { depth--; if (depth == 0) break; }
        end++;
    }
    if (depth != 0) return -1;  /* JSON 括号不匹配 */
    if ((end - start + 1) < params_len) {
        memcpy(params_out, start, end - start + 1);
        params_out[end - start + 1] = '\0';
    }

    return 0;
}

/* ================================================================
 * 工具函数 — 从 JSON 片段中提取值
 * 策略: 简单的 "key":"value" 或 "key":value 扫描
 * ================================================================ */

int iot_json_get_str(const char *json, const char *key,
                     char *val_out, int max_len)
{
    char search[64];
    const char *start, *end;

    if (!json || !key || !val_out) return -1;
    snprintf(search, sizeof(search), "\"%s\"", key);
    start = strstr(json, search);
    if (!start) return -1;
    start = strchr(start + strlen(search), ':');
    if (!start) return -1;
    start = strchr(start, '"');
    if (!start) return -1;
    start++;
    end = strchr(start, '"');
    if (!end) return -1;
    if ((end - start) < max_len) {
        memcpy(val_out, start, end - start);
        val_out[end - start] = '\0';
        return 0;
    }
    return -1;
}

int iot_json_get_float(const char *json, const char *key, float *val_out)
{
    char search[64];
    const char *start;

    if (!json || !key || !val_out) return -1;
    snprintf(search, sizeof(search), "\"%s\"", key);
    start = strstr(json, search);
    if (!start) return -1;
    start = strchr(start + strlen(search), ':');
    if (!start) return -1;
    start++;  /* skip ':' */
    while (*start == ' ' || *start == '\t') start++;
    *val_out = (float)atof(start);
    return 0;
}

int iot_json_get_int(const char *json, const char *key, int *val_out)
{
    if (!json || !key || !val_out) return -1;
    float f;
    if (iot_json_get_float(json, key, &f) != 0) return -1;
    *val_out = (int)f;
    return 0;
}

int iot_json_get_bool(const char *json, const char *key, bool *val_out)
{
    char search[64];
    const char *start;

    if (!json || !key || !val_out) return -1;
    snprintf(search, sizeof(search), "\"%s\"", key);
    start = strstr(json, search);
    if (!start) return -1;
    start = strchr(start + strlen(search), ':');
    if (!start) return -1;
    start++;
    while (*start == ' ' || *start == '\t') start++;
    *val_out = (start[0] == 't' || start[0] == 'T' || start[0] == '1');
    return 0;
}
