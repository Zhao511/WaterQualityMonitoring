/**
 * ============================================================
 * 系统初始化 (FreeRTOS 版本)
 * ============================================================
 * - SystemInit() — 时钟配置 (HSE + PLL → 72MHz)
 * - SystemCoreClockUpdate() — 动态查询当前时钟频率
 *
 * 注意: SysTick 初始化/延时函数已移除，
 *       FreeRTOS 内核接管 SysTick。
 *       如需微秒级忙等延时，请使用 DWT 或 TIM 外设。
 * ============================================================
 */

#include "stm32f10x.h"
#include "stm32f10x_rcc.h"

#define SYSCLK_FREQ_72MHz  72000000

uint32_t SystemCoreClock = SYSCLK_FREQ_72MHz;

const uint8_t AHBPrescTable[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
const uint8_t APBPrescTable[8]  = {0, 0, 0, 0, 1, 2, 3, 4};

/**
 * @brief  系统初始化 — 配置 HSE + PLL → 72MHz
 */
void SystemInit(void)
{
    /* HSI ON */
    RCC->CR |= (uint32_t)0x00000001;

    /* Flash: 2 wait states */
    FLASH->ACR |= FLASH_ACR_LATENCY_2;

    /* HCLK = SYSCLK, PCLK2 = HCLK, PCLK1 = HCLK/2 */
    RCC->CFGR &= (uint32_t)0xF8FF0000;
    RCC->CFGR |= (uint32_t)0x00000400;

    /* HSE ON */
    RCC->CR &= (uint32_t)0xFEF6FFFF;
    RCC->CR |= (uint32_t)0x00010000;
    while ((RCC->CR & RCC_CR_HSERDY) == 0);

    /* PLL: HSE × 9 = 72MHz */
    RCC->CFGR &= (uint32_t)0xFF0FFFFF;
    RCC->CFGR |= (uint32_t)0x001D0000;

    /* PLL ON */
    RCC->CR |= (uint32_t)0x01000000;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0);

    /* PLL as system clock */
    RCC->CFGR &= (uint32_t)0x00FFFFFF;
    RCC->CFGR |= (uint32_t)0x00000002;
    while ((RCC->CFGR & RCC_CFGR_SWS) != 0x00000008);
}

/**
 * @brief  更新 SystemCoreClock 变量
 */
void SystemCoreClockUpdate(void)
{
    uint32_t tmp = 0, pllmull = 0, pllsource = 0;

    tmp = RCC->CFGR & RCC_CFGR_SWS;

    switch (tmp)
    {
        case 0x00:
            SystemCoreClock = HSI_VALUE;
            break;
        case 0x04:
            SystemCoreClock = HSE_VALUE;
            break;
        case 0x08:
            pllmull  = (RCC->CFGR & RCC_CFGR_PLLMULL) >> 18;
            pllsource = RCC->CFGR & RCC_CFGR_PLLSRC;
            pllmull += 2;
            if (pllsource == 0x00)
                SystemCoreClock = (HSI_VALUE >> 1) * pllmull;
            else
                SystemCoreClock = (HSE_VALUE >> 1) * pllmull;
            break;
        default:
            SystemCoreClock = HSI_VALUE;
            break;
    }

    tmp = AHBPrescTable[((RCC->CFGR >> 4) & 0x0F)];
    SystemCoreClock >>= tmp;
}
