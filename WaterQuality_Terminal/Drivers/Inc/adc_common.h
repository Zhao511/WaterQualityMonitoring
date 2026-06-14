#ifndef __ADC_COMMON_H
#define __ADC_COMMON_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

/* ADC 超时保护 (~5ms@72MHz, 避免阻塞任务导致 WDG 复位) */
#define ADC_TIMEOUT_MAX 0xFFFF

/* ADC 参考电压与分辨率 */
#define ADC_VREF         3.3f
#define ADC_RESOLUTION   4096.0f

/* ADC 通道定义 */
#define ADC_CH_PH         ADC_Channel_0   /* PA0 - pH 传感器 */
#define ADC_CH_TDS        ADC_Channel_1   /* PA1 - TDS 传感器 */
#define ADC_CH_TURBIDITY  ADC_Channel_2   /* PA2 - 浊度传感器 */
#define ADC_CH_TEMP       ADC_Channel_3   /* PA3 - 温度传感器 */

/* 状态码 */
#define ADC_READ_OK        0
#define ADC_READ_TIMEOUT   1

void ADC_Common_Init(void);
uint8_t ADC_ReadChannel(uint8_t ADC_Channel, uint16_t *adc_value);

#ifdef __cplusplus
}
#endif

#endif
