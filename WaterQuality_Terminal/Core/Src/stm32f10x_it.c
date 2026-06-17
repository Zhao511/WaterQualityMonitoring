/**
 * 中断服务程序 (FreeRTOS 版本)
 *
 * USART 分配:
 *   USART1 (PA9/PA10)  — 调试输出 → 板载 USB-UART
 *   USART2 (PD5/PD6)   — GPS 模块 (重映射)
 *   USART3 (PB10/PB11) — LoRa 模块
 *
 * 故障诊断:
 *   HardFault / MemManage / BusFault — 保存现场到 fault_snapshot
 */

#include "stm32f10x_it.h"
#include "lora.h"
#include "gps.h"
#include "usart_debug.h"

extern uint8_t  gps_rx_buffer[];
extern uint16_t gps_rx_index;
extern uint8_t  lora_rx_buffer[];
extern uint16_t lora_rx_index;
extern uint8_t  debug_rx_buffer[];
extern uint16_t debug_rx_index;

/* ================================================================
 *  故障现场快照 —— 发生 HardFault/BusFault/MemManage 时自动保存
 *  调试方法: Keil 暂停后在 Watch 窗口输入 fault_snapshot 查看全部字段
 *  fault_snapshot.pc → 故障指令地址，Disassembly 窗口跳转定位
 *  fault_snapshot.cfsr/hfsr/bfar → SCB 故障寄存器，查表定位根因
 *
 *  注意: 不加 static，否则 ARMCC 汇编器无法通过 LDR =fault_snapshot 引用
 *  使用命名结构体 typedef，否则 Keil Watch 窗口无法解析匿名 struct
 * ================================================================ */
typedef volatile struct {
    uint32_t r0, r1, r2, r3, r12, lr, pc, psr;
    uint32_t cfsr, hfsr, mmfar, bfar;
    uint32_t msp, psp;
    uint32_t fault_type;  /* 0=HardFault, 1=MemManage, 2=BusFault, 3=UsageFault */
} FaultSnapshot;

FaultSnapshot fault_snapshot;

static void UART_RX_ISR(USART_TypeDef *USARTx, uint8_t *buf,
                         uint16_t *idx, uint16_t buf_size)
{
    /* 溢出错误处理: 读 SR(已由if完成)→读 DR 即可清除 ORE */
    if (USARTx->SR & (uint32_t)0x00000008)           /* ORE = bit3 */
    {
        (void)USARTx->DR;                            /* 读 DR 清 ORE */
    }

    if (USART_GetITStatus(USARTx, USART_IT_RXNE) != RESET)
    {
        uint8_t data = (uint8_t)USART_ReceiveData(USARTx);
        if (*idx < buf_size)
            buf[(*idx)++] = data;
        else
        {
            /* 缓冲区满: 丢弃当前字节, 保护已有数据
             * 防止 idx 重置为 0 导致已积累数据被破坏 (如 ping 报文碎片化) */
        }
        USART_ClearITPendingBit(USARTx, USART_IT_RXNE);
    }
}

/* USART1 — Debug */
void USART1_IRQHandler(void)
{
    UART_RX_ISR(USART1, debug_rx_buffer, &debug_rx_index, DEBUG_BUFFER_SIZE);
}

/* USART2 — GPS */
void USART2_IRQHandler(void)
{
    UART_RX_ISR(USART2, gps_rx_buffer, &gps_rx_index, GPS_BUFFER_SIZE);
}

/* USART3 — LoRa */
void USART3_IRQHandler(void)
{
    UART_RX_ISR(USART3, lora_rx_buffer, &lora_rx_index, LORA_BUFFER_SIZE);
}

/* ================================================================
 *  故障诊断 Handler — 覆盖 startup.s 中的 [WEAK] 弱符号
 *
 *  Cortex-M3 自动压栈顺序(低地址→高地址): R0,R1,R2,R3,R12,LR,PC,xPSR
 *  使用 naked 属性防止编译器自动压栈/修改 SP，确保读取真实现场
 *
 *  注意: __asm 语法在 ARMCCv5 中使用 { } 块, 在 GCC 中使用 volatile("...")
 * ================================================================ */
