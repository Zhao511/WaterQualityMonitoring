#ifndef __SENSOR_TDS_H
#define __SENSOR_TDS_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

/* TDS 传感器校准系数 */
#define TDS_K_VALUE 1.0f

void     TDS_Sensor_Init(void);
float    TDS_Sensor_Read(float temperature);
uint16_t TDS_Sensor_ReadRaw(void);
float    TDS_Sensor_Calculate(uint16_t adc_value, float temperature);

#ifdef __cplusplus
}
#endif

#endif
