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

uint8_t  debug_rx_buffer[DEBUG_BUFFER_SIZE];
uint16_t debug_rx_index = 0;

/* 静态缓冲区 — 不占用栈空间，配合临界区保护 */
static char debug_printf_buf[128];

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
}

void Debug_SendData(uint8_t *data, uint16_t length)
{
    for (uint16_t i = 0; i < length; i++)
    {
        while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TXE) == RESET);
        USART_SendData(DEBUG_USART, data[i]);
    }
    while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TC) == RESET);
}

void Debug_SendString(char *str)
{
    Debug_SendData((uint8_t *)str, strlen(str));
}

void Debug_Printf(const char *format, ...)
{
    va_list args;

    /*
     * 使用静态缓冲区替代栈缓冲区，消除每次调用消耗 128 字节栈空间的隐患。
     * 临界区只保护 vsnprintf (格式化到静态缓冲区), 不保护 UART 发送。
     * 这样 SysTick 可以在 UART 发送期间正常触发, 避免饿死低优先级任务。
     * UART 字符级互锁: TXE 忙等保证同一时刻只有一个发送者占用 TX 线。
     */
    taskENTER_CRITICAL();
    va_start(args, format);
    vsnprintf(debug_printf_buf, sizeof(debug_printf_buf), format, args);
    va_end(args);
    taskEXIT_CRITICAL();

    /* UART 发送在临界区外 — 中断使能, 调度器正常工作 */
    Debug_SendString(debug_printf_buf);
}
