#include "rc522.h"
#include "stm32f10x_spi.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "usart_debug.h"

/* ========== 内部辅助宏 ========== */
#define RC522_CS_LOW()   GPIO_ResetBits(RC522_CS_PORT, RC522_CS_PIN)
#define RC522_CS_HIGH()  GPIO_SetBits(RC522_CS_PORT, RC522_CS_PIN)
#define RC522_RST_HIGH() GPIO_SetBits(RC522_RST_PORT, RC522_RST_PIN)

/* SPI 收发等待宏 (带超时保护, 防止 SPI 总线异常时死循环 → WDG 复位)
 * 正常 SPI @4.5MHz 下 TXE/RXNE 在 ~2μs 内就绪。
 * 2000 次循环 ≈ 140μs 超时，足够安全裕度且不会触发 WDG。 */
#define SPI_WAIT_TXE()   do { \
    uint32_t _spi_to = 2000; \
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET && --_spi_to > 0); \
} while(0)
#define SPI_WAIT_RXNE()  do { \
    uint32_t _spi_to = 2000; \
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_RXNE) == RESET && --_spi_to > 0); \
} while(0)

/* ========== 初始化 ========== */
void RC522_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    SPI_InitTypeDef  SPI_InitStructure;

    /* 释放 JTAG 占用的 PB4(JNTRST), 保留 SWD(PA13/PA14) 用于调试 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

    /* CS (PB4) + RST (PB5) — 推挽输出 */
    GPIO_InitStructure.GPIO_Pin   = RC522_CS_PIN | RC522_RST_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(RC522_CS_PORT, &GPIO_InitStructure);

    /* SPI2: SCK(PB13) + MISO(PB14) + MOSI(PB15) — 复用推挽 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* SPI2 配置 */
    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16;
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial     = 7;
    SPI_Init(SPI2, &SPI_InitStructure);
    SPI_Cmd(SPI2, ENABLE);

    /* 初始电平 */
    RC522_CS_HIGH();
    RC522_RST_HIGH();

    /* 模块初始化序列 */
    RC522_Reset();
    RC522_WriteRegister(RC522_REG_T_MODE,      RC522_T_MODE_VAL);
    RC522_WriteRegister(RC522_REG_T_PRESCALER, RC522_T_PRESCALER_VAL);
    RC522_WriteRegister(RC522_REG_T_COUNTER,   RC522_T_COUNTER_VAL);
    RC522_WriteRegister(RC522_REG_T_RELOAD,    RC522_T_RELOAD_VAL);
    RC522_WriteRegister(RC522_REG_TX_ASK,      RC522_TX_ASK_VAL);
    RC522_WriteRegister(RC522_REG_MODE,        RC522_MODE_VAL);
    RC522_AntennaOn();

    /* 诊断: 读 VersionReg (0x37), MFRC522 应为 0x92 */
    {
        uint8_t ver = RC522_ReadRegister(0x37);
        Debug_Printf("[RC522] VersionReg=0x%02X (expected 0x92)\r\n", ver);
    }
}

/* ========== 寄存器读写 ========== */

void RC522_Reset(void)
{
    uint32_t to;

    RC522_WriteRegister(RC522_REG_COMMAND, 0x0F);

    /* 等待软复位完成: CommandReg 从 0x20 变为 0x00, ~50μs */
    to = 10000;
    while (RC522_ReadRegister(RC522_REG_COMMAND) != 0x00 && --to > 0);
}

void RC522_WriteRegister(uint8_t addr, uint8_t value)
{
    RC522_CS_LOW();

    /* 发送地址字节 */
    SPI_WAIT_TXE();
    SPI_I2S_SendData(SPI2, (addr << 1) & 0x7E);
    SPI_WAIT_RXNE();
    SPI_I2S_ReceiveData(SPI2);

    /* 发送数据字节 */
    SPI_WAIT_TXE();
    SPI_I2S_SendData(SPI2, value);
    SPI_WAIT_RXNE();
    SPI_I2S_ReceiveData(SPI2);

    RC522_CS_HIGH();
}

