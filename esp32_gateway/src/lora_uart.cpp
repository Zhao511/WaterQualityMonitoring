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
 * 辅助: 等待 AUX=HIGH
 * ================================================================ */
static bool lora_wait_aux(uint32_t timeout_ms = 2000)
{
    uint32_t t0 = millis();
    while (digitalRead(LORA_AUX_PIN) != HIGH)
    {
        if (millis() - t0 > timeout_ms) return false;
        delay(10);
    }
    return true;
}

/* ================================================================
 * 初始化 — ATK-LORA-01 透传模式
 * - AT 配置模式 (MD0=HIGH):  波特率固定 115200
 * - 通信透传模式 (MD0=LOW):  波特率 115200 (模块不支持 AT+BAUD, 与AT模式相同)
 * ================================================================ */
void lora_init()
{
    /* GPIO */
    pinMode(LORA_MD0_PIN, OUTPUT);
    digitalWrite(LORA_MD0_PIN, HIGH);   /* 进入 AT 配置模式 */
    pinMode(LORA_AUX_PIN, INPUT_PULLUP);

    /* UART 初始化为 AT 配置波特率 */
    lora_set_baud(LORA_AT_BAUD_RATE);
    delay(300);

    DEBUG_SERIAL.printf("[LoRa] Init: AT mode | baud=%u | MD0=HIGH\n",
                        (unsigned)LORA_AT_BAUD_RATE);

    /* 确认模块在线 */
    bool at_ok = lora_at("AT", 800);

    if (at_ok)
    {
        /* 查询地址 (ATK-LORA-01 支持的唯一查询命令) */
        lora_at("AT+ADDR?", 500);

        DEBUG_SERIAL.println("[LoRa] Module online (AT+BAUD/ADDR=/CH=/RATE= not supported)");
    }
    else
    {
        DEBUG_SERIAL.println("[LoRa] WARN: AT no response at 115200 baud");
        DEBUG_SERIAL.println("[LoRa] Check VCC/GND/TX/RX/MD0 wiring");
    }

    /* 切回透传模式 (MD0=LOW), 波特率 115200 (模块固定) */
    delay(50);
    digitalWrite(LORA_MD0_PIN, LOW);
    delay(300);
    lora_set_baud(LORA_BAUD_RATE);
    lora_wait_aux(3000);

    DEBUG_SERIAL.printf("[LoRa] Transparent mode | baud=%u | AUX=%s\n",
                        (unsigned)LORA_BAUD_RATE,
                        digitalRead(LORA_AUX_PIN) == HIGH ? "HIGH" : "LOW");

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
 * 发送 → STM32
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

                StaticJsonDocument<512> doc;
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
                DEBUG_SERIAL.println("[LoRa] RX overflow, flushed");
                rx_buffer = "";
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
