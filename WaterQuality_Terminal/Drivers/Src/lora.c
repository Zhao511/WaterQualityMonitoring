#include "lora.h"
#include "stm32f10x_usart.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "misc.h"
#include <string.h>
#include <stdio.h>

/* 兼容旧 ISR 的线形缓冲区 */
uint8_t  lora_rx_buffer[LORA_BUFFER_SIZE];
uint16_t lora_rx_index = 0;

/* 高效的环形缓冲区 */
RingBuffer lora_rb;

/* 简易延时 (非精确, ~72MHz 下约 1ms 循环 12000 次) */
static void Lora_DelayMs(volatile uint32_t ms)
{
    while (ms--) {
        volatile uint32_t i = 8000;
        while (i--) { __NOP(); }
    }
}

/* ========== GPIO 控制 ========== */

/**
 * @brief  设置 MD0 引脚电平
 * @param  level: 0=低电平(透传), 1=高电平(配置)
 */
void LoRa_SetMode(uint8_t mode)
{
    if (mode) {
        GPIO_SetBits(LORA_MD0_PORT, LORA_MD0_PIN);
    } else {
        GPIO_ResetBits(LORA_MD0_PORT, LORA_MD0_PIN);
    }
}

/**
 * @brief  读取 AUX 引脚状态
 * @return 1 = 空闲, 0 = 忙
 */
uint8_t LoRa_AuxReady(void)
{
    return ((LORA_AUX_PORT->IDR & LORA_AUX_PIN) != 0) ? 1 : 0;
}

/**
 * @brief  阻塞等待 AUX 就绪 (HIGH)
 * @param  timeout_ms: 超时时间 (ms), 0 = 无限等待
 */
void LoRa_WaitAux(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (!LoRa_AuxReady()) {
        Lora_DelayMs(1);
        elapsed++;
        if (timeout_ms > 0 && elapsed >= timeout_ms) break;
    }
}

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

/* ========== LoRa 模块配置 ========== */

/**
 * @brief  向 LoRa 模块发送 AT 指令 (配置模式下使用)
 * @return 0=成功, 非0=超时或无响应
 */
static int LoRa_SendAT(const char *cmd, char *resp, uint16_t resp_max)
{
    /* 等待模块空闲 */
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 发送 AT 指令 */
    uint16_t len = (uint16_t)strlen(cmd);
    LoRa_SendData((uint8_t *)cmd, len);

    /* 等待响应 */
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);
    uint16_t rlen = LoRa_ReceiveData((uint8_t *)resp, resp_max - 1);
    if (rlen > 0) {
        resp[rlen] = '\0';
        return 0;
    }
    return -1;
}

/**
 * @brief  重新配置 USART3 波特率 (不改变其他参数)
 */
static void LoRa_USART_SetBaud(uint32_t baud)
{
    USART_Cmd(LORA_USART, DISABLE);
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate            = baud;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(LORA_USART, &USART_InitStructure);
    USART_Cmd(LORA_USART, ENABLE);
}

/**
 * @brief  初始化 LoRa 模块 (USART3 + MD0/AUX + 参数配置)
 *
 * 流程:
 *   1. GPIO / USART 初始化, 先用 9600 与模块通信 (出厂默认波特率)
 *   2. 若 9600 无响应则尝试 LORA_BAUD_RATE
 *   3. 发送 AT+BAUD 切换到目标波特率
 *   4. 配置地址 / 信道
 *   5. 进入透传模式
 */
