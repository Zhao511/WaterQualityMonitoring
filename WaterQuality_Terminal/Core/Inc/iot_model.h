/**
 * ============================================================
 * 华为云 IoT 物模型 — 4 服务数据结构定义
 * ============================================================
 * 架构: STM32F103 ←LoRa→ ESP32网关 ←MQTT→ 华为云IoT
 * 本文件定义设备侧完整物模型，字段名严格对齐云端物模型规格
 * ============================================================
 */

#ifndef __IOT_MODEL_H
#define __IOT_MODEL_H

#ifdef __cplusplus
 extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "stm32f10x.h"

/* ================================================================
 * 通用常量
 * ================================================================ */
#define IOT_DEVICE_ID_MAX    16
#define IOT_DEVICE_ID_DEFAULT "WQ_001"
#define IOT_RFID_LEN         11
#define IOT_GPS_STR_LEN      32
#define IOT_TIME_STR_LEN     20
#define IOT_ALARM_ID_LEN     20
#define IOT_JSON_BUF_SIZE    512

/* ================================================================
 * 服务 1: DeviceStatus — 设备状态
 * ================================================================ */

typedef enum {
    POWER_SOURCE_BATTERY = 0,
    POWER_SOURCE_MAINS   = 1
} PowerSource;

typedef enum {
    WORK_MODE_NORMAL   = 0,
    WORK_MODE_LOWPOWER = 1,
    WORK_MODE_DEBUG    = 2
} WorkState;

typedef struct {
    bool        online;                    /* 设备在线状态 (恒为 true)      */
    float       battery;                   /* 电池电量百分比 0.0~100.0     */
    int8_t      signal;                    /* LoRa RSSI 信号强度 dBm       */
    PowerSource power;                     /* 供电方式: 电池/市电          */
    WorkState   work_state;                /* 当前工作模式                 */
    char        last_report[IOT_TIME_STR_LEN]; /* 上一次数据上报时间戳    */
} DeviceStatus;

/* ================================================================
 * 服务 2: Alarm — 报警事件
 * ================================================================ */

typedef enum {
    ALARM_TYPE_WATER   = 0,   /* 水质超限 */
    ALARM_TYPE_BATTERY = 1,   /* 电池低电 */
    ALARM_TYPE_SIGNAL  = 2,   /* 信号弱   */
    ALARM_TYPE_GPS     = 3    /* 定位异常 */
} AlarmType;

typedef enum {
    ALARM_LEVEL_NORMAL   = 0, /* 普通 */
    ALARM_LEVEL_SERIOUS  = 1, /* 严重 */
    ALARM_LEVEL_CRITICAL = 2  /* 紧急 */
} AlarmLevel;

typedef enum {
    ALARM_STATUS_UNHANDLED = 0, /* 未处理 */
    ALARM_STATUS_CLEARED   = 1, /* 已清除 */
    ALARM_STATUS_MUTED     = 2  /* 静音   */
} AlarmStatus;

typedef struct {
    char        alarm_id[IOT_ALARM_ID_LEN]; /* 告警唯一编号               */
    AlarmType   alarm_type;                 /* 告警分类                   */
    char        device_id[IOT_DEVICE_ID_MAX]; /* 触发告警的设备编号       */
    char        rfid[IOT_RFID_LEN];         /* 监测点位唯一ID             */
    float       current_value;              /* 传感器当前采集数值         */
    float       threshold;                  /* 触发告警的阈值             */
    AlarmLevel  alarm_level;                /* 告警等级                   */
    char        alarm_time[IOT_TIME_STR_LEN]; /* 告警触发时间戳           */
    AlarmStatus status;                     /* 告警状态                   */
} Alarm;

/* ================================================================
 * 服务 3: Water_status — 水质情况 (可读可写)
 * ================================================================ */

typedef struct {
    float  tds;                /* TDS 电导率数值 (mg/L)              */
    float  ph;                 /* pH 酸碱度                          */
    float  temp;               /* 水体温度 (Celsius)                 */
    char   rfid[IOT_RFID_LEN]; /* 监测点位编号                       */
    char   gps[IOT_GPS_STR_LEN]; /* 点位地理位置文本 "lon,lat"      */

    /* --- 阈值 (云端可写, set_threshold 命令修改) --- */
    float  ph_min;
    float  ph_max;
    float  tds_max;
    float  temp_min;
    float  temp_max;
} WaterStatus;

/* ================================================================
 * 服务 4: gps — 位置信息
 * ================================================================ */

typedef enum {
    GPS_NO_FIX = 0,  /* 未定位   */
    GPS_FIX_2D = 1,  /* 2D 定位  */
    GPS_FIX_3D = 2   /* 3D 定位  */
} GPSStatus;

typedef struct {
    float     longitude;   /* 经度坐标 (decimal degrees) */
    float     latitude;    /* 纬度坐标 (decimal degrees) */
    GPSStatus gps_status;  /* GPS 定位状态               */
} GPS;

/* ================================================================
 * 命令相关结构体 (共用)
 * ================================================================ */

#define IOT_CMD_NAME_MAX    32
#define IOT_PARAMS_MAX      256
#define IOT_RESPONSE_MAX    256

/* 传感器校准值 */
typedef struct {
    float ph_offset;
    float tds_offset;
    float temp_offset;
} SensorCalibration;

/* ================================================================
 * 全局默认阈值 (set_threshold 可覆盖)
 * ================================================================ */
#define IOT_DEFAULT_PH_MIN           6.5f
#define IOT_DEFAULT_PH_MAX           8.5f
#define IOT_DEFAULT_TEMP_MIN         15.0f
#define IOT_DEFAULT_TEMP_MAX         35.0f
#define IOT_DEFAULT_TDS_MAX          1000.0f

/* ================================================================
 * 传感器物理范围常量 (用于异常值过滤，超过此范围视为传感器故障)
 * ================================================================ */
#define IOT_PH_VALID_MIN       0.0f
#define IOT_PH_VALID_MAX      14.0f
#define IOT_TDS_VALID_MIN      0.0f
#define IOT_TDS_VALID_MAX   5000.0f
#define IOT_TEMP_VALID_MIN     0.0f
#define IOT_TEMP_VALID_MAX    60.0f
#define IOT_TEMP_DEFAULT      25.0f       /* 传感器故障时的回退温度 (°C) */
#define IOT_TEMP_FAULT_SENTINEL 99.0f     /* 传感器故障标记值 (短路) */
#define IOT_TEMP_FAULT_OPEN   -99.0f      /* 传感器故障标记值 (开路) */
#define IOT_PH_DEFAULT         7.0f       /* pH 传感器故障回退值 (中性) */

/* 告警触发额外阈值 */
#define IOT_BATTERY_LOW_THRESHOLD    10.0f
#define IOT_SIGNAL_WEAK_THRESHOLD    -100
#define IOT_RSSI_QUERY_FAILED         -120  /* AT+RSSI? 查询失败时返回 */
#define IOT_GPS_STALE_SECONDS        600     /* 10 分钟无 GPS 定位触发告警 */

/* ================================================================
 * 上报间隔默认值 (秒)
 * ================================================================ */
#define IOT_DEFAULT_REPORT_INTERVAL   5      /* Water_status 上报间隔   */
#define IOT_DEVICE_STATUS_INTERVAL    60     /* DeviceStatus 上报间隔   */
#define IOT_GPS_REPORT_INTERVAL       30     /* GPS 上报间隔            */

#ifdef __cplusplus
}
#endif

#endif /* __IOT_MODEL_H */
