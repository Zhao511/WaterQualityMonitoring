/**
 * GPS 模块驱动 — USART2 重映射 (PD5=TX, PD6=RX)
 *
 * 注意: 硬件接线上 GPS 模块的 TX 接 STM32 的 PD6(RX)
 *                     GPS 模块的 RX 接 STM32 的 PD5(TX)
 */

#include "gps.h"
#include "stm32f10x_usart.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "misc.h"
#include <string.h>
#include <stdlib.h>

uint8_t  gps_rx_buffer[GPS_BUFFER_SIZE];
uint16_t gps_rx_index = 0;

static GPS_Data gps_data = {0};

void GPS_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    /* 使能 GPIOD 和 USART2 时钟 + AFIO (重映射需要) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(GPS_USART_RCC, ENABLE);

    /* USART2 重映射到 PD5/PD6 */
    GPIO_PinRemapConfig(GPIO_Remap_USART2, ENABLE);

    /* TX: PD5 */
    GPIO_InitStructure.GPIO_Pin   = GPS_TX_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPS_TX_PORT, &GPIO_InitStructure);

    /* RX: PD6 */
    GPIO_InitStructure.GPIO_Pin  = GPS_RX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPS_RX_PORT, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate            = 9600;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(GPS_USART, &USART_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel                   = GPS_USART_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_ITConfig(GPS_USART, USART_IT_RXNE, ENABLE);
    USART_Cmd(GPS_USART, ENABLE);
}

uint8_t GPS_ValidateChecksum(const char *nmea)
{
    if (nmea[0] != '$') return 0;

    uint8_t checksum = 0;
    const char *p = nmea + 1;

    while (*p && *p != '*')
    {
        checksum ^= (uint8_t)*p++;
    }

    if (*p != '*') return 0;

    p++;
    char hex[3] = {0};
    hex[0] = *p++;
    hex[1] = (*p) ? *p : '\0';
    uint8_t expected = (uint8_t)strtol(hex, NULL, 16);

    return (checksum == expected) ? 1 : 0;
}

uint8_t GPS_ParseNMEA(const char *nmea, GPS_Data *data)
{
    /* static buffer — avoids 128 bytes on stack per call (called from GPS_ProcessData) */
    static char sentence[128];

    if (strstr(nmea, "$GPGGA") != NULL)
    {
        if (!GPS_ValidateChecksum(nmea)) return 0;

        strncpy(sentence, nmea, sizeof(sentence) - 1);
        sentence[sizeof(sentence) - 1] = '\0';

        char *saveptr;
        char *token = strtok_r(sentence, ",", &saveptr);
        if (token == NULL) return 0;

        token = strtok_r(NULL, ",", &saveptr);
        if (token) strncpy(data->time, token, sizeof(data->time) - 1);

        token = strtok_r(NULL, ",", &saveptr);
        if (token) strncpy(data->lat, token, sizeof(data->lat) - 1);

        token = strtok_r(NULL, ",", &saveptr);
        if (token) data->lat_hem = token[0];

        token = strtok_r(NULL, ",", &saveptr);
        if (token) strncpy(data->lon, token, sizeof(data->lon) - 1);

        token = strtok_r(NULL, ",", &saveptr);
        if (token) data->lon_hem = token[0];

        token = strtok_r(NULL, ",", &saveptr);
        if (token) data->fix = (uint8_t)atoi(token);

        token = strtok_r(NULL, ",", &saveptr);
        if (token) data->satellites = (uint8_t)atoi(token);

        token = strtok_r(NULL, ",", &saveptr);
        if (token) data->hdop = atof(token);

        token = strtok_r(NULL, ",", &saveptr);
        if (token) data->altitude = atof(token);

        return 1;
    }

    if (strstr(nmea, "$GPRMC") != NULL)
    {
        if (!GPS_ValidateChecksum(nmea)) return 0;

        /* reuse the static sentence[] buffer declared above */
        strncpy(sentence, nmea, sizeof(sentence) - 1);
        sentence[sizeof(sentence) - 1] = '\0';

        char *saveptr;
        char *token = strtok_r(sentence, ",", &saveptr);
        if (token == NULL) return 0;

        token = strtok_r(NULL, ",", &saveptr);
        if (token) strncpy(data->time, token, sizeof(data->time) - 1);

        return 1;
    }

    return 0;
}

void GPS_ProcessData(void)
{
    if (gps_rx_index == 0) return;

    USART_ITConfig(GPS_USART, USART_IT_RXNE, DISABLE);

    char *start = (char *)gps_rx_buffer;
    char *end   = strchr(start, '\n');

    if (end != NULL)
    {
        uint16_t nmea_len = (uint16_t)(end - start);
        if (nmea_len > 0 && start[nmea_len - 1] == '\r') nmea_len--;

        if (nmea_len < 127)
        {
            static char nmea_copy[128];
            memcpy(nmea_copy, start, nmea_len);
            nmea_copy[nmea_len] = '\0';
            GPS_ParseNMEA(nmea_copy, &gps_data);
        }

        uint16_t remaining = gps_rx_index - (uint16_t)(end - start) - 1;
        if (remaining > 0) memmove(gps_rx_buffer, end + 1, remaining);
        gps_rx_index = remaining;
    }

    USART_ITConfig(GPS_USART, USART_IT_RXNE, ENABLE);
}

void GPS_GetData(GPS_Data *data)
{
    *data = gps_data;
}
