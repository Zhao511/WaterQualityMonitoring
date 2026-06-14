#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"
#include "stm32f10x_rcc.h"

/* ========== 系统函数声明 ========== */
void SystemClock_Config(void);
void GPIO_Config(void);

#ifdef __cplusplus
}
#endif

#endif
