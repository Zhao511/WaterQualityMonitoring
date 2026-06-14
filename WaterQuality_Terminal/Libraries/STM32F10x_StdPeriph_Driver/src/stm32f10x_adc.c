#include "stm32f10x_adc.h"

void ADC_Init(ADC_TypeDef* ADCx, ADC_InitTypeDef* ADC_InitStruct)
{
  uint32_t tmpreg1 = 0, tmpreg2 = 0;
  
  tmpreg1 = ADCx->CR1;
  tmpreg1 &= (uint32_t)0xF0FFFFFF;
  tmpreg1 |= ADC_InitStruct->ADC_Mode;
  ADCx->CR1 = tmpreg1;
  
  tmpreg2 = ADCx->CR2;
  tmpreg2 &= (uint32_t)0xFFF0FFFF;
  tmpreg2 |= (uint32_t)(ADC_InitStruct->ADC_DataAlign << 11);
  ADCx->CR2 = tmpreg2;
  
  tmpreg2 &= (uint32_t)0xFFFFFFF3;
  tmpreg2 |= (uint32_t)(ADC_InitStruct->ADC_ExternalTrigConv << 17);
  ADCx->CR2 = tmpreg2;
  
  tmpreg1 = ADCx->CR1;
  tmpreg1 &= (uint32_t)0xFDFFFFFF;
  tmpreg1 |= (uint32_t)(ADC_InitStruct->ADC_ScanConvMode << 8);
  ADCx->CR1 = tmpreg1;
  
  tmpreg2 = ADCx->CR2;
  tmpreg2 &= (uint32_t)0xFFFFFFFE;
  tmpreg2 |= (uint32_t)ADC_InitStruct->ADC_ContinuousConvMode;
  ADCx->CR2 = tmpreg2;
  
  ADCx->SQR1 &= (uint32_t)0xFFF0FFFF;
  ADCx->SQR1 |= (uint32_t)((ADC_InitStruct->ADC_NbrOfChannel - 1) << 20);
}

void ADC_Cmd(ADC_TypeDef* ADCx, FunctionalState NewState)
{
  if (NewState != DISABLE)
  {
    ADCx->CR2 |= (uint32_t)0x00000001;
  }
  else
  {
    ADCx->CR2 &= (uint32_t)0xFFFFFFFE;
  }
}

void ADC_ResetCalibration(ADC_TypeDef* ADCx)
{
  ADCx->CR2 |= (uint32_t)0x00000002;
}

FlagStatus ADC_GetResetCalibrationStatus(ADC_TypeDef* ADCx)
{
  FlagStatus bitstatus = RESET;
  if ((ADCx->CR2 & (uint32_t)0x00000002) != (uint32_t)0x00)
  {
    bitstatus = SET;
  }
  return bitstatus;
}

void ADC_StartCalibration(ADC_TypeDef* ADCx)
{
  ADCx->CR2 |= (uint32_t)0x00000004;
}

FlagStatus ADC_GetCalibrationStatus(ADC_TypeDef* ADCx)
{
  FlagStatus bitstatus = RESET;
  if ((ADCx->CR2 & (uint32_t)0x00000004) != (uint32_t)0x00)
  {
    bitstatus = SET;
  }
  return bitstatus;
}

void ADC_SoftwareStartConvCmd(ADC_TypeDef* ADCx, FunctionalState NewState)
{
  if (NewState != DISABLE)
  {
    ADCx->CR2 |= (uint32_t)0x00400000;
  }
  else
  {
    ADCx->CR2 &= (uint32_t)0xFFBFFFFF;
  }
}

FlagStatus ADC_GetFlagStatus(ADC_TypeDef* ADCx, uint8_t ADC_FLAG)
{
  FlagStatus bitstatus = RESET;
  if ((ADCx->SR & ADC_FLAG) != (uint8_t)0x00)
  {
    bitstatus = SET;
  }
  return bitstatus;
}

uint16_t ADC_GetConversionValue(ADC_TypeDef* ADCx)
{
  return (uint16_t)ADCx->DR;
}

void ADC_RegularChannelConfig(ADC_TypeDef* ADCx, uint8_t ADC_Channel, uint8_t Rank, uint32_t ADC_SampleTime)
{
  uint32_t tmpreg = 0;
  
  if (Rank == 1)
  {
    ADCx->SQR3 &= (uint32_t)0xFFFFFFE0;
    ADCx->SQR3 |= (uint32_t)ADC_Channel;
  }
  
  tmpreg = ADCx->SMPR2;
  tmpreg &= ~(uint32_t)(0x07 << (3 * ADC_Channel));
  tmpreg |= (uint32_t)(ADC_SampleTime << (3 * ADC_Channel));
  ADCx->SMPR2 = tmpreg;
}
