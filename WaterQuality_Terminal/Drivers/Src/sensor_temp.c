#include "sensor_temp.h"
#include "adc_common.h"
#include "iot_model.h"
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
        return IOT_TEMP_DEFAULT; /* 超时返回常温 */
    }
    return Temp_Sensor_Calculate(adc_value);
}

/**
 * @brief  根据 ADC 值计算温度
 * @param  adc_value: 12-bit ADC 采样值
 * @return 温度值 (°C)，异常时返回 sentinel 值:
 *         -99.0 = NTC 开路 (Vout ≈ VREF)
 *         +99.0 = NTC 短路 (Vout ≈ GND)
 * @note   NTC 热敏电阻分压法
 *         R_ntc = R_pullup * Vout / (Vref - Vout)
 *         T(K) = 1 / (1/T0 + ln(R/R0)/B)
 *         其中 T0 = 298.15K (25°C), R0 = 10kΩ, B = 3950
 */
float Temp_Sensor_Calculate(uint16_t adc_value)
{
    float voltage = (float)adc_value * ADC_VREF / ADC_RESOLUTION;

    /* 开路检测: Vout > 99% VREF → NTC 断开 → 返回故障标记 */
    if (voltage >= ADC_VREF * 0.99f)
    {
        return -99.0f;  /* Sentinel: 传感器开路故障 */
    }

    /* 短路检测: Vout < 1% VREF → NTC 短路 → 返回故障标记 */
    if (voltage <= ADC_VREF * 0.01f)
    {
        return 99.0f;   /* Sentinel: 传感器短路故障 */
    }

    float ntc_resistance = NTC_R_PULLUP * voltage / (ADC_VREF - voltage);
    float temperature_k = 1.0f / (1.0f / TEMP_REF_K
                          + logf(ntc_resistance / NTC_R25) / NTC_B_VALUE);
    float temp_c = temperature_k - 273.15f; /* 开尔文 → 摄氏度 */

    /* 末级钳位: NTC 物理极限范围，防止浮点异常值 */
    if (temp_c < -40.0f) temp_c = -40.0f;
    if (temp_c > 125.0f) temp_c = 125.0f;

    return temp_c;
}
