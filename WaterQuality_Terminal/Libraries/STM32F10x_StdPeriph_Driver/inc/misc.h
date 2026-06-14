#ifndef __MISC_H
#define __MISC_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

void NVIC_Init(NVIC_InitTypeDef* NVIC_InitStruct);

#ifdef __cplusplus
}
#endif

#endif
