#include "misc.h"

#define NVIC_VectTab_RAM             ((uint32_t)0x20000000)
#define NVIC_VectTab_FLASH           ((uint32_t)0x08000000)

void NVIC_Init(NVIC_InitTypeDef* NVIC_InitStruct)
{
  uint32_t tmppriority = 0x00;
  
  if (NVIC_InitStruct->NVIC_IRQChannelCmd != DISABLE)
  {
    tmppriority = (0x700 - ((SCB->AIRCR) & (uint32_t)0x700)) >> 0x08;
    tmppriority = (0x4 - tmppriority);
    tmppriority = (uint32_t)NVIC_InitStruct->NVIC_IRQChannelPreemptionPriority << tmppriority;
    
    tmppriority |= NVIC_InitStruct->NVIC_IRQChannelSubPriority & (uint32_t)0x0F;
    
    NVIC->IP[NVIC_InitStruct->NVIC_IRQChannel] = tmppriority;
    
    NVIC->ISER[NVIC_InitStruct->NVIC_IRQChannel >> 0x05] = (uint32_t)0x01 << (NVIC_InitStruct->NVIC_IRQChannel & (uint8_t)0x1F);
  }
  else
  {
    NVIC->ICER[NVIC_InitStruct->NVIC_IRQChannel >> 0x05] = (uint32_t)0x01 << (NVIC_InitStruct->NVIC_IRQChannel & (uint8_t)0x1F);
  }
}
