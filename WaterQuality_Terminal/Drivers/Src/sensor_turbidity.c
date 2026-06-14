#include "sensor_turbidity.h"
#include "adc_common.h"

/**
 * @brief  初始化浊度传感器
 * @note   ADC/GPIO 已在 ADC_Common_Init() 中统一配置
 */
void Turbidity_Sensor_Init(void)
{
    /* 浊度传感器无额外初始化需求 */
}

/**
 * @brief  读取浊度值（带超时保护）
 * @return 浊度值 (NTU)，超时返回 0
 */
float Turbidity_Sensor_Read(void)
{
    uint16_t adc_value;

    if (ADC_ReadChannel(ADC_CH_TURBIDITY, &adc_value) != ADC_READ_OK)
    {
        return 0.0f; /* 超时返回安全值 */
    }
    return Turbidity_Sensor_Calculate(adc_value);
}

/**
 * @brief  根据 ADC 值计算浊度
 * @param  adc_value: 12-bit ADC 采样值
 * @return 浊度值 (NTU)
 * @note   使用二次多项式拟合: NTU = -1120.4*V² + 5742.3*V - 4352.9
 */
float Turbidity_Sensor_Calculate(uint16_t adc_value)
{
    float voltage = (float)adc_value * ADC_VREF / ADC_RESOLUTION;
    float turbidity = -1120.4f * voltage * voltage
                      + 5742.3f * voltage
                      - 4352.9f;
    if (turbidity < 0.0f) turbidity = 0.0f;
    return turbidity;
}
