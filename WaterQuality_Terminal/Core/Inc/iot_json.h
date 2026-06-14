/**
 * ============================================================
 * 物联网 JSON 编解码 — 轻量手写实现 (无第三方依赖)
 * ============================================================
 * 上行: C 结构体 → JSON 字符串 (通过 LoRa 发给 ESP32 网关)
 * 下行: JSON 字符串 → 提取命令参数 (ESP32 网关转发云端指令)
 * ============================================================
 */

#ifndef __IOT_JSON_H
#define __IOT_JSON_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "iot_model.h"

/* ================================================================
 * 上行 — 属性上报序列化
 * 返回: 写入字节数 (不含 '\0'), <=0 表示失败
 * ================================================================ */
int iot_json_serialize_device_status(const DeviceStatus *s,
                                      char *buf, int max_len);
int iot_json_serialize_water_status(const WaterStatus *s,
                                     char *buf, int max_len);
int iot_json_serialize_alarm(const Alarm *a,
                              char *buf, int max_len);
int iot_json_serialize_gps(const GPS *g,
                            char *buf, int max_len);

/* ================================================================
 * 上行 — 命令响应序列化
 * ================================================================ */
int iot_json_serialize_response(const char *cmd_name,
                                 bool result, const char *msg,
                                 char *buf, int max_len);

/* 带额外参数的响应 (如 request_location 返回 lon/lat) */
int iot_json_serialize_response_ex(const char *cmd_name,
                                    bool result, const char *msg,
                                    const char *extra_fmt, ...
                                    /* variadic key:value pairs */);

/* ================================================================
 * 下行 — 命令解析
 * 输入 JSON: {"cmd":"set_threshold","svc":"Water_status","params":{...}}
 * 输出: svc_out=服务名, cmd_out=命令名, params_out=参数字符串
 * 返回: 0=成功, -1=格式错误
 * ================================================================ */
int iot_json_parse_cmd(const char *json,
                       char *svc_out, int svc_len,
                       char *cmd_out, int cmd_len,
                       char *params_out, int params_len);

/* ================================================================
 * 工具函数 — JSON 字符串中提取 key 的值
 * 用法: iot_json_get_str(json, "param", buf, sizeof(buf))
 *        iot_json_get_float(json, "min", &val)
 *        iot_json_get_int(json, "mode", &val)
 * ================================================================ */
int iot_json_get_str(const char *json, const char *key,
                     char *val_out, int max_len);
int iot_json_get_float(const char *json, const char *key, float *val_out);
int iot_json_get_int(const char *json, const char *key, int *val_out);
int iot_json_get_bool(const char *json, const char *key, bool *val_out);

#ifdef __cplusplus
}
#endif

#endif /* __IOT_JSON_H */
