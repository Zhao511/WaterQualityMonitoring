/**
 * ============================================================
 * 物联网业务服务层 — 感知层实现
 * ============================================================
 * 职责: 传感器采集校准 / 阈值管理 / 告警检测上报 / 命令执行
 * 不在感知层: 告警模式管理 (自动/手动/静音/清除) → 应用层负责
 * ============================================================
 */

#include "iot_service.h"
#include "sensor_ph.h"
#include "sensor_tds.h"
#include "sensor_temp.h"
#include "gps.h"
#include "lora.h"
#include "led_rgb.h"
#include "usart_debug.h"

/* NVIC_SystemReset 由 V3.5.0 CMSIS core_cm3.h 提供, 无需自定义 */

#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* IoT 任务句柄 (main.c 定义, 用于即时上报通知) */
extern TaskHandle_t xIOTTaskHandle;
#define IOT_IMMEDIATE_REPORT_BIT   0x01

/* ================================================================
 * 全局单例
 * ================================================================ */
/* 全局单例定义在 main.c, 此处仅声明 (见 iot_service.h extern) */

/* ================================================================
 * 内部状态
 * ================================================================ */
static SensorCalibration g_cal;
static uint32_t g_alarm_seq = 0;
static uint32_t g_sync_time_base = 0;
static uint32_t g_sync_tick_base = 0;

static struct {
    bool   enable;
    float  center_lon;
    float  center_lat;
    float  radius;
    char   action[16];
    char   fence_id[16];
} g_geo_fence;

/* 告警模式管理 */
typedef enum {
    ALARM_MODE_AUTO   = 0,  /* 自动: 阈值超限自动产生告警 */
    ALARM_MODE_MANUAL = 1   /* 手动: 暂停自动告警生成 */
} AlarmMode;

static AlarmMode   g_alarm_mode      = ALARM_MODE_AUTO;
static uint32_t    g_alarm_mute_until_tick = 0;  /* 静音截止 tick */

/* 最近一条告警记录 (用于 clear 操作) */
static Alarm       g_last_alarm;
static bool        g_last_alarm_valid = false;

/* ================================================================
 * 时间戳
 * ================================================================ */
static void make_timestamp(char *buf, int max_len)
{
    uint32_t sec;
    if (g_sync_time_base > 0) {
        sec = g_sync_time_base +
              (xTaskGetTickCount() - g_sync_tick_base) / configTICK_RATE_HZ;
    } else {
        sec = xTaskGetTickCount() / configTICK_RATE_HZ;
    }
    snprintf(buf, max_len, "%02d:%02d:%02d",
             (sec / 3600) % 24, (sec / 60) % 60, sec % 60);
}

/* ================================================================
 * 设备状态
 * ================================================================ */
void IOT_DeviceStatus_Init(DeviceStatus *s)
{
    memset(s, 0, sizeof(*s));
    s->online     = true;
    s->battery    = 100.0f;
    s->signal     = 0;
    s->power      = POWER_SOURCE_MAINS;
    s->work_state = WORK_MODE_NORMAL;
    make_timestamp(s->last_report, IOT_TIME_STR_LEN);
}

void IOT_DeviceStatus_Update(DeviceStatus *s)
{
    s->signal = IOT_Get_LoRa_RSSI();
    make_timestamp(s->last_report, IOT_TIME_STR_LEN);
}

int8_t IOT_Get_LoRa_RSSI(void)
{
    /* 静态缓存: 启动时查询一次, 之后直接返回缓存值
     * 避免频繁切换配置模式导致 RF 数据丢失 (ping 被 AT 响应缓冲区吞掉) */
    static int8_t  s_rssi_cached = -55;
    static bool    s_rssi_queried = false;

    if (!s_rssi_queried) {
        char resp[32];
        int rssi = -55;

        if (LoRa_SendATCmd("AT+RSSI?", resp, sizeof(resp)) == 0) {
            char *p = strstr(resp, "RSSI");
            if (p) {
                p = strchr(p, ':');
                if (p) rssi = (int)strtol(p + 1, NULL, 10);
            } else {
                rssi = (int)strtol(resp, NULL, 10);
            }
            if (rssi > 0) rssi = -rssi;
        }
        s_rssi_cached = (int8_t)rssi;
        s_rssi_queried = true;
    }

    return s_rssi_cached;
}

