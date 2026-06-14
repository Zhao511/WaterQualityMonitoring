#ifndef __STM32F10x_SPI_H
#define __STM32F10x_SPI_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

#define SPI_Direction_2Lines_FullDuplex    ((uint16_t)0x0000)

#define SPI_Mode_Master                    ((uint16_t)0x0104)
#define SPI_Mode_Slave                     ((uint16_t)0x0000)

#define SPI_DataSize_8b                    ((uint16_t)0x0000)
#define SPI_DataSize_16b                   ((uint16_t)0x0800)

#define SPI_CPOL_Low                       ((uint16_t)0x0000)
#define SPI_CPOL_High                      ((uint16_t)0x0002)

#define SPI_CPHA_1Edge                     ((uint16_t)0x0000)
#define SPI_CPHA_2Edge                     ((uint16_t)0x0001)

#define SPI_NSS_Soft                       ((uint16_t)0x0200)
#define SPI_NSS_Hard                       ((uint16_t)0x0000)

#define SPI_BaudRatePrescaler_2            ((uint16_t)0x0000)
#define SPI_BaudRatePrescaler_4            ((uint16_t)0x0008)
#define SPI_BaudRatePrescaler_8            ((uint16_t)0x0010)
#define SPI_BaudRatePrescaler_16           ((uint16_t)0x0018)

#define SPI_FirstBit_MSB                   ((uint16_t)0x0000)
#define SPI_FirstBit_LSB                   ((uint16_t)0x0080)

#define SPI_I2S_FLAG_TXE                   ((uint16_t)0x0002)
#define SPI_I2S_FLAG_RXNE                  ((uint16_t)0x0001)

void SPI_Init(SPI_TypeDef* SPIx, SPI_InitTypeDef* SPI_InitStruct);
void SPI_Cmd(SPI_TypeDef* SPIx, FunctionalState NewState);
void SPI_I2S_SendData(SPI_TypeDef* SPIx, uint16_t Data);
uint16_t SPI_I2S_ReceiveData(SPI_TypeDef* SPIx);
FlagStatus SPI_I2S_GetFlagStatus(SPI_TypeDef* SPIx, uint16_t SPI_I2S_FLAG);

#ifdef __cplusplus
}
#endif

#endif
