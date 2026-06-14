#ifndef __STM32F10x_GPIO_H
#define __STM32F10x_GPIO_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

#define GPIO_Mode_AIN            ((uint16_t)0x0000)
#define GPIO_Mode_IN_FLOATING    ((uint16_t)0x0004)
#define GPIO_Mode_IPD            ((uint16_t)0x0028)
#define GPIO_Mode_IPU            ((uint16_t)0x0048)
#define GPIO_Mode_Out_OD         ((uint16_t)0x0014)
#define GPIO_Mode_Out_PP         ((uint16_t)0x0010)
#define GPIO_Mode_AF_OD          ((uint16_t)0x001C)
#define GPIO_Mode_AF_PP          ((uint16_t)0x0018)

#define GPIO_Speed_10MHz         ((uint16_t)0x0001)
#define GPIO_Speed_2MHz          ((uint16_t)0x0002)
#define GPIO_Speed_50MHz         ((uint16_t)0x0003)

#define GPIO_Remap_USART2        ((uint32_t)0x00000008)

void GPIO_Init(GPIO_TypeDef* GPIOx, GPIO_InitTypeDef* GPIO_InitStruct);
void GPIO_SetBits(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin);
void GPIO_ResetBits(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin);
void GPIO_PinRemapConfig(uint32_t GPIO_Remap, FunctionalState NewState);

#ifdef __cplusplus
}
#endif

#endif
