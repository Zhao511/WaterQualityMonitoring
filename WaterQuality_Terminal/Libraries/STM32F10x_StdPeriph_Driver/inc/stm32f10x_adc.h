#ifndef __STM32F10x_ADC_H
#define __STM32F10x_ADC_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

#define ADC_Mode_Independent               ((uint32_t)0x00000000)

#define ADC_SampleTime_1Cycles5            ((uint32_t)0x00000000)
#define ADC_SampleTime_7Cycles5            ((uint32_t)0x00000001)
#define ADC_SampleTime_13Cycles5           ((uint32_t)0x00000002)
#define ADC_SampleTime_28Cycles5           ((uint32_t)0x00000003)
#define ADC_SampleTime_41Cycles5           ((uint32_t)0x00000004)
#define ADC_SampleTime_55Cycles5           ((uint32_t)0x00000005)
#define ADC_SampleTime_71Cycles5           ((uint32_t)0x00000006)
#define ADC_SampleTime_239Cycles5          ((uint32_t)0x00000007)

#define ADC_ExternalTrigConv_None          ((uint32_t)0x00000000)

#define ADC_DataAlign_Right                ((uint32_t)0x00000000)
#define ADC_DataAlign_Left                 ((uint32_t)0x00000800)

#define ADC_Channel_0                      ((uint8_t)0x00)
#define ADC_Channel_1                      ((uint8_t)0x01)
#define ADC_Channel_2                      ((uint8_t)0x02)
#define ADC_Channel_3                      ((uint8_t)0x03)

void ADC_Init(ADC_TypeDef* ADCx, ADC_InitTypeDef* ADC_InitStruct);
void ADC_Cmd(ADC_TypeDef* ADCx, FunctionalState NewState);
void ADC_ResetCalibration(ADC_TypeDef* ADCx);
FlagStatus ADC_GetResetCalibrationStatus(ADC_TypeDef* ADCx);
void ADC_StartCalibration(ADC_TypeDef* ADCx);
FlagStatus ADC_GetCalibrationStatus(ADC_TypeDef* ADCx);
void ADC_SoftwareStartConvCmd(ADC_TypeDef* ADCx, FunctionalState NewState);
FlagStatus ADC_GetFlagStatus(ADC_TypeDef* ADCx, uint8_t ADC_FLAG);
uint16_t ADC_GetConversionValue(ADC_TypeDef* ADCx);
void ADC_RegularChannelConfig(ADC_TypeDef* ADCx, uint8_t ADC_Channel, uint8_t Rank, uint32_t ADC_SampleTime);

#ifdef __cplusplus
}
#endif

#endif
