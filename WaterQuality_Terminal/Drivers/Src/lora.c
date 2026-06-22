#include "lora.h"
#include "stm32f10x_usart.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "misc.h"
#include "usart_debug.h"
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
    return ((LORA_AUX_PORT->IDR & LORA_AUX_PIN) == 0) ? 1 : 0;
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
 *
 * ATK-LORA-01 配置模式 AUX 时序 (正点原子官方手册):
 *   发送\r\n → AUX变HIGH(忙) → 模块处理 → 发响应 → AUX变LOW(空闲)
 *
 * 关键: 发送后必须先等 AUX=HIGH (进入忙碌), 再等 AUX=LOW (响应就绪)
 *       跳过HIGH等待会导致过早读取, 响应为空的时序bug
 *
 * @return 0=成功, 非0=超时或无响应
 */
static int LoRa_SendAT(const char *cmd, char *resp, uint16_t resp_max)
{
    /* 1. 清空接收缓冲 (避免读到上一条指令的残留回显) */
    {
        uint8_t dummy[LORA_BUFFER_SIZE];
        LoRa_ReceiveData(dummy, sizeof(dummy));
    }

    /* 2. 等待模块空闲 (AUX=LOW) */
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 3. 发送 AT 指令 */
    uint16_t len = (uint16_t)strlen(cmd);
    LoRa_SendData((uint8_t *)cmd, len);

    /* 4. 等待模块开始处理 (AUX=HIGH)
     *    ATK-LORA-01 收到完整 \r\n 后 AUX 变高, 表示忙
     *    超时 200ms 防止模块异常时死等 */
    {
        uint32_t elapsed = 0;
        while (LoRa_AuxReady() && elapsed < 200) {
            Lora_DelayMs(1);
            elapsed++;
        }
    }

    /* 5. 等待模块处理完成 (AUX=LOW), 响应就绪 */
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 6. 读取响应 (包含模块回显 + 实际响应) */
    uint16_t rlen = LoRa_ReceiveData((uint8_t *)resp, resp_max - 1);
    if (rlen > 0) {
        resp[rlen] = '\0';
        return 0;
    }
    return -1;
}

/**
 * @brief  初始化 LoRa 模块 (USART3 + MD0/AUX + 参数配置)
 *
 * 流程 (正点原子 ATK-LORA-01 V3.0 固件):
 *   1. GPIO / USART3 直接使用 LORA_BAUD_RATE (115200, ATK-LORA-01 出厂默认)
 *   2. MD0=HIGH 进入配置模式, 等待 >=300ms 稳定, 等待 AUX=LOW
 *   3. AT 连通性测试
 *   4. AT+ADDR / AT+WLRATE / AT+TPOWER / AT+UART — V3.0 标准 AT 指令
 *   5. AT+FLASH 保存参数
 *   6. MD0=LOW 回到透传模式
 *
 * V3.0 关键差异: 功率编号相反 (3=20dBm), WLRATE 合并速率+信道, ADDR 十六进制逗号格式
 */