/* ================================================================
 * 告警检测 — 感知层只做阈值比对 + 生成告警事件
 * 告警模式 (自动/手动/静音/清除) 由应用层/云端管理
 * ================================================================ */
void IOT_Alarm_Init(void)
{
    g_alarm_seq = 0;
    g_alarm_mode = ALARM_MODE_AUTO;
    g_alarm_mute_until_tick = 0;
    g_last_alarm_valid = false;
}

int IOT_Alarm_Check(const WaterStatus *ws, const char *rfid,
                    Alarm *alarm_out)
{
    if (!ws || !alarm_out) return 0;

    /* 手动模式下暂停自动告警生成 */
    if (g_alarm_mode == ALARM_MODE_MANUAL)
        return 0;

    /* 检查全局静音状态 (按类型位掩码过滤) */
    if (g_alarm_mute_until_tick > 0) {
        if (xTaskGetTickCount() < g_alarm_mute_until_tick)
            return 0;  /* 仍在静音期内, 跳过所有告警 */
        else
            g_alarm_mute_until_tick = 0;  /* 静音期已过, 自动恢复 */
    }

    /* 辅助宏: 保存告警并返回 */
    #define SAVE_ALARM_AND_RETURN(ret_val) do { \
        g_last_alarm = *alarm_out; \
        g_last_alarm_valid = true; \
        return (ret_val); \
    } while(0)

    uint8_t fail_count = 0;
    float   worst_val  = 0;
    float   worst_thr  = 0;

    /* 水质五项检查 (始终开启，不可禁用) */
    if (ws->ph < ws->ph_min) {
        fail_count++; worst_val = ws->ph; worst_thr = ws->ph_min;
    } else if (ws->ph > ws->ph_max) {
        fail_count++; worst_val = ws->ph; worst_thr = ws->ph_max;
    }
    if (ws->tds > ws->tds_max) {
        fail_count++;
        if (ws->tds > worst_val || fail_count == 1) {
            worst_val = ws->tds; worst_thr = ws->tds_max;
        }
    }
    if (ws->temp < ws->temp_min) {
        fail_count++; worst_val = ws->temp; worst_thr = ws->temp_min;
    } else if (ws->temp > ws->temp_max) {
        fail_count++; worst_val = ws->temp; worst_thr = ws->temp_max;
    }
    if (fail_count > 0) {
        memset(alarm_out, 0, sizeof(*alarm_out));
        g_alarm_seq++;
        snprintf(alarm_out->alarm_id, IOT_ALARM_ID_LEN,
                 "ALM_%04lu", (unsigned long)g_alarm_seq);
        alarm_out->alarm_type    = ALARM_TYPE_WATER;
        memcpy(alarm_out->device_id, IOT_DEVICE_ID_DEFAULT, IOT_DEVICE_ID_MAX - 1);
        memcpy(alarm_out->rfid, rfid ? rfid : "", IOT_RFID_LEN - 1);
        alarm_out->current_value = worst_val;
        alarm_out->threshold     = worst_thr;
        alarm_out->alarm_level   = (fail_count >= 3) ? ALARM_LEVEL_CRITICAL
                                 : (fail_count >= 2) ? ALARM_LEVEL_SERIOUS
                                 : ALARM_LEVEL_NORMAL;
        alarm_out->status        = ALARM_STATUS_UNHANDLED;
        make_timestamp(alarm_out->alarm_time, IOT_TIME_STR_LEN);
        SAVE_ALARM_AND_RETURN(1);
    }

    /* 电池低电告警 */
    if (g_device_status.battery < IOT_BATTERY_LOW_THRESHOLD) {
        memset(alarm_out, 0, sizeof(*alarm_out));
        g_alarm_seq++;
        snprintf(alarm_out->alarm_id, IOT_ALARM_ID_LEN,
                 "ALM_%04lu", (unsigned long)g_alarm_seq);
        alarm_out->alarm_type    = ALARM_TYPE_BATTERY;
        memcpy(alarm_out->device_id, IOT_DEVICE_ID_DEFAULT, IOT_DEVICE_ID_MAX - 1);
        memcpy(alarm_out->rfid, rfid ? rfid : "", IOT_RFID_LEN - 1);
        alarm_out->current_value = g_device_status.battery;
        alarm_out->threshold     = IOT_BATTERY_LOW_THRESHOLD;
        alarm_out->alarm_level   = ALARM_LEVEL_SERIOUS;
        alarm_out->status        = ALARM_STATUS_UNHANDLED;
        make_timestamp(alarm_out->alarm_time, IOT_TIME_STR_LEN);
        SAVE_ALARM_AND_RETURN(2);
    }

    /* 信号弱告警 */
    if (g_device_status.signal < IOT_SIGNAL_WEAK_THRESHOLD &&
        g_device_status.signal != 0) {
        memset(alarm_out, 0, sizeof(*alarm_out));
        g_alarm_seq++;
        snprintf(alarm_out->alarm_id, IOT_ALARM_ID_LEN,
                 "ALM_%04lu", (unsigned long)g_alarm_seq);
        alarm_out->alarm_type    = ALARM_TYPE_SIGNAL;
        memcpy(alarm_out->device_id, IOT_DEVICE_ID_DEFAULT, IOT_DEVICE_ID_MAX - 1);
        memcpy(alarm_out->rfid, rfid ? rfid : "", IOT_RFID_LEN - 1);
        alarm_out->current_value = g_device_status.signal;
        alarm_out->threshold     = IOT_SIGNAL_WEAK_THRESHOLD;
        alarm_out->alarm_level   = ALARM_LEVEL_NORMAL;
        alarm_out->status        = ALARM_STATUS_UNHANDLED;
        make_timestamp(alarm_out->alarm_time, IOT_TIME_STR_LEN);
        SAVE_ALARM_AND_RETURN(3);
    }

    return 0;
    #undef SAVE_ALARM_AND_RETURN
}

