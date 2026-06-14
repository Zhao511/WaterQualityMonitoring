/**
 * ============================================================
 * LoRa UART 实现 — 正点原子 ATK-LORA-01 透传模式
 * ============================================================
 */

#include "lora_uart.h"
#include <ArduinoJson.h>

static lora_rx_callback_t rx_callback = nullptr;
static String rx_buffer = "";

/* ================================================================
 * 初始化
 * ================================================================ */
void lora_init()
{
    pinMode(LORA_MD0_PIN, OUTPUT);
    digitalWrite(LORA_MD0_PIN, LOW);   /* MD0=0: 通信/透传模式 */

    LORA_SERIAL.begin(LORA_BAUD_RATE, SERIAL_8N1,
                      LORA_RX_PIN, LORA_TX_PIN);

    DEBUG_SERIAL.println("[LoRa] UART initialized, 9600-8N1, transparent mode");
}

/* ================================================================
 * 注册回调
 * ================================================================ */
void lora_on_receive(lora_rx_callback_t cb)
{
    rx_callback = cb;
}

/* ================================================================
 * 查询接收
 * ================================================================ */
bool lora_available()
{
    return rx_buffer.length() > 0;
}

/* ================================================================
 * 发送 JSON → STM32
 * ================================================================ */
void lora_send(const String &json)
{
    LORA_SERIAL.print(json);
    LORA_SERIAL.print('\n');   /* 换行帧尾 — STM32 侧以此分隔帧 */
    DEBUG_SERIAL.print("[LoRa TX] ");
    DEBUG_SERIAL.println(json);
}

/* ================================================================
 * 轮询 — 从 UART 收字节，按 '\n' 换行分帧后回调
 * STM32 每发一条 JSON 后跟 '\n' 作为帧尾
 * ================================================================ */
void lora_loop()
{
    while (LORA_SERIAL.available() > 0)
    {
        char c = (char)LORA_SERIAL.read();

        /* 换行分隔: 收到 '\n' 表示一帧结束 */
        if (c == '\n')
        {
            if (rx_buffer.length() > 0)
            {
                /* 去除可能的 '\r' */
                rx_buffer.trim();

                DEBUG_SERIAL.print("[LoRa RX] ");
                DEBUG_SERIAL.println(rx_buffer);

                /* 解析 service_id 字段 (兼容旧 service), 触发回调 */
                StaticJsonDocument<512> doc;
                DeserializationError err = deserializeJson(doc, rx_buffer);
                if (!err)
                {
                    const char *svc = doc["service_id"] | doc["service"] | "";
                    if (rx_callback) rx_callback(String(svc), rx_buffer);
                }
                else
                {
                    /* 命令响应: {"rsp":"xxx",...} */
                    const char *rsp = doc["rsp"] | "";
                    if (rx_callback) rx_callback(String("response:") + rsp, rx_buffer);
                }
            }
            rx_buffer = "";
        }
        else
        {
            rx_buffer += c;
            /* 防止缓冲区溢出 */
            if (rx_buffer.length() > LORA_RX_BUF_SIZE)
            {
                DEBUG_SERIAL.println("[LoRa] RX overflow, flushed");
                rx_buffer = "";
            }
        }
    }
}