void LoRa_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;
    char              at_resp[32];

    Debug_Printf("[LoRa] Init start\r\n");

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

    /* ---- AUX: PB12 (下拉输入) ---- */
    GPIO_InitStructure.GPIO_Pin  = LORA_AUX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(LORA_AUX_PORT, &GPIO_InitStructure);

    /* ---- USART3: 直接使用 LORA_BAUD_RATE (ATK-LORA-01 出厂默认 115200) ---- */
    USART_InitStructure.USART_BaudRate            = LORA_BAUD_RATE;
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
    Debug_Printf("[LoRa] GPIO+USART3 ready (baud=%lu), waiting module power-on...\r\n",
                 (uint32_t)LORA_BAUD_RATE);
    Lora_DelayMs(300);

    /* ================================================================
     * 进入配置模式 (MD0=HIGH), 等待模块就绪
     * 正点原子教程: MD0 切换后需 >=300ms 稳定时间
     * ================================================================ */
    LoRa_SetMode(1);
    Lora_DelayMs(300);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* ================================================================
     * AT 连通性测试
     * ================================================================ */
    if (LoRa_SendAT("AT\r\n", at_resp, sizeof(at_resp)) == 0) {
        Debug_Printf("[LoRa] AT resp: %s\r\n", at_resp);
    } else {
        Debug_Printf("[LoRa] WARN: AT no response, check wiring\r\n");
    }

    /* ================================================================
     * 配置参数 — ATK-LORA-01 V3.0 标准 AT 指令
     * WLRATE=<rate>,<ch>  TPOWER 3=20dBm  ADDR 十六进制逗号
     * ================================================================ */
    {
        char at_cmd[24];

        /* 地址 (十六进制逗号格式, V3.0 标准) */
        snprintf(at_cmd, sizeof(at_cmd), "AT+ADDR=%02X,%02X\r\n",
                 (LORA_DEFAULT_ADDRESS >> 8) & 0xFF, LORA_DEFAULT_ADDRESS & 0xFF);
        LoRa_SendAT(at_cmd, at_resp, sizeof(at_resp));

        /* 空中速率+信道: WLRATE=2,0 (2.4kbps, CH0) */
        snprintf(at_cmd, sizeof(at_cmd), "AT+WLRATE=%d,%d\r\n",
                 LORA_DEFAULT_RATE, LORA_DEFAULT_CHANNEL);
        LoRa_SendAT(at_cmd, at_resp, sizeof(at_resp));

        /* 发射功率: TPOWER=3 (20dBm, V3.0 中 3=最大) */
        LoRa_SendAT("AT+TPOWER=3\r\n", at_resp, sizeof(at_resp));

        /* 透传串口: UART=7,0 (115200 8N1) */
        LoRa_SendAT("AT+UART=7,0\r\n", at_resp, sizeof(at_resp));

        /* 保存到 Flash */
        LoRa_SendAT("AT+FLASH\r\n", at_resp, sizeof(at_resp));

        /* 查询确认 */
        LoRa_SendAT("AT+ADDR?\r\n", at_resp, sizeof(at_resp));
        Debug_Printf("[LoRa] ADDR? resp: %s\r\n", at_resp[0] ? at_resp : "(no response)");
    }

    /* ================================================================
     * 回到透传模式 (MD0=LOW)
     * 正点原子教程: MD0 切换后需 >=300ms 稳定时间
     * ================================================================ */
    LoRa_SetMode(0);
    Lora_DelayMs(300);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);
    Debug_Printf("[LoRa] Init done, transparent mode\r\n");
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
        uint32_t timeout = 100000;  /* TXE 超时保护 ~100ms */
        while (USART_GetFlagStatus(LORA_USART, USART_FLAG_TXE) == RESET) {
            if (--timeout == 0) break;
        }
        USART_SendData(LORA_USART, data[i]);
    }
    uint32_t timeout = 100000;  /* TC 超时保护 ~100ms */
    while (USART_GetFlagStatus(LORA_USART, USART_FLAG_TC) == RESET) {
        if (--timeout == 0) break;
    }
}

/**
 * @brief  从环形缓冲区读取 LoRa 接收数据（O(n) 复杂度，无内存移动）
 * @param  buffer:     输出缓冲区
 * @param  max_length: 最多读取字节数
 * @return 实际读取的字节数
 */
uint16_t LoRa_ReceiveData(uint8_t *buffer, uint16_t max_length)
{
    /* 原子交换: 仅在读取+清零 lora_rx_index 时关中断 (2 条指令)
     * 数据转移在中断开启状态下进行, 新到达的字节写入复位后的缓冲
     * 避免长时间禁 ISR 导致 USART DR 溢出 (ORE) 丢数据 */
    uint16_t count;
    __disable_irq();
    count = lora_rx_index;
    lora_rx_index = 0;
    __enable_irq();

    /* 将快照数据转移到环形缓冲 (ISR 此时可继续写入) */
    for (uint16_t i = 0; i < count; i++)
    {
        RingBuffer_Put(&lora_rb, lora_rx_buffer[i]);
    }

    /* 从环形缓冲区读取数据 */
    return RingBuffer_Read(&lora_rb, buffer, max_length);
}

