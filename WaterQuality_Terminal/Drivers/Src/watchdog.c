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
    "1000ms", " 200ms", "1000ms", " 500ms", " event"
};

static const uint32_t hb_stack_words[HEARTBEAT_COUNT] = {
    256, 256, 512, 128, 128
};

/* ================================================================
 * 任务优先级 (用于启动时打印表格)
 * ================================================================ */
static const uint32_t hb_priorities[HEARTBEAT_COUNT] = {
    3, 2, 2, 1, 1
};

/* ================================================================
 * 用于去重: 记录上次告警的任务, 避免重复打印
 * HEARTBEAT_COUNT 表示 "无告警"
 * ================================================================ */
static HeartbeatTask last_stuck_task = HEARTBEAT_COUNT;

/* ================================================================
 * WDG_Init — 使能 LSI + 配置 IWDG + 启动
 * 注意: 必须在调度器启动前调用 (在 main 中, 创建任务之前)
 *        一旦启动 IWDG, 只能由硬件复位停止
 * ================================================================ */
void WDG_Init(void)
{
    uint32_t timeout;

    /* 1. 使能 LSI (内部低速时钟 ~40kHz) */
    RCC->CSR |= RCC_CSR_LSION;

    /* 2. 等待 LSI 就绪 (带超时保护, 防止硬件故障时死循环) */
    timeout = 0;
    while (((RCC->CSR & RCC_CSR_LSIRDY) == 0) && (timeout < 0xFFFF))
    {
        timeout++;
    }

    /* 3. 解锁 IWDG 密钥寄存器 — 允许写 PR 和 RLR */
    IWDG->KR = IWDG_KEY_ENABLE_WRITE;

    /* 4. 设置预分频器 = 256 */
    IWDG->PR = IWDG_PRESCALER_USED;

    /* 5. 设置重装载值 (12-bit, 0~4095) */
    IWDG->RLR = IWDG_RELOAD_VALUE;

    /* 6. 等待寄存器更新完成 (PVU/RVU 标志清零) */
    while (IWDG->SR != 0)
    {
        /* 等待预分频器和重装载寄存器更新完毕 */
    }

    /* 7. 重装载计数器 (将 RLR 加载到递减计数器) */
    IWDG->KR = IWDG_KEY_RELOAD;

    /* 8. 启动 IWDG (此操作不可逆! 之后只能由复位停止) */
    IWDG->KR = IWDG_KEY_START;

    /* 9. 初始化所有心跳时间戳 */
    TickType_t now = xTaskGetTickCount();
    for (uint8_t i = 0; i < HEARTBEAT_COUNT; i++)
    {
        hb_ticks[i] = now;
    }

    last_stuck_task = HEARTBEAT_COUNT;
}

/* ================================================================
 * WDG_Feed — 重装载 IWDG 计数器
 * 由看门狗监控任务在确认所有任务健康后调用
 * ================================================================ */
void WDG_Feed(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

/* ================================================================
 * WDG_DebugFreeze — 调试暂停时冻结 IWDG
 * 设置 DBGMCU->CR 的 bit8, 当 CPU 被调试器暂停时 IWDG 停止计数
 * 避免调试断点导致意外复位
 * ================================================================ */
void WDG_DebugFreeze(void)
{
    DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP;
}

/* ================================================================
 * WDG_Heartbeat — 应用任务上报心跳
 * 每个任务在循环末尾调用, 记录当前系统 tick
 * ================================================================ */
void WDG_Heartbeat(HeartbeatTask task)
{
    if (task < HEARTBEAT_COUNT)
    {
        hb_ticks[task] = xTaskGetTickCount();
    }
}

/* ================================================================
 * WDG_CheckAll — 检查所有任务心跳
 *
 * 遍历所有心跳槽位, 若任一任务的 (当前时间 - 最后心跳) >= 超时阈值,
 * 则判定该任务卡死:
 *   - 打印告警信息 (直接使用 Debug_Printf, 不使用互斥量,
 *     因为卡死的任务可能正持有 xDebugMutex)
 *   - 不调用 WDG_Feed(), IWDG 将在 ~4s 后溢出复位
 *   - 去重: 同一任务只打印一次告警
 *
 * 若所有心跳正常, 则调用 WDG_Feed() 喂狗
 * ================================================================ */
void WDG_CheckAll(void)
{
    TickType_t now = xTaskGetTickCount();

    for (uint8_t i = 0; i < HEARTBEAT_COUNT; i++)
    {
        TickType_t elapsed = now - hb_ticks[i];

        if (elapsed >= hb_timeouts[i])
        {
            /* 任务卡死! */
            if (last_stuck_task != (HeartbeatTask)i)
            {
                /* 去重: 同一任务只打印一次 */
                Debug_Printf(
                    "\r\n========================================\r\n");
                Debug_Printf(
                    "[WDG] *** ALERT: Task '%s' STUCK! ***\r\n", hb_names[i]);
                Debug_Printf(
                    "[WDG] Last heartbeat: %lums ago (timeout: %lums)\r\n",
                    (unsigned long)(elapsed * portTICK_PERIOD_MS),
                    (unsigned long)(hb_timeouts[i] * portTICK_PERIOD_MS));
                Debug_Printf(
                    "[WDG] IWDG NOT fed -- system will reset in ~4s\r\n");
                Debug_Printf(
                    "========================================\r\n\r\n");
                last_stuck_task = (HeartbeatTask)i;
            }
            /* 不喂狗 — IWDG 将溢出复位 */
            return;
        }
    }

    /* 所有任务正常 — 喂狗 */
    if (last_stuck_task != HEARTBEAT_COUNT)
    {
        /* 之前有任务卡死, 现在恢复了 (不太可能, 但处理这种情况) */
        Debug_Printf("[WDG] All tasks recovered. Resuming feed.\r\n");
        last_stuck_task = HEARTBEAT_COUNT;
    }

    WDG_Feed();
}

/* ================================================================
 * WDG_PrintTaskTable — 打印任务配置表 (启动时调用一次)
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
 * WDG_PrintStatus — 打印心跳状态摘要 (看门狗任务周期性调用)
 * 输出格式:
 *   [WDG] Status: Sensor=0.2s GPS=0.0s IoT=0.5s RFID=0.3s LED=0.1s | ALL OK
 * 或异常时:
 *   [WDG] Status: Sensor=0.2s GPS=0.0s IoT=5.1s RFID=0.3s LED=0.1s | *** IoT STUCK! ***
 * ================================================================ */
void WDG_PrintStatus(void)
{
    TickType_t now = xTaskGetTickCount();
    uint8_t stuck_count = 0;
    char line[128];
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
    {
        snprintf(line + pos, sizeof(line) - pos, "| ALL OK");
    }
    else
    {
        /* 找出第一个卡死的任务名 */
        for (uint8_t i = 0; i < HEARTBEAT_COUNT; i++)
        {
            if ((now - hb_ticks[i]) >= hb_timeouts[i])
            {
                snprintf(line + pos, sizeof(line) - pos,
                         "| *** %s STUCK! ***", hb_names[i]);
                break;
            }
        }
    }

    Debug_Printf("%s\r\n", line);
}
