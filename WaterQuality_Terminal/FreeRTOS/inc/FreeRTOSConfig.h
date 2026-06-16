/*
 * FreeRTOS V10.x + STM32F103C8T6 (Cortex-M3) 配置文件
 *
 * 内核源码: https://github.com/FreeRTOS/FreeRTOS-Kernel
 * 将内核文件放入 FreeRTOS-Kernel/ 目录后即可编译
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ================================================================
 * 硬件相关
 * ================================================================ */
extern uint32_t SystemCoreClock;
#define configCPU_CLOCK_HZ              ( SystemCoreClock )

/* ================================================================
 * 调度器配置
 * ================================================================ */
#define configUSE_16_BIT_TICKS          0
#define configUSE_PREEMPTION            1
#define configUSE_TIME_SLICING          1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1
#define configUSE_TICKLESS_IDLE         0
#define configTICK_RATE_HZ              ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES            ( 5 )
#define configMINIMAL_STACK_SIZE        ( ( uint16_t ) 64 )
#define configMAX_TASK_NAME_LEN         ( 12 )
#define configIDLE_SHOULD_YIELD         1

/* ================================================================
 * 内存管理
 * ================================================================ */
#define configTOTAL_HEAP_SIZE           ( ( size_t ) ( 12 * 1024 ) )  /* 12KB: 6任务+5队列+3互斥量 */
#define configAPPLICATION_ALLOCATED_HEAP 0

/* ================================================================
 * 软件定时器
 * ================================================================ */
#define configUSE_TIMERS                0
#define configTIMER_TASK_PRIORITY       ( 2 )
#define configTIMER_QUEUE_LENGTH        5
#define configTIMER_TASK_STACK_DEPTH    ( configMINIMAL_STACK_SIZE )

/* ================================================================
 * 可选功能
 * ================================================================ */
#define INCLUDE_vTaskDelay              1
#define INCLUDE_xTaskDelayUntil         1
#define configUSE_MUTEXES               1
#define configUSE_RECURSIVE_MUTEXES     0
#define configUSE_COUNTING_SEMAPHORES   0
#define configQUEUE_REGISTRY_SIZE       5
#define configUSE_QUEUE_SETS            0
#define configUSE_TASK_NOTIFICATIONS    1
#define configUSE_TRACE_FACILITY        0

/* ================================================================
 * 钩子函数
 * ================================================================ */
#define configUSE_IDLE_HOOK             0
#define configUSE_TICK_HOOK             0
#define configUSE_MALLOC_FAILED_HOOK    0
#define configCHECK_FOR_STACK_OVERFLOW  1    /* 方法1: 上下文切换时检查栈指针 */

/* ================================================================
 * 运行时统计（调试用，默认关闭以节约资源）
 * ================================================================ */
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/* ================================================================
 * 协程（已废弃，不使用）
 * ================================================================ */
#define configUSE_CO_ROUTINES           0
#define configMAX_CO_ROUTINE_PRIORITIES ( 2 )

/* ================================================================
 * Cortex-M3 中断优先级配置 (STM32 使用 4-bit 优先级)
 * ================================================================ */
#ifdef __NVIC_PRIO_BITS
  #define configPRIO_BITS               __NVIC_PRIO_BITS
#else
  #define configPRIO_BITS               4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY          0x0f
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY     0x05

/* 以下为内核内部使用的移位后优先级值 */
#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* ================================================================
 * ISR 名称映射 (GCC/ARM_CM3 port 使用 CMSIS 标准名)
 * FreeRTOS port.c 中定义的函数需映射到启动文件中的弱符号
 * ================================================================ */
#define xPortPendSVHandler              PendSV_Handler
#define xPortSysTickHandler             SysTick_Handler
#define vPortSVCHandler                 SVC_Handler

/* ================================================================
 * 断言
 * ================================================================ */
#define configASSERT( x )   if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }

/* ================================================================
 * 标准库包含
 * ================================================================ */
#include <stdint.h>

#endif /* FREERTOS_CONFIG_H */
