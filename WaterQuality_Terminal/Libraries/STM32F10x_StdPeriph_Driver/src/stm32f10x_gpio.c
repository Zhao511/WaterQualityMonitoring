#include "stm32f10x_gpio.h"

void GPIO_Init(GPIO_TypeDef* GPIOx, GPIO_InitTypeDef* GPIO_InitStruct)
{
  uint32_t currentmode = 0x00, currentpin = 0x00, pinpos = 0x00, pos = 0x00;
  uint32_t tmpreg = 0x00, pinmask = 0x00;
  
  currentmode = ((uint32_t)GPIO_InitStruct->GPIO_Mode) & ((uint32_t)0x0F);
  
  if ((((uint32_t)GPIO_InitStruct->GPIO_Mode) & ((uint32_t)0x10)) != 0x00)
  {
    currentmode |= (uint32_t)GPIO_InitStruct->GPIO_Speed;
  }
  
  if (((uint32_t)GPIO_InitStruct->GPIO_Pin & ((uint32_t)0x00FF)) != 0x00)
  {
    tmpreg = GPIOx->CRL;
    for (pinpos = 0x00; pinpos < 0x08; pinpos++)
    {
      pos = ((uint32_t)0x01) << pinpos;
      currentpin = (GPIO_InitStruct->GPIO_Pin) & pos;
      if (currentpin == pos)
      {
        pos = pinpos << 2;
        pinmask = ((uint32_t)0x0F) << pos;
        tmpreg &= ~pinmask;
        tmpreg |= (currentmode << pos);
      }
    }
    GPIOx->CRL = tmpreg;
  }
  
  if ((GPIO_InitStruct->GPIO_Pin & ((uint32_t)0xFF00)) != 0x00)
  {
    tmpreg = GPIOx->CRH;
    for (pinpos = 0x00; pinpos < 0x08; pinpos++)
    {
      pos = (((uint32_t)0x01) << (pinpos + 0x08));
      currentpin = ((GPIO_InitStruct->GPIO_Pin) & pos);
      if (currentpin == pos)
      {
        pos = pinpos << 2;
        pinmask = ((uint32_t)0x0F) << pos;
        tmpreg &= ~pinmask;
        tmpreg |= (currentmode << pos);
      }
    }
    GPIOx->CRH = tmpreg;
  }
}

void GPIO_SetBits(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin)
{
  GPIOx->BSRR = GPIO_Pin;
}

void GPIO_ResetBits(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin)
{
  GPIOx->BRR = GPIO_Pin;
}

void GPIO_PinRemapConfig(uint32_t GPIO_Remap, FunctionalState NewState)
{
  uint32_t tmp = 0x00, tmpreg = 0x00, tmpmask = 0x00;

  /* 获取重映射偏移和掩码 */
  tmpreg = AFIO->MAPR;
  tmpmask = (GPIO_Remap & (uint32_t)0x001F0000) >> 16;
  tmp = GPIO_Remap & (uint32_t)0x0000FFFF;

  if (NewState != DISABLE)
  {
    tmpreg &= ~tmpmask;
    tmpreg |= tmp;
  }
  else
  {
    tmpreg &= ~(tmpmask | tmp);
  }

  AFIO->MAPR = tmpreg;
}
