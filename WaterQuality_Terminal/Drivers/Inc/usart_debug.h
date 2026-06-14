#ifndef __USART_DEBUG_H
#define __USART_DEBUG_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

/* ========== 调试串口: USART1 (PA9=TX, PA10=RX) ==========
 * 说明: 大多数 STM32F103C8T6 开发板的板载 USB-UART (CH340/CP2102)
 *       接在 PA9/PA10，因此将 Debug 输出改到 USART1。
 *       GPS 模块改用 USART2 重映射到 PD5/PD6。
 */
#define DEBUG_USART          USART1
#define DEBUG_USART_RCC      RCC_APB2Periph_USART1
#define DEBUG_TX_PIN         GPIO_Pin_9
#define DEBUG_RX_PIN         GPIO_Pin_10
#define DEBUG_TX_PORT        GPIOA
#define DEBUG_RX_PORT        GPIOA
#define DEBUG_USART_IRQn     USART1_IRQn

#define DEBUG_BUFFER_SIZE    256

extern uint8_t  debug_rx_buffer[DEBUG_BUFFER_SIZE];
extern uint16_t debug_rx_index;

void Debug_USART_Init(void);
void Debug_SendString(char *str);
void Debug_SendData(uint8_t *data, uint16_t length);
void Debug_Printf(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif
