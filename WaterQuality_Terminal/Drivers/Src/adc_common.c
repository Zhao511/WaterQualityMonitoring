#include "adc_common.h"
#include "stm32f10x_adc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "usart_debug.h"

#include "FreeRTOS.h"
#include "semphr.h"

/* ADC1 互斥锁 (main.c 中创建), 保护通道配置+转换的原子性 */
extern SemaphoreHandle_t xADCMutex;

/**
  * 函    数：ADC及GPIO初始化（参考江科大7-1 AD单通道教程 + ST官方V3.5.0库）
  * 参    数：无
  * 返 回 值：无
  * 说    明：统一初始化ADC1及传感器GPIO（PA0~PA3）
  *          pH=PA0, TDS=PA1, Temp=PA3
  */
void ADC_Common_Init(void)
{
	/*开启时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);	//开启ADC1的时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);	//开启GPIOA的时钟

	/*设置ADC时钟*/
	RCC_ADCCLKConfig(RCC_PCLK2_Div6);						//选择时钟6分频，ADCCLK = 72MHz / 6 = 12MHz

	Debug_Printf("  [ADC] RCC...\r\n");

	/*GPIO初始化*/
	{
		GPIO_InitTypeDef GPIO_InitStructure;				//定义结构体变量
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;		//模式，选择模拟输入
		GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
		GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
		GPIO_Init(GPIOA, &GPIO_InitStructure);				//将PA0~PA3初始化为模拟输入
	}

	Debug_Printf("  [ADC] GPIO...\r\n");

	/*复位ADC1到默认值（标准库步骤）*/
	ADC_DeInit(ADC1);

	/*规则组通道配置（放在Init之前，对齐江科大教程顺序）*/
	ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_55Cycles5);	//PA0, 序列1
	ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 2, ADC_SampleTime_55Cycles5);	//PA1, 序列2
	ADC_RegularChannelConfig(ADC1, ADC_Channel_3, 3, ADC_SampleTime_55Cycles5);	//PA3, 序列3

	/*ADC初始化*/
	{
		ADC_InitTypeDef ADC_InitStructure;					//定义结构体变量
		ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;	//模式，选择独立模式，即单独使用ADC1
		ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;	//数据对齐，选择右对齐
		ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;	//外部触发，使用软件触发，不需要外部触发
		ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;	//连续转换，失能，每转换一次规则组序列后停止
		ADC_InitStructure.ADC_ScanConvMode = DISABLE;		//扫描模式，失能，只转换规则组的序列1这一个位置
		ADC_InitStructure.ADC_NbrOfChannel = 3;				//通道数，为3
		ADC_Init(ADC1, &ADC_InitStructure);					//将结构体变量交给ADC_Init，配置ADC1
	}

	Debug_Printf("  [ADC] Init...\r\n");

	/*ADC使能*/
	ADC_Cmd(ADC1, ENABLE);									//使能ADC1，ADC开始运行

	/*ADC校准（标准库流程：无需超时保护）*/
	{
		Debug_Printf("  [ADC] Calibrating...\r\n");
		ADC_ResetCalibration(ADC1);							//复位校准
		while (ADC_GetResetCalibrationStatus(ADC1) == SET);	//等待复位校准完成
		ADC_StartCalibration(ADC1);							//开始校准
		while (ADC_GetCalibrationStatus(ADC1) == SET);		//等待校准完成
		Debug_Printf("  [ADC] Calibration OK\r\n");
	}

	Debug_Printf("  [ADC] Done\r\n");

	/* 诊断: 打印 ADC 关键寄存器, 确认外设存活 */
	Debug_Printf("  [ADC Diag] CR2=0x%08lX SR=0x%08lX\r\n",
	             (unsigned long)ADC1->CR2, (unsigned long)ADC1->SR);
	Debug_Printf("  [ADC Diag] ADON=%lu CONT=%lu CAL=%lu RSTCAL=%lu "
	             "EXTTRIG=%lu EXTSEL=%lu\r\n",
	             (unsigned long)((ADC1->CR2 >> 0) & 1),
	             (unsigned long)((ADC1->CR2 >> 1) & 1),
	             (unsigned long)((ADC1->CR2 >> 2) & 1),
	             (unsigned long)((ADC1->CR2 >> 3) & 1),
	             (unsigned long)((ADC1->CR2 >> 20) & 1),
	             (unsigned long)((ADC1->CR2 >> 17) & 7));
}

/**
  * 函    数：读取指定ADC通道的数值（带超时保护 + 互斥锁）
  * 参    数：ADC_Channel  ADC通道号
  * 参    数：adc_value   输出ADC数值的指针
  * 返 回 值：ADC_READ_OK(0) 成功, ADC_READ_TIMEOUT(1) 超时
  * 说    明：互斥锁保护ADC1外设，防止vSensorTask(Prio3)与
  *          vIOTTask(Prio2, IOT_Collect_All_Sensors)竞争。
  *          200ms超时：正常转换约6us，200ms覆盖极端阻塞场景。
  */
uint8_t ADC_ReadChannel(uint8_t ADC_Channel, uint16_t *adc_value)
{
	uint32_t timeout = ADC_TIMEOUT_MAX;
	uint8_t  result;

	/* 互斥锁：保护通道配置 + 启动转换 + 等待EOC的原子性 */
	if (xADCMutex != NULL) {
		if (xSemaphoreTake(xADCMutex, pdMS_TO_TICKS(200)) != pdPASS) {
			return ADC_READ_TIMEOUT;
		}
	}

	/* 配置通道 + 采样时间 */
	ADC_RegularChannelConfig(ADC1, ADC_Channel, 1, ADC_SampleTime_55Cycles5);

	/* 软件触发AD转换一次 */
	ADC_SoftwareStartConvCmd(ADC1, ENABLE);

	/* 等待EOC标志位，即等待AD转换结束，带超时保护 */
	while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
	{
		if (--timeout == 0)
		{
			result = ADC_READ_TIMEOUT;
			goto exit;
		}
	}

	/* 读数据寄存器，得到AD转换的结果 */
	*adc_value = ADC_GetConversionValue(ADC1);
	result = ADC_READ_OK;

exit:
	if (xADCMutex != NULL) {
		xSemaphoreGive(xADCMutex);
	}
	return result;
}
