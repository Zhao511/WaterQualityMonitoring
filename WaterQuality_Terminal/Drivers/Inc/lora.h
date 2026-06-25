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

/* MD0: 模式控制 (PA8) — HIGH=配置模式, LOW=透传模式 */
#define LORA_MD0_PIN      GPIO_Pin_8
#define LORA_MD0_PORT     GPIOA
#define LORA_MD0_RCC      RCC_APB2Periph_GPIOA

/* AUX: 模块忙指示 (PB12) — LOW=空闲, HIGH=忙 */
#define LORA_AUX_PIN      GPIO_Pin_12
#define LORA_AUX_PORT     GPIOB
#define LORA_AUX_RCC      RCC_APB2Periph_GPIOB

/* 默认 LoRa 参数 (与 ESP32 侧一致) */
#define LORA_BAUD_RATE         115200  /* ATK-LORA-01 出厂默认, 模块固定 */
#define LORA_DEFAULT_ADDRESS   0
#define LORA_DEFAULT_CHANNEL   0      /* 433MHz (ATK-LORA-01 默认) */
#define LORA_DEFAULT_RATE      5      /* 19.2kbps (原2.4kbps单帧仅~55B,装不下200B JSON) */

/* AUX 超时 (ms) — 等待模块就绪的最长时间 */
#define LORA_AUX_TIMEOUT_MS    500

/* ========== 缓冲区 ========== */
#define LORA_BUFFER_SIZE  240   /* 帧协议: max 203B帧 + 余量 */
#define LORA_DATA_SIZE    240   /* 与 LORA_BUFFER_SIZE 一致 */

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
void     LoRa_FlushToRingBuffer(void);  /* 仅转移 ISR→环形缓冲, 不消费 */
void     LoRa_SetAddress(uint8_t address);
void     LoRa_SetChannel(uint8_t channel);
void     LoRa_SetMode(uint8_t mode);      /* 0=透传, 1=配置 */
uint8_t  LoRa_AuxReady(void);            /* 检查 AUX 是否就绪 */
void     LoRa_WaitAux(uint32_t timeout_ms);
int      LoRa_SendATCmd(const char *cmd, char *resp, uint16_t resp_max);  /* 发送 AT 指令 (自动切换模式) */

/* 环形缓冲区操作 */
void     RingBuffer_Init(RingBuffer *rb);
uint8_t  RingBuffer_Put(RingBuffer *rb, uint8_t data);
uint8_t  RingBuffer_Get(RingBuffer *rb, uint8_t *data);
uint16_t RingBuffer_Read(RingBuffer *rb, uint8_t *buffer, uint16_t max_length);

#ifdef __cplusplus
}
#endif

#endif
