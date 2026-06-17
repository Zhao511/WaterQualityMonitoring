/**
 * ============================================================
 * IWDG 独立看门狗驱动 — STM32F103C8T6
 * ============================================================
 * 架构: 最高优先级监控任务统一喂狗, 各应用任务上报心跳
 * 超时: ~4s (LSI 40kHz / 预分频256 / 重装载625)
 * 调试: DBGMCU 冻结 — 断点暂停时 IWDG 停止计数
 *
 * 注意: 本项目使用精简版 stm32f10x.h, 不含 IWDG/DBGMCU
 *        所有寄存器定义自包含在本文件中
 * ============================================================
 */

#ifndef __WATCHDOG_H
#define __WATCHDOG_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

/*
 * 注: IWDG_BASE / IWDG_TypeDef / DBGMCU_BASE / DBGMCU_TypeDef /
 *     RCC_CSR_LSION / RCC_CSR_LSIRDY 等寄存器级定义
 *     现在由 V3.5.0 CMSIS stm32f10x.h 提供, 不再在此重复。
 *     这里只保留 IWDG 应用层简写宏 (SPL stm32f10x_iwdg.h 用的是不同命名)。
 */

/* ---- IWDG 密钥值 (写入 KR) ---- */
#define IWDG_KEY_ENABLE_WRITE   ((uint16_t)0x5555)   /* 解锁 PR/RLR 写保护 */
#define IWDG_KEY_RELOAD         ((uint16_t)0xAAAA)   /* 重装载计数器 */
#define IWDG_KEY_START          ((uint16_t)0xCCCC)   /* 启动看门狗 (不可逆) */

/* ---- IWDG 预分频器 (写入 PR 的低 3 位) ---- */
#define IWDG_Prescaler_4        ((uint8_t)0x00)      /* /4   */
#define IWDG_Prescaler_8        ((uint8_t)0x01)      /* /8   */
#define IWDG_Prescaler_16       ((uint8_t)0x02)      /* /16  */
#define IWDG_Prescaler_32       ((uint8_t)0x03)      /* /32  */
#define IWDG_Prescaler_64       ((uint8_t)0x04)      /* /64  */
#define IWDG_Prescaler_128      ((uint8_t)0x05)      /* /128 */
#define IWDG_Prescaler_256      ((uint8_t)0x06)      /* /256 */

/* ---- IWDG 状态寄存器标志 ---- */
#define IWDG_FLAG_PVU           ((uint16_t)0x0001)   /* 预分频器更新中 */
#define IWDG_FLAG_RVU           ((uint16_t)0x0002)   /* 重装载值更新中 */

#define DBGMCU_CR_DBG_IWDG_STOP ((uint32_t)0x00000100)  /* Bit 8: 调试时停止 IWDG */

/* ================================================================
 * IWDG 超时计算
 *   LSI 典型值 40kHz, 预分频器 = 256, 重装载值 = 625
 *   超时 = 625 × 256 / 40000 = 4.0s
 * ================================================================ */
#define IWDG_PRESCALER_USED     IWDG_Prescaler_256
#define IWDG_RELOAD_VALUE       ((uint16_t)625)

/* ================================================================
 * 任务心跳系统
 * ================================================================ */
typedef enum {
    HEARTBEAT_SENSOR = 0,
    HEARTBEAT_GPS,
    HEARTBEAT_IOT,
    HEARTBEAT_RFID,
    HEARTBEAT_LED,
    HEARTBEAT_COUNT
} HeartbeatTask;

#define HB_TIMEOUT_SENSOR_MS    ((TickType_t)3000)
#define HB_TIMEOUT_GPS_MS       ((TickType_t)1000)
#define HB_TIMEOUT_IOT_MS       ((TickType_t)5000)
#define HB_TIMEOUT_RFID_MS      ((TickType_t)3000)
#define HB_TIMEOUT_LED_MS       ((TickType_t)10000)

/* ================================================================
 * 公开 API
 * ================================================================ */
void WDG_Init(void);
void WDG_Feed(void);
void WDG_DebugFreeze(void);
void WDG_Heartbeat(HeartbeatTask task);
void WDG_CheckAll(void);
void WDG_PrintTaskTable(void);
void WDG_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* __WATCHDOG_H */
