#ifndef __SENSOR_TEMP_H
#define __SENSOR_TEMP_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

/* NTC 热敏电阻参数 */
#define NTC_B_VALUE   3950.0f
#define NTC_R25       10000.0f
#define NTC_R_PULLUP  10000.0f
#define TEMP_REF_K    298.15f  /* 25°C 开尔文 */

void  Temp_Sensor_Init(void);
float Temp_Sensor_Read(void);
float Temp_Sensor_Calculate(uint16_t adc_value);

#ifdef __cplusplus
}
#endif

#endif
