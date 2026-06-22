/**
 * ============================================================
 * ESP32 LoRa 网关 — 全局配置
 * ============================================================
 * 架构: STM32 ←LoRa→ ESP32 ←WiFi/MQTT→ 华为云IoT
 * ============================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

/* ================================================================
 * WiFi 配置
 * ================================================================ */
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"
#define WIFI_TIMEOUT_MS 20000       /* WiFi 连接超时 (ms) */
#define WIFI_RETRY_MAX  5            /* 最大重试次数 */

/* ================================================================
 * 华为云 IoT 设备身份 (从华为云控制台获取)
 * ================================================================ */
#define HUAWEI_DEVICE_ID    "YOUR_DEVICE_ID"
#define HUAWEI_PRODUCT_ID   "YOUR_PRODUCT_ID"
#define HUAWEI_DEVICE_SECRET "YOUR_DEVICE_SECRET"

/* 华为云 IoT MQTT 接入点 (根据你注册的区域选择)
 * 华南-广州: iot-mqtts.cn-north-4.myhuaweicloud.com
 * 华北-北京: iot-mqtts.cn-north-1.myhuaweicloud.com
 * 华东-上海: iot-mqtts.cn-east-3.myhuaweicloud.com
 */
#define HUAWEI_MQTT_HOST    "YOUR_MQTT_HOST"  /* e.g. xxx.st1.iotda-device.cn-south-1.myhuaweicloud.com */
#define HUAWEI_MQTT_PORT    8883         /* TLS: 8883 */

/* MQTT 心跳 */
#define MQTT_KEEPALIVE_SEC  120
#define MQTT_RETRY_MAX      3

/* ================================================================
 * LoRa 模块 (正点原子 ATK-LORA-01) — 与 STM32 侧配对
 * ================================================================ */
#define LORA_SERIAL         Serial2     /* UART2 */
#define LORA_BAUD_RATE      115200      /* 模块出厂默认 115200 */
#define LORA_RX_PIN         26          /* GPIO26 ← 模块 TXD */
#define LORA_TX_PIN         25          /* GPIO25 → 模块 RXD */
#define LORA_AUX_PIN        35          /* GPIO35 ← 模块 AUX (仅输入, 状态指示) */
#define LORA_MD0_PIN        32          /* GPIO32 → 模块 MD0 (模式切换) */

/* 兼容旧代码的别名 */
#define LORA_M0_PIN         LORA_MD0_PIN

/* LoRa 缓冲区 */
#define LORA_RX_BUF_SIZE    2048
#define LORA_TX_BUF_SIZE    512

/* ================================================================
 * 物联网 4 服务 — 属性上报 Topic (华为云 IoTDA 标准格式)
 * ================================================================ */
#define TOPIC_PROPERTY_REPORT   "$oc/devices/%s/sys/properties/report"
#define TOPIC_COMMAND_RECEIVE   "$oc/devices/%s/sys/commands/#"
#define TOPIC_COMMAND_RESPONSE  "$oc/devices/%s/sys/commands/response/request_id=%s"

/* ================================================================
 * 调试输出
 * ================================================================ */
#define DEBUG_SERIAL        Serial
#define DEBUG_BAUD          115200

#endif /* CONFIG_H */
