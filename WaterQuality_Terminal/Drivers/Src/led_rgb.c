#include "led_rgb.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

/**
 * @brief  初始化 RGB LED (PB6/PB7/PB8)
 */
void LED_RGB_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = LED_RED_PIN | LED_GREEN_PIN | LED_BLUE_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &GPIO_InitStructure);

    LED_RGB_Off();
}

/**
 * @brief  设置预定义颜色
 * @param  color: LED_Color 枚举值
 */
void LED_RGB_SetColor(LED_Color color)
{
    switch (color)
    {
        case LED_COLOR_RED:
            LED_ON(LED_RED_PIN);
            LED_OFF(LED_GREEN_PIN | LED_BLUE_PIN);
            break;

        case LED_COLOR_GREEN:
            LED_ON(LED_GREEN_PIN);
            LED_OFF(LED_RED_PIN | LED_BLUE_PIN);
            break;

        case LED_COLOR_BLUE:
            LED_ON(LED_BLUE_PIN);
            LED_OFF(LED_RED_PIN | LED_GREEN_PIN);
            break;

        case LED_COLOR_YELLOW:
            LED_ON(LED_RED_PIN | LED_GREEN_PIN);
            LED_OFF(LED_BLUE_PIN);
            break;

        case LED_COLOR_CYAN:
            LED_ON(LED_GREEN_PIN | LED_BLUE_PIN);
            LED_OFF(LED_RED_PIN);
            break;

        case LED_COLOR_MAGENTA:
            LED_ON(LED_RED_PIN | LED_BLUE_PIN);
            LED_OFF(LED_GREEN_PIN);
            break;

        case LED_COLOR_WHITE:
            LED_ON(LED_RED_PIN | LED_GREEN_PIN | LED_BLUE_PIN);
            break;

        case LED_COLOR_OFF:
        default:
            LED_RGB_Off();
            break;
    }
}

/**
 * @brief  设置 RGB 三通道独立开关（PWM 不可用时替代方案）
 * @param  red:   0=灭, >0=亮
 * @param  green: 0=灭, >0=亮
 * @param  blue:  0=灭, >0=亮
 */
void LED_RGB_SetColorRGB(uint8_t red, uint8_t green, uint8_t blue)
{
    if (red)   LED_ON(LED_RED_PIN);
    else       LED_OFF(LED_RED_PIN);

    if (green) LED_ON(LED_GREEN_PIN);
    else       LED_OFF(LED_GREEN_PIN);

    if (blue)  LED_ON(LED_BLUE_PIN);
    else       LED_OFF(LED_BLUE_PIN);
}

/**
 * @brief  关闭所有 LED 通道
 */
void LED_RGB_Off(void)
{
    LED_ALL_OFF();
}