#if defined(__CC_ARM)
/*
 * Keil ARMCC v5 inline assembly handlers
 * WARNING: ARMCC assembler does NOT support non-ASCII characters inside
 * __asm { } blocks. All comments MUST be ASCII-only.
 *
 * Safety: MSP is validated before reading the exception stack frame.
 * If MSP is outside valid SRAM (0x20000000-0x20005000), frame extraction
 * is skipped to prevent a secondary fault / Lockup.
 */
#define SRAM_START 0x20000000
#define SRAM_END   0x20005000

__asm void HardFault_Handler(void)
{
    PRESERVE8
    IMPORT  fault_snapshot

    LDR     R2, =fault_snapshot

    ;; Step 0: save MSP/PSP first (register reads, always safe)
    MRS     R0, MSP
    STR     R0, [R2, #48]
    MRS     R0, PSP
    STR     R0, [R2, #52]

    ;; Step 1: check if MSP is in valid SRAM range
    MRS     R0, MSP
    LDR     R1, =SRAM_START
    CMP     R0, R1
    BLO     hf_skip_frame
    LDR     R1, =SRAM_END
    CMP     R0, R1
    BHS     hf_skip_frame

    ;; Step 2a: MSP valid, extract stacked frame (8 regs)
    LDR     R1, [R0, #0]
    STR     R1, [R2, #0]            ;; r0
    LDR     R1, [R0, #4]
    STR     R1, [R2, #4]            ;; r1
    LDR     R1, [R0, #8]
    STR     R1, [R2, #8]            ;; r2
    LDR     R1, [R0, #12]
    STR     R1, [R2, #12]           ;; r3
    LDR     R1, [R0, #16]
    STR     R1, [R2, #16]           ;; r12
    LDR     R1, [R0, #20]
    STR     R1, [R2, #20]           ;; lr (EXC_RETURN)
    LDR     R1, [R0, #24]
    STR     R1, [R2, #24]           ;; pc (faulting instruction)
    LDR     R1, [R0, #28]
    STR     R1, [R2, #28]           ;; xpsr
    B        hf_save_scb

    ;; Step 2b: MSP invalid (stack overflow), zero frame fields
hf_skip_frame
    MOVS    R1, #0
    STR     R1, [R2, #0]            ;; r0=0 (marker: MSP was corrupt)
    STR     R1, [R2, #4]            ;; r1=0
    STR     R1, [R2, #8]            ;; r2=0
    STR     R1, [R2, #12]           ;; r3=0
    STR     R1, [R2, #16]           ;; r12=0
    STR     R1, [R2, #20]           ;; lr=0
    STR     R1, [R2, #24]           ;; pc=0 (key: pc=0 means MSP corrupt)
    STR     R1, [R2, #28]           ;; xpsr=0

    ;; Step 3: save SCB fault regs (always safe)
hf_save_scb
    LDR     R3, =0xE000ED28
    LDR     R1, [R3, #0]
    STR     R1, [R2, #32]           ;; CFSR
    LDR     R1, [R3, #4]
    STR     R1, [R2, #36]           ;; HFSR
    LDR     R1, [R3, #12]
    STR     R1, [R2, #40]           ;; MMFAR
    LDR     R1, [R3, #16]
    STR     R1, [R2, #44]           ;; BFAR

    MOVS    R1, #0
    STR     R1, [R2, #56]           ;; fault_type = 0

    ;; Step 4: Signal fault type via USART1 (0x48='H' if TXE ready)
    LDR     R3, =0x40013800         ;; USART1_SR
    LDR     R1, [R3, #0]
    LSLS    R1, R1, #25             ;; TXE(bit7)->N flag
    BPL     hf_uart_skip            ;; TXE not ready, skip
    LDR     R3, =0x40013804         ;; USART1_DR
    MOVS    R1, #0x48               ;; 'H'
    STRH    R1, [R3, #0]
hf_uart_skip

    ;; Step 5: Configure PB6 push-pull + LED blink 3 times
    LDR     R3, =0x40010C00         ;; GPIOB_CRL
    LDR     R1, [R3, #0]
    LDR     R2, =0xF0FFFFFF         ;; Clear bits[27:24] for PB6
    ANDS    R1, R1, R2
    ORR     R1, R1, #0x03000000     ;; MODE6=11, CNF6=00 (GPIO PP 50MHz)
    STR     R1, [R3, #0]
    MOVS    R1, #0x0040             ;; PB6 mask
    MOVS    R4, #3                  ;; 3 blinks
hf_blink
    STR     R1, [R3, #0x14]         ;; BRR: PB6=LOW, LED ON (common-anode)
    LDR     R2, =0xFFFFF
hf_dly1 SUBS    R2, R2, #1
    BNE     hf_dly1
    STR     R1, [R3, #0x10]         ;; BSRR: PB6=HIGH, LED OFF
    LDR     R2, =0xFFFFF
hf_dly2 SUBS    R2, R2, #1
    BNE     hf_dly2
    SUBS    R4, R4, #1
    BNE     hf_blink

    ;; [SET BREAKPOINT HERE] -> Watch fault_snapshot
    B        .
    ALIGN
}

__asm void MemManage_Handler(void)
{
    PRESERVE8
    IMPORT  fault_snapshot

    LDR     R2, =fault_snapshot

    MRS     R0, MSP
    STR     R0, [R2, #48]
    MRS     R0, PSP
    STR     R0, [R2, #52]

    MRS     R0, MSP
    LDR     R1, =SRAM_START
    CMP     R0, R1
    BLO     mm_skip
    LDR     R1, =SRAM_END
    CMP     R0, R1
    BHS     mm_skip

    LDR     R1, [R0, #24]
    STR     R1, [R2, #24]
    B        mm_save_scb

mm_skip
    MOVS    R1, #0
    STR     R1, [R2, #24]

mm_save_scb
    LDR     R3, =0xE000ED28
    LDR     R1, [R3, #0]
    STR     R1, [R2, #32]           ;; CFSR
    LDR     R1, [R3, #12]
    STR     R1, [R2, #40]           ;; MMFAR
    MOVS    R1, #1
    STR     R1, [R2, #56]

    ;; Signal fault via USART1 (0x4D='M')
    LDR     R3, =0x40013800
    LDR     R1, [R3, #0]
    LSLS    R1, R1, #25
    BPL     mm_uart_skip
    LDR     R3, =0x40013804
    MOVS    R1, #0x4D               ;; 'M'
    STRH    R1, [R3, #0]
mm_uart_skip

    ;; Configure PB6 + blink 3 times
    LDR     R3, =0x40010C00
    LDR     R1, [R3, #0]
    LDR     R2, =0xF0FFFFFF
    ANDS    R1, R1, R2
    ORR     R1, R1, #0x03000000
    STR     R1, [R3, #0]
    MOVS    R1, #0x0040
    MOVS    R4, #3
mm_blink
    STR     R1, [R3, #0x14]
    LDR     R2, =0xFFFFF
mm_dly1 SUBS    R2, R2, #1
    BNE     mm_dly1
    STR     R1, [R3, #0x10]
    LDR     R2, =0xFFFFF
mm_dly2 SUBS    R2, R2, #1
    BNE     mm_dly2
    SUBS    R4, R4, #1
    BNE     mm_blink
    B        .
    ALIGN
}

__asm void BusFault_Handler(void)
{
    PRESERVE8
    IMPORT  fault_snapshot

    LDR     R2, =fault_snapshot

    MRS     R0, MSP
    STR     R0, [R2, #48]
    MRS     R0, PSP
    STR     R0, [R2, #52]

    MRS     R0, MSP
    LDR     R1, =SRAM_START
    CMP     R0, R1
    BLO     bf_skip
    LDR     R1, =SRAM_END
    CMP     R0, R1
    BHS     bf_skip

    LDR     R1, [R0, #24]
    STR     R1, [R2, #24]
    B        bf_save_scb

bf_skip
    MOVS    R1, #0
    STR     R1, [R2, #24]

bf_save_scb
    LDR     R3, =0xE000ED28
    LDR     R1, [R3, #0]
    STR     R1, [R2, #32]           ;; CFSR
    LDR     R1, [R3, #16]
    STR     R1, [R2, #44]           ;; BFAR
    MOVS    R1, #2
    STR     R1, [R2, #56]

    ;; Signal fault via USART1 (0x42='B')
    LDR     R3, =0x40013800
    LDR     R1, [R3, #0]
    LSLS    R1, R1, #25
    BPL     bf_uart_skip
    LDR     R3, =0x40013804
    MOVS    R1, #0x42               ;; 'B'
    STRH    R1, [R3, #0]
bf_uart_skip

    ;; Configure PB6 + blink 3 times
    LDR     R3, =0x40010C00
    LDR     R1, [R3, #0]
    LDR     R2, =0xF0FFFFFF
    ANDS    R1, R1, R2
    ORR     R1, R1, #0x03000000
    STR     R1, [R3, #0]
    MOVS    R1, #0x0040
    MOVS    R4, #3
bf_blink
    STR     R1, [R3, #0x14]
    LDR     R2, =0xFFFFF
bf_dly1 SUBS    R2, R2, #1
    BNE     bf_dly1
    STR     R1, [R3, #0x10]
    LDR     R2, =0xFFFFF
bf_dly2 SUBS    R2, R2, #1
    BNE     bf_dly2
    SUBS    R4, R4, #1
    BNE     bf_blink
    B        .
    ALIGN
}

#else
/*
 * GNU GCC / Clang — __asm__ volatile 语法
 *
 * 同样的 MSP 校验逻辑: 防止栈溢出后读取非法栈帧导致 Lockup
 */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile (
        "LDR    R2, =fault_snapshot             \n"

        /* Step 0: save MSP/PSP first (safe, no memory access) */
        "MRS    R0, MSP                         \n"
        "STR    R0, [R2, #48]                   \n"
        "MRS    R0, PSP                         \n"
        "STR    R0, [R2, #52]                   \n"

        /* Step 1: validate MSP in SRAM [0x20000000, 0x20005000) */
        "MRS    R0, MSP                         \n"
        "LDR    R1, =0x20000000                 \n"
        "CMP    R0, R1                          \n"
        "BLO    0f                              \n"  /* skip if MSP < SRAM */
        "LDR    R1, =0x20005000                 \n"
        "CMP    R0, R1                          \n"
        "BHS    0f                              \n"  /* skip if MSP >= SRAM_END */

        /* Step 2a: MSP valid, extract stack frame */
        "LDR    R1, [R0, #0]                    \n"
        "STR    R1, [R2, #0]                    \n"  /* r0   */
        "LDR    R1, [R0, #4]                    \n"
        "STR    R1, [R2, #4]                    \n"  /* r1   */
        "LDR    R1, [R0, #8]                    \n"
        "STR    R1, [R2, #8]                    \n"  /* r2   */
        "LDR    R1, [R0, #12]                   \n"
        "STR    R1, [R2, #12]                   \n"  /* r3   */
        "LDR    R1, [R0, #16]                   \n"
        "STR    R1, [R2, #16]                   \n"  /* r12  */
        "LDR    R1, [R0, #20]                   \n"
        "STR    R1, [R2, #20]                   \n"  /* lr   */
        "LDR    R1, [R0, #24]                   \n"
        "STR    R1, [R2, #24]                   \n"  /* pc   */
        "LDR    R1, [R0, #28]                   \n"
        "STR    R1, [R2, #28]                   \n"  /* xpsr */
        "B      1f                              \n"

        /* Step 2b: MSP invalid, zero out frame regs */
        "0:                                     \n"
        "MOVS   R1, #0                          \n"
        "STR    R1, [R2, #0]                    \n"  /* r0=0 */
        "STR    R1, [R2, #4]                    \n"  /* r1=0 */
        "STR    R1, [R2, #8]                    \n"  /* r2=0 */
        "STR    R1, [R2, #12]                   \n"  /* r3=0 */
        "STR    R1, [R2, #16]                   \n"  /* r12=0 */
        "STR    R1, [R2, #20]                   \n"  /* lr=0 */
        "STR    R1, [R2, #24]                   \n"  /* pc=0 (MSP corrupted!) */
        "STR    R1, [R2, #28]                   \n"  /* xpsr=0 */

        /* Step 3: save SCB regs (always safe) */
        "1:                                     \n"
        "LDR    R3, =0xE000ED28                 \n"
        "LDR    R1, [R3, #0]                    \n"
        "STR    R1, [R2, #32]                   \n"  /* CFSR */
        "LDR    R1, [R3, #4]                    \n"
        "STR    R1, [R2, #36]                   \n"  /* HFSR */
        "LDR    R1, [R3, #12]                   \n"
        "STR    R1, [R2, #40]                   \n"  /* MMFAR */
        "LDR    R1, [R3, #16]                   \n"
        "STR    R1, [R2, #44]                   \n"  /* BFAR */

        "MOVS   R1, #0                          \n"
        "STR    R1, [R2, #56]                   \n"  /* type=0 */

        /* Signal fault via USART1 + LED blink 3x */
        "LDR    R3, =0x40013800                 \n"  /* USART1_SR */
        "LDR    R1, [R3, #0]                    \n"
        "LSLS   R1, R1, #25                     \n"
        "BPL    2f                              \n"
        "LDR    R3, =0x40013804                 \n"  /* USART1_DR */
        "MOVS   R1, #'H'                        \n"
        "STRH   R1, [R3, #0]                    \n"
        "2:                                     \n"
        "LDR    R3, =0x40010C00                 \n"  /* GPIOB_CRL */
        "LDR    R1, [R3, #0]                    \n"
        "LDR    R2, =0xF0FFFFFF                 \n"
        "ANDS   R1, R1, R2                      \n"
        "ORR    R1, R1, #0x03000000             \n"
        "STR    R1, [R3, #0]                    \n"
        "MOVS   R1, #0x0040                     \n"
        "MOVS   R4, #3                          \n"
        "3:                                     \n"
        "STR    R1, [R3, #0x14]                 \n"  /* BRR: ON */
        "LDR    R2, =0xFFFFF                    \n"
        "4:    SUBS    R2, R2, #1               \n"
        "BNE    4b                              \n"
        "STR    R1, [R3, #0x10]                 \n"  /* BSRR: OFF */
        "LDR    R2, =0xFFFFF                    \n"
        "5:    SUBS    R2, R2, #1               \n"
        "BNE    5b                              \n"
        "SUBS   R4, R4, #1                      \n"
        "BNE    3b                              \n"
        "B      .                               \n"
    );
}

__attribute__((naked)) void MemManage_Handler(void)
{
    __asm volatile (
        "LDR    R2, =fault_snapshot             \n"

        "MRS    R0, MSP                         \n"
        "STR    R0, [R2, #48]                   \n"
        "MRS    R0, PSP                         \n"
        "STR    R0, [R2, #52]                   \n"

        "MRS    R0, MSP                         \n"
        "LDR    R1, =0x20000000                 \n"
        "CMP    R0, R1                          \n"
        "BLO    0f                              \n"
        "LDR    R1, =0x20005000                 \n"
        "CMP    R0, R1                          \n"
        "BHS    0f                              \n"

        "LDR    R1, [R0, #24]                   \n"
        "STR    R1, [R2, #24]                   \n"
        "B      1f                              \n"

        "0:                                     \n"
        "MOVS   R1, #0                          \n"
        "STR    R1, [R2, #24]                   \n"

        "1:                                     \n"
        "LDR    R3, =0xE000ED28                 \n"
        "LDR    R1, [R3, #0]                    \n"
        "STR    R1, [R2, #32]                   \n"  /* CFSR */
        "LDR    R1, [R3, #12]                   \n"
        "STR    R1, [R2, #40]                   \n"  /* MMFAR */
        "MOVS   R1, #1                          \n"
        "STR    R1, [R2, #56]                   \n"

        /* Signal MemManage via USART1 + LED blink */
        "LDR    R3, =0x40013800                 \n"
        "LDR    R1, [R3, #0]                    \n"
        "LSLS   R1, R1, #25                     \n"
        "BPL    2f                              \n"
        "LDR    R3, =0x40013804                 \n"
        "MOVS   R1, #'M'                        \n"
        "STRH   R1, [R3, #0]                    \n"
        "2:                                     \n"
        "LDR    R3, =0x40010C00                 \n"
        "LDR    R1, [R3, #0]                    \n"
        "LDR    R2, =0xF0FFFFFF                 \n"
        "ANDS   R1, R1, R2                      \n"
        "ORR    R1, R1, #0x03000000             \n"
        "STR    R1, [R3, #0]                    \n"
        "MOVS   R1, #0x0040                     \n"
        "MOVS   R4, #3                          \n"
        "3:                                     \n"
        "STR    R1, [R3, #0x14]                 \n"
        "LDR    R2, =0xFFFFF                    \n"
        "4:    SUBS    R2, R2, #1               \n"
        "BNE    4b                              \n"
        "STR    R1, [R3, #0x10]                 \n"
        "LDR    R2, =0xFFFFF                    \n"
        "5:    SUBS    R2, R2, #1               \n"
        "BNE    5b                              \n"
        "SUBS   R4, R4, #1                      \n"
        "BNE    3b                              \n"
        "B      .                               \n"
    );
}

__attribute__((naked)) void BusFault_Handler(void)
{
    __asm volatile (
        "LDR    R2, =fault_snapshot             \n"

        "MRS    R0, MSP                         \n"
        "STR    R0, [R2, #48]                   \n"
        "MRS    R0, PSP                         \n"
        "STR    R0, [R2, #52]                   \n"

        "MRS    R0, MSP                         \n"
        "LDR    R1, =0x20000000                 \n"
        "CMP    R0, R1                          \n"
        "BLO    0f                              \n"
        "LDR    R1, =0x20005000                 \n"
        "CMP    R0, R1                          \n"
        "BHS    0f                              \n"

        "LDR    R1, [R0, #24]                   \n"
        "STR    R1, [R2, #24]                   \n"
        "B      1f                              \n"

        "0:                                     \n"
        "MOVS   R1, #0                          \n"
        "STR    R1, [R2, #24]                   \n"

        "1:                                     \n"
        "LDR    R3, =0xE000ED28                 \n"
        "LDR    R1, [R3, #0]                    \n"
        "STR    R1, [R2, #32]                   \n"  /* CFSR */
        "LDR    R1, [R3, #16]                   \n"
        "STR    R1, [R2, #44]                   \n"  /* BFAR */
        "MOVS   R1, #2                          \n"
        "STR    R1, [R2, #56]                   \n"

        /* Signal BusFault via USART1 + LED blink */
        "LDR    R3, =0x40013800                 \n"
        "LDR    R1, [R3, #0]                    \n"
        "LSLS   R1, R1, #25                     \n"
        "BPL    2f                              \n"
        "LDR    R3, =0x40013804                 \n"
        "MOVS   R1, #'B'                        \n"
        "STRH   R1, [R3, #0]                    \n"
        "2:                                     \n"
        "LDR    R3, =0x40010C00                 \n"
        "LDR    R1, [R3, #0]                    \n"
        "LDR    R2, =0xF0FFFFFF                 \n"
        "ANDS   R1, R1, R2                      \n"
        "ORR    R1, R1, #0x03000000             \n"
        "STR    R1, [R3, #0]                    \n"
        "MOVS   R1, #0x0040                     \n"
        "MOVS   R4, #3                          \n"
        "3:                                     \n"
        "STR    R1, [R3, #0x14]                 \n"
        "LDR    R2, =0xFFFFF                    \n"
        "4:    SUBS    R2, R2, #1               \n"
        "BNE    4b                              \n"
        "STR    R1, [R3, #0x10]                 \n"
        "LDR    R2, =0xFFFFF                    \n"
        "5:    SUBS    R2, R2, #1               \n"
        "BNE    5b                              \n"
        "SUBS   R4, R4, #1                      \n"
        "BNE    3b                              \n"
        "B      .                               \n"
    );
}

#endif /* __CC_ARM vs GNUC */
