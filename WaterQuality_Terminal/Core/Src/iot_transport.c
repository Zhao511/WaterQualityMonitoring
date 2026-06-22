/**
 * ============================================================
 * 物联网传输适配层 — LoRa ↔ 物模型 协议转换
 * ============================================================
 */

#include "iot_model.h"
#include "iot_json.h"
#include "iot_service.h"
#include "lora.h"
#include "led_rgb.h"

/* LoRa ISR 字节计数器 (stm32f10x_it.c 中定义, ISR 递增) */
extern volatile uint32_t g_lora_isr_byte_count;
#include "usart_debug.h"
#include "adc_common.h"     /* TURBIDITY_SENSOR_ENABLED */
#include <stdio.h>
#include <string.h>

/* ================================================================
 * 属性上报 — 通过 LoRa 发送序列化 JSON
 * ================================================================ */
void IOT_Report_Property(const char *json_payload)
{
    if (!json_payload) return;
    uint16_t len = (uint16_t)strlen(json_payload);
    if (len > 0 && len < (LORA_DATA_SIZE - 2)) {
        /* json + \n 合并为一次发送, 确保在同一 RF 帧内 */
        static char tx_buf[LORA_DATA_SIZE];
        uint16_t total = (uint16_t)snprintf(tx_buf, sizeof(tx_buf), "%s\n", json_payload);
        if (total < sizeof(tx_buf)) {
            LoRa_SendData((uint8_t *)tx_buf, total);
            LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);  /* 等待 RF 传输完成 */
            Debug_Printf("IOT TX: %s\r\n", json_payload);
        }
    } else if (len >= (LORA_DATA_SIZE - 2)) {
        /* JSON 超过 LoRa 单帧上限, 丢弃并告警 */
        Debug_Printf("[IOT] WARN: JSON too large (%d bytes, max %d), dropped!\r\n",
                     len, LORA_DATA_SIZE - 3);
    }
}

/* ================================================================
 * 帧协议常量 (ESP32 ↔ STM32 统一)
 * ================================================================ */
#define FRAME_HEADER        0xAA   /* 帧起始标识                     */
#define FRAME_MAX_PAYLOAD   200    /* 最大载荷 (命令≤150B, 留余量)   */
#define FRAME_ACK_PAYLOAD   0x00   /* ACK 帧载荷长度 (0=ACK)        */
#define FRAME_NAK_PAYLOAD   0xFF   /* NAK 帧载荷长度 (0xFF=NAK)     */

/* 接收状态机 */
typedef enum {
    FRAME_IDLE,                    /* 等待帧头 0xAA                 */
    FRAME_GOT_HEADER,              /* 已收帧头, 等待长度字节         */
    FRAME_READING,                 /* 正在收载荷                     */
    FRAME_GOT_CHECKSUM            /* 已收校验, 等待验证             */
} FrameState;

static FrameState frame_state = FRAME_IDLE;
static uint8_t    frame_payload[FRAME_MAX_PAYLOAD];
static uint8_t    frame_length = 0;
static uint8_t    frame_index  = 0;

/* ---- 发送 ACK/NAK 帧 (3 字节: 帧头+长度+校验) ---- */
static void Frame_SendAck(void)
{
    uint8_t ack[3];
    ack[0] = FRAME_HEADER;
    ack[1] = FRAME_ACK_PAYLOAD;                /* 长度=0 表示 ACK   */
    ack[2] = FRAME_HEADER ^ FRAME_ACK_PAYLOAD; /* XOR 校验          */
    LoRa_SendData(ack, 3);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);
}

static void Frame_SendNak(void)
{
    uint8_t nak[3];
    nak[0] = FRAME_HEADER;
    nak[1] = FRAME_NAK_PAYLOAD;                /* 长度=0xFF 表示 NAK */
    nak[2] = FRAME_HEADER ^ FRAME_NAK_PAYLOAD; /* XOR 校验           */
    LoRa_SendData(nak, 3);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);
}

/* ================================================================
 * 命令接收 — 帧状态机
 *
 * 流程: 逐字节读 → FRAME_IDLE(找0xAA) → GOT_HEADER(读长度)
 *       → READING(收满载荷) → GOT_CHECKSUM(验证) → ACK+解析
 *
 * 关键特性:
 *   - 收满指定字节才解析, 不提前截断
 *   - 校验失败回 NAK, 丢弃帧, 状态机复位
 *   - 校验通过回 ACK, 然后解析载荷 (JSON / A0 / A1 / ping)
 * ================================================================ */
