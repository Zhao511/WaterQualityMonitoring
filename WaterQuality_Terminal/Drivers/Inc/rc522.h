#ifndef __RC522_H
#define __RC522_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

/* ========== 硬件引脚定义 ========== */
#define RC522_CS_PIN    GPIO_Pin_4
#define RC522_CS_PORT   GPIOB
#define RC522_RST_PIN   GPIO_Pin_5
#define RC522_RST_PORT  GPIOB

#define RC522_MAX_LEN   16

/* ========== RC522 寄存器地址 ========== */
#define RC522_REG_COMMAND         0x01  /* 命令寄存器 */
#define RC522_REG_COM_IEN         0x02  /* 中断使能 */
#define RC522_REG_DIV_IEN         0x03  /* 中断请求 */
#define RC522_REG_COM_IRQ         0x04  /* 中断标志 */
#define RC522_REG_DIV_IRQ         0x05  /* 中断标志 */
#define RC522_REG_ERROR           0x06  /* 错误标志 */
#define RC522_REG_STATUS2         0x08  /* 接收状态 */
#define RC522_REG_FIFO_DATA       0x09  /* FIFO 数据 */
#define RC522_REG_FIFO_LEVEL      0x0A  /* FIFO 字节数 */
#define RC522_REG_CONTROL         0x0C  /* 控制寄存器 */
#define RC522_REG_BIT_FRAMING     0x0D  /* 位帧调整 */
#define RC522_REG_MODE            0x11  /* 收发模式 */
#define RC522_REG_TX_CONTROL      0x14  /* 天线驱动控制 */
#define RC522_REG_TX_ASK          0x15  /* 调制宽度 */
#define RC522_REG_T_MODE          0x2A  /* 定时器模式 */
#define RC522_REG_T_PRESCALER     0x2B  /* 定时器预分频 */
#define RC522_REG_T_RELOAD        0x2C  /* 定时器重载值 (高字节) */
#define RC522_REG_T_COUNTER       0x2D  /* 定时器计数器值 (低字节) */

/* ========== 配置值 ========== */
#define RC522_T_MODE_VAL          0x8D  /* 定时器自动启动 */
#define RC522_T_PRESCALER_VAL     0x3E  /* 预分频值 */
#define RC522_T_COUNTER_VAL       30    /* 定时器低字节 */
#define RC522_T_RELOAD_VAL        0     /* 定时器高字节 */
#define RC522_TX_ASK_VAL          0x40  /* 强制 100% ASK */
#define RC522_MODE_VAL            0x3D  /* CRC 校验值 0x6363 */

#define RC522_FIFO_FLUSH          0x80

/* ========== PCD 命令 ========== */
#define PCD_IDLE                  0x00
#define PCD_AUTHENT               0x0E
#define PCD_RECEIVE               0x08
#define PCD_TRANSMIT              0x04
#define PCD_TRANSCEIVE            0x0C
#define PCD_RESETPHASE            0x0F
#define PCD_CALCCRC               0x03

/* ========== PICC 命令 ========== */
#define PICC_REQA                 0x26
#define PICC_WUPA                 0x52
#define PICC_CT                   0x88
#define PICC_SEL_CL1              0x93
#define PICC_SEL_CL2              0x95
#define PICC_SEL_CL3              0x97
#define PICC_HALT                 0x50

/* ========== 状态码 ========== */
#define MI_OK                     0
#define MI_NOTAGERR               1
#define MI_ERR                    2

/* ========== 中断标志掩码 ========== */
#define IRQ_LO_ALERT              0x01
#define IRQ_IDLE                  0x04
#define IRQ_RX                    0x20
#define IRQ_TX                    0x10
#define IRQ_TIMER                 0x01

/* ========== API ========== */
void     RC522_Init(void);
void     RC522_Reset(void);
void     RC522_WriteRegister(uint8_t addr, uint8_t value);
uint8_t  RC522_ReadRegister(uint8_t addr);
void     RC522_SetBitMask(uint8_t addr, uint8_t mask);
void     RC522_ClearBitMask(uint8_t addr, uint8_t mask);
void     RC522_AntennaOn(void);
void     RC522_AntennaOff(void);
uint8_t  RC522_Request(uint8_t reqMode, uint8_t *TagType);
uint8_t  RC522_ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen,
                      uint8_t *backData, uint16_t *backLen);
uint8_t  RC522_SelectTag(uint8_t *serNum);
uint8_t  RC522_Auth(uint8_t authMode, uint8_t BlockAddr,
                    uint8_t *Sectorkey, uint8_t *serNum);
uint8_t  RC522_ReadBlock(uint8_t blockAddr, uint8_t *recvData);
uint8_t  RC522_WriteBlock(uint8_t blockAddr, uint8_t *writeData);
void     RC522_Halt(void);

#ifdef __cplusplus
}
#endif

#endif
