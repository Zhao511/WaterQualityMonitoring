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

/* ================================================================
 * IWDG 寄存器定义 (基地址 0x40003000, APB1 总线)
 * ================================================================ */
#define IWDG_BASE               ((uint32_t)0x40003000)

typedef struct {
    __IO uint32_t KR;           /* 密钥寄存器    offset 0x00 */
    __IO uint32_t PR;           /* 预分频寄存器  offset 0x04 */
    __IO uint32_t RLR;          /* 重装载寄存器  offset 0x08 */
    __IO uint32_t SR;           /* 状态寄存器    offset 0x0C */
} IWDG_TypeDef;

#define IWDG                    ((IWDG_TypeDef *) IWDG_BASE)

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

/* ================================================================
 * DBGMCU 寄存器定义 (基地址 0xE0042000, Cortex-M3 系统地址)
 * ================================================================ */
#define DBGMCU_BASE             ((uint32_t)0xE0042000)

typedef struct {
    __IO uint32_t IDCODE;       /* ID 码寄存器   offset 0x00 */
    __IO uint32_t CR;           /* 控制寄存器    offset 0x04 */
} DBGMCU_TypeDef;

#define DBGMCU                  ((DBGMCU_TypeDef *) DBGMCU_BASE)

#define DBGMCU_CR_DBG_IWDG_STOP ((uint32_t)0x00000100)  /* Bit 8: 调试时停止 IWDG */

/* ================================================================
 * RCC CSR 寄存器 LSI 控制位
 * ================================================================ */
#define RCC_CSR_LSION           ((uint32_t)0x00000001)   /* 内部低速时钟使能 */
#define RCC_CSR_LSIRDY          ((uint32_t)0x00000002)   /* 内部低速时钟就绪 */

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
#define HB_TIMEOUT_RFID_MS      ((TickType_t)1500)
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
