#ifndef __STM32F10x_USART_H
#define __STM32F10x_USART_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

#define USART_WordLength_8b      ((uint16_t)0x0000)
#define USART_WordLength_9b      ((uint16_t)0x1000)

#define USART_StopBits_1         ((uint16_t)0x0000)
#define USART_StopBits_0_5       ((uint16_t)0x1000)
#define USART_StopBits_2         ((uint16_t)0x2000)
#define USART_StopBits_1_5       ((uint16_t)0x3000)

#define USART_Parity_No          ((uint16_t)0x0000)
#define USART_Parity_Even        ((uint16_t)0x0400)
#define USART_Parity_Odd         ((uint16_t)0x0600)

#define USART_Mode_Rx            ((uint16_t)0x0004)
#define USART_Mode_Tx            ((uint16_t)0x0008)

#define USART_HardwareFlowControl_None    ((uint16_t)0x0000)

#define USART_IT_PE              ((uint16_t)0x0028)
#define USART_IT_TXE             ((uint16_t)0x0727)
#define USART_IT_RXNE            ((uint16_t)0x0525)
#define USART_IT_TC              ((uint16_t)0x0626)

#define USART_IRQn_USART1        37
#define USART_IRQn_USART2        38

void USART_Init(USART_TypeDef* USARTx, USART_InitTypeDef* USART_InitStruct);
void USART_Cmd(USART_TypeDef* USARTx, FunctionalState NewState);
void USART_SendData(USART_TypeDef* USARTx, uint16_t Data);
uint16_t USART_ReceiveData(USART_TypeDef* USARTx);
FlagStatus USART_GetFlagStatus(USART_TypeDef* USARTx, uint16_t USART_FLAG);
void USART_ClearFlag(USART_TypeDef* USARTx, uint16_t USART_FLAG);
void USART_ITConfig(USART_TypeDef* USARTx, uint16_t USART_IT, FunctionalState NewState);
FlagStatus USART_GetITStatus(USART_TypeDef* USARTx, uint16_t USART_IT);
void USART_ClearITPendingBit(USART_TypeDef* USARTx, uint16_t USART_IT);

#ifdef __cplusplus
}
#endif

#endif
