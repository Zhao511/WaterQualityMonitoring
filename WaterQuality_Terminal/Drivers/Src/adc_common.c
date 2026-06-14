#include "adc_common.h"
#include "stm32f10x_adc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

/**
 * @brief  统一初始化 ADC1 及所有传感器 GPIO (PA0~PA3)
 * @note   将各传感器 Init 中重复的 RCC/GPIO 配置集中于此
 *         pH=PA0, TDS=PA1, Turbidity=PA2, Temp=PA3
 */
void ADC_Common_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    ADC_InitTypeDef  ADC_InitStructure;

    /* 使能 GPIOA 与 ADC1 时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOA, ENABLE);

    /* 配置 PA0~PA3 为模拟输入 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 配置 ADC1 工作模式 */
    ADC_InitStructure.ADC_Mode               = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode       = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign          = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    /* 使能并校准 ADC1 */
    ADC_Cmd(ADC1, ENABLE);

    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));
}

/**
 * @brief  读取指定 ADC 通道的数值（带超时保护）
 * @param  ADC_Channel: ADC 通道号
 * @param  adc_value:   输出 ADC 数值的指针
 * @return ADC_READ_OK(0) 成功, ADC_READ_TIMEOUT(1) 超时
 */
uint8_t ADC_ReadChannel(uint8_t ADC_Channel, uint16_t *adc_value)
{
    uint32_t timeout = ADC_TIMEOUT_MAX;

    /* 配置通道 */
    ADC_RegularChannelConfig(ADC1, ADC_Channel, 1, ADC_SampleTime_55Cycles5);

    /* 启动单次转换 */
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    /* 等待转换完成（带超时保护） */
    while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC))
    {
        if (--timeout == 0)
        {
            return ADC_READ_TIMEOUT;
        }
    }

    *adc_value = ADC_GetConversionValue(ADC1);
    return ADC_READ_OK;
}
