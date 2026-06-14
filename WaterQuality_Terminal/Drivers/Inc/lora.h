#ifndef __LORA_H
#define __LORA_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

/* ========== 硬件引脚定义 ========== */
#define LORA_USART        USART3
#define LORA_USART_RCC    RCC_APB1Periph_USART3
#define LORA_TX_PIN       GPIO_Pin_10
#define LORA_RX_PIN       GPIO_Pin_11
#define LORA_TX_PORT      GPIOB
#define LORA_RX_PORT      GPIOB
#define LORA_USART_IRQn   USART3_IRQn

/* ========== 缓冲区 ========== */
#define LORA_BUFFER_SIZE  256
#define LORA_DATA_SIZE    256   /* 与 LORA_BUFFER_SIZE 一致, 容纳 200+ 字节 JSON */

/* ========== 环形缓冲区结构 ========== */
typedef struct {
    uint8_t  buffer[LORA_BUFFER_SIZE];
    uint16_t head;    /* 写入位置 (ISR 写入) */
    uint16_t tail;    /* 读取位置 (主循环读取) */
    uint16_t count;   /* 当前数据量 */
} RingBuffer;

/* 全局环形缓冲区 */
extern RingBuffer lora_rb;

/* 兼容旧接口的变量（ISR 仍写入此缓冲区，由进程函数转移到环形缓冲） */
extern uint8_t  lora_rx_buffer[LORA_BUFFER_SIZE];
extern uint16_t lora_rx_index;

/* ========== API ========== */
void     LoRa_Init(void);
void     LoRa_SendData(uint8_t *data, uint16_t length);
uint16_t LoRa_ReceiveData(uint8_t *buffer, uint16_t max_length);
void     LoRa_SetAddress(uint8_t address);
void     LoRa_SetChannel(uint8_t channel);

/* 环形缓冲区操作 */
void     RingBuffer_Init(RingBuffer *rb);
uint8_t  RingBuffer_Put(RingBuffer *rb, uint8_t data);
uint8_t  RingBuffer_Get(RingBuffer *rb, uint8_t *data);
uint16_t RingBuffer_Read(RingBuffer *rb, uint8_t *buffer, uint16_t max_length);

#ifdef __cplusplus
}
#endif

#endif
