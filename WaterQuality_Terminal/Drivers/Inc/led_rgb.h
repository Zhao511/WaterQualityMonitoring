#ifndef __LED_RGB_H
#define __LED_RGB_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

/* ========== 硬件引脚定义 ========== */
#define LED_RED_PIN    GPIO_Pin_6
#define LED_GREEN_PIN  GPIO_Pin_7
#define LED_BLUE_PIN   GPIO_Pin_8
#define LED_PORT       GPIOB

/*
 * ========== LED 模块类型配置 ==========
 * 当前模块为共阳极 (Common Anode)：低电平点亮
 * 如更换为共阴极模块，将 CA 改为 0 即可全局生效
 */
#define LED_COMMON_ANODE  1   /* 1=共阳极, 0=共阴极 */

#if LED_COMMON_ANODE
  /* 共阳极: ResetBits=点亮, SetBits=熄灭 */
  #define LED_ON(pin)   GPIO_ResetBits(LED_PORT, (pin))
  #define LED_OFF(pin)  GPIO_SetBits(LED_PORT, (pin))
  #define LED_ALL_OFF() GPIO_SetBits(LED_PORT, LED_RED_PIN | LED_GREEN_PIN | LED_BLUE_PIN)
#else
  /* 共阴极: SetBits=点亮, ResetBits=熄灭 */
  #define LED_ON(pin)   GPIO_SetBits(LED_PORT, (pin))
  #define LED_OFF(pin)  GPIO_ResetBits(LED_PORT, (pin))
  #define LED_ALL_OFF() GPIO_ResetBits(LED_PORT, LED_RED_PIN | LED_GREEN_PIN | LED_BLUE_PIN)
#endif

/* ========== 颜色枚举 ========== */
typedef enum {
    LED_COLOR_RED = 0,
    LED_COLOR_GREEN,
    LED_COLOR_BLUE,
    LED_COLOR_YELLOW,
    LED_COLOR_CYAN,
    LED_COLOR_MAGENTA,
    LED_COLOR_WHITE,
    LED_COLOR_OFF
} LED_Color;

/* ========== API ========== */
void LED_RGB_Init(void);
void LED_RGB_SetColor(LED_Color color);
void LED_RGB_SetColorRGB(uint8_t red, uint8_t green, uint8_t blue);
void LED_RGB_Off(void);

#ifdef __cplusplus
}
#endif

#endif