uint8_t RC522_ReadRegister(uint8_t addr)
{
    uint8_t value;

    RC522_CS_LOW();

    /* 发送读地址 (最高位 = 1) */
    SPI_WAIT_TXE();
    SPI_I2S_SendData(SPI2, ((addr << 1) & 0x7E) | 0x80);
    SPI_WAIT_RXNE();
    SPI_I2S_ReceiveData(SPI2);

    /* 发送空字节，读取返回值 */
    SPI_WAIT_TXE();
    SPI_I2S_SendData(SPI2, 0x00);
    SPI_WAIT_RXNE();
    value = (uint8_t)SPI_I2S_ReceiveData(SPI2);

    RC522_CS_HIGH();
    return value;
}

void RC522_SetBitMask(uint8_t addr, uint8_t mask)
{
    uint8_t tmp = RC522_ReadRegister(addr);
    RC522_WriteRegister(addr, tmp | mask);
}

void RC522_ClearBitMask(uint8_t addr, uint8_t mask)
{
    uint8_t tmp = RC522_ReadRegister(addr);
    RC522_WriteRegister(addr, tmp & (~mask));
}

/* ========== 天线控制 ========== */

void RC522_AntennaOn(void)
{
    uint8_t tmp = RC522_ReadRegister(RC522_REG_TX_CONTROL);
    if (!(tmp & 0x03))
    {
        RC522_SetBitMask(RC522_REG_TX_CONTROL, 0x03);
    }
}

void RC522_AntennaOff(void)
{
    RC522_ClearBitMask(RC522_REG_TX_CONTROL, 0x03);
}

/* ========== 卡片操作 ========== */

uint8_t RC522_Request(uint8_t reqMode, uint8_t *TagType)
{
    uint8_t  status;
    uint16_t backBits;

    RC522_WriteRegister(RC522_REG_BIT_FRAMING, 0x07);
    TagType[0] = reqMode;

    status = RC522_ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backBits);

    /* MI_NOTAGERR(1) = 无卡, 是正常状态, 不要转成 MI_ERR */
    if (status == MI_OK && backBits != 0x10)
    {
        status = MI_ERR;
    }
    return status;
}

uint8_t RC522_ToCard(uint8_t  command,
                     uint8_t *sendData, uint8_t  sendLen,
                     uint8_t *backData,  uint16_t *backLen)
{
    uint8_t  status   = MI_ERR;
    uint8_t  irqEn    = 0x00;
    uint8_t  waitIRq  = 0x00;
    uint8_t  lastBits;
    uint8_t  n;
    uint16_t i;

    switch (command)
    {
        case PCD_AUTHENT:
            irqEn   = 0x12;
            waitIRq = 0x10;
            break;
        case PCD_TRANSCEIVE:
            irqEn   = 0x77;
            waitIRq = 0x30;
            break;
        default:
            break;
    }

    RC522_WriteRegister(RC522_REG_COM_IEN,  irqEn | RC522_FIFO_FLUSH);
    RC522_ClearBitMask(RC522_REG_COM_IRQ,  0x80);
    RC522_SetBitMask(RC522_REG_FIFO_LEVEL, 0x80);
    RC522_WriteRegister(RC522_REG_COMMAND, PCD_IDLE);

    /* 写入 FIFO */
    for (i = 0; i < sendLen; i++)
    {
        RC522_WriteRegister(RC522_REG_FIFO_DATA, sendData[i]);
    }

    /* 执行命令 */
    RC522_WriteRegister(RC522_REG_COMMAND, command);

    if (command == PCD_TRANSCEIVE)
    {
        RC522_SetBitMask(RC522_REG_BIT_FRAMING, 0x80);
    }

    /* 等待命令完成（带超时） */
    i = 600;
    do
    {
        n = RC522_ReadRegister(RC522_REG_COM_IRQ);
        i--;
    } while ((i != 0) && !(n & IRQ_IDLE) && !(n & waitIRq));

    RC522_ClearBitMask(RC522_REG_BIT_FRAMING, 0x80);

    if (i != 0)
    {
        if (!(RC522_ReadRegister(RC522_REG_ERROR) & 0x1B))
        {
            status = MI_OK;

            if (n & irqEn & IRQ_TIMER)
            {
                status = MI_NOTAGERR;
            }

            if (command == PCD_TRANSCEIVE)
            {
                n        = RC522_ReadRegister(RC522_REG_FIFO_LEVEL);
                lastBits = RC522_ReadRegister(RC522_REG_CONTROL) & 0x07;

                if (lastBits)
                    *backLen = (n - 1) * 8 + lastBits;
                else
                    *backLen = n * 8;

                if (n == 0)       n = 1;
                if (n > RC522_MAX_LEN) n = RC522_MAX_LEN;

                for (i = 0; i < n; i++)
                {
                    backData[i] = RC522_ReadRegister(RC522_REG_FIFO_DATA);
                }
            }
        }
        else
        {
            status = MI_ERR;
        }
    }

    return status;
}

