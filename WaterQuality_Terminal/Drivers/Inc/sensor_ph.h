#ifndef __SENSOR_PH_H
#define __SENSOR_PH_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

/* pH 传感器校准参数 */
#define PH_VREF          3.3f
#define PH_ADC_RESOLUTION 4096.0f

void  PH_Sensor_Init(void);
float PH_Sensor_Read(void);
float PH_Sensor_Calculate(uint16_t adc_value);

#ifdef __cplusplus
}
#endif

#endif
