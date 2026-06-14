/**
 * 最小 LED 闪烁测试 —— 不依赖标准库、不依赖 main、不依赖 SystemInit
 * 仅用 Reset_Handler 直接闪灯，验证硬件基本功能
 */
#include "stm32f10x.h"

/* 使用自定义入口点，完全绕过 C 运行时初始化 */
__asm void __main(void)
{
    ; ===== 最小系统初始化 =====
    ; 1. 使能 GPIOB 时钟 (APB2)
    LDR     R0, =0x40021018     ; RCC_APB2ENR
    LDR     R1, [R0]
    ORR     R1, R1, #0x08      ; GPIOBEN (bit 3)
    STR     R1, [R0]

    ; 2. 配置 PB6 为推挽输出 (CNF=00, MODE=11)
    LDR     R0, =0x40010C00     ; GPIOB_CRL
    LDR     R1, [R0]
    BIC     R1, R1, #0x0F000000 ; 清 PB6 配置位 (24-27)
    ORR     R1, R1, #0x03000000 ; MODE6=11 (50MHz output)
    STR     R1, [R0]

    ; 3. PB6 初始高电平 (共阳极: LED 灭)
    LDR     R0, =0x40010C10     ; GPIOB_BSRR
    LDR     R1, =0x0040         ; PB6
    STR     R1, [R0]            ; BSRR -> PB6=H

    ; ===== 主循环: 闪烁 PB6 =====
blink_loop
    ; LED ON  (PB6 = 0)
    LDR     R0, =0x40010C14     ; GPIOB_BRR
    LDR     R1, =0x0040
    STR     R1, [R0]

    ; 延时 ~500ms
    LDR     R2, =4800000          ; 72MHz/约 500ms
delay_on
    SUBS    R2, R2, #1
    BNE     delay_on

    ; LED OFF (PB6 = 1)
    LDR     R0, =0x40010C10     ; GPIOB_BSRR
    LDR     R1, =0x0040
    STR     R1, [R0]

    ; 延时 ~500ms
    LDR     R2, =4800000
delay_off
    SUBS    R2, R2, #1
    BNE     delay_off

    B       blink_loop
}
