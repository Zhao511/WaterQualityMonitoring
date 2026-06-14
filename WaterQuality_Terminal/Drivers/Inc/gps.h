#ifndef __GPS_H
#define __GPS_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

/* ========== GPS 模块: USART2 重映射 (PD5=TX, PD6=RX) ==========
 * 说明: 之前 GPS 使用 USART1(PA9/PA10)，但板载 USB-UART 也在 PA9/PA10。
 *       现将 GPS 改到 USART2 重映射引脚 PD5/PD6，解放 USART1 给调试输出。
 *       GPS 模块接线: TX→PD6(RX), RX→PD5(TX)
 */
#define GPS_USART           USART2
#define GPS_USART_RCC       RCC_APB1Periph_USART2
#define GPS_TX_PIN          GPIO_Pin_5
#define GPS_RX_PIN          GPIO_Pin_6
#define GPS_TX_PORT         GPIOD
#define GPS_RX_PORT         GPIOD
#define GPS_USART_IRQn      USART2_IRQn

#define GPS_BUFFER_SIZE     512

typedef struct {
    char    lat[12];
    char    lon[12];
    char    time[10];
    char    lat_hem;      /* 'N' or 'S' */
    char    lon_hem;      /* 'E' or 'W' */
    uint8_t fix;
    uint8_t satellites;
    float   hdop;
    float   altitude;
} GPS_Data;

extern uint8_t  gps_rx_buffer[GPS_BUFFER_SIZE];
extern uint16_t gps_rx_index;

void     GPS_Init(void);
void     GPS_ProcessData(void);
void     GPS_GetData(GPS_Data *data);
uint8_t  GPS_ParseNMEA(const char *nmea, GPS_Data *data);
uint8_t  GPS_ValidateChecksum(const char *nmea);

#ifdef __cplusplus
}
#endif

#endif
