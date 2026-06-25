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
 * 编译期开关: 帧协议层诊断日志
 *   0 = 禁用 (生产模式, 减少中断屏蔽窗口)
 *   1 = 启用 (调试模式)
 * ================================================================ */
#define IOT_TRANSPORT_DEBUG  0

/* ================================================================
 * 帧接收超时 (ms) — 防止状态机在噪声干扰下永久卡在 FRAME_READING
 * ================================================================ */
#define FRAME_TIMEOUT_MS     500

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
#if IOT_TRANSPORT_DEBUG
            Debug_Printf("IOT TX: %s\r\n", json_payload);
#endif
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

/* 前置声明 */
void IOT_Report_DeviceStatus(const DeviceStatus *s);

/* 接收状态机 */
typedef enum {
    FRAME_IDLE,                    /* 等待帧头 0xAA                 */
    FRAME_GOT_HEADER,              /* 已收帧头, 等待长度字节         */
    FRAME_GOT_ADDR,                /* 已收长度, 等待/检查地址字节    */
    FRAME_READING,                 /* 正在收载荷                     */
    FRAME_GOT_CHECKSUM            /* 已收校验, 等待验证             */
} FrameState;

static FrameState frame_state   = FRAME_IDLE;
static uint8_t    frame_payload[FRAME_MAX_PAYLOAD];
static uint8_t    frame_length  = 0;   /* 载荷长度 (不含地址字节)    */
static uint8_t    frame_index   = 0;
static bool       frame_ignored = false; /* 地址不匹配, 静默丢弃    */
static uint8_t    frame_dst_addr = 0;   /* 实际收到的目标地址(用于校验)*/
static TickType_t frame_start_tick = 0; /* 帧接收开始时间 (超时复位) */

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
#if IOT_TRANSPORT_DEBUG
    {
        uint32_t isr_cnt = g_lora_isr_byte_count;
        Debug_Printf("[IoT] RX len=%u ISR=%lu state=%d\r\n",
                     len, (unsigned long)isr_cnt, (int)frame_state);
    }