/* ================================================================
 * 阈值管理
 * ================================================================ */
void IOT_Threshold_Init(WaterStatus *ws)
{
    if (!ws) return;
    ws->ph_min        = IOT_DEFAULT_PH_MIN;
    ws->ph_max        = IOT_DEFAULT_PH_MAX;
    ws->tds_max       = IOT_DEFAULT_TDS_MAX;
    ws->temp_min      = IOT_DEFAULT_TEMP_MIN;
    ws->temp_max      = IOT_DEFAULT_TEMP_MAX;
}

bool IOT_Threshold_Set(WaterStatus *ws, const char *param, float value)
{
    if (!ws || !param) return false;

    if      (strcmp(param, "ph_min") == 0)        ws->ph_min        = value;
    else if (strcmp(param, "ph_max") == 0)        ws->ph_max        = value;
    else if (strcmp(param, "tds_max") == 0)       ws->tds_max       = value;
    else if (strcmp(param, "temp_min") == 0)      ws->temp_min      = value;
    else if (strcmp(param, "temp_max") == 0)      ws->temp_max      = value;
    else return false;

    Debug_Printf("IOT: Threshold %s = %.2f\r\n", param, value);
    return true;
}

/* ================================================================
 * 传感器校准
 * ================================================================ */
void IOT_Calibration_Init(void)  { memset(&g_cal, 0, sizeof(g_cal)); }

void IOT_Calibration_Set(const char *sensor, const char *mode, float value)
{
    if (!sensor || !mode) return;
    if (strcmp(mode, "offset") == 0) {
        if      (strcmp(sensor, "ph")        == 0) g_cal.ph_offset        = value;
        else if (strcmp(sensor, "tds")       == 0) g_cal.tds_offset       = value;
        else if (strcmp(sensor, "temp")      == 0) g_cal.temp_offset      = value;
    }
    Debug_Printf("IOT: Calibrate %s %s = %.3f\r\n", sensor, mode, value);
}

void IOT_Calibration_Get(SensorCalibration *cal) { if (cal) *cal = g_cal; }

float IOT_Apply_Calibration(const char *sensor, float raw)
{
    if (!sensor) return raw;
    if      (strcmp(sensor, "ph")        == 0) return raw + g_cal.ph_offset;
    else if (strcmp(sensor, "tds")       == 0) return raw + g_cal.tds_offset;
    else if (strcmp(sensor, "temp")      == 0) return raw + g_cal.temp_offset;
    return raw;
}

/* ================================================================
 * 传感器数据验证 — 物理范围检查 + 异常时回退安全值
 * ================================================================ */
