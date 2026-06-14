#ifndef __SENSOR_TURBIDITY_H
#define __SENSOR_TURBIDITY_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

void  Turbidity_Sensor_Init(void);
float Turbidity_Sensor_Read(void);
float Turbidity_Sensor_Calculate(uint16_t adc_value);

#ifdef __cplusplus
}
#endif

#endif