uint8_t RC522_SelectTag(uint8_t *serNum)
{
    uint8_t  status;
    uint8_t  i;
    uint16_t backLen;

    RC522_WriteRegister(RC522_REG_BIT_FRAMING, 0x00);
    serNum[0] = PICC_SEL_CL1;
    serNum[1] = 0x20;

    status = RC522_ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &backLen);

    /* MIFARE Classic 1K (4B UID) → 应答 5 字节=40bit=0x28 */
    if ((status == MI_OK) && (backLen == 0x28))
    {
        /* serNum[0..3]=UID, serNum[4]=BCC, 无需移位 */
    }
    else if ((status == MI_OK) && (backLen == 0x18))
    {
        /* 3 字节应答 (兼容旧格式): 跳过第 1 字节 */
        for (i = 0; i < 5; i++)
            serNum[i] = serNum[i + 1];
    }
    else
    {
        status = MI_ERR;
    }
    return status;
}

uint8_t RC522_Auth(uint8_t authMode, uint8_t BlockAddr,
                   uint8_t *Sectorkey, uint8_t *serNum)
{
    uint8_t  status;
    uint16_t recvBits;
    uint8_t  i;
    uint8_t  buff[12];

    buff[0] = authMode;
    buff[1] = BlockAddr;
    for (i = 0; i < 6; i++)  buff[i + 2] = Sectorkey[i];
    for (i = 0; i < 4; i++)  buff[i + 8] = serNum[i];

    status = RC522_ToCard(PCD_AUTHENT, buff, 12, buff, &recvBits);

    if ((status != MI_OK) || (!(RC522_ReadRegister(RC522_REG_STATUS2) & 0x08)))
    {
        status = MI_ERR;
    }
    return status;
}

uint8_t RC522_ReadBlock(uint8_t blockAddr, uint8_t *recvData)
{
    uint8_t  status;
    uint16_t backLen;

    recvData[0] = PICC_CT;
    recvData[1] = blockAddr;

    status = RC522_ToCard(PCD_TRANSCEIVE, recvData, 2, recvData, &backLen);

    if ((status != MI_OK) || (backLen != 0x90))
    {
        status = MI_ERR;
    }
    return status;
}

uint8_t RC522_WriteBlock(uint8_t blockAddr, uint8_t *writeData)
{
    uint8_t  status;
    uint16_t backLen;
    uint8_t  i;
    uint8_t  buff[18];

    buff[0] = PICC_CT;
    buff[1] = blockAddr;
    for (i = 0; i < 16; i++)  buff[i + 2] = writeData[i];

    status = RC522_ToCard(PCD_TRANSCEIVE, buff, 18, buff, &backLen);

    if ((status != MI_OK) || ((buff[0] & 0x0F) != 0x0A))
    {
        status = MI_ERR;
    }
    return status;
}

void RC522_Halt(void)
{
    uint16_t unUsed;
    uint8_t  buff[4];

    buff[0] = PICC_HALT;
    buff[1] = 0;
    RC522_ToCard(PCD_TRANSCEIVE, buff, 2, buff, &unUsed);
}
