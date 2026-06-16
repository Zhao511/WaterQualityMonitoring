/**
 * ============================================================
 * 物联网业务服务层 — 设备状态 / 告警 / 命令分发
 * ============================================================
 */

#ifndef __IOT_SERVICE_H
#define __IOT_SERVICE_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "iot_model.h"
#include "iot_json.h"

/* ================================================================
 * 设备状态管理
 * ================================================================ */
void IOT_DeviceStatus_Init(DeviceStatus *status);
void IOT_DeviceStatus_Update(DeviceStatus *status);

/* LoRa RSSI 查询接口 (需要 LoRa 模块支持 AT 指令) */
int8_t IOT_Get_LoRa_RSSI(void);

/* ================================================================
 * 告警服务 (感知层只检测+上报，模式管理由应用层/云端负责)
 * ================================================================ */
void IOT_Alarm_Init(void);

/**
 * @brief  检查传感器数据是否触发告警
 * @param  ws        当前 WaterStatus (含阈值)
 * @param  rfid      当前点位 RFID
 * @param  alarm_out 输出: 生成的告警 (仅当有告警时才填充)
 * @return 0=无告警, 1=水质告警, 2=电池告警, 3=信号告警, 4=GPS告警
 */
int  IOT_Alarm_Check(const WaterStatus *ws, const char *rfid,
                     Alarm *alarm_out);

/* ================================================================
 * 阈值管理
 * ================================================================ */
void IOT_Threshold_Init(WaterStatus *ws);

/**
 * @brief  设置阈值
 * @param  ws    目标 WaterStatus
 * @param  param 参数名: "ph_min","ph_max","tds_max","turbidity_max","temp_min","temp_max"
 * @param  value 新阈值
 * @return true=成功, false=未知参数名
 */
bool IOT_Threshold_Set(WaterStatus *ws, const char *param, float value);

/* ================================================================
 * 传感器校准
 * ================================================================ */
void IOT_Calibration_Init(void);
void IOT_Calibration_Set(const char *sensor, const char *mode, float value);
void IOT_Calibration_Get(SensorCalibration *cal);

/**
 * @brief  应用校准值到传感器读数
 */
float IOT_Apply_Calibration(const char *sensor, float raw_value);

/* ================================================================
 * 传感器数据验证 — 物理范围检查 + 异常时回退安全值
 * @param  sensor  传感器名 ("ph"/"tds"/"temp"/"turbidity")
 * @param  raw     传感器原始读数
 * @param  clamped 输出: 钳位后的安全值 (raw 在范围内则不变)
 * @return true=在物理范围内, false=异常已被钳位
 * ================================================================ */
bool  IOT_Validate_SensorData(const char *sensor, float raw, float *clamped);

/* ================================================================
 * GPS NMEA → Decimal 转换
 * @brief  NMEA 格式 ddmm.mmmm → 十进制度数
 * @param  nmea   NMEA 经纬度字符串 (如 "4807.038,N")
 * @param  hem    半球 ('N','S','E','W')
 * @return 十进制度数, 错误返回 0
 */
float IOT_GPS_NMEA2Decimal(const char *nmea, char hem);
void  IOT_GPS_GetDecimal(GPS *gps_out);

/* ================================================================
 * 电子围栏检查
 * @param  lat  当前纬度 (decimal degrees)
 * @param  lon  当前经度 (decimal degrees)
 * @return 0=围栏内, 1=越界
 */
int   IOT_GeoFence_Check(float lat, float lon);

/* ================================================================
 * 命令分发器 (12 条感知层命令，告警模式管理由应用层负责)
 * @param  svc        服务名 ("DeviceStatus"/"Alarm"/"Water_status"/"gps")
 * @param  cmd        命令名
 * @param  params     JSON 参数字符串
 * @param  rsp_buf    响应输出缓冲区
 * @param  max_len    缓冲区大小
 * @return true=成功, false=命令执行失败
 * ================================================================ */
bool IOT_Cmd_Dispatch(const char *svc, const char *cmd,
                      const char *params,
                      char *rsp_buf, int max_len);

/* ================================================================
 * 全局单例 — 物模型实例 (main.c 中定义, 此处 extern)
 * ================================================================ */
extern WaterStatus    g_water_status;
extern DeviceStatus   g_device_status;
extern GPS            g_gps_data;
extern uint32_t       g_report_interval_sec;

#ifdef __cplusplus
}
#endif

#endif /* __IOT_SERVICE_H */
