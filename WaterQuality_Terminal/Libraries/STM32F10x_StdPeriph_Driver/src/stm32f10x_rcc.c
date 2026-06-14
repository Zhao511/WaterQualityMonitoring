#include "stm32f10x_rcc.h"

void RCC_DeInit(void)
{
  RCC->CR |= (uint32_t)0x00000001;
  RCC->CFGR &= (uint32_t)0xF8FF0000;
  RCC->CR &= (uint32_t)0xFEF6FFFF;
  RCC->CR &= (uint32_t)0xFFFBFFFF;
  RCC->CFGR &= (uint32_t)0xFF0FFFFF;
  RCC->CIR = (uint32_t)0x00000000;
}

void RCC_HSEConfig(uint32_t RCC_HSE)
{
  if (RCC_HSE == RCC_HSE_ON)
  {
    RCC->CR |= RCC_HSE_ON;
  }
  else
  {
    RCC->CR &= ~RCC_HSE_ON;
  }
}

ErrorStatus RCC_WaitForHSEStartUp(void)
{
  uint32_t timeout = 0xFFFF;
  while ((RCC->CR & RCC_CR_HSERDY) == 0 && timeout > 0)
  {
    timeout--;
  }
  if ((RCC->CR & RCC_CR_HSERDY) != 0)
  {
    return SUCCESS;
  }
  else
  {
    return ERROR;
  }
}

void RCC_HCLKConfig(uint32_t RCC_SYSCLK)
{
  RCC->CFGR &= (uint32_t)0xFFFFFFF8;
  RCC->CFGR |= RCC_SYSCLK;
}

void RCC_PCLK1Config(uint32_t RCC_HCLK)
{
  RCC->CFGR &= (uint32_t)0xFFFFF8FF;
  RCC->CFGR |= RCC_HCLK;
}

void RCC_PCLK2Config(uint32_t RCC_HCLK)
{
  RCC->CFGR &= (uint32_t)0xFFFFC7FF;
  RCC->CFGR |= (RCC_HCLK << 11);
}

void RCC_PLLConfig(uint32_t RCC_PLLSource, uint32_t RCC_PLLMul)
{
  RCC->CFGR &= (uint32_t)0xFFC03FFF;
  RCC->CFGR |= (RCC_PLLSource | RCC_PLLMul);
}

void RCC_PLLCmd(FunctionalState NewState)
{
  if (NewState != DISABLE)
  {
    RCC->CR |= RCC_PLL_ON;
  }
  else
  {
    RCC->CR &= ~RCC_PLL_ON;
  }
}

void RCC_SYSCLKConfig(uint32_t RCC_SYSCLKSource)
{
  RCC->CFGR &= (uint32_t)0xFFFFFFFC;
  RCC->CFGR |= RCC_SYSCLKSource;
}

uint8_t RCC_GetSYSCLKSource(void)
{
  return ((uint8_t)(RCC->CFGR & (uint32_t)0x0000000C));
}

void RCC_APB2PeriphClockCmd(uint32_t RCC_APB2Periph, FunctionalState NewState)
{
  if (NewState != DISABLE)
  {
    RCC->APB2ENR |= RCC_APB2Periph;
  }
  else
  {
    RCC->APB2ENR &= ~RCC_APB2Periph;
  }
}

void RCC_APB1PeriphClockCmd(uint32_t RCC_APB1Periph, FunctionalState NewState)
{
  if (NewState != DISABLE)
  {
    RCC->APB1ENR |= RCC_APB1Periph;
  }
  else
  {
    RCC->APB1ENR &= ~RCC_APB1Periph;
  }
}

FlagStatus RCC_GetFlagStatus(uint32_t RCC_FLAG)
{
  FlagStatus bitstatus = RESET;
  if ((RCC->CR & RCC_FLAG) != (uint32_t)RESET)
  {
    bitstatus = SET;
  }
  else
  {
    bitstatus = RESET;
  }
  return bitstatus;
}
