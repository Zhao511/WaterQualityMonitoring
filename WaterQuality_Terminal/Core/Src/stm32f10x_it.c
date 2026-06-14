/**
 * 中断服务程序 (FreeRTOS 版本)
 *
 * USART 分配:
 *   USART1 (PA9/PA10)  — 调试输出 → 板载 USB-UART
 *   USART2 (PD5/PD6)   — GPS 模块 (重映射)
 *   USART3 (PB10/PB11) — LoRa 模块
 */

#include "stm32f10x_it.h"
#include "lora.h"
#include "gps.h"
#include "usart_debug.h"

extern uint8_t  gps_rx_buffer[];
extern uint16_t gps_rx_index;
extern uint8_t  lora_rx_buffer[];
extern uint16_t lora_rx_index;
extern uint8_t  debug_rx_buffer[];
extern uint16_t debug_rx_index;

static void UART_RX_ISR(USART_TypeDef *USARTx, uint8_t *buf,
                         uint16_t *idx, uint16_t buf_size)
{
    if (USART_GetITStatus(USARTx, USART_IT_RXNE) != RESET)
    {
        uint8_t data = (uint8_t)USART_ReceiveData(USARTx);
        if (*idx < buf_size)
            buf[(*idx)++] = data;
        else
        {
            *idx = 0;
            buf[(*idx)++] = data;
        }
        USART_ClearITPendingBit(USARTx, USART_IT_RXNE);
    }
}

/* USART1 — Debug */
void USART1_IRQHandler(void)
{
    UART_RX_ISR(USART1, debug_rx_buffer, &debug_rx_index, DEBUG_BUFFER_SIZE);
}

/* USART2 — GPS */
void USART2_IRQHandler(void)
{
    UART_RX_ISR(USART2, gps_rx_buffer, &gps_rx_index, GPS_BUFFER_SIZE);
}

/* USART3 — LoRa */
void USART3_IRQHandler(void)
{
    UART_RX_ISR(USART3, lora_rx_buffer, &lora_rx_index, LORA_BUFFER_SIZE);
}
