/**
 * ============================================================
 * ESP32 LoRa 网关 — 主程序
 * ============================================================
 * 架构: STM32 ←LoRa→ ESP32 ←WiFi/MQTT→ 华为云IoT
 *
 * 数据流:
 *   LoRa RX (STM32→) → 解析 service_id → MQTT 属性上报
 *   MQTT CMD (云端→)  → 转发 STM32     → LoRa TX
 *   LoRa RX (STM32←RSP)→ 解析 rsp       → MQTT 命令响应
 * ============================================================
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include "config.h"
#include "thing_model.h"
#include "lora_uart.h"
#include "mqtt_huawei.h"

/* ================================================================
 * 终端路由表 — RFID → LoRa 地址 映射
 * 自动学习: 收到 Water_status 上报时, 从 rfid+addr 字段建立映射
 * 老化机制: 5 分钟未收到终端数据则移除路由
 * ================================================================ */
struct TerminalRoute {
    uint8_t addr;
    uint32_t last_seen_ms;
};
static std::map<String, TerminalRoute> g_terminal_routes;

/* ================================================================
 * LED 状态指示
 * ================================================================ */
#define LED_PIN        2        /* ESP32 板载 LED (GPIO2) */
#define LED_WIFI_OK    1000     /* WiFi 已连: 1s 闪一次   */
#define LED_MQTT_OK    200      /* MQTT 已连: 快速闪      */
#define LED_ERROR      100      /* 错误: 急促闪            */

static uint32_t g_led_last = 0;
static bool     g_led_state = false;

static void led_update()
{
    uint32_t now = millis();
    int interval;

    if (mqtt_connected())      interval = LED_MQTT_OK;
    else if (WiFi.isConnected()) interval = LED_WIFI_OK;
    else                        interval = LED_ERROR;

    if (now - g_led_last > (uint32_t)interval) {
        g_led_last = now;
        g_led_state = !g_led_state;
        digitalWrite(LED_PIN, g_led_state ? HIGH : LOW);
    }
}

/* ================================================================
 * LoRa → MQTT 转发 (STM32 上报 → 华为云)
 * ================================================================ */
static void on_lora_receive(const String &svc, const String &json)
{
    DEBUG_SERIAL.print("[FWD] LoRa→Cloud  service=");
    DEBUG_SERIAL.print(svc);
    DEBUG_SERIAL.print("  len=");
    DEBUG_SERIAL.println(json.length());

    if (svc == "DeviceStatus" || svc == "Water_status" ||
        svc == "Alarm" || svc == "gps")
    {
        /* ---- 自动学习终端路由: 从属性上报提取 rfid+addr, 建立映射 ---- */
        StaticJsonDocument<1024> prop_doc;
        DeserializationError prop_err = deserializeJson(prop_doc, json);
        if (!prop_err) {
            JsonObject props = prop_doc["properties"];
            int addr = props["addr"] | -1;
            const char *rfid = props["rfid"] | "";
            if (addr >= 0 && strlen(rfid) > 0) {
                TerminalRoute &r = g_terminal_routes[String(rfid)];
                r.addr = (uint8_t)addr;
                r.last_seen_ms = millis();
                DEBUG_SERIAL.printf("[ROUTE] learned RFID=%s -> addr=%d\n", rfid, addr);
            }
        }

        /* 属性上报 → 直接转发到华为云 */
        mqtt_report_property(json);
    }
    else if (svc.startsWith("response:"))
    {
        /* STM32 响应格式 {"rsp":"cmd","result":true,"msg":"ok","longitude":...,...}
         * → 遍历所有字段转发到华为云 IoTDA 命令响应格式 */
        String cmd_name = svc.substring(9);  /* 去掉 "response:" 前缀 */

        /* 内部链路探测 ping/pong 和 A0/A1 告警确认不上报云平台 */
        if (cmd_name == "ping") {
            DEBUG_SERIAL.println("[FWD] LoRa ping pong (internal), skip cloud");
            return;
        }
        if (cmd_name == "alarm") {
            DEBUG_SERIAL.println("[FWD] LoRa alarm ack (internal), skip cloud");
            return;
        }

        StaticJsonDocument<256> stm32_rsp;
        DeserializationError err = deserializeJson(stm32_rsp, json);
        if (!err) {
            bool result = stm32_rsp["result"] | false;

            StaticJsonDocument<512> iotda_rsp;
            iotda_rsp["result_code"] = result ? 0 : 1;
            iotda_rsp["response_name"] = cmd_name;

            /* 遍历 STM32 响应的所有字段, 转发到 paras
             * (含 msg, longitude, latitude, fix_time, fence_id 等) */
            JsonObject paras = iotda_rsp.createNestedObject("paras");
            for (JsonPair kv : stm32_rsp.as<JsonObject>()) {
                const char *key = kv.key().c_str();
                /* 跳过 rsp (命令名已用作 response_name) */
                if (strcmp(key, "rsp") == 0) continue;
                paras[key] = kv.value();
            }

            String iotda_json;
            serializeJson(iotda_rsp, iotda_json);
            mqtt_response_cmd(iotda_json);
        }
    }
    else
    {
        DEBUG_SERIAL.print("[FWD] Unknown service: ");
        DEBUG_SERIAL.println(svc);
    }
}

