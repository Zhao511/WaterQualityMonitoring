#include "sensor_ph.h"
#include "adc_common.h"

/**
 * @brief  初始化 pH 传感器
 * @note   ADC/GPIO 已在 ADC_Common_Init() 中统一配置，
 *         此处仅做传感器特有的初始化操作
 */
void PH_Sensor_Init(void)
{
    /* pH 传感器无额外初始化需求，ADC 由 ADC_Common_Init 统一管理 */
}

/**
 * @brief  读取 pH 值（带超时保护）
 * @return pH 值，超时返回 7.0（中性）
 */
float PH_Sensor_Read(void)
{
    uint16_t adc_value;

    if (ADC_ReadChannel(ADC_CH_PH, &adc_value) != ADC_READ_OK)
    {
        return 7.0f; /* 超时返回中性值 */
    }
    return PH_Sensor_Calculate(adc_value);
}

/**
 * @brief  根据 ADC 值计算 pH
 * @param  adc_value: 12-bit ADC 采样值
 * @return pH 值
 * @note   公式: pH = 7.0 + (2.5 - Vout) / 0.18
 *         其中 Vout = adc_value * 3.3 / 4096
 */
float PH_Sensor_Calculate(uint16_t adc_value)
{
    float voltage = (float)adc_value * PH_VREF / PH_ADC_RESOLUTION;
    float ph_value = 7.0f + (2.5f - voltage) / 0.18f;
    return ph_value;
}
