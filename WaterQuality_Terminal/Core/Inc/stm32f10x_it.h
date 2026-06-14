#ifndef __STM32F10x_IT_H
#define __STM32F10x_IT_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"
#include "stm32f10x_usart.h"

/* ========== UART 中断服务函数声明 ==========
 * USART1 — 调试输出 (PA9/PA10 → 板载 USB-UART)
 * USART2 — GPS 模块  (PD5/PD6 重映射)
 * USART3 — LoRa 模块 (PB10/PB11)
 * SysTick/PendSV/SVC — FreeRTOS 内核接管
 */
void USART1_IRQHandler(void);  /* Debug */
void USART2_IRQHandler(void);  /* GPS   */
void USART3_IRQHandler(void);  /* LoRa  */

#ifdef __cplusplus
}
#endif

#endif
