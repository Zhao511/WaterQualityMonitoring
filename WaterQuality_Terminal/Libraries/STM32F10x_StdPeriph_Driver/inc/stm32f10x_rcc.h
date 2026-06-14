#ifndef __STM32F10x_RCC_H
#define __STM32F10x_RCC_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

#define RCC_HSE_ON               ((uint32_t)0x00010000)
#define RCC_HSE_OFF              ((uint32_t)0x00000000)
#define RCC_CR_HSERDY            ((uint32_t)0x00020000)
#define RCC_PLL_ON               ((uint32_t)0x01000000)
#define RCC_CR_PLLRDY            ((uint32_t)0x02000000)

#define RCC_CFGR_SWS             ((uint32_t)0x0000000C)
#define RCC_CFGR_SWS_PLL         ((uint32_t)0x00000008)

#define RCC_SYSCLKSource_PLLCLK  ((uint32_t)0x00000002)

#define RCC_PLLSource_HSE_Div1   ((uint32_t)0x00010000)
#define RCC_PLLMul_9             ((uint32_t)0x00000700)

#define RCC_SYSCLK_Div1          ((uint32_t)0x00000000)
#define RCC_HCLK_Div1            ((uint32_t)0x00000000)
#define RCC_PCLK1_Div2           ((uint32_t)0x00001000)
#define RCC_PCLK2_Div1           ((uint32_t)0x00000000)

#define RCC_FLAG_HSERDY          RCC_CR_HSERDY
#define RCC_FLAG_PLLRDY          RCC_CR_PLLRDY

void RCC_DeInit(void);
void RCC_HSEConfig(uint32_t RCC_HSE);
ErrorStatus RCC_WaitForHSEStartUp(void);
void RCC_HCLKConfig(uint32_t RCC_SYSCLK);
void RCC_PCLK1Config(uint32_t RCC_HCLK);
void RCC_PCLK2Config(uint32_t RCC_HCLK);
void RCC_PLLConfig(uint32_t RCC_PLLSource, uint32_t RCC_PLLMul);
void RCC_PLLCmd(FunctionalState NewState);
void RCC_SYSCLKConfig(uint32_t RCC_SYSCLKSource);
uint8_t RCC_GetSYSCLKSource(void);
void RCC_APB2PeriphClockCmd(uint32_t RCC_APB2Periph, FunctionalState NewState);
void RCC_APB1PeriphClockCmd(uint32_t RCC_APB1Periph, FunctionalState NewState);
FlagStatus RCC_GetFlagStatus(uint32_t RCC_FLAG);

#ifdef __cplusplus
}
#endif

#endif
