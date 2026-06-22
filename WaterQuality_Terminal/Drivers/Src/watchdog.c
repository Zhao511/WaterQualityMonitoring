/**
 * ============================================================
 * IWDG 独立看门狗驱动 — 实现
 * ============================================================
 */

#include "watchdog.h"
#include "usart_debug.h"
#include <stdio.h>

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"

/* ================================================================
 * 心跳时间戳 (各任务最后一次上报时的系统 tick)
 * ================================================================ */
static TickType_t hb_ticks[HEARTBEAT_COUNT];

/* ================================================================
 * 各任务心跳超时阈值 (tick 数)
 * ================================================================ */
static const TickType_t hb_timeouts[HEARTBEAT_COUNT] = {
    pdMS_TO_TICKS(HB_TIMEOUT_SENSOR_MS),
    pdMS_TO_TICKS(HB_TIMEOUT_GPS_MS),
    pdMS_TO_TICKS(HB_TIMEOUT_IOT_MS),
    pdMS_TO_TICKS(HB_TIMEOUT_RFID_MS),
    pdMS_TO_TICKS(HB_TIMEOUT_LED_MS)
};

/* ================================================================
 * 任务名称 (用于日志输出)
 * ================================================================ */
static const char * const hb_names[HEARTBEAT_COUNT] = {
    "Sensor", "GPS", "IoT", "RFID", "LED"
};

/* ================================================================
 * 各任务描述信息 (用于启动时打印表格)
 * ================================================================ */
static const char * const hb_periods[HEARTBEAT_COUNT] = {
    "5000ms", "1000ms", "1000ms", "2000ms", " event"
};

static const uint32_t hb_stack_words[HEARTBEAT_COUNT] = {
    384, 256, 512, 128, 128
};

/* ================================================================
 * 任务优先级 (用于启动时打印表格)
 * ================================================================ */
static const uint32_t hb_priorities[HEARTBEAT_COUNT] = {
    3, 2, 2, 1, 1
};

/* ================================================================
 * 用于去重: 记录上次告警的任务, 避免重复打印
 * ================================================================ */
static HeartbeatTask last_stuck_task = HEARTBEAT_COUNT;

/* ================================================================
 * WDG_Init — 使能 LSI + 配置 IWDG + 启动
 * ================================================================ */
void WDG_Init(void)
{
    uint32_t timeout;

    /* 1. 使能 LSI (内部低速时钟 ~40kHz) */
    RCC->CSR |= RCC_CSR_LSION;

    /* 2. 等待 LSI 就绪 (带超时) */
    timeout = 0;
    while (((RCC->CSR & RCC_CSR_LSIRDY) == 0) && (timeout < 0xFFFF))
        timeout++;

    /* 3. 解锁 IWDG */
    IWDG->KR = IWDG_KEY_ENABLE_WRITE;

    /* 4. 预分频器 = 256 */
    IWDG->PR = IWDG_PRESCALER_USED;

    /* 5. 重装载值 */
    IWDG->RLR = IWDG_RELOAD_VALUE;

    /* 6. 等待寄存器更新完成 (带超时) */
    timeout = 0;
    while ((IWDG->SR != 0) && (timeout < 0xFFFF))
        timeout++;

    /* 7. 重装载 + 启动 */
    IWDG->KR = IWDG_KEY_RELOAD;
    IWDG->KR = IWDG_KEY_START;

    /* 8. 初始化心跳时间戳 */
    TickType_t now = xTaskGetTickCount();
    for (uint8_t i = 0; i < HEARTBEAT_COUNT; i++)
        hb_ticks[i] = now;

    last_stuck_task = HEARTBEAT_COUNT;
}

/* ================================================================
 * WDG_Feed — 重装载 IWDG 计数器
 * ================================================================ */
