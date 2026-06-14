/**
 * ============================================================
 * 物联网传输适配层 — LoRa ↔ 物模型 协议转换
 * ============================================================
 */

#include "iot_model.h"
#include "iot_json.h"
#include "iot_service.h"
#include "lora.h"
#include "usart_debug.h"
#include <string.h>

/* ================================================================
 * 属性上报 — 通过 LoRa 发送序列化 JSON
 * ================================================================ */
void IOT_Report_Property(const char *json_payload)
{
    if (!json_payload) return;
    uint16_t len = (uint16_t)strlen(json_payload);
    if (len > 0 && len < (LORA_DATA_SIZE - 1)) {
        LoRa_SendData((uint8_t *)json_payload, len);
        LoRa_SendData((uint8_t *)"\n", 1);  /* 换行分隔帧 */
        Debug_Printf("IOT TX: %s\r\n", json_payload);
    } else if (len >= (LORA_DATA_SIZE - 1)) {
        /* JSON 超过 LoRa 单帧上限, 丢弃并告警 */
        Debug_Printf("[IOT] WARN: JSON too large (%d bytes, max %d), dropped!\r\n",
                     len, LORA_DATA_SIZE - 2);
    }
}

/* ================================================================
 * 命令接收 — 从 LoRa RX 取数据 → 解析 → 分发 → 回复
 * ================================================================ */
void IOT_Process_Incoming(void)
{
    static uint8_t rx_buf[LORA_BUFFER_SIZE];
    uint16_t len;

    len = LoRa_ReceiveData(rx_buf, sizeof(rx_buf) - 1);
    if (len == 0) return;

    rx_buf[len] = '\0';
    Debug_Printf("IOT RX: %s\r\n", (char *)rx_buf);

    /* 检查是否是 LoRa 链路 ping 探测, 直接回复 pong */
    if (strstr((const char *)rx_buf, "\"ping\"") != NULL)
    {
        IOT_Report_Property(
            "{\"rsp\":\"ping\",\"result\":true,\"msg\":\"pong\"}");
        return;
    }

    /* 解析命令 */
    char svc[IOT_CMD_NAME_MAX]    = {0};
    char cmd[IOT_CMD_NAME_MAX]    = {0};
    char params[IOT_PARAMS_MAX]   = {0};
    char rsp[IOT_RESPONSE_MAX]    = {0};

    if (iot_json_parse_cmd((const char *)rx_buf,
                           svc, sizeof(svc),
                           cmd, sizeof(cmd),
                           params, sizeof(params)) == 0)
    {
        bool ok = IOT_Cmd_Dispatch(svc, cmd, params, rsp, sizeof(rsp));

        /* 构造并发送响应 */
        if (rsp[0] == '\0') {
            /* dispatch 未填充 rsp, 手动构造 */
            iot_json_serialize_response(cmd, ok, ok ? "ok" : "fail",
                                         rsp, sizeof(rsp));
        }
        IOT_Report_Property(rsp);
    }
    else
    {
        IOT_Report_Property(
            "{\"rsp\":\"_\",\"result\":false,\"msg\":\"parse error\"}");
    }
}

/* ================================================================
 * 批量上报 — 四服务按需上报
 * ================================================================ */

void IOT_Report_DeviceStatus(const DeviceStatus *s)
{
    char buf[IOT_JSON_BUF_SIZE];
    if (iot_json_serialize_device_status(s, buf, sizeof(buf)) > 0)
        IOT_Report_Property(buf);
}

void IOT_Report_WaterStatus(const WaterStatus *s)
{
    char buf[IOT_JSON_BUF_SIZE];
    if (iot_json_serialize_water_status(s, buf, sizeof(buf)) > 0)
        IOT_Report_Property(buf);
}

void IOT_Report_Alarm(const Alarm *a)
{
    char buf[IOT_JSON_BUF_SIZE];
    if (iot_json_serialize_alarm(a, buf, sizeof(buf)) > 0)
        IOT_Report_Property(buf);
}

void IOT_Report_GPS(const GPS *g)
{
    char buf[IOT_JSON_BUF_SIZE];
    if (iot_json_serialize_gps(g, buf, sizeof(buf)) > 0)
        IOT_Report_Property(buf);
}

/* ================================================================
 * 全量水质采集 (request_immediate_report 命令触发)
 * ================================================================ */
#include "sensor_ph.h"
#include "sensor_tds.h"
#include "sensor_turbidity.h"
#include "sensor_temp.h"

void IOT_Collect_All_Sensors(WaterStatus *ws)
{
    if (!ws) return;

    ws->ph   = IOT_Apply_Calibration("ph", PH_Sensor_Read());
    ws->temp = IOT_Apply_Calibration("temp", Temp_Sensor_Read());
    float temp_for_tds = ws->temp;
    if (temp_for_tds < ws->temp_min || temp_for_tds > ws->temp_max)
        temp_for_tds = 25.0f;
    ws->tds  = IOT_Apply_Calibration("tds", TDS_Sensor_Read(temp_for_tds));

    /* 注: 无浊度传感器, turbidity 固定为 0 */
}
