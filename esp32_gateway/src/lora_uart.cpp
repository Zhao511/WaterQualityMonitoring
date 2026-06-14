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

/* ---- 波特率扫描列表 ---- */
static const uint32_t BAUD_SCAN[] = { LORA_BAUD_RATE, 9600, 19200, 38400, 4800, 14400, 57600, 2400 };
#define BAUD_SCAN_N (sizeof(BAUD_SCAN) / sizeof(BAUD_SCAN[0]))

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
 * 初始化
 * ================================================================ */
void lora_init()
{
    /* GPIO */
    pinMode(LORA_MD0_PIN, OUTPUT);
    digitalWrite(LORA_MD0_PIN, HIGH);   /* 配置模式 */
    pinMode(LORA_AUX_PIN, INPUT_PULLUP);

    /* UART 初始化 */
    lora_set_baud(LORA_BAUD_RATE);
    delay(200);

    DEBUG_SERIAL.printf("[LoRa] Init: baud=%u | MD0=HIGH(cfg) | AUX=GPIO%d(%s)\n",
                        (unsigned)LORA_BAUD_RATE, LORA_AUX_PIN,
                        digitalRead(LORA_AUX_PIN) == HIGH ? "HIGH" : "LOW");

    /* === 波特率检测 + 自适应 === */
    bool at_ok = lora_at("AT", 600);

    if (!at_ok)
    {
        /* 扫描其他波特率 */
        DEBUG_SERIAL.println("[LoRa] Scanning baud rates...");
        uint32_t found = 0;
        for (size_t i = 0; i < BAUD_SCAN_N; i++)
        {
            if (BAUD_SCAN[i] == LORA_BAUD_RATE) continue;  /* 已试过 */
            DEBUG_SERIAL.printf("[LoRa]   try %u...", (unsigned)BAUD_SCAN[i]);
            lora_set_baud(BAUD_SCAN[i]);
            if (lora_at("AT", 600))
            {
                DEBUG_SERIAL.println(" OK!");
                found = BAUD_SCAN[i];
                break;
            }
            DEBUG_SERIAL.println(" no");
        }

        if (found)
        {
            /* 找到非默认波特率, 强制改回 LORA_BAUD_RATE */
            DEBUG_SERIAL.printf("[LoRa] Found at %u, resetting to %u...\n",
                                (unsigned)found, (unsigned)LORA_BAUD_RATE);
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "AT+BAUD=%u", (unsigned)LORA_BAUD_RATE);
            lora_at(cmd, 800);
            lora_set_baud(LORA_BAUD_RATE);
            delay(200);
            at_ok = lora_at("AT", 600);
        }
        else
        {
            DEBUG_SERIAL.println("[LoRa] ERROR: No response at any baud rate!");
            DEBUG_SERIAL.println("[LoRa] Check VCC/GND/TX/RX/MD0 wiring.");
        }
    }

    if (at_ok)
    {
        /* === 诊断: 固件版本 + 帮助 === */
        lora_at("AT+VER?", 500);
        lora_at("AT+HELP", 500);

        /* === 查询当前 RF 参数 (不同固件命令名不同) === */
        lora_at("AT+ADDR?", 500);
        lora_at("AT+CH?", 500);
        lora_at("AT+CHANNEL?", 500);
        lora_at("AT+FREQ?", 500);
        lora_at("AT+BAND?", 500);
        lora_at("AT+RATE?", 500);
        lora_at("AT+RF?", 500);

        /* === 仅在后续配置全部失败时才复位 (避免反复重置已配置好的模块) === */
        bool need_reset = false;

        /* === 配置地址 === */
        if (at_ok)
        {
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "AT+ADDR=%d", LORA_DEFAULT_ADDRESS);
            if (!lora_at(cmd, 800))
            {
                snprintf(cmd, sizeof(cmd), "AT+ADDR=%02X", LORA_DEFAULT_ADDRESS);
                if (!lora_at(cmd, 800)) need_reset = true;
            }
        }

        /* === 配置信道 (只尝试有响应的命令) === */
        if (at_ok)
        {
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "AT+CH=%d", LORA_DEFAULT_CHANNEL);
            bool ch_ok = lora_at(cmd, 800);

            if (!ch_ok) {
                snprintf(cmd, sizeof(cmd), "AT+CHANNEL=%d", LORA_DEFAULT_CHANNEL);
                ch_ok = lora_at(cmd, 800);
            }
            if (!ch_ok) {
                snprintf(cmd, sizeof(cmd), "AT+RF=%d", LORA_DEFAULT_CHANNEL);
                ch_ok = lora_at(cmd, 800);
            }
            if (!ch_ok) {
                snprintf(cmd, sizeof(cmd), "AT+FREQ=%d", LORA_DEFAULT_CHANNEL);
                if (!lora_at(cmd, 800)) need_reset = true;
            }
        }

        /* === 仅在地址/信道配置全部失败时才出厂复位 === */
        if (need_reset && at_ok) {
            DEBUG_SERIAL.println("[LoRa] Address/Channel config failed, trying factory reset...");
            bool reset_ok = lora_at("AT+RESET", 1000);
            if (!reset_ok) reset_ok = lora_at("AT+RELOAD", 1000);
            if (!reset_ok) reset_ok = lora_at("AT+RENEW", 1000);
            if (!reset_ok) reset_ok = lora_at("AT+DEFAULT", 1000);
            if (!reset_ok) reset_ok = lora_at("AT+RESTORE", 1000);

            if (reset_ok) {
                DEBUG_SERIAL.println("[LoRa] Factory reset OK, re-detecting baud...");
                delay(1000);
                lora_set_baud(9600);
                delay(300);
                at_ok = lora_at("AT", 600);
                if (!at_ok) {
                    lora_set_baud(LORA_BAUD_RATE);
                    delay(300);
                    at_ok = lora_at("AT", 600);
                }
                /* 复位后重新配置地址和信道 */
                if (at_ok) {
                    char cmd[32];
                    snprintf(cmd, sizeof(cmd), "AT+ADDR=%d", LORA_DEFAULT_ADDRESS);
                    if (!lora_at(cmd, 800)) {
                        snprintf(cmd, sizeof(cmd), "AT+ADDR=%02X", LORA_DEFAULT_ADDRESS);
                        lora_at(cmd, 800);
                    }
                    snprintf(cmd, sizeof(cmd), "AT+CH=%d", LORA_DEFAULT_CHANNEL);
                    if (!lora_at(cmd, 800)) {
                        snprintf(cmd, sizeof(cmd), "AT+CHANNEL=%d", LORA_DEFAULT_CHANNEL);
                        if (!lora_at(cmd, 800)) {
                            snprintf(cmd, sizeof(cmd), "AT+RF=%d", LORA_DEFAULT_CHANNEL);
                            lora_at(cmd, 800);
                        }
                    }
                }
            }
        }

        /* === 显式配置空中速率, 确保两端一致 (默认 2.4kbps) === */
        if (at_ok) {
            /* ATK-LORA-01: AT+RATE=<0~5> (0=0.3k, 1=1.2k, 2=2.4k, 3=4.8k, 4=9.6k, 5=19.2k) */
            if (!lora_at("AT+RATE=2", 800)) {
                /* E220 兼容格式: AT+PARAM=<SF>,<BW>,<CR>,<preamble> */
                lora_at("AT+PARAM=9,7,1,8", 800);
            }
        }

        /* === 最终确认 === */
        lora_at("AT+ADDR?", 500);
        lora_at("AT+CH?", 500);
        lora_at("AT+RATE?", 500);
    }

    /* === 切回透传模式 === */
    delay(50);
    digitalWrite(LORA_MD0_PIN, LOW);
    delay(300);
    lora_wait_aux(3000);

    int aux = digitalRead(LORA_AUX_PIN);
    DEBUG_SERIAL.printf("[LoRa] Transparent mode | baud=%u | AUX=%s\n",
                        (unsigned)LORA_BAUD_RATE,
                        aux == HIGH ? "HIGH-ready" : "LOW?!");
    if (aux != HIGH)
    {
        DEBUG_SERIAL.println("[LoRa] NOTE: AUX stays LOW in transparent mode — ");
        DEBUG_SERIAL.println("[LoRa]   this is normal if the paired module is transmitting.");
        DEBUG_SERIAL.println("[LoRa]   Data can still be received on UART.");
    }

    g_lora_heartbeat_last = millis();
    DEBUG_SERIAL.printf("[LoRa] addr=%d channel=%d — listening...\n",
                        LORA_DEFAULT_ADDRESS, LORA_DEFAULT_CHANNEL);
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
