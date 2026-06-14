#include "sensor_temp.h"
#include "adc_common.h"
#include <math.h>

/**
 * @brief  初始化温度传感器
 * @note   ADC/GPIO 已在 ADC_Common_Init() 中统一配置
 */
void Temp_Sensor_Init(void)
{
    /* 温度传感器无额外初始化需求 */
}

/**
 * @brief  读取温度值（带超时保护）
 * @return 温度值 (°C)，超时返回 25.0
 */
float Temp_Sensor_Read(void)
{
    uint16_t adc_value;

    if (ADC_ReadChannel(ADC_CH_TEMP, &adc_value) != ADC_READ_OK)
    {
        return 25.0f; /* 超时返回常温 */
    }
    return Temp_Sensor_Calculate(adc_value);
}

/**
 * @brief  根据 ADC 值计算温度
 * @param  adc_value: 12-bit ADC 采样值
 * @return 温度值 (°C)
 * @note   NTC 热敏电阻分压法
 *         R_ntc = R_pullup * Vout / (Vref - Vout)
 *         T(K) = 1 / (1/T0 + ln(R/R0)/B)
 *         其中 T0 = 298.15K (25°C), R0 = 10kΩ, B = 3950
 */
float Temp_Sensor_Calculate(uint16_t adc_value)
{
    float voltage = (float)adc_value * ADC_VREF / ADC_RESOLUTION;

    /* 防止除零 */
    if (voltage >= ADC_VREF)
    {
        voltage = ADC_VREF - 0.001f;
    }

    float ntc_resistance = NTC_R_PULLUP * voltage / (ADC_VREF - voltage);
    float temperature_k = 1.0f / (1.0f / TEMP_REF_K
                          + logf(ntc_resistance / NTC_R25) / NTC_B_VALUE);
    return temperature_k - 273.15f; /* 开尔文 → 摄氏度 */
}
