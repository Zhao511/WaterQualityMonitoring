/**
 * ============================================================
 * LoRa UART 通信层 — 与 STM32 数据收发
 * ============================================================
 * 数据帧格式: 纯 JSON 文本 (无帧头帧尾，靠 '\n' 分隔)
 * STM32 → ESP32: 属性上报 / 命令响应
 * ESP32 → STM32: 云端下发命令
 *
 * 模块: 正点原子 ATK-LORA-01
 * 模式: 透传 (MD0=LOW)
 * ============================================================
 */

#ifndef LORA_UART_H
#define LORA_UART_H

#include <Arduino.h>
#include "config.h"

/* LoRa 默认参数 (必须与 STM32 侧 lora.h 一致) */
#define LORA_DEFAULT_ADDRESS    0
#define LORA_DEFAULT_CHANNEL    0     /* 433MHz */

/* 心跳日志间隔 (ms) — 30s 输出一次, 确认轮询存活 */
#define LORA_HEARTBEAT_INTERVAL_MS   30000

/* ================================================================
 * 初始化 / 轮询
 * ================================================================ */
void lora_init();
void lora_loop();

/* ================================================================
 * 发送 — ESP32 → STM32
 * ================================================================ */
void lora_send(const String &json);

/* ================================================================
 * 帧协议发送 (规范帧 + 停等ACK + 间隔控制 + 超时重发)
 *
 * 帧格式: [0xAA] [LEN 1B] [DST_ADDR 1B] [PAYLOAD (LEN-1) bytes] [XOR_CHECKSUM 1B]
 * ACK 帧: [0xAA] [0x00] [0xAA]  (3字节, LEN=0 表示ACK)
 * NAK 帧: [0xAA] [0xFF] [0x55]  (3字节, LEN=0xFF 表示NAK)
 *
 * DST_ADDR: 0x00=广播(所有终端处理), 0x01~0xFE=定向终端地址
 * LEN 包含 DST_ADDR (有效载荷 = LEN - 1)
 * XOR 校验覆盖: 0xAA ^ LEN ^ DST_ADDR ^ payload[0..LEN-2]
 *
 * 行为: 发帧 → 等ACK(≤500ms) → 超时重发(最多2次) → 发前≥200ms间隔
 * 返回: true=收到ACK, false=全部重发失败
 * ================================================================ */
#define LORA_FRAME_HEADER           0xAA
#define LORA_FRAME_MAX_PAYLOAD      200
#define LORA_FRAME_ACK_TIMEOUT_MS   800
#define LORA_FRAME_MIN_INTERVAL_MS  200
#define LORA_FRAME_TX_JITTER_MIN    10     /* 发送前最小随机抖动 ms */
#define LORA_FRAME_TX_JITTER_MAX    80     /* 发送前最大随机抖动 ms */
#define LORA_FRAME_ADDR_BROADCAST   0x00

bool lora_send_framed(const String &payload,
                      uint8_t dst_addr = LORA_FRAME_ADDR_BROADCAST,
                      int max_retries = 6);

/* ================================================================
 * 接收 — STM32 → ESP32 (回调注册)
 * ================================================================ */
typedef void (*lora_rx_callback_t)(const String &svc, const String &json);
void lora_on_receive(lora_rx_callback_t cb);

/* ================================================================
 * 查询接收缓冲区是否有完整 JSON 帧
 * ================================================================ */
bool lora_available();

/* ================================================================
 * 统计
 * ================================================================ */
uint32_t lora_frames_count();   /* 累计收到的完整帧数 */

#endif /* LORA_UART_H */