#endif
    if (len == 0) {
        /* 帧接收超时检查: 非 IDLE 状态下超时 → 复位状态机
         * 防止噪声字节伪装帧头后状态机永久卡在 FRAME_READING */
        if (frame_state != FRAME_IDLE) {
            TickType_t elapsed = xTaskGetTickCount() - frame_start_tick;
            if (elapsed >= pdMS_TO_TICKS(FRAME_TIMEOUT_MS)) {
#if IOT_TRANSPORT_DEBUG
                Debug_Printf("[IoT] Frame timeout (%lums), resetting state machine\r\n",
                             (unsigned long)(elapsed * portTICK_PERIOD_MS));
#endif
                frame_state   = FRAME_IDLE;
                frame_index   = 0;
                frame_ignored = false;
            }
        }
        return;
    }

    /* 逐字节喂入状态机 */
    for (uint16_t i = 0; i < len; i++)
    {
        uint8_t byte = rx_buf[i];

        /* 帧超时保护: 非 IDLE 状态下超时 → 丢弃部分帧, 重新开始
         * 防止噪声伪装帧头后状态机缓慢消费后续字节卡在 FRAME_READING */
        if (frame_state != FRAME_IDLE) {
            TickType_t elapsed = xTaskGetTickCount() - frame_start_tick;
            if (elapsed >= pdMS_TO_TICKS(FRAME_TIMEOUT_MS)) {
#if IOT_TRANSPORT_DEBUG
                Debug_Printf("[IoT] Frame timeout in loop (%lums), reset\r\n",
                             (unsigned long)(elapsed * portTICK_PERIOD_MS));
#endif
                frame_state   = FRAME_IDLE;
                frame_index   = 0;
                frame_ignored = false;
                break;  /* 剩余字节丢弃, 下一周期重新解析 */
            }
        }

        switch (frame_state)
        {
        case FRAME_IDLE:
            if (byte == FRAME_HEADER) {
                frame_state = FRAME_GOT_HEADER;
                frame_start_tick = xTaskGetTickCount();  /* 记录帧开始时间 */
            }
            /* 非帧头字节: 丢弃 (可能是旧数据碎片) */
            break;

        case FRAME_GOT_HEADER:
            frame_length = byte;
            if (frame_length == FRAME_ACK_PAYLOAD) {
                /* 网关发来的 ACK 帧 (长度=0), 忽略 */
                frame_state = FRAME_IDLE;
            } else if (frame_length == FRAME_NAK_PAYLOAD) {
                /* 网关发来的 NAK 帧, 忽略 */
                frame_state = FRAME_IDLE;
            } else if (frame_length >= 1 && frame_length <= (FRAME_MAX_PAYLOAD + 1)) {
                /* LEN 包含地址字节, 有效帧: LEN=1(仅地址) 到 LEN=201(地址+200B载荷)
                 * 进入地址检查状态 */
                frame_state = FRAME_GOT_ADDR;
            } else {
                /* 非法长度, 帧头可能是假阳性, 复位 */
                frame_state = FRAME_IDLE;
            }
            break;

        case FRAME_GOT_ADDR:
            {
                frame_dst_addr = byte;  /* 保存实际地址, 用于校验计算 */
                /* LEN 包含 DST_ADDR 自身; 纯载荷 = LEN - 1 */
                uint8_t payload_len = frame_length - 1;

                /* 检查地址: 广播(0x00) 或 匹配本机地址 → 处理 */
                if (frame_dst_addr == 0x00 || frame_dst_addr == g_terminal_addr) {
                    frame_ignored = false;
                } else {
                    /* 不是发给本机的, 静默消费剩余字节但不处理 */
                    frame_ignored = true;
                }

                if (payload_len == 0) {
                    /* 仅有地址, 无载荷 (如未来的寻址 ping) */
                    frame_state = FRAME_GOT_CHECKSUM;
                } else {
                    frame_length = payload_len;  /* 后续使用纯载荷长度 */
                    frame_index  = 0;
                    frame_state  = FRAME_READING;
                }
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
                if (frame_ignored) {
                    /* 地址不匹配: 静默丢弃, 不发 ACK/NAK (避免碰撞) */
                    frame_state = FRAME_IDLE;
                    break;
                }

                /* 计算 XOR 校验: 帧头 ^ LEN(含地址) ^ DST_ADDR(实际收到) ^ 载荷逐字节
                 * 注: frame_length 在此处已变为纯载荷长度 (见 FRAME_GOT_ADDR),
                 *     frame_dst_addr 是实际收到的目标地址 (广播=0x00 或本机地址) */
                uint8_t orig_len = frame_length + 1;  /* 恢复原始 LEN */
                uint8_t calc_csum = FRAME_HEADER ^ orig_len ^ frame_dst_addr;
                for (uint8_t j = 0; j < frame_length; j++) {
                    calc_csum ^= frame_payload[j];
                }

#if IOT_TRANSPORT_DEBUG
                Debug_Printf("[IoT] FRAME csum: calc=0x%02X recv=0x%02X len=%d addr=%d ignored=%d\r\n",
                             calc_csum, byte, frame_length, frame_dst_addr, frame_ignored);
#endif

                if (byte == calc_csum) {
                    /* 校验通过 → ACK → 处理载荷 */
                    Frame_SendAck();
                    /* ACK 发送期间 ISR 可能已积累新字节, 转移到环形缓冲区防止溢出 */
                    LoRa_FlushToRingBuffer();

                    frame_payload[frame_length] = '\0';
#if IOT_TRANSPORT_DEBUG
                    Debug_Printf("IOT RX[%u]: %s\r\n", frame_length, frame_payload);
#endif

                    /* 判断载荷类型 */
                    if (frame_length == 2 && frame_payload[0] == 'A') {
                        /* ---- 极简告警: A0 / A1 ---- */
                        if (frame_payload[1] == '0') {
                            g_device_status.alarm_active = true;
                            LED_RGB_SetColor(LED_COLOR_RED);
                            g_pending_device_status_report = true;
                            Debug_Printf("[IoT] A0: alarm_active=true LED=RED\r\n");
                        } else if (frame_payload[1] == '1') {
                            g_device_status.alarm_active = false;
                            LED_RGB_SetColor(LED_COLOR_GREEN);
                            g_pending_device_status_report = true;
                            Debug_Printf("[IoT] A1: alarm_active=false LED=GREEN\r\n");
                        }
                    }
                    else if (strstr((const char *)frame_payload, "\"ping\"") != NULL)
                    {
                        /* ---- ping 探测 ---- */
                        IOT_Report_Property(
                            "{\"rsp\":\"ping\",\"result\":true,\"msg\":\"pong\"}");
                        LoRa_FlushToRingBuffer();  /* 响应发送期间可能有新数据到达 */
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
                            LoRa_FlushToRingBuffer();  /* 响应发送期间可能有新数据到达 */
                        }
                        else
                        {
                            IOT_Report_Property(
                                "{\"rsp\":\"_\",\"result\":false,\"msg\":\"parse error\"}");
                            LoRa_FlushToRingBuffer();  /* 响应发送期间可能有新数据到达 */
                        }
                    }
                }
                else
                {
                    /* 校验失败 → NAK, 丢弃帧 */
                    Frame_SendNak();
                    LoRa_FlushToRingBuffer();  /* NAK 发送期间可能有新数据到达 */
#if IOT_TRANSPORT_DEBUG
                    Debug_Printf("[IoT] Frame csum mismatch (calc=0x%02X recv=0x%02X)\r\n",
                                 calc_csum, byte);
#endif
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
