#include "sensor_tds.h"
#include "adc_common.h"
#include <math.h>

/**
 * @brief  初始化 TDS 传感器
 * @note   ADC/GPIO 已在 ADC_Common_Init() 中统一配置
 */
void TDS_Sensor_Init(void)
{
    /* TDS 传感器无额外初始化需求 */
}

/**
 * @brief  读取 TDS 原始 ADC 值（带超时保护）
 * @return ADC 原始值，超时返回 0
 */
uint16_t TDS_Sensor_ReadRaw(void)
{
    uint16_t adc_value;

    if (ADC_ReadChannel(ADC_CH_TDS, &adc_value) != ADC_READ_OK)
    {
        return 0;
    }
    return adc_value;
}

/**
 * @brief  读取 TDS 值（带温度补偿）
 * @param  temperature: 当前水温 °C
 * @return TDS 值 (ppm)，超时返回 0
 */
float TDS_Sensor_Read(float temperature)
{
    uint16_t adc_value;

    if (ADC_ReadChannel(ADC_CH_TDS, &adc_value) != ADC_READ_OK)
    {
        return 0.0f; /* ADC 读取超时 */
    }
    return TDS_Sensor_Calculate(adc_value, temperature);
}

/**
 * @brief  根据 ADC 值和温度计算 TDS
 * @param  adc_value:   12-bit ADC 采样值
 * @param  temperature: 当前水温 °C
 * @return TDS 值 (ppm)
 * @note   先做温度补偿，再用三次多项式拟合
 *         V_comp = V_raw / (1 + 0.02 * (T - 25))
 *         TDS = (133.42*V³ - 255.86*V² + 857.39*V) * K
 */
float TDS_Sensor_Calculate(uint16_t adc_value, float temperature)
{
    float voltage = (float)adc_value * ADC_VREF / ADC_RESOLUTION;

    /* 温度补偿系数 */
    float compensation = 1.0f + 0.02f * (temperature - 25.0f);
    float compensated_v = voltage / compensation;

    /* 三次多项式拟合 TDS 值 */
    float tds_value = (133.42f * compensated_v * compensated_v * compensated_v
                     - 255.86f * compensated_v * compensated_v
                     + 857.39f * compensated_v) * TDS_K_VALUE;

    if (tds_value < 0.0f) tds_value = 0.0f;
    return tds_value;
}