void IOT_Process_Incoming(void)
{
    static uint8_t rx_buf[LORA_BUFFER_SIZE];
    uint16_t len;

    len = LoRa_ReceiveData(rx_buf, sizeof(rx_buf) - 1);
    {
        uint32_t isr_cnt = g_lora_isr_byte_count;
        Debug_Printf("[IoT] RX len=%u ISR=%lu state=%d\r\n",
                     len, (unsigned long)isr_cnt, (int)frame_state);
    }
    if (len == 0) return;

    /* 逐字节喂入状态机 */
    for (uint16_t i = 0; i < len; i++)
    {
        uint8_t byte = rx_buf[i];

        switch (frame_state)
        {
        case FRAME_IDLE:
            if (byte == FRAME_HEADER) {
                frame_state = FRAME_GOT_HEADER;
            }
            /* 非帧头字节: 丢弃 (可能是旧数据碎片) */
            break;

        case FRAME_GOT_HEADER:
            frame_length = byte;
            if (frame_length == FRAME_ACK_PAYLOAD) {
                /* 网关发来的 ACK 帧 (长度=0), 忽略 (STM32 不发 ACK) */
                /* 校验字节会在下一个状态被消费, 但这里直接跳过 */
                frame_state = FRAME_IDLE;  /* 等下一个校验字节不实际, 直接复位 */
                /* 注意: 下一字节是 ACK 的校验字节, 会被当作垃圾丢弃 */
            } else if (frame_length == FRAME_NAK_PAYLOAD) {
                /* 网关发来的 NAK 帧, 忽略 */
                frame_state = FRAME_IDLE;
            } else if (frame_length > 0 && frame_length <= FRAME_MAX_PAYLOAD) {
                frame_index = 0;
                frame_state = FRAME_READING;
            } else {
                /* 非法长度, 帧头可能是假阳性, 复位 */
                frame_state = FRAME_IDLE;
            }
            break;

        case FRAME_READING:
            frame_payload[frame_index++] = byte;
            if (frame_index >= frame_length) {
                frame_state = FRAME_GOT_CHECKSUM;
            }
            break;

        case FRAME_GOT_CHECKSUM:
            {
                /* 计算 XOR 校验: 帧头 ^ 长度 ^ 载荷逐字节 */
                uint8_t calc_csum = FRAME_HEADER ^ frame_length;
                for (uint8_t j = 0; j < frame_length; j++) {
                    calc_csum ^= frame_payload[j];
                }

                if (byte == calc_csum) {
                    /* 校验通过 → ACK → 处理载荷 */
                    Frame_SendAck();
                    frame_payload[frame_length] = '\0';
                    Debug_Printf("IOT RX[%u]: %s\r\n", frame_length, frame_payload);

                    /* 判断载荷类型 */
                    if (frame_length == 2 && frame_payload[0] == 'A') {
                        /* ---- 极简告警: A0 / A1 ---- */
                        if (frame_payload[1] == '0') {
                            g_device_status.alarm_active = true;
                            LED_RGB_SetColor(LED_COLOR_RED);
                            Debug_Printf("[IoT] A0: alarm_active=true LED=RED\r\n");
                        } else if (frame_payload[1] == '1') {
                            g_device_status.alarm_active = false;
                            LED_RGB_SetColor(LED_COLOR_GREEN);
                            Debug_Printf("[IoT] A1: alarm_active=false LED=GREEN\r\n");
                        }
                    }
                    else if (strstr((const char *)frame_payload, "\"ping\"") != NULL)
                    {
                        /* ---- ping 探测 ---- */
                        IOT_Report_Property(
                            "{\"rsp\":\"ping\",\"result\":true,\"msg\":\"pong\"}");
                    }
                    else
                    {
                        /* ---- JSON 命令 ---- */
                        char svc[IOT_CMD_NAME_MAX]  = {0};
                        char cmd[IOT_CMD_NAME_MAX]  = {0};
                        char params[IOT_PARAMS_MAX] = {0};
                        char rsp[IOT_RESPONSE_MAX]  = {0};

                        if (iot_json_parse_cmd((const char *)frame_payload,
                                               svc, sizeof(svc),
                                               cmd, sizeof(cmd),
                                               params, sizeof(params)) == 0)
                        {
                            bool ok = IOT_Cmd_Dispatch(svc, cmd, params, rsp, sizeof(rsp));
                            if (rsp[0] == '\0') {
                                iot_json_serialize_response(cmd, ok,
                                    ok ? "ok" : "fail", rsp, sizeof(rsp));
                            }
                            IOT_Report_Property(rsp);
                        }
                        else
                        {
                            IOT_Report_Property(
                                "{\"rsp\":\"_\",\"result\":false,\"msg\":\"parse error\"}");
                        }
                    }
                }
                else
                {
                    /* 校验失败 → NAK, 丢弃帧 */
                    Frame_SendNak();
                    Debug_Printf("[IoT] Frame csum mismatch (calc=0x%02X recv=0x%02X)\r\n",
                                 calc_csum, byte);
                }
                frame_state = FRAME_IDLE;
            }
            break;
        }
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

    float raw_ph, raw_temp, raw_tds;

    raw_ph   = IOT_Apply_Calibration("ph", PH_Sensor_Read());
    IOT_Validate_SensorData("ph", raw_ph, &ws->ph);

    raw_temp = IOT_Apply_Calibration("temp", Temp_Sensor_Read());
    IOT_Validate_SensorData("temp", raw_temp, &ws->temp);

    /* 温度合理性检查 — 使用物理范围常量 (不受 set_threshold 影响) */
    float temp_for_tds = ws->temp;
    if (temp_for_tds < IOT_TEMP_VALID_MIN || temp_for_tds > IOT_TEMP_VALID_MAX)
        temp_for_tds = IOT_TEMP_DEFAULT;

    raw_tds  = IOT_Apply_Calibration("tds", TDS_Sensor_Read(temp_for_tds));
    IOT_Validate_SensorData("tds", raw_tds, &ws->tds);

#if TURBIDITY_SENSOR_ENABLED
    {
        float raw_turb = IOT_Apply_Calibration("turbidity", Turbidity_Sensor_Read());
        IOT_Validate_SensorData("turbidity", raw_turb, &ws->turbidity);
    }
#else
    /* 注: 无浊度传感器, turbidity 固定为 0 */
    ws->turbidity = 0;
#endif
}
