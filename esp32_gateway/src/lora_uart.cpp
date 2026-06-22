/**
 * ============================================================
 * LoRa UART 实现 — 正点原子 ATK-LORA-01 透传模式
 * ============================================================
 */

#include "lora_uart.h"
#include <ArduinoJson.h>

static lora_rx_callback_t rx_callback = nullptr;
static String rx_buffer = "";
static uint32_t g_lora_heartbeat_last = 0;
static uint32_t g_lora_bytes_total = 0;
static uint32_t g_lora_frames_total = 0;

/* ================================================================
 * 辅助: 切换串口波特率
 * ================================================================ */
static void lora_set_baud(uint32_t baud)
{
    LORA_SERIAL.end();
    delay(50);
    LORA_SERIAL.begin(baud, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    delay(50);
}

/* ================================================================
 * 辅助: 发 AT 指令, 等待响应 (返回 true=收到 "OK")
 * ================================================================ */
static bool lora_at(const char *cmd, uint32_t timeout_ms = 600)
{
    while (LORA_SERIAL.available()) { LORA_SERIAL.read(); }

    LORA_SERIAL.print(cmd);
    LORA_SERIAL.print("\r\n");
    LORA_SERIAL.flush();

    String resp;
    uint32_t t0 = millis();
    while (millis() - t0 < timeout_ms)
    {
        while (LORA_SERIAL.available())
        {
            char c = (char)LORA_SERIAL.read();
            resp += c;
        }
        if (resp.indexOf("OK") >= 0) break;
        delay(5);
    }
    resp.trim();

    if (resp.length() > 0)
        DEBUG_SERIAL.printf("[LoRa AT] %-18s -> %s\n", cmd, resp.c_str());
    else
        DEBUG_SERIAL.printf("[LoRa AT] %-18s -> <timeout>\n", cmd);

    return resp.indexOf("OK") >= 0;
}

/* ================================================================
 * 辅助: 等待 AUX=LOW (空闲)
 * ATK-LORA-01: AUX LOW=空闲, HIGH=忙
 * ================================================================ */
static bool lora_wait_aux(uint32_t timeout_ms = 2000)
{
    uint32_t t0 = millis();
    while (digitalRead(LORA_AUX_PIN) != LOW)
    {
        if (millis() - t0 > timeout_ms) return false;
        delay(10);
    }
    return true;
}

/* ================================================================
 * 初始化 — 正点原子 ATK-LORA-01 透传模式
 * - 模块波特率出厂默认 115200, 全程不切换
 * - 两端一致 LORA_BAUD_RATE=115200 (STM32 侧 lora.h 同步)
 * ================================================================ */
void lora_init()
{
    /* GPIO */
    pinMode(LORA_MD0_PIN, OUTPUT);
    digitalWrite(LORA_MD0_PIN, HIGH);   /* 进入 AT 配置模式 */
    pinMode(LORA_AUX_PIN, INPUT_PULLDOWN);

    /* UART 115200 (出厂默认, 全程不切换) */
    lora_set_baud(LORA_BAUD_RATE);
    delay(300);

    DEBUG_SERIAL.printf("[LoRa] Init: AT mode | baud=%u | MD0=HIGH\n",
                        (unsigned)LORA_BAUD_RATE);

    /* 确认模块在线 + 配置 RF 参数 (V3.0 指令, 与 STM32 侧一致) */
    if (lora_at("AT", 800)) {
        lora_at("AT+ADDR=00,00");
        lora_at("AT+WLRATE=5,0");
        lora_at("AT+TPOWER=3");
        lora_at("AT+UART=7,0");
        lora_at("AT+FLASH");
        lora_at("AT+ADDR?", 500);
        DEBUG_SERIAL.println("[LoRa] Module online, RF params configured (V3.0)");
    } else {
        DEBUG_SERIAL.println("[LoRa] WARN: AT no response, check wiring");
    }

    /* 切回透传模式 (MD0=LOW), 不切换波特率 */
    delay(50);
    digitalWrite(LORA_MD0_PIN, LOW);
    delay(300);
    lora_wait_aux(3000);

    DEBUG_SERIAL.printf("[LoRa] Transparent mode | baud=%u | AUX=%s (LOW=ready, HIGH=busy)\n",
                        (unsigned)LORA_BAUD_RATE,
                        digitalRead(LORA_AUX_PIN) == LOW ? "LOW(ready)" : "HIGH(busy)");

    g_lora_heartbeat_last = millis();
    DEBUG_SERIAL.println("[LoRa] Listening for STM32 data...");
}

/* ================================================================
 * 注册回调
 * ================================================================ */
void lora_on_receive(lora_rx_callback_t cb)
{
    rx_callback = cb;
}

bool lora_available()
{
    return rx_buffer.length() > 0;
}

uint32_t lora_frames_count()
{
    return g_lora_frames_total;
}

/* ================================================================
 * 发送 → STM32 (旧版 JSON+换行, STM32 属性上报方向仍用此格式)
 * ================================================================ */
void lora_send(const String &json)
{
    lora_wait_aux(500);
    LORA_SERIAL.print(json);
    LORA_SERIAL.print('\n');
    DEBUG_SERIAL.print("[LoRa TX] ");
    DEBUG_SERIAL.println(json);
}

/* ================================================================
 * 帧协议发送 → STM32 (规范帧 + 停等ACK + 间隔 + 重发)
 * ================================================================ */
bool lora_send_framed(const String &payload, int max_retries)
{
    uint8_t len = (uint8_t)payload.length();
    if (len > LORA_FRAME_MAX_PAYLOAD) {
        DEBUG_SERIAL.printf("[LoRa] FRAME too large: %d > %d\n", len, LORA_FRAME_MAX_PAYLOAD);
        return false;
    }

    /* 构造帧: [HEADER] [LEN] [PAYLOAD...] [XOR_CHECKSUM] */
    uint8_t frame[260];  /* 1+1+250+1=253, 留余量 */
    frame[0] = LORA_FRAME_HEADER;
    frame[1] = len;
    memcpy(&frame[2], payload.c_str(), len);
    uint8_t csum = LORA_FRAME_HEADER ^ len;
    for (uint8_t i = 0; i < len; i++) {
        csum ^= (uint8_t)payload[i];
    }
    frame[2 + len] = csum;
    int frame_size = 3 + len;

    /* 禁止高速连发: 距上次发送至少 200ms */
    static uint32_t s_last_send_ms = 0;
    uint32_t now = millis();
    if (now - s_last_send_ms < LORA_FRAME_MIN_INTERVAL_MS) {
        delay(LORA_FRAME_MIN_INTERVAL_MS - (now - s_last_send_ms));
    }

    for (int retry = 0; retry <= max_retries; retry++)
    {
        /* 清空 RX 缓冲 (丢弃上次 NAK/残留的碎片字节) */
        while (LORA_SERIAL.available()) { LORA_SERIAL.read(); }

        lora_wait_aux(500);

        LORA_SERIAL.write(frame, frame_size);
        LORA_SERIAL.flush();
        s_last_send_ms = millis();

        DEBUG_SERIAL.printf("[LoRa TX FRAME] len=%d retry=%d csum=0x%02X\n",
                            len, retry, csum);

        /* 等 ACK: 匹配 3 字节模式 0xAA 0x00 0xAA */
        uint8_t ack_state = 0;  /* 0=等帧头 1=等长度 2=等校验 */
        uint32_t t0 = millis();
        while (millis() - t0 < LORA_FRAME_ACK_TIMEOUT_MS)
        {
            if (LORA_SERIAL.available())
            {
                uint8_t b = LORA_SERIAL.read();

                if (ack_state == 0) {
                    if (b == LORA_FRAME_HEADER) ack_state = 1;
                }
                else if (ack_state == 1) {
                    if (b == 0x00) ack_state = 2;           /* ACK 长度=0 */
                    else if (b == 0xFF) { /* NAK, 不等校验字节直接重发 */
                        DEBUG_SERIAL.println("[LoRa] NAK received");
                        ack_state = 0;
                        goto retry_next;
                    }
                    else ack_state = 0;                      /* 非 ACK/NAK, 复位 */
                }
                else /* ack_state == 2 */ {
                    if (b == (LORA_FRAME_HEADER ^ 0x00)) {  /* ACK 校验=0xAA */
                        DEBUG_SERIAL.println("[LoRa] ACK received");
                        return true;
                    }
                    ack_state = 0;
                }
            }
            delay(1);
        }
        DEBUG_SERIAL.printf("[LoRa] ACK timeout (retry=%d)\n", retry);
retry_next: ;
    }
    return false;
}

/* ================================================================
 * 轮询接收
 * ================================================================ */
void lora_loop()
{
    bool has_data = false;

    while (LORA_SERIAL.available() > 0)
    {
        has_data = true;
        char c = (char)LORA_SERIAL.read();
        g_lora_bytes_total++;

        if (c == '\n')
        {
            if (rx_buffer.length() > 0)
            {
                rx_buffer.trim();
                g_lora_frames_total++;

                DEBUG_SERIAL.print("[LoRa RX] ");
                DEBUG_SERIAL.println(rx_buffer);

                StaticJsonDocument<1024> doc;
                DeserializationError err = deserializeJson(doc, rx_buffer);
                if (!err)
                {
                    /* 先检查是否是 STM32 命令响应 ({"rsp":"cmd",...}),
                     * 再按 service_id 处理属性上报。
                     * 修复: 旧代码仅在 JSON 解析失败时才检查 rsp,
                     * 但响应是合法 JSON, 导致被静默丢弃。 */
                    const char *rsp = doc["rsp"] | "";
                    if (strlen(rsp) > 0) {
                        if (rx_callback) rx_callback(String("response:") + rsp, rx_buffer);
                    } else {
                        const char *svc = doc["service_id"] | doc["service"] | "";
                        if (rx_callback) rx_callback(String(svc), rx_buffer);
                    }
                }
                else
                {
                    DEBUG_SERIAL.print("[LoRa] JSON parse error: ");
                    DEBUG_SERIAL.println(rx_buffer);
                }
            }
            rx_buffer = "";
        }
        else
        {
            rx_buffer += c;
            if (rx_buffer.length() > LORA_RX_BUF_SIZE)
            {
                /* 缓冲区溢出: 找最后一个 '}' 作为 JSON 边界, 丢弃前面碎片 */
                int last_brace = rx_buffer.lastIndexOf('}');
                if (last_brace > 0 && last_brace < (int)rx_buffer.length() - 1)
                {
                    rx_buffer = rx_buffer.substring(last_brace + 1);
                }
                else
                {
                    DEBUG_SERIAL.println("[LoRa] RX overflow, flushed");
                    rx_buffer = "";
                }
            }
        }
    }

    /* 心跳 (30s) */
    uint32_t now = millis();
    if (now - g_lora_heartbeat_last >= LORA_HEARTBEAT_INTERVAL_MS)
    {
        int aux = digitalRead(LORA_AUX_PIN);
        DEBUG_SERIAL.printf("[LoRa] Heartbeat: %lus | bytes=%lu frames=%lu | AUX=%d | %s\n",
                            now / 1000, g_lora_bytes_total, g_lora_frames_total, aux,
                            g_lora_frames_total > 0 ? "receiving" : "no data");
        g_lora_heartbeat_last = now;
    }
}
