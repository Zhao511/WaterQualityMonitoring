#include "lora.h"
#include "stm32f10x_usart.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "misc.h"
#include <string.h>

/* 兼容旧 ISR 的线形缓冲区 */
uint8_t  lora_rx_buffer[LORA_BUFFER_SIZE];
uint16_t lora_rx_index = 0;

/* 高效的环形缓冲区 */
RingBuffer lora_rb;

/* ========== 环形缓冲区实现 ========== */

/**
 * @brief  初始化环形缓冲区
 */
void RingBuffer_Init(RingBuffer *rb)
{
    memset(rb->buffer, 0, LORA_BUFFER_SIZE);
    rb->head  = 0;
    rb->tail  = 0;
    rb->count = 0;
}

/**
 * @brief  向环形缓冲区写入一个字节
 * @return 1 = 成功, 0 = 缓冲区满
 */
uint8_t RingBuffer_Put(RingBuffer *rb, uint8_t data)
{
    if (rb->count >= LORA_BUFFER_SIZE)
    {
        return 0; /* 缓冲区满 */
    }
    rb->buffer[rb->head] = data;
    rb->head = (rb->head + 1) % LORA_BUFFER_SIZE;
    rb->count++;
    return 1;
}

/**
 * @brief  从环形缓冲区读取一个字节
 * @return 1 = 成功, 0 = 缓冲区空
 */
uint8_t RingBuffer_Get(RingBuffer *rb, uint8_t *data)
{
    if (rb->count == 0)
    {
        return 0; /* 缓冲区空 */
    }
    *data = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % LORA_BUFFER_SIZE;
    rb->count--;
    return 1;
}

/**
 * @brief  从环形缓冲区批量读取数据
 * @param  buffer:     输出缓冲区
 * @param  max_length: 最多读取字节数
 * @return 实际读取的字节数
 */
uint16_t RingBuffer_Read(RingBuffer *rb, uint8_t *buffer, uint16_t max_length)
{
    uint16_t i;
    for (i = 0; i < max_length; i++)
    {
        if (!RingBuffer_Get(rb, &buffer[i]))
        {
            break;
        }
    }
    return i;
}

/* ========== LoRa 模块 ========== */

/**
 * @brief  初始化 LoRa 模块 (USART3)
 */
void LoRa_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    RCC_APB1PeriphClockCmd(LORA_USART_RCC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    /* TX: PB10 */
    GPIO_InitStructure.GPIO_Pin   = LORA_TX_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LORA_TX_PORT, &GPIO_InitStructure);

    /* RX: PB11 */
    GPIO_InitStructure.GPIO_Pin  = LORA_RX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(LORA_RX_PORT, &GPIO_InitStructure);

    /* USART3 配置 */
    USART_InitStructure.USART_BaudRate            = 9600;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(LORA_USART, &USART_InitStructure);

    /* 中断配置 */
    NVIC_InitStructure.NVIC_IRQChannel                   = LORA_USART_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_ITConfig(LORA_USART, USART_IT_RXNE, ENABLE);
    USART_Cmd(LORA_USART, ENABLE);

    /* 初始化环形缓冲区 */
    RingBuffer_Init(&lora_rb);
}

/**
 * @brief  通过 LoRa 发送数据
 * @param  data:   数据指针
 * @param  length: 数据长度
 */
void LoRa_SendData(uint8_t *data, uint16_t length)
{
    for (uint16_t i = 0; i < length; i++)
    {
        while (USART_GetFlagStatus(LORA_USART, USART_FLAG_TXE) == RESET);
        USART_SendData(LORA_USART, data[i]);
    }
    while (USART_GetFlagStatus(LORA_USART, USART_FLAG_TC) == RESET);
}

/**
 * @brief  从环形缓冲区读取 LoRa 接收数据（O(n) 复杂度，无内存移动）
 * @param  buffer:     输出缓冲区
 * @param  max_length: 最多读取字节数
 * @return 实际读取的字节数
 */
uint16_t LoRa_ReceiveData(uint8_t *buffer, uint16_t max_length)
{
    /* 将 ISR 线形缓冲区数据转移到环形缓冲 */
    USART_ITConfig(LORA_USART, USART_IT_RXNE, DISABLE);

    uint16_t i;
    for (i = 0; i < lora_rx_index; i++)
    {
        RingBuffer_Put(&lora_rb, lora_rx_buffer[i]);
    }
    lora_rx_index = 0;

    USART_ITConfig(LORA_USART, USART_IT_RXNE, ENABLE);

    /* 从环形缓冲区读取数据 */
    return RingBuffer_Read(&lora_rb, buffer, max_length);
}

/**
 * @brief  设置 LoRa 模块地址
 */
void LoRa_SetAddress(uint8_t address)
{
    uint8_t cmd[5] = {0xC0, 0x01, address, 0x00, 0x00};
    LoRa_SendData(cmd, 5);
}

/**
 * @brief  设置 LoRa 模块信道
 */
void LoRa_SetChannel(uint8_t channel)
{
    uint8_t cmd[5] = {0xC0, 0x02, channel, 0x00, 0x00};
    LoRa_SendData(cmd, 5);
}
