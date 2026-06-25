#include "usart_debug.h"
#include "stm32f10x_usart.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "misc.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

uint8_t  debug_rx_buffer[DEBUG_BUFFER_SIZE];
uint16_t debug_rx_index = 0;

/* 静态缓冲区 — 不占用栈空间，配合互斥锁保护
 * 512 字节足够容纳最长告警 JSON (~210B) + 前缀，避免截断 */
static char debug_printf_buf[512];

/* 互斥锁 — 替代 taskENTER_CRITICAL，仅阻止多任务并发写 UART
 * 不关闭中断，确保 LoRa/GPS ISR 在发送期间正常运行
 * 在 Debug_USART_Init 中创建 (调度器启动前，单线程上下文安全) */
static SemaphoreHandle_t debug_uart_mutex = NULL;

/* UART 硬件超时计数器 (对齐 lora.c 风格: ~7ms @72MHz, 远大于 115200bps 单字节 87us) */
#define DEBUG_UART_TIMEOUT  100000

/**
 * @brief  初始化调试串口 (USART1, PA9=TX, PA10=RX, 115200bps)
 */
void Debug_USART_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    /* USART1 在 APB2 总线上 */
    RCC_APB2PeriphClockCmd(DEBUG_USART_RCC | RCC_APB2Periph_GPIOA, ENABLE);

    /* TX: PA9 — 复用推挽 */
    GPIO_InitStructure.GPIO_Pin   = DEBUG_TX_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DEBUG_TX_PORT, &GPIO_InitStructure);

    /* RX: PA10 — 浮空输入 */
    GPIO_InitStructure.GPIO_Pin  = DEBUG_RX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(DEBUG_RX_PORT, &GPIO_InitStructure);

    /* USART1 参数 */
    USART_InitStructure.USART_BaudRate            = 115200;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(DEBUG_USART, &USART_InitStructure);

    /* 中断配置 */
    NVIC_InitStructure.NVIC_IRQChannel                   = DEBUG_USART_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_ITConfig(DEBUG_USART, USART_IT_RXNE, ENABLE);
    USART_Cmd(DEBUG_USART, ENABLE);

    /* 创建互斥锁 (调度器启动前调用, 单线程上下文安全)
     * 替代 taskENTER_CRITICAL: 仅阻止并发写 UART, 不关闭中断 */
    debug_uart_mutex = xSemaphoreCreateMutex();
}

void Debug_SendData(uint8_t *data, uint16_t length)
{
    for (uint16_t i = 0; i < length; i++)
    {
        uint32_t timeout = DEBUG_UART_TIMEOUT;
        while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TXE) == RESET) {
            if (--timeout == 0) break;  /* 超时保护: USART 硬件故障时不卡死 */
        }
        if (timeout > 0) {
            USART_SendData(DEBUG_USART, data[i]);
        }
    }
    {
        uint32_t timeout = DEBUG_UART_TIMEOUT;
        while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TC) == RESET) {
            if (--timeout == 0) break;  /* 超时保护 */
        }
    }
}

void Debug_SendString(char *str)
{
    Debug_SendData((uint8_t *)str, strlen(str));
}

void Debug_Printf(const char *format, ...)
{
    va_list args;

    /*
     * 使用互斥锁替代 taskENTER_CRITICAL():
     * - 仅阻止其他任务并发写 UART/缓冲区，不关闭中断
     * - LoRa/GPS ISR 在发送期间正常运行，避免数据丢失
     * - 200ms 超时防止死锁 (例如 ISR 中误调 Debug_Printf 或持有者异常)
     *
     * 注: 调度器启动前 debug_uart_mutex 已创建, 且此时为单线程上下文,
     *     xSemaphoreTake 在调度器未运行时不会阻塞, 安全。
     */
    if (debug_uart_mutex != NULL) {
        if (xSemaphoreTake(debug_uart_mutex, pdMS_TO_TICKS(200)) != pdPASS) {
            return;  /* 超时: 放弃本次输出, 防止系统死锁 */
        }
    }

    va_start(args, format);
    vsnprintf(debug_printf_buf, sizeof(debug_printf_buf), format, args);
    va_end(args);
    Debug_SendString(debug_printf_buf);

    if (debug_uart_mutex != NULL) {
        xSemaphoreGive(debug_uart_mutex);
    }
}