void WDG_Feed(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

/* ================================================================
 * WDG_DebugFreeze — 调试暂停时冻结 IWDG
 * ================================================================ */
void WDG_DebugFreeze(void)
{
    DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP;
}

/* ================================================================
 * WDG_Heartbeat — 应用任务上报心跳
 * ================================================================ */
void WDG_Heartbeat(HeartbeatTask task)
{
    if (task < HEARTBEAT_COUNT)
        hb_ticks[task] = xTaskGetTickCount();
}

/* ================================================================
 * WDG_CheckAll — 检查所有任务心跳, 全部正常则喂狗
 * ================================================================ */
void WDG_CheckAll(void)
{
    TickType_t now = xTaskGetTickCount();

    for (uint8_t i = 0; i < HEARTBEAT_COUNT; i++)
    {
        if ((now - hb_ticks[i]) >= hb_timeouts[i])
        {
            if (last_stuck_task != (HeartbeatTask)i)
            {
                Debug_Printf(
                    "\r\n========================================\r\n");
                Debug_Printf(
                    "[WDG] *** ALERT: Task '%s' STUCK! ***\r\n", hb_names[i]);
                Debug_Printf(
                    "[WDG] Last heartbeat: %lums ago (timeout: %lums)\r\n",
                    (unsigned long)((now - hb_ticks[i]) * portTICK_PERIOD_MS),
                    (unsigned long)(hb_timeouts[i] * portTICK_PERIOD_MS));
                Debug_Printf(
                    "[WDG] IWDG NOT fed -- system will reset in ~4s\r\n");
                Debug_Printf(
                    "========================================\r\n\r\n");
                last_stuck_task = (HeartbeatTask)i;
            }
            return;
        }
    }

    if (last_stuck_task != HEARTBEAT_COUNT)
    {
        Debug_Printf("[WDG] All tasks recovered. Resuming feed.\r\n");
        last_stuck_task = HEARTBEAT_COUNT;
    }

    WDG_Feed();
}

/* ================================================================
 * WDG_PrintTaskTable — 打印任务配置表
 * ================================================================ */
void WDG_PrintTaskTable(void)
{
    Debug_Printf(
        "\r\nTask      Prio  Period   HB Timeout  Stack\r\n");
    Debug_Printf(
        "----------------------------------------------\r\n");
    Debug_Printf(
        "Watchdog    4    1000ms      ---       128W\r\n");

    for (uint8_t i = 0; i < HEARTBEAT_COUNT; i++)
    {
        Debug_Printf("%-10s  %lu    %s    %5lums     %luW\r\n",
                     hb_names[i],
                     (unsigned long)hb_priorities[i],
                     hb_periods[i],
                     (unsigned long)(hb_timeouts[i] * portTICK_PERIOD_MS),
                     (unsigned long)hb_stack_words[i]);
    }

    Debug_Printf(
        "----------------------------------------------\r\n");
}

/* ================================================================
 * WDG_PrintStatus — 打印心跳状态摘要
 * ================================================================ */
void WDG_PrintStatus(void)
{
    TickType_t now = xTaskGetTickCount();
    uint8_t stuck_count = 0;
    static char line[128];
    int pos = 0;

    pos += snprintf(line + pos, sizeof(line) - pos,
                    "[WDG] Status: ");

    for (uint8_t i = 0; i < HEARTBEAT_COUNT; i++)
    {
        TickType_t elapsed = now - hb_ticks[i];
        float elapsed_s = (float)(elapsed * portTICK_PERIOD_MS) / 1000.0f;
        uint8_t is_stuck = (elapsed >= hb_timeouts[i]) ? 1 : 0;

        pos += snprintf(line + pos, sizeof(line) - pos,
                        "%s=%.1fs%s ",
                        hb_names[i], elapsed_s,
                        is_stuck ? "!" : "");

        if (is_stuck) stuck_count++;
    }

    if (stuck_count == 0)
        snprintf(line + pos, sizeof(line) - pos, "| ALL OK");
    else
        for (uint8_t i = 0; i < HEARTBEAT_COUNT; i++)
            if ((now - hb_ticks[i]) >= hb_timeouts[i])
            {
                snprintf(line + pos, sizeof(line) - pos,
                         "| *** %s STUCK! ***", hb_names[i]);
                break;
            }

    Debug_Printf("%s\r\n", line);
}