/* ================================================================
 * MQTT → LoRa 转发 (华为云命令 → STM32)
 *
 * 华为云下发命令格式:
 * {
 *   "service_id": "Water_status",
 *   "command_name": "set_threshold",
 *   "paras": {"param":"pH","min":6.0,"max":9.0}
 * }
 *
 * 转发给 STM32 格式 (精简):
 * {"cmd":"set_threshold","svc":"Water_status","params":{"param":"pH","min":6.0,"max":9.0}}
 * ================================================================ */
static void on_cloud_command(const String &cmd_json)
{
    DEBUG_SERIAL.print("[FWD] Cloud→LoRa: ");
    DEBUG_SERIAL.println(cmd_json);

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, cmd_json);
    if (err) {
        DEBUG_SERIAL.println("[FWD] JSON parse error");
        return;
    }

    const char *svc = doc["service_id"] | "";
    const char *cmd = doc["command_name"] | "";

    /* ---- 查找目标终端地址: 从命令 paras.rfid 查路由表 ---- */
    const char *target_rfid = doc["paras"]["rfid"] | "";
    uint8_t dst_addr = LORA_FRAME_ADDR_BROADCAST;  /* 默认广播 */

    if (strlen(target_rfid) > 0) {
        auto it = g_terminal_routes.find(String(target_rfid));
        if (it != g_terminal_routes.end()) {
            dst_addr = it->second.addr;
            DEBUG_SERIAL.printf("[ROUTE] RFID=%s -> addr=%d (directed)\n",
                                target_rfid, dst_addr);
        } else {
            DEBUG_SERIAL.printf("[ROUTE] RFID=%s not found, using broadcast\n",
                                target_rfid);
        }
    }

    /* ---- 极简告警控制: set_alarm_mode → A0/A1 ----
     * 自动/手动模式判断由 Web/App 负责, ESP32 仅翻译
     * "alert"/"auto" → A0 (开启告警)
     * "normal"/"manual" → A1 (关闭告警) */
    if (strcmp(svc, "Alarm") == 0 && strcmp(cmd, "set_alarm_mode") == 0)
    {
        const char *mode = doc["paras"]["mode"] | "";
        bool disable = (strcmp(mode, "normal") == 0 || strcmp(mode, "manual") == 0);

        DEBUG_SERIAL.printf("[FWD] set_alarm_mode mode=%s -> %s  dst=%d\n",
                            mode, disable ? "A1 (disable)" : "A0 (enable)",
                            dst_addr);

        /* 帧协议发送 (带终端地址) + 停等 ACK */
        bool acked = lora_send_framed(disable ? "A1" : "A0", dst_addr);

        /* 云端回复: ACK 确认后才返回成功 */
        StaticJsonDocument<256> rsp;
        rsp["result_code"] = acked ? 0 : 1;
        rsp["response_name"] = cmd;
        String rsp_json;
        serializeJson(rsp, rsp_json);
        mqtt_response_cmd(rsp_json);
        return;
    }

    /* 构造转发给 STM32 的精简命令 JSON (其他命令保持不变) */
    StaticJsonDocument<512> fwd;
    fwd["cmd"]  = cmd;
    fwd["svc"]  = svc;

    /* Huawei Cloud "paras" → STM32 "params" */
    JsonObject paras = doc["paras"];
    if (!paras.isNull()) {
        fwd["params"] = paras;
    }

    String fwd_json;
    serializeJson(fwd, fwd_json);

    /* 帧协议发送 (带终端地址) */
    lora_send_framed(fwd_json, dst_addr);
}

/* ================================================================
 * 周期性上报 (ESP32 自身状态, 如 LoRa 链路心跳)
 * 可选: 按需上报 ESP32 自身设备状态给云端
 * ================================================================ */
