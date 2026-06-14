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
#include "config.h"
#include "thing_model.h"
#include "lora_uart.h"
#include "mqtt_huawei.h"

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
        /* 属性上报 → 直接转发到华为云 */
        mqtt_report_property(json);
    }
    else if (svc.startsWith("response:"))
    {
        /* STM32 响应格式 {"rsp":"cmd","result":true,"msg":"ok","longitude":...,...}
         * → 遍历所有字段转发到华为云 IoTDA 命令响应格式 */
        String cmd_name = svc.substring(9);  /* 去掉 "response:" 前缀 */

        /* 内部链路探测 ping/pong 不上报云平台 */
        if (cmd_name == "ping") {
            DEBUG_SERIAL.println("[FWD] LoRa ping pong (internal), skip cloud");
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

    /* 构造转发给 STM32 的精简命令 JSON */
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

    lora_send(fwd_json);
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
        lora_send("{\"ping\":1}");
    }
    frames_last = lora_frames_count();
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
}