void LoRa_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    /* ---- 时钟使能 ---- */
    RCC_APB1PeriphClockCmd(LORA_USART_RCC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | LORA_MD0_RCC | LORA_AUX_RCC, ENABLE);

    /* ---- TX: PB10 (复用推挽) ---- */
    GPIO_InitStructure.GPIO_Pin   = LORA_TX_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LORA_TX_PORT, &GPIO_InitStructure);

    /* ---- RX: PB11 (浮空输入) ---- */
    GPIO_InitStructure.GPIO_Pin  = LORA_RX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(LORA_RX_PORT, &GPIO_InitStructure);

    /* ---- MD0: PA8 (推挽输出, 初始低=透传) ---- */
    GPIO_InitStructure.GPIO_Pin   = LORA_MD0_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz;
    GPIO_Init(LORA_MD0_PORT, &GPIO_InitStructure);
    GPIO_ResetBits(LORA_MD0_PORT, LORA_MD0_PIN);

    /* ---- AUX: PB12 (上拉输入) ---- */
    GPIO_InitStructure.GPIO_Pin  = LORA_AUX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(LORA_AUX_PORT, &GPIO_InitStructure);

    /* ---- USART3: 先用出厂默认 9600 ---- */
    USART_InitStructure.USART_BaudRate            = 9600;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(LORA_USART, &USART_InitStructure);

    /* ---- 中断配置 ---- */
    NVIC_InitStructure.NVIC_IRQChannel                   = LORA_USART_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_ITConfig(LORA_USART, USART_IT_RXNE, ENABLE);
    USART_Cmd(LORA_USART, ENABLE);

    /* ---- 环形缓冲区 ---- */
    RingBuffer_Init(&lora_rb);

    /* 等待模块上电稳定 */
    Lora_DelayMs(300);

    /* ================================================================
     * 波特率自适应: 找到模块当前波特率, 然后切换到 LORA_BAUD_RATE
     * ================================================================ */
    uint32_t current_baud = 0;
    char     at_resp[32];

    /* 波特率扫描列表 (常见速率, 出厂默认 9600 优先) */
    static const uint32_t baud_list[] = {
        9600, 115200, 19200, 38400, 4800, 57600, 2400, 14400
    };
    const size_t baud_count = sizeof(baud_list) / sizeof(baud_list[0]);

    /* 进入配置模式 (MD0=HIGH) */
    LoRa_SetMode(1);
    Lora_DelayMs(50);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 扫描波特率 */
    for (size_t i = 0; i < baud_count; i++)
    {
        LoRa_USART_SetBaud(baud_list[i]);
        Lora_DelayMs(80);
        if (LoRa_SendAT("AT\r\n", at_resp, sizeof(at_resp)) == 0)
        {
            current_baud = baud_list[i];
            break;
        }
    }

    /* 如果找到的波特率不是目标波特率, 发送 AT+BAUD 切换 */
    if (current_baud != 0 && current_baud != LORA_BAUD_RATE)
    {
        char at_cmd[20];
        snprintf(at_cmd, sizeof(at_cmd), "AT+BAUD=%d\r\n", LORA_BAUD_RATE);
        LoRa_SendAT(at_cmd, at_resp, sizeof(at_resp));
        LoRa_USART_SetBaud(LORA_BAUD_RATE);
        Lora_DelayMs(100);
    }
    else if (current_baud == 0)
    {
        /* 所有波特率都失败, 回退到 LORA_BAUD_RATE 继续 (模块可能异常) */
        LoRa_USART_SetBaud(LORA_BAUD_RATE);
        Lora_DelayMs(100);
    }

    /* ================================================================
     * 配置参数 (在当前波特率下, 尝试多种 AT 指令格式)
     * ================================================================ */
    {
        LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

        /* 地址 — 十进制和十六进制都尝试 */
        {
            char at_cmd[20];
            snprintf(at_cmd, sizeof(at_cmd), "AT+ADDR=%d\r\n", LORA_DEFAULT_ADDRESS);
            if (LoRa_SendAT(at_cmd, at_resp, sizeof(at_resp)) != 0)
            {
                snprintf(at_cmd, sizeof(at_cmd), "AT+ADDR=%02X\r\n", LORA_DEFAULT_ADDRESS);
                LoRa_SendAT(at_cmd, at_resp, sizeof(at_resp));
            }
        }

        /* 信道 — 多种命令名和格式 */
        {
            char at_cmd[20];
            snprintf(at_cmd, sizeof(at_cmd), "AT+CH=%d\r\n", LORA_DEFAULT_CHANNEL);
            if (LoRa_SendAT(at_cmd, at_resp, sizeof(at_resp)) != 0)
            {
                snprintf(at_cmd, sizeof(at_cmd), "AT+CHANNEL=%d\r\n", LORA_DEFAULT_CHANNEL);
                if (LoRa_SendAT(at_cmd, at_resp, sizeof(at_resp)) != 0)
                {
                    snprintf(at_cmd, sizeof(at_cmd), "AT+CHANNEL=%02X\r\n", LORA_DEFAULT_CHANNEL);
                    LoRa_SendAT(at_cmd, at_resp, sizeof(at_resp));
                }
            }
        }

        /* 查询确认 */
        LoRa_SendAT("AT+ADDR?\r\n", at_resp, sizeof(at_resp));
        LoRa_SendAT("AT+CH?\r\n", at_resp, sizeof(at_resp));
    }

    /* ================================================================
     * 回到透传模式 (MD0=LOW)
     * ================================================================ */
    LoRa_SetMode(0);
    Lora_DelayMs(50);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);
}

/**
 * @brief  通过 LoRa 发送数据 (透传模式)
 * @param  data:   数据指针
 * @param  length: 数据长度
 */
void LoRa_SendData(uint8_t *data, uint16_t length)
{
    /* 等待模块空闲再发送 */
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

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
 * @brief  设置 LoRa 模块地址 (透传模式下无效, 需先切到配置模式)
 */
void LoRa_SetAddress(uint8_t address)
{
    /* 切换到配置模式 */
    LoRa_SetMode(1);
    Lora_DelayMs(50);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 发送 AT 指令 */
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "AT+ADDR=%d\r\n", address);
    LoRa_SendData((uint8_t *)cmd, (uint16_t)strlen(cmd));
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 切回透传模式 */
    LoRa_SetMode(0);
    Lora_DelayMs(50);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);
}

/**
 * @brief  设置 LoRa 模块信道 (透传模式下无效, 需先切到配置模式)
 */
void LoRa_SetChannel(uint8_t channel)
{
    /* 切换到配置模式 */
    LoRa_SetMode(1);
    Lora_DelayMs(50);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 发送 AT 指令 */
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "AT+CHANNEL=%d\r\n", channel);
    LoRa_SendData((uint8_t *)cmd, (uint16_t)strlen(cmd));
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 切回透传模式 */
    LoRa_SetMode(0);
    Lora_DelayMs(50);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);
}

/**
 * @brief  发送 AT 指令 (自动切换配置模式, 发送后恢复透传)
 * @param  cmd:      AT 指令字符串 (不含 \r\n)
 * @param  resp:     响应缓冲区
 * @param  resp_max: 响应缓冲区大小
 * @return 0=成功, -1=超时
 */
int LoRa_SendATCmd(const char *cmd, char *resp, uint16_t resp_max)
{
    int ret = -1;

    /* 切换到配置模式 */
    LoRa_SetMode(1);
    Lora_DelayMs(50);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 发送 AT 指令 */
    uint16_t len = (uint16_t)strlen(cmd);
    LoRa_SendData((uint8_t *)cmd, len);
    LoRa_SendData((uint8_t *)"\r\n", 2);

    /* 等待响应 */
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);
    Lora_DelayMs(50);
    uint16_t rlen = LoRa_ReceiveData((uint8_t *)resp, resp_max - 1);
    if (rlen > 0) {
        resp[rlen] = '\0';
        ret = 0;
    }

    /* 切回透传模式 */
    LoRa_SetMode(0);
    Lora_DelayMs(50);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    return ret;
}
