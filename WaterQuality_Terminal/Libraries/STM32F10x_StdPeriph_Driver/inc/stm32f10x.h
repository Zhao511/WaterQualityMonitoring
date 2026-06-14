#ifndef __STM32F10x_H
#define __STM32F10x_H

#ifdef __cplusplus
 extern "C" {
#endif

typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;
typedef signed int int32_t;
typedef signed short int16_t;
typedef signed char int8_t;

#define __IO volatile
#define __NOP      __nop

typedef enum {RESET = 0, SET = !RESET} FlagStatus, ITStatus;
typedef enum {ERROR = 0, SUCCESS = !ERROR} ErrorStatus;
typedef enum {DISABLE = 0, ENABLE = !DISABLE} FunctionalState;

#define IS_FUNCTIONAL_STATE(STATE) (((STATE) == DISABLE) || ((STATE) == ENABLE))

typedef enum IRQn {
  NonMaskableInt_IRQn         = -14,
  MemoryManagement_IRQn       = -12,
  BusFault_IRQn               = -11,
  UsageFault_IRQn             = -10,
  SVCall_IRQn                 = -5,
  DebugMonitor_IRQn           = -4,
  PendSV_IRQn                 = -2,
  SysTick_IRQn                = -1,
  WWDG_IRQn                   = 0,
  PVD_IRQn                    = 1,
  TAMPER_IRQn                 = 2,
  RTC_IRQn                    = 3,
  FLASH_IRQn                  = 4,
  RCC_IRQn                    = 5,
  EXTI0_IRQn                  = 6,
  EXTI1_IRQn                  = 7,
  EXTI2_IRQn                  = 8,
  EXTI3_IRQn                  = 9,
  EXTI4_IRQn                  = 10,
  DMA1_Channel1_IRQn          = 11,
  DMA1_Channel2_IRQn          = 12,
  DMA1_Channel3_IRQn          = 13,
  DMA1_Channel4_IRQn          = 14,
  DMA1_Channel5_IRQn          = 15,
  DMA1_Channel6_IRQn          = 16,
  DMA1_Channel7_IRQn          = 17,
  ADC1_2_IRQn                 = 18,
  USB_HP_CAN1_TX_IRQn         = 19,
  USB_LP_CAN1_RX0_IRQn        = 20,
  CAN1_RX1_IRQn               = 21,
  CAN1_SCE_IRQn               = 22,
  EXTI9_5_IRQn                = 23,
  TIM1_BRK_IRQn               = 24,
  TIM1_UP_IRQn                = 25,
  TIM1_TRG_COM_IRQn           = 26,
  TIM1_CC_IRQn                = 27,
  TIM2_IRQn                   = 28,
  TIM3_IRQn                   = 29,
  TIM4_IRQn                   = 30,
  I2C1_EV_IRQn                = 31,
  I2C1_ER_IRQn                = 32,
  I2C2_EV_IRQn                = 33,
  I2C2_ER_IRQn                = 34,
  SPI1_IRQn                   = 35,
  SPI2_IRQn                   = 36,
  USART1_IRQn                 = 37,
  USART2_IRQn                 = 38,
  USART3_IRQn                 = 39,
  EXTI15_10_IRQn              = 40,
  RTC_Alarm_IRQn              = 41,
  USBWakeUp_IRQn              = 42
} IRQn_Type;

typedef struct {
  __IO uint32_t EVCR;
  __IO uint32_t MAPR;
  __IO uint32_t EXTICR[4];
  uint32_t RESERVED0;
  __IO uint32_t MAPR2;
} AFIO_TypeDef;

typedef struct {
  __IO uint32_t CRL;
  __IO uint32_t CRH;
  __IO uint32_t IDR;
  __IO uint32_t ODR;
  __IO uint32_t BSRR;
  __IO uint32_t BRR;
  __IO uint32_t LCKR;
} GPIO_TypeDef;

typedef struct {
  __IO uint32_t CR;
  __IO uint32_t CFGR;
  __IO uint32_t CIR;
  __IO uint32_t APB2RSTR;
  __IO uint32_t APB1RSTR;
  __IO uint32_t AHBENR;
  __IO uint32_t APB2ENR;
  __IO uint32_t APB1ENR;
  __IO uint32_t BDCR;
  __IO uint32_t CSR;
} RCC_TypeDef;

typedef struct {
  __IO uint32_t SR;
  __IO uint32_t DR;
  __IO uint32_t BRR;
  __IO uint32_t CR1;
  __IO uint32_t CR2;
  __IO uint32_t CR3;
  __IO uint32_t GTPR;
} USART_TypeDef;

typedef struct {
  __IO uint32_t CR1;
  __IO uint32_t CR2;
  __IO uint32_t SR;
  __IO uint32_t DR;
  __IO uint32_t CRCPR;
  __IO uint32_t RXCRCR;
  __IO uint32_t TXCRCR;
  __IO uint32_t I2SCFGR;
  __IO uint32_t I2SPR;
} SPI_TypeDef;

typedef struct {
  __IO uint32_t SR;
  __IO uint32_t CR1;
  __IO uint32_t CR2;
  __IO uint32_t SMPR1;
  __IO uint32_t SMPR2;
  __IO uint32_t JOFR1;
  __IO uint32_t JOFR2;
  __IO uint32_t JOFR3;
  __IO uint32_t JOFR4;
  __IO uint32_t HTR;
  __IO uint32_t LTR;
  __IO uint32_t SQR1;
  __IO uint32_t SQR2;
  __IO uint32_t SQR3;
  __IO uint32_t JSQR;
  __IO uint32_t JDR1;
  __IO uint32_t JDR2;
  __IO uint32_t JDR3;
  __IO uint32_t JDR4;
  __IO uint32_t DR;
} ADC_TypeDef;

typedef struct {
  __IO uint32_t CPUID;
  __IO uint32_t ICSR;
  __IO uint32_t VTOR;
  __IO uint32_t AIRCR;
  __IO uint32_t SCR;
  __IO uint32_t CCR;
  __IO uint32_t SHPR1;
  __IO uint32_t SHPR2;
  __IO uint32_t SHPR3;
  __IO uint32_t SHCSR;
  __IO uint32_t CFSR;
  __IO uint32_t HFSR;
  __IO uint32_t DFSR;
  __IO uint32_t MMFAR;
  __IO uint32_t BFAR;
  __IO uint32_t AFSR;
} SCB_TypeDef;

typedef struct {
  __IO uint32_t ISER[8];
  __IO uint32_t ICER[8];
  __IO uint32_t ISPR[8];
  __IO uint32_t ICPR[8];
  __IO uint32_t IABR[8];
  __IO uint8_t IP[240];
  __IO uint32_t STIR;
} NVIC_TypeDef;

typedef struct {
  uint8_t NVIC_IRQChannel;
  uint8_t NVIC_IRQChannelPreemptionPriority;
  uint8_t NVIC_IRQChannelSubPriority;
  FunctionalState NVIC_IRQChannelCmd;
} NVIC_InitTypeDef;

#define PERIPH_BASE           ((uint32_t)0x40000000)
#define APB2PERIPH_BASE       (PERIPH_BASE + 0x10000)
#define AHBPERIPH_BASE        (PERIPH_BASE + 0x20000)
#define APB1PERIPH_BASE       (PERIPH_BASE + 0x00000)
#define SCS_BASE              ((uint32_t)0xE000E000)

#define AFIO_BASE             (APB2PERIPH_BASE + 0x0000)
#define GPIOA_BASE            (APB2PERIPH_BASE + 0x0800)
#define GPIOB_BASE            (APB2PERIPH_BASE + 0x0C00)
#define GPIOC_BASE            (APB2PERIPH_BASE + 0x1000)
#define GPIOD_BASE            (APB2PERIPH_BASE + 0x1400)
#define RCC_BASE              (AHBPERIPH_BASE + 0x1000)
#define USART1_BASE           (APB2PERIPH_BASE + 0x3800)
#define USART2_BASE           (APB1PERIPH_BASE + 0x4400)
#define USART3_BASE           (APB1PERIPH_BASE + 0x4800)
#define SPI2_BASE             (APB1PERIPH_BASE + 0x3800)
#define ADC1_BASE             (APB2PERIPH_BASE + 0x2400)
#define SCB_BASE              (SCS_BASE + 0x0D00)
#define NVIC_BASE             (SCS_BASE + 0x0100)

#define AFIO                  ((AFIO_TypeDef *) AFIO_BASE)
#define GPIOA                 ((GPIO_TypeDef *) GPIOA_BASE)
#define GPIOB                 ((GPIO_TypeDef *) GPIOB_BASE)
#define GPIOC                 ((GPIO_TypeDef *) GPIOC_BASE)
#define GPIOD                 ((GPIO_TypeDef *) GPIOD_BASE)
#define RCC                   ((RCC_TypeDef *) RCC_BASE)
#define USART1                ((USART_TypeDef *) USART1_BASE)
#define USART2                ((USART_TypeDef *) USART2_BASE)
#define USART3                ((USART_TypeDef *) USART3_BASE)
#define SPI2                  ((SPI_TypeDef *) SPI2_BASE)
#define ADC1                  ((ADC_TypeDef *) ADC1_BASE)
#define SCB                   ((SCB_TypeDef *) SCB_BASE)
#define NVIC                  ((NVIC_TypeDef *) NVIC_BASE)

#define RCC_APB2Periph_AFIO    ((uint32_t)0x00000001)
#define RCC_APB2Periph_GPIOA   ((uint32_t)0x00000004)
#define RCC_APB2Periph_GPIOB   ((uint32_t)0x00000008)
#define RCC_APB2Periph_GPIOC   ((uint32_t)0x00000010)
#define RCC_APB2Periph_GPIOD   ((uint32_t)0x00000020)
#define RCC_APB2Periph_USART1  ((uint32_t)0x00004000)
#define RCC_APB2Periph_ADC1    ((uint32_t)0x00000200)
#define RCC_APB1Periph_USART2  ((uint32_t)0x00020000)
#define RCC_APB1Periph_USART3  ((uint32_t)0x00040000)
#define RCC_APB1Periph_SPI2    ((uint32_t)0x00004000)

#define USART_FLAG_TXE         ((uint16_t)0x0080)
#define USART_FLAG_RXNE        ((uint16_t)0x0020)
#define USART_FLAG_TC          ((uint16_t)0x0040)

#define ADC_FLAG_EOC           ((uint8_t)0x01)

#define GPIO_Pin_0             ((uint16_t)0x0001)
#define GPIO_Pin_1             ((uint16_t)0x0002)
#define GPIO_Pin_2             ((uint16_t)0x0004)
#define GPIO_Pin_3             ((uint16_t)0x0008)
#define GPIO_Pin_4             ((uint16_t)0x0010)
#define GPIO_Pin_5             ((uint16_t)0x0020)
#define GPIO_Pin_6             ((uint16_t)0x0040)
#define GPIO_Pin_7             ((uint16_t)0x0080)
#define GPIO_Pin_8             ((uint16_t)0x0100)
#define GPIO_Pin_9             ((uint16_t)0x0200)
#define GPIO_Pin_10            ((uint16_t)0x0400)
#define GPIO_Pin_11            ((uint16_t)0x0800)
#define GPIO_Pin_12            ((uint16_t)0x1000)
#define GPIO_Pin_13            ((uint16_t)0x2000)
#define GPIO_Pin_14            ((uint16_t)0x4000)
#define GPIO_Pin_15            ((uint16_t)0x8000)

typedef struct {
  uint16_t GPIO_Pin;
  uint32_t GPIO_Speed;
  uint16_t GPIO_Mode;
} GPIO_InitTypeDef;

typedef struct {
  uint32_t USART_BaudRate;
  uint16_t USART_WordLength;
  uint16_t USART_StopBits;
  uint16_t USART_Parity;
  uint16_t USART_Mode;
  uint16_t USART_HardwareFlowControl;
} USART_InitTypeDef;

typedef struct {
  uint16_t SPI_Direction;
  uint16_t SPI_Mode;
  uint16_t SPI_DataSize;
  uint16_t SPI_CPOL;
  uint16_t SPI_CPHA;
  uint16_t SPI_NSS;
  uint16_t SPI_BaudRatePrescaler;
  uint16_t SPI_FirstBit;
  uint16_t SPI_CRCPolynomial;
} SPI_InitTypeDef;

typedef struct {
  uint32_t ADC_Mode;
  FunctionalState ADC_ScanConvMode;
  FunctionalState ADC_ContinuousConvMode;
  uint32_t ADC_ExternalTrigConv;
  uint32_t ADC_DataAlign;
  uint8_t ADC_NbrOfChannel;
} ADC_InitTypeDef;

typedef struct {
  __IO uint32_t ACR;
  __IO uint32_t KEYR;
  __IO uint32_t OPTKEYR;
  __IO uint32_t SR;
  __IO uint32_t CR;
  __IO uint32_t AR;
  __IO uint32_t RESERVED;
  __IO uint32_t OBR;
  __IO uint32_t WRPR;
} FLASH_TypeDef;

#define FLASH_BASE             ((uint32_t)0x40022000)
#define FLASH                  ((FLASH_TypeDef *) FLASH_BASE)

#define FLASH_ACR_LATENCY_2    ((uint32_t)0x00000010)

#define RCC_CFGR_SWS           ((uint32_t)0x0000000C)
#define RCC_CFGR_PLLMULL       ((uint32_t)0x0000F800)
#define RCC_CFGR_PLLSRC        ((uint32_t)0x00010000)

#define HSI_VALUE              ((uint32_t)8000000)
#define HSE_VALUE              ((uint32_t)8000000)

#define RCC_HCLK_Div2          ((uint32_t)0x00000400)

#ifdef USE_STDPERIPH_DRIVER
  #include "stm32f10x_conf.h"
#endif

#ifdef __cplusplus
}
#endif

#endif