static uint32_t g_heartbeat_last = 0;
static uint32_t g_ping_last = 0;
#define HEARTBEAT_INTERVAL_SEC  120
#define PING_INTERVAL_SEC        60    /* 60s 无数据则发 ping 探测 STM32 */

static void heartbeat_check()
{
    uint32_t now = millis();
    if (now - g_heartbeat_last < HEARTBEAT_INTERVAL_SEC * 1000UL) return;
    g_heartbeat_last = now;

    /* 网关自身在线心跳
     * 注: signal 字段在 STM32 侧表示 LoRa RSSI, 网关侧使用 WiFi RSSI
     *     表示网关自身的网络连接质量 (dBm) */
    DeviceStatus gw;
    gw.online      = true;
    gw.battery     = 100.0f;
    gw.signal      = WiFi.RSSI();
    gw.power       = 1;  /* mains */
    gw.work_state  = 0;  /* normal */
    gw.last_report = String(millis() / 1000);

    mqtt_report_property(gw.toCloudJson());
}

/* LoRa 链路探测: 发送 ping 给 STM32, STM32 收到后会回复 */
static void lora_ping_check()
{
    static uint32_t frames_last = 0;  /* 用于对比帧数变化 */
    uint32_t now = millis();
    if (now - g_ping_last < PING_INTERVAL_SEC * 1000UL) return;
    g_ping_last = now;

    /* 如果帧数没有变化, 说明一直没有收到 STM32 数据, 发 ping 探测 */
    if (lora_frames_count() == frames_last)
    {
        DEBUG_SERIAL.println("[PING] Sending LoRa ping to STM32...");
        lora_send_framed("{\"ping\":1}");
    }
    frames_last = lora_frames_count();
}

/* 路由老化: 清理超过 5 分钟未更新的终端路由 */
#define ROUTE_AGE_TIMEOUT_MS    (5 * 60 * 1000)

static void route_cleanup()
{
    static uint32_t s_last_cleanup = 0;
    uint32_t now = millis();
    if (now - s_last_cleanup < 30000) return;  /* 每 30s 检查一次 */
    s_last_cleanup = now;

    auto it = g_terminal_routes.begin();
    while (it != g_terminal_routes.end()) {
        if (now - it->second.last_seen_ms > ROUTE_AGE_TIMEOUT_MS) {
            DEBUG_SERIAL.printf("[ROUTE] aged out RFID=%s addr=%d\n",
                                it->first.c_str(), it->second.addr);
            it = g_terminal_routes.erase(it);
        } else {
            ++it;
        }
    }
}

/* ================================================================
 * setup() — 初始化
 * ================================================================ */
void setup()
{
    /* 串口 */
    DEBUG_SERIAL.begin(DEBUG_BAUD);
    DEBUG_SERIAL.println();
    DEBUG_SERIAL.println("========================================");
    DEBUG_SERIAL.println(" ESP32 LoRa Gateway for Huawei Cloud IoT");
    DEBUG_SERIAL.println(" STM32 <--LoRa--> ESP32 <--MQTT--> Cloud");
    DEBUG_SERIAL.println("========================================");

    /* LED */
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    /* LoRa UART */
    lora_init();
    lora_on_receive(on_lora_receive);

    /* WiFi + MQTT */
    mqtt_init();
    mqtt_on_command(on_cloud_command);

    DEBUG_SERIAL.println("[SYS] Gateway ready.");
    DEBUG_SERIAL.println("[SYS] Waiting for STM32 LoRa data...");
    DEBUG_SERIAL.println("[SYS] If no [LoRa RX] appears within 60s:");
    DEBUG_SERIAL.println("[SYS]   1. Check STM32 is running (see 'IOT TX:' on STM32 debug UART)");
    DEBUG_SERIAL.println("[SYS]   2. Check both LoRa modules: address=0, channel=0, baud=115200");
    DEBUG_SERIAL.println("[SYS]   3. Verify LoRa module wiring (TX→RX, RX→TX, MD0→GND)");
}

/* ================================================================
 * loop() — 主循环
 * ================================================================ */
void loop()
{
    /* MQTT 维护 (WiFi重连 / MQTT重连 / 消息处理) */
    mqtt_loop();

    /* LoRa RX 轮询 (STM32 数据接收) */
    lora_loop();

    /* LED 闪烁 */
    led_update();

    /* 可选: 网关心跳上报 */
    heartbeat_check();

    /* LoRa 链路探测: 收不到数据时主动 ping STM32 */
    lora_ping_check();

    /* 路由老化: 清理超时终端 */
    route_cleanup();
}