bool IOT_Validate_SensorData(const char *sensor, float raw, float *clamped)
{
    if (!sensor || !clamped) return false;

    bool valid = true;

    if (strcmp(sensor, "ph") == 0) {
        if (raw < IOT_PH_VALID_MIN || raw > IOT_PH_VALID_MAX) {
            Debug_Printf("[Sensor] WARN: pH=%.2f out of range [%.1f,%.1f], clamped to %.1f\r\n",
                         raw, IOT_PH_VALID_MIN, IOT_PH_VALID_MAX, IOT_PH_DEFAULT);
            *clamped = IOT_PH_DEFAULT;
            valid = false;
        } else {
            *clamped = raw;
        }
    }
    else if (strcmp(sensor, "tds") == 0) {
        if (raw < IOT_TDS_VALID_MIN || raw > IOT_TDS_VALID_MAX) {
            Debug_Printf("[Sensor] WARN: TDS=%.1f out of range [%.1f,%.1f], clamped to 0\r\n",
                         raw, IOT_TDS_VALID_MIN, IOT_TDS_VALID_MAX);
            *clamped = 0.0f;
            valid = false;
        } else {
            *clamped = raw;
        }
    }
    else if (strcmp(sensor, "temp") == 0) {
        /* 检测 sentinel 值（传感器故障标记）或超出物理范围 */
        if (raw <= IOT_TEMP_FAULT_OPEN || raw >= IOT_TEMP_FAULT_SENTINEL ||
            raw < IOT_TEMP_VALID_MIN || raw > IOT_TEMP_VALID_MAX) {
            Debug_Printf("[Sensor] WARN: Temp=%.1f out of range [%.1f,%.1f], clamped to %.1f\r\n",
                         raw, IOT_TEMP_VALID_MIN, IOT_TEMP_VALID_MAX, IOT_TEMP_DEFAULT);
            *clamped = IOT_TEMP_DEFAULT;
            valid = false;
        } else {
            *clamped = raw;
        }
    }
            Debug_Printf("[Sensor] WARN: Turb=%.1f out of range [%.1f,%.1f], clamped to 0\r\n",
    return valid;
}

/* ================================================================
 * GPS NMEA → Decimal
 * ================================================================ */
float IOT_GPS_NMEA2Decimal(const char *nmea, char hem)
{
    if (!nmea || strlen(nmea) < 4) return 0.0f;
    float raw = (float)atof(nmea);
    int deg = (hem == 'N' || hem == 'S')
              ? (int)(raw / 100.0f)
              : (int)(raw / 100.0f);
    raw = deg + (raw - deg * 100.0f) / 60.0f;
    if (hem == 'S' || hem == 'W') raw = -raw;
    return raw;
}

void IOT_GPS_GetDecimal(GPS *gps_out)
{
    if (!gps_out) return;
    GPS_Data raw_gps;
    GPS_GetData(&raw_gps);

    /* 使用 NMEA 数据中的实际半球指示符 */
    char lat_hem = (raw_gps.lat_hem == 'S' || raw_gps.lat_hem == 'N')
                   ? raw_gps.lat_hem : 'N';
    char lon_hem = (raw_gps.lon_hem == 'W' || raw_gps.lon_hem == 'E')
                   ? raw_gps.lon_hem : 'E';

    gps_out->latitude  = IOT_GPS_NMEA2Decimal(raw_gps.lat, lat_hem);
    gps_out->longitude = IOT_GPS_NMEA2Decimal(raw_gps.lon, lon_hem);
    gps_out->gps_status = (raw_gps.fix >= 2) ? GPS_FIX_3D
                        : (raw_gps.fix >= 1) ? GPS_FIX_2D
                        : GPS_NO_FIX;
}

/* ================================================================
 * 电子围栏检查 — 基于 Haversine 公式计算距离
 * ================================================================ */
#include <math.h>

#define EARTH_RADIUS_M  6371000.0f

static float to_rad(float deg) { return deg * 3.14159265359f / 180.0f; }

static float haversine_distance(float lat1, float lon1, float lat2, float lon2)
{
    float dlat = to_rad(lat2 - lat1);
    float dlon = to_rad(lon2 - lon1);
    float a = sinf(dlat / 2.0f) * sinf(dlat / 2.0f) +
              cosf(to_rad(lat1)) * cosf(to_rad(lat2)) *
              sinf(dlon / 2.0f) * sinf(dlon / 2.0f);
    float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
    return EARTH_RADIUS_M * c;
}

int IOT_GeoFence_Check(float lat, float lon)
{
    if (!g_geo_fence.enable) return 0;

    float dist = haversine_distance(lat, lon,
                                     g_geo_fence.center_lat,
                                     g_geo_fence.center_lon);
    if (dist > g_geo_fence.radius) {
        Debug_Printf("IOT: GeoFence breached! dist=%.0fm > radius=%.0fm\r\n",
                     dist, g_geo_fence.radius);
        return 1;  /* 越界 */
    }
    return 0;
}

/* ================================================================
 * 命令分发器 — 12 条感知层命令
 * 告警模式管理 (set_alarm_mode / mute_alarm / clear_alarm) 由应用层负责
 * ================================================================ */
bool IOT_Cmd_Dispatch(const char *svc, const char *cmd,
                      const char *params,
                      char *rsp_buf, int max_len)
{
    if (!svc || !cmd || !rsp_buf) return false;

    /* ---- DeviceStatus (4 条) ---- */
    if (strcmp(svc, "DeviceStatus") == 0) {

        if (strcmp(cmd, "set_work_mode") == 0) {
            int mode = 0;
            iot_json_get_int(params, "mode", &mode);
            if (mode >= 0 && mode <= 2) {
                g_device_status.work_state = (WorkState)mode;
                iot_json_serialize_response(cmd, true, "ok", rsp_buf, max_len);
            } else {
                iot_json_serialize_response(cmd, false, "invalid mode", rsp_buf, max_len);
            }
            return true;
        }

        if (strcmp(cmd, "remote_reboot") == 0) {
            int delay_sec = 0;
            iot_json_get_int(params, "delay", &delay_sec);
            Debug_Printf("IOT: Remote reboot requested, delay=%ds\r\n", delay_sec);
            iot_json_serialize_response(cmd, true, "rebooting", rsp_buf, max_len);
            /* 注: STM32 当前无软件延迟重启机制, 立即执行复位 */
            NVIC_SystemReset();
            return true;
        }

        if (strcmp(cmd, "ota_upgrade") == 0) {
            iot_json_serialize_response(cmd, false, "OTA not supported", rsp_buf, max_len);
            return true;
        }

        if (strcmp(cmd, "set_lora_param") == 0) {
            int freq = 0, sf = 0, power = 0;
            iot_json_get_int(params, "frequency", &freq);
            iot_json_get_int(params, "spreading_factor", &sf);
            iot_json_get_int(params, "power", &power);

            char at_cmd[32], at_resp[32];
            bool all_ok = true;

            /* 空中速率+信道: AT+WLRATE=<rate>,<ch> (V3.0 合并指令) */
            if ((freq > 0 || sf > 0) && all_ok) {
                int rate = (sf > 0) ? sf : LORA_DEFAULT_RATE;
                int ch   = (freq > 0) ? freq : LORA_DEFAULT_CHANNEL;
                snprintf(at_cmd, sizeof(at_cmd), "AT+WLRATE=%d,%d", rate, ch);
                if (LoRa_SendATCmd(at_cmd, at_resp, sizeof(at_resp)) != 0)
                    all_ok = false;
            }

            /* 发射功率: AT+TPOWER (V3.0: 3=20dBm) */
            if (power > 0 && all_ok) {
                snprintf(at_cmd, sizeof(at_cmd), "AT+TPOWER=%d", power);
                if (LoRa_SendATCmd(at_cmd, at_resp, sizeof(at_resp)) != 0)
                    all_ok = false;
            }

            Debug_Printf("IOT: LoRa params freq=%d sf=%d power=%d -- %s\r\n",
                         freq, sf, power, all_ok ? "OK" : "partial fail");
            iot_json_serialize_response(cmd, all_ok, all_ok ? "ok" : "at cmd failed", rsp_buf, max_len);
            return true;
        }
    }

    /* ---- Alarm (3 条) ---- */
    if (strcmp(svc, "Alarm") == 0) {
        if (strcmp(cmd, "set_alarm_mode") == 0) {
            char mode_str[16] = {0};
            int buffer_time = 0;
            iot_json_get_str(params, "mode", mode_str, sizeof(mode_str));
            iot_json_get_int(params, "buffer_time", &buffer_time);
            if (strcmp(mode_str, "manual") == 0) {
                g_alarm_mode = ALARM_MODE_MANUAL;
            } else {
                g_alarm_mode = ALARM_MODE_AUTO;
            }
            Debug_Printf("IOT: Alarm mode = %s, buffer=%ds\r\n", mode_str, buffer_time);
            iot_json_serialize_response(cmd, true, "ok", rsp_buf, max_len);
            return true;
        }
        if (strcmp(cmd, "clear_alarm") == 0) {
            char alarm_id[IOT_ALARM_ID_LEN] = {0};
            iot_json_get_str(params, "alarm_id", alarm_id, sizeof(alarm_id));
            /* 清除最近一条告警 (或指定 alarm_id 的告警) */
            if (g_last_alarm_valid) {
                if (alarm_id[0] == '\0' ||
                    strcmp(alarm_id, g_last_alarm.alarm_id) == 0) {
                    g_last_alarm.status = ALARM_STATUS_CLEARED;
                    g_last_alarm_valid = false;
                }
            }
            Debug_Printf("IOT: Clear alarm %s\r\n", alarm_id[0] ? alarm_id : "(all)");
            iot_json_serialize_response(cmd, true, "cleared", rsp_buf, max_len);
            return true;
        }
        if (strcmp(cmd, "mute_alarm") == 0) {
            int duration = 0;
            char types[64] = {0};
            iot_json_get_int(params, "duration", &duration);
            iot_json_get_str(params, "types", types, sizeof(types));
            if (duration > 0) {
                g_alarm_mute_until_tick = xTaskGetTickCount()
                    + (uint32_t)duration * configTICK_RATE_HZ;
            }
            Debug_Printf("IOT: Mute alarm duration=%ds types=%s\r\n", duration, types);
            iot_json_serialize_response(cmd, true, "muted", rsp_buf, max_len);
            return true;
        }
    }

    /* ---- Water_status (5 条) ---- */
    if (strcmp(svc, "Water_status") == 0) {

        if (strcmp(cmd, "set_report_interval") == 0) {
            int interval = 0;
            iot_json_get_int(params, "interval", &interval);
            if (interval >= 1 && interval <= 3600) {
                g_report_interval_sec = (uint32_t)interval;
                iot_json_serialize_response(cmd, true, "ok", rsp_buf, max_len);
            } else {
                iot_json_serialize_response(cmd, false, "invalid interval", rsp_buf, max_len);
            }
            return true;
        }

        if (strcmp(cmd, "set_threshold") == 0) {
            char param[32] = {0};
            float min = 0, max = 0;
            iot_json_get_str(params, "param", param, sizeof(param));
            iot_json_get_float(params, "min", &min);
            iot_json_get_float(params, "max", &max);

            if (strcmp(param, "ph") == 0 || strcmp(param, "pH") == 0) {
                g_water_status.ph_min = min; g_water_status.ph_max = max;
            } else if (strcmp(param, "tds") == 0) {
                g_water_status.tds_max = max;
            } else if (strcmp(param, "temp") == 0) {
                g_water_status.temp_min = min; g_water_status.temp_max = max;
                return true;
            }
            Debug_Printf("IOT: Threshold %s [%.2f, %.2f]\r\n", param, min, max);
            iot_json_serialize_response(cmd, true, "ok", rsp_buf, max_len);
            return true;
        }

        if (strcmp(cmd, "calibrate_sensor") == 0) {
            char sensor[16] = {0}, mode[16] = {0};
            float value = 0;
            iot_json_get_str(params, "sensor", sensor, sizeof(sensor));
            iot_json_get_str(params, "mode", mode, sizeof(mode));
            iot_json_get_float(params, "value", &value);
            IOT_Calibration_Set(sensor, mode, value);
            iot_json_serialize_response(cmd, true, "ok", rsp_buf, max_len);
            return true;
        }

        if (strcmp(cmd, "led_control") == 0) {
            char color_str[16] = {0}, mode_str[16] = {0};
            iot_json_get_str(params, "color", color_str, sizeof(color_str));
            iot_json_get_str(params, "mode", mode_str, sizeof(mode_str));
            LED_Color c = LED_COLOR_OFF;
            if      (strcmp(color_str, "red")    == 0) c = LED_COLOR_RED;
            else if (strcmp(color_str, "green")  == 0) c = LED_COLOR_GREEN;
            else if (strcmp(color_str, "blue")   == 0) c = LED_COLOR_BLUE;
            else if (strcmp(color_str, "yellow") == 0) c = LED_COLOR_YELLOW;
            else if (strcmp(color_str, "white")  == 0) c = LED_COLOR_WHITE;
            LED_RGB_SetColor(c);
            Debug_Printf("IOT: LED color=%s mode=%s\r\n", color_str, mode_str);
            iot_json_serialize_response(cmd, true, "ok", rsp_buf, max_len);
            return true;
        }

        if (strcmp(cmd, "request_immediate_report") == 0) {
            /* 通知 IoT 任务立即上报 */
            if (xIOTTaskHandle != NULL) {
                xTaskNotify(xIOTTaskHandle, IOT_IMMEDIATE_REPORT_BIT, eSetBits);
            }
            iot_json_serialize_response(cmd, true, "reporting", rsp_buf, max_len);
            return true;
        }
    }

    /* ---- gps (3 条, clear_alarm/set_alarm_mode/mute_alarm 在应用层) ---- */
    if (strcmp(svc, "gps") == 0) {

        if (strcmp(cmd, "set_gps_mode") == 0) {
            int mode = 0, interval = 0;
            iot_json_get_int(params, "mode", &mode);
            iot_json_get_int(params, "interval", &interval);
            Debug_Printf("IOT: GPS mode=%d interval=%d (stub)\r\n", mode, interval);
            iot_json_serialize_response(cmd, true, "ok", rsp_buf, max_len);
            return true;
        }

        if (strcmp(cmd, "request_location") == 0) {
            IOT_GPS_GetDecimal(&g_gps_data);
            snprintf(rsp_buf, max_len,
                "{\"rsp\":\"%s\",\"result\":true,\"msg\":\"ok\","
                "\"longitude\":%.6f,\"latitude\":%.6f,"
                "\"fix_time\":\"%s\"}",
                cmd, g_gps_data.longitude, g_gps_data.latitude,
                g_device_status.last_report);
            return true;
        }

        if (strcmp(cmd, "set_geo_fence") == 0) {
            int enable = 0;
            iot_json_get_int(params, "enable", &enable);
            g_geo_fence.enable = (enable != 0);
            iot_json_get_float(params, "center_lon", &g_geo_fence.center_lon);
            iot_json_get_float(params, "center_lat", &g_geo_fence.center_lat);
            iot_json_get_float(params, "radius", &g_geo_fence.radius);
            iot_json_get_str(params, "action", g_geo_fence.action, sizeof(g_geo_fence.action));
            snprintf(g_geo_fence.fence_id, sizeof(g_geo_fence.fence_id), "FENCE_001");
            snprintf(rsp_buf, max_len,
                "{\"rsp\":\"%s\",\"result\":true,\"msg\":\"ok\",\"fence_id\":\"%s\"}",
                cmd, g_geo_fence.fence_id);
            return true;
        }
    }

    /* ---- 跨服务命令 (不绑定特定 service_id) ---- */
    if (strcmp(cmd, "sync_time") == 0) {
        char source[16] = {0}, manual_time[32] = {0};
        iot_json_get_str(params, "source", source, sizeof(source));
        iot_json_get_str(params, "manual_time", manual_time, sizeof(manual_time));

        g_sync_tick_base = xTaskGetTickCount();

        /* 解析 manual_time: 支持 "HH:MM:SS" 格式和纯数字 Unix 时间戳 */
        if (manual_time[0] != '\0') {
            int hh = 0, mm = 0, ss = 0;
            if (sscanf(manual_time, "%d:%d:%d", &hh, &mm, &ss) == 3) {
                g_sync_time_base = (uint32_t)(hh * 3600 + mm * 60 + ss);
            } else {
                /* 尝试作为 Unix 时间戳解析, 转换为当日秒数 (UTC) */
                long ts = atol(manual_time);
                if (ts > 0) {
                    g_sync_time_base = (uint32_t)(ts % 86400);
                }
            }
        }

        Debug_Printf("IOT: Time sync source=%s manual=%s base=%lu\r\n",
                     source, manual_time, (unsigned long)g_sync_time_base);
        iot_json_serialize_response(cmd, true, "synced", rsp_buf, max_len);
        return true;
    }

    /* 未匹配 */
    snprintf(rsp_buf, max_len,
        "{\"rsp\":\"%s\",\"result\":false,\"msg\":\"unknown command\"}", cmd);
    return false;
}
