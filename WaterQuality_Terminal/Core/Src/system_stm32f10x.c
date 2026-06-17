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
    volatile uint32_t timeout;

    /* ---- 标准复位序列 (来自 ST 官方 SystemInit) ---- */
    RCC->CR |= (uint32_t)0x00000001;          /* HSION */

    /* 复位 CFGR: SW, HPRE, PPRE1, PPRE2, ADCPRE, MCO */
    RCC->CFGR &= (uint32_t)0xF8FF0000;
    /* 先清除 ADCPRE 位，再设置为 PCLK2/6 (12MHz) */
    RCC->CFGR &= ~((uint32_t)(3UL << 14));       /* 清 ADCPRE[1:0] */
    RCC->CFGR |=  (uint32_t)(2UL << 14);         /* ADCPRE = PCLK2/6 (12MHz), ADC 最大 14MHz */

    /* 关 HSE, CSS, PLL */
    RCC->CR &= (uint32_t)0xFEF6FFFF;

    /* 清 HSEBYP */
    RCC->CR &= (uint32_t)0xFFFBFFFF;

    /* 清 PLLSRC, PLLXTPRE, PLLMUL, USBPRE */
    RCC->CFGR &= (uint32_t)0xFF80FFFF;

    /* 关所有 RCC 中断并清 pending */
    RCC->CIR = 0x009F0000;

    /* ---- Flash: Prefetch + 2 wait states ---- */
    FLASH->ACR |= 0x00000010;                 /* PRFTBE */
    FLASH->ACR &= ~((uint32_t)0x00000007);    /* 清 LATENCY */
    FLASH->ACR |= 0x00000002;                 /* LATENCY=2 (72MHz) */

    /* ---- 总线预分频 ---- */
    RCC->CFGR |= (uint32_t)0x00000400;        /* PPRE1 = HCLK/2 */

    /* ---- 尝试 HSE ---- */
    RCC->CR |= (uint32_t)0x00010000;          /* HSEON */
    timeout = 0;
    while ((RCC->CR & RCC_CR_HSERDY) == 0) {
        if (++timeout >= 0x10000) {
            RCC->CR &= ~((uint32_t)0x00010000); /* 关 HSE */
            goto use_hsi;
        }
    }

    /* ---- HSE OK → PLL ×9 = 72MHz ---- */
    RCC->CFGR |= (uint32_t)0x001D0000;        /* PLLSRC=HSE, PLLMUL=9 */
    /* 设置 ADCPRE = PCLK2/6 (12MHz) */
    RCC->CFGR &= ~((uint32_t)(3UL << 14));    /* 清 ADCPRE[1:0] */
    RCC->CFGR |=  (uint32_t)(2UL << 14);      /* ADCPRE = PCLK2/6 */
    RCC->CR |= (uint32_t)0x01000000;          /* PLLON */
    timeout = 0;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0) {
        if (++timeout >= 0x10000) goto use_hsi;
    }

    /* 切换到 PLL */
    RCC->CFGR &= ~((uint32_t)0x00000003);
    RCC->CFGR |= (uint32_t)0x00000002;        /* SW = PLL */
    timeout = 0;
    while ((RCC->CFGR & RCC_CFGR_SWS) != 0x00000008) {
        if (++timeout >= 0x10000) goto use_hsi;
    }
    SystemCoreClock = 72000000;
    return;

use_hsi:
    /* 回退到 HSI + PLL → 72MHz (适配无外部晶振的开发板) */
    RCC->CR &= ~((uint32_t)0x01010000);       /* 关 PLL + HSE */
    
    /* HSI/2 = 4MHz, PLL ×18 = 72MHz */
    RCC->CFGR &= ~((uint32_t)0xFF80FFFF);     /* 清 PLLSRC, PLLMUL */
    RCC->CFGR |= (uint32_t)0x00C00000;        /* PLLSRC=HSI/2, PLLMUL=18 */
    
    /* 确保 ADCPRE = PCLK2/6 (12MHz) */
    RCC->CFGR &= ~((uint32_t)(3UL << 14));    /* 清 ADCPRE[1:0] */
    RCC->CFGR |=  (uint32_t)(2UL << 14);      /* ADCPRE = PCLK2/6 */
    
    RCC->CR |= (uint32_t)0x01000000;          /* PLLON */
    timeout = 0;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0) {
        if (++timeout >= 0x10000) {
            /* PLL启动失败，回退到HSI 8MHz */
            RCC->CR &= ~((uint32_t)0x01000000);
            RCC->CFGR &= ~((uint32_t)0x00000003);
            SystemCoreClock = 8000000;
            return;
        }
    }
    
    /* 切换到 PLL */
    RCC->CFGR &= ~((uint32_t)0x00000003);
    RCC->CFGR |= (uint32_t)0x00000002;        /* SW = PLL */
    timeout = 0;
    while ((RCC->CFGR & RCC_CFGR_SWS) != 0x00000008) {
        if (++timeout >= 0x10000) {
            RCC->CFGR &= ~((uint32_t)0x00000003);
            SystemCoreClock = 8000000;
            return;
        }
    }
    SystemCoreClock = 72000000;
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
