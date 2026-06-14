#include "stm32f10x_spi.h"

void SPI_Init(SPI_TypeDef* SPIx, SPI_InitTypeDef* SPI_InitStruct)
{
  uint16_t tmpreg = 0x00;
  
  tmpreg = SPIx->CR1;
  tmpreg &= (uint16_t)0x3040;
  tmpreg |= (uint16_t)(SPI_InitStruct->SPI_Direction | SPI_InitStruct->SPI_Mode |
                       SPI_InitStruct->SPI_DataSize | SPI_InitStruct->SPI_CPOL |
                       SPI_InitStruct->SPI_CPHA | SPI_InitStruct->SPI_NSS |
                       SPI_InitStruct->SPI_BaudRatePrescaler | SPI_InitStruct->SPI_FirstBit);
  SPIx->CR1 = tmpreg;
  
  SPIx->CRCPR = SPI_InitStruct->SPI_CRCPolynomial;
}

void SPI_Cmd(SPI_TypeDef* SPIx, FunctionalState NewState)
{
  if (NewState != DISABLE)
  {
    SPIx->CR1 |= (uint16_t)0x0040;
  }
  else
  {
    SPIx->CR1 &= (uint16_t)0xFFBF;
  }
}

void SPI_I2S_SendData(SPI_TypeDef* SPIx, uint16_t Data)
{
  SPIx->DR = Data;
}

uint16_t SPI_I2S_ReceiveData(SPI_TypeDef* SPIx)
{
  return SPIx->DR;
}

FlagStatus SPI_I2S_GetFlagStatus(SPI_TypeDef* SPIx, uint16_t SPI_I2S_FLAG)
{
  FlagStatus bitstatus = RESET;
  if ((SPIx->SR & SPI_I2S_FLAG) != (uint16_t)0x00)
  {
    bitstatus = SET;
  }
  else
  {
    bitstatus = RESET;
  }
  return bitstatus;
}
