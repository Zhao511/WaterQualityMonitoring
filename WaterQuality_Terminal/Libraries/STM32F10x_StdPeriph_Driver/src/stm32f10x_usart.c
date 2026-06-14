#include "stm32f10x_usart.h"

void USART_Init(USART_TypeDef* USARTx, USART_InitTypeDef* USART_InitStruct)
{
  uint32_t tmpreg = 0x00, apbclock;

  /* APB2(72MHz): USART1;  APB1(36MHz): USART2, USART3 */
  if (USARTx == USART1)
      apbclock = 72000000;
  else
      apbclock = 36000000;
  
  tmpreg = USARTx->CR1;
  tmpreg &= (uint32_t)0xFFFFF8FF;
  tmpreg |= (uint32_t)USART_InitStruct->USART_WordLength;
  USARTx->CR1 = tmpreg;
  
  tmpreg = USARTx->CR2;
  tmpreg &= (uint32_t)0xFFFFCFFF;
  tmpreg |= (uint32_t)(USART_InitStruct->USART_StopBits << 12);
  USARTx->CR2 = tmpreg;
  
  tmpreg = USARTx->CR1;
  tmpreg &= (uint32_t)0xFFFFFFCF;
  tmpreg |= (uint32_t)(USART_InitStruct->USART_Parity << 9);
  USARTx->CR1 = tmpreg;
  
  tmpreg = USARTx->CR3;
  tmpreg &= (uint32_t)0xFFFFF0FF;
  tmpreg |= (uint32_t)(USART_InitStruct->USART_HardwareFlowControl << 8);
  USARTx->CR3 = tmpreg;
  
  tmpreg = USARTx->CR1;
  tmpreg &= (uint32_t)0xFFFFFF3F;
  tmpreg |= (uint32_t)USART_InitStruct->USART_Mode;
  USARTx->CR1 = tmpreg;
  
  USARTx->BRR = (uint32_t)(apbclock / USART_InitStruct->USART_BaudRate);
}

void USART_Cmd(USART_TypeDef* USARTx, FunctionalState NewState)
{
  if (NewState != DISABLE)
  {
    USARTx->CR1 |= (uint32_t)0x2000;
  }
  else
  {
    USARTx->CR1 &= (uint32_t)0xDFFF;
  }
}

void USART_SendData(USART_TypeDef* USARTx, uint16_t Data)
{
  USARTx->DR = (Data & (uint16_t)0x01FF);
}

uint16_t USART_ReceiveData(USART_TypeDef* USARTx)
{
  return (uint16_t)(USARTx->DR & (uint16_t)0x01FF);
}

FlagStatus USART_GetFlagStatus(USART_TypeDef* USARTx, uint16_t USART_FLAG)
{
  FlagStatus bitstatus = RESET;
  
  if ((USARTx->SR & USART_FLAG) != (uint16_t)0x00)
  {
    bitstatus = SET;
  }
  else
  {
    bitstatus = RESET;
  }
  
  return bitstatus;
}

void USART_ClearFlag(USART_TypeDef* USARTx, uint16_t USART_FLAG)
{
  USARTx->SR = (uint16_t)~USART_FLAG;
}

void USART_ITConfig(USART_TypeDef* USARTx, uint16_t USART_IT, FunctionalState NewState)
{
  uint32_t usartreg = 0x00, itpos = 0x00, itmask = 0x00;
  
  usartreg = (((uint8_t)USART_IT) >> 0x05);
  
  itpos = USART_IT & (uint16_t)0x1F;
  itmask = (uint32_t)0x01 << itpos;
  
  if (usartreg == 0x01)
  {
    if (NewState != DISABLE)
    {
      USARTx->CR1 |= itmask;
    }
    else
    {
      USARTx->CR1 &= ~itmask;
    }
  }
  else if (usartreg == 0x02)
  {
    if (NewState != DISABLE)
    {
      USARTx->CR2 |= itmask;
    }
    else
    {
      USARTx->CR2 &= ~itmask;
    }
  }
  else if (usartreg == 0x03)
  {
    if (NewState != DISABLE)
    {
      USARTx->CR3 |= itmask;
    }
    else
    {
      USARTx->CR3 &= ~itmask;
    }
  }
}

FlagStatus USART_GetITStatus(USART_TypeDef* USARTx, uint16_t USART_IT)
{
  FlagStatus bitstatus = RESET;
  uint32_t itpos = 0x00, itmask = 0x00, usartreg = 0x00;
  
  itpos = USART_IT & (uint16_t)0x1F;
  itmask = ((uint32_t)0x01) << itpos;
  usartreg = (((uint8_t)USART_IT) >> 0x05);
  
  if (usartreg == 0x01)
  {
    usartreg = USARTx->CR1;
  }
  else if (usartreg == 0x02)
  {
    usartreg = USARTx->CR2;
  }
  else
  {
    usartreg = USARTx->CR3;
  }
  
  if (((USARTx->SR & itmask) != (uint16_t)0x00) && ((usartreg & itmask) != (uint16_t)0x00))
  {
    bitstatus = SET;
  }
  else
  {
    bitstatus = RESET;
  }
  
  return bitstatus;
}

void USART_ClearITPendingBit(USART_TypeDef* USARTx, uint16_t USART_IT)
{
  uint16_t itpos = 0x00;
  uint16_t itmask = 0x00;
  
  itpos = USART_IT & (uint16_t)0x1F;
  itmask = (uint16_t)(0x01 << itpos);
  USARTx->SR = (uint16_t)~itmask;
}