/**
 * @brief  设置 LoRa 模块地址 (透传模式下无效, 需先切到配置模式)
 */
void LoRa_SetAddress(uint8_t address)
{
    /* 切换到配置模式, 等 300ms 稳定 (正点原子教程) */
    LoRa_SetMode(1);
    Lora_DelayMs(300);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 发送 AT 指令 */
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "AT+ADDR=%02X,%02X\r\n",
             (address >> 8) & 0xFF, address & 0xFF);
    LoRa_SendData((uint8_t *)cmd, (uint16_t)strlen(cmd));

    /* 等待模块开始处理 (AUX=HIGH), 超时 200ms */
    {
        uint32_t elapsed = 0;
        while (LoRa_AuxReady() && elapsed < 200) {
            Lora_DelayMs(1);
            elapsed++;
        }
    }

    /* 等待模块处理完成 (AUX=LOW) */
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 切回透传模式, 等 300ms 稳定 */
    LoRa_SetMode(0);
    Lora_DelayMs(300);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);
}

/**
 * @brief  设置 LoRa 模块信道 (透传模式下无效, 需先切到配置模式)
 */
void LoRa_SetChannel(uint8_t channel)
{
    /* 切换到配置模式, 等 300ms 稳定 (正点原子教程) */
    LoRa_SetMode(1);
    Lora_DelayMs(300);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 发送 AT 指令 */
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "AT+WLRATE=%d,%d\r\n", LORA_DEFAULT_RATE, channel);
    LoRa_SendData((uint8_t *)cmd, (uint16_t)strlen(cmd));

    /* 等待模块开始处理 (AUX=HIGH), 超时 200ms */
    {
        uint32_t elapsed = 0;
        while (LoRa_AuxReady() && elapsed < 200) {
            Lora_DelayMs(1);
            elapsed++;
        }
    }

    /* 等待模块处理完成 (AUX=LOW) */
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 切回透传模式, 等 300ms 稳定 */
    LoRa_SetMode(0);
    Lora_DelayMs(300);
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

    /* 1. 清空接收缓冲 (避免读到上一条指令的残留回显) */
    {
        uint8_t dummy[LORA_BUFFER_SIZE];
        LoRa_ReceiveData(dummy, sizeof(dummy));
    }

    /* 2. 进入配置模式, 等 300ms 稳定 (正点原子教程) */
    LoRa_SetMode(1);
    Lora_DelayMs(300);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 3. 发送 AT 指令 + \r\n */
    uint16_t len = (uint16_t)strlen(cmd);
    LoRa_SendData((uint8_t *)cmd, len);
    LoRa_SendData((uint8_t *)"\r\n", 2);

    /* 4. 等待模块开始处理 (AUX=HIGH), 超时 200ms 防止死等 */
    {
        uint32_t elapsed = 0;
        while (LoRa_AuxReady() && elapsed < 200) {
            Lora_DelayMs(1);
            elapsed++;
        }
    }

    /* 5. 等待模块处理完成 (AUX=LOW), 响应就绪 */
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    /* 6. 读取响应 */
    uint16_t rlen = LoRa_ReceiveData((uint8_t *)resp, resp_max - 1);
    if (rlen > 0) {
        resp[rlen] = '\0';
        ret = 0;
    }

    /* 7. 回到透传模式, 等 300ms 稳定 */
    LoRa_SetMode(0);
    Lora_DelayMs(300);
    LoRa_WaitAux(LORA_AUX_TIMEOUT_MS);

    return ret;
}
