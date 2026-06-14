/**
 * ============================================================
 * 华为云 IoT 物模型 — 4 服务数据结构 (STM32 侧同步)
 * ============================================================
 */

#ifndef THING_MODEL_H
#define THING_MODEL_H

#include <ArduinoJson.h>

/* ================================================================
 * 服务 1: DeviceStatus — 设备状态
 * ================================================================ */

/* 供电方式 */
static const char* POWER_STR[] = {"battery", "mains"};
/* 工作模式 */
static const char* WORK_MODE_STR[] = {"normal", "lowpower", "debug"};

struct DeviceStatus {
    bool   online;
    float  battery;
    int    signal;
    int    power;      /* 0=battery, 1=mains */
    int    work_state; /* 0=normal, 1=lowpower, 2=debug */
    String last_report;

    /* 序列化为华为云属性上报 JSON */
    String toCloudJson() {
        StaticJsonDocument<256> data;
        data["online"]      = online;
        data["battery"]     = battery;
        data["signal"]      = signal;
        data["power"]       = POWER_STR[power];
        data["work_state"]  = WORK_MODE_STR[work_state];
        data["last_report"] = last_report;

        StaticJsonDocument<384> root;
        root["service_id"] = "DeviceStatus";
        root["properties"] = data;

        String out;
        serializeJson(root, out);
        return out;
    }
};

/* ================================================================
 * 服务 2: Alarm — 报警事件
 * ================================================================ */

static const char* ALARM_TYPE_STR[]  = {"water", "battery", "signal", "gps"};
static const char* ALARM_LEVEL_STR[] = {"normal", "serious", "critical"};
static const char* ALARM_STATUS_STR[] = {"unhandled", "cleared", "muted"};

struct Alarm {
    String alarm_id;
    int    alarm_type;    /* 0=water, 1=battery, 2=signal, 3=gps */
    String device_id;
    String rfid;
    float  current_value;
    float  threshold;
    int    alarm_level;   /* 0=normal, 1=serious, 2=critical */
    String alarm_time;
    int    status;        /* 0=unhandled, 1=cleared, 2=muted */

    String toCloudJson() {
        StaticJsonDocument<384> data;
        data["alarm_id"]      = alarm_id;
        data["alarm_type"]    = ALARM_TYPE_STR[alarm_type];
        data["device_id"]     = device_id;
        data["rfid"]          = rfid;
        data["current_value"] = current_value;
        data["threshold"]     = threshold;
        data["alarm_level"]   = ALARM_LEVEL_STR[alarm_level];
        data["alarm_time"]    = alarm_time;
        data["status"]        = ALARM_STATUS_STR[status];

        StaticJsonDocument<512> root;
        root["service_id"] = "Alarm";
        root["properties"] = data;

        String out;
        serializeJson(root, out);
        return out;
    }
};

/* ================================================================
 * 服务 3: Water_status — 水质情况
 * ================================================================ */

struct WaterStatus {
    float  tds;
    float  ph;
    float  temp;
    String rfid;
    String gps;

    String toCloudJson() {
        StaticJsonDocument<320> data;
        data["tds"]       = tds;
        data["pH"]        = ph;
        data["temp"]      = temp;
        data["rfid"]      = rfid;
        data["gps"]       = gps;

        StaticJsonDocument<448> root;
        root["service_id"] = "Water_status";
        root["properties"] = data;

        String out;
        serializeJson(root, out);
        return out;
    }
};

/* ================================================================
 * 服务 4: gps — 位置信息
 * ================================================================ */

struct GPS {
    float longitude;
    float latitude;
    int   gps_status;  /* 0=no_fix, 1=2D, 2=3D */

    String toCloudJson() {
        StaticJsonDocument<192> data;
        data["longitude"]  = longitude;
        data["latitude"]   = latitude;
        data["gps_status"] = gps_status;

        StaticJsonDocument<256> root;
        root["service_id"] = "gps";
        root["properties"] = data;

        String out;
        serializeJson(root, out);
        return out;
    }
};

#endif /* THING_MODEL_H */
