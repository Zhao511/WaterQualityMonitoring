# ============================================================
# 智慧水质监测系统 — FreeRTOS Makefile
# MCU: STM32F103C8T6  |  RTOS: FreeRTOS
# ============================================================

TARGET  = water_quality_monitor

# --- 工具链 ---
CC      = arm-none-eabi-gcc
AS      = arm-none-eabi-as
LD      = arm-none-eabi-ld
OBJCOPY = arm-none-eabi-objcopy
OBJDUMP = arm-none-eabi-objdump
SIZE    = arm-none-eabi-size

# --- 编译选项 ---
CPU      = -mcpu=cortex-m3 -mthumb
CFLAGS   = $(CPU) -std=c99 -O0 -g3 -Wall -Wextra \
           -fno-common -ffunction-sections -fdata-sections \
           -DSTM32F10X_MD -DUSE_STDPERIPH_DRIVER
LDFLAGS  = -T stm32f103c8t6.ld -Wl,--gc-sections \
           -Wl,-Map=$(TARGET).map -specs=nosys.specs -specs=nano.specs -lm

# ============================================================
# 源文件
# ============================================================

# --- 用户应用源码 ---
SRCS  = Core/Src/main.c
SRCS += Core/Src/stm32f10x_it.c
SRCS += Core/Src/system_stm32f10x.c
SRCS += Drivers/Src/sensor_ph.c
SRCS += Drivers/Src/sensor_tds.c
SRCS += Drivers/Src/sensor_turbidity.c
SRCS += Drivers/Src/sensor_temp.c
SRCS += Drivers/Src/adc_common.c
SRCS += Drivers/Src/rc522.c
SRCS += Drivers/Src/gps.c
SRCS += Drivers/Src/lora.c
SRCS += Drivers/Src/led_rgb.c
SRCS += Drivers/Src/usart_debug.c

# --- IoT 物模型 ---
SRCS += Core/Src/iot_json.c
SRCS += Core/Src/iot_service.c
SRCS += Core/Src/iot_transport.c

# --- STM32 标准外设库 ---
LIB_DIR = Libraries/STM32F10x_StdPeriph_Driver/src
SRCS += $(LIB_DIR)/misc.c
SRCS += $(LIB_DIR)/stm32f10x_adc.c
SRCS += $(LIB_DIR)/stm32f10x_gpio.c
SRCS += $(LIB_DIR)/stm32f10x_rcc.c
SRCS += $(LIB_DIR)/stm32f10x_spi.c
SRCS += $(LIB_DIR)/stm32f10x_usart.c

# --- FreeRTOS 内核 (请从 github.com/FreeRTOS/FreeRTOS-Kernel 下载) ---
# 将内核源码放入 FreeRTOS-Kernel/ 目录
RTOS_DIR = FreeRTOS-Kernel/FreeRTOS-Kernel
SRCS += $(RTOS_DIR)/tasks.c
SRCS += $(RTOS_DIR)/queue.c
SRCS += $(RTOS_DIR)/list.c
SRCS += $(RTOS_DIR)/timers.c
SRCS += $(RTOS_DIR)/portable/MemMang/heap_4.c
SRCS += $(RTOS_DIR)/portable/GCC/ARM_CM3/port.c

# 如使用 IAR/Keil 请替换:
#   portable/IAR/ARM_CM3/port.c
#   portable/RVDS/ARM_CM3/port.c

# --- 汇编启动文件 ---
ASMS  = Core/Src/startup_stm32f10x_md.s

# --- 生成目标文件列表 ---
OBJS  = $(SRCS:.c=.o) $(ASMS:.s=.o)

# ============================================================
# 头文件路径
# ============================================================
INCLUDES  = -ICore/Inc
INCLUDES += -IDrivers/Inc
INCLUDES += -ILibraries/STM32F10x_StdPeriph_Driver/inc
INCLUDES += -IFreeRTOS/inc
INCLUDES += -I$(RTOS_DIR)/include
INCLUDES += -I$(RTOS_DIR)/portable/GCC/ARM_CM3

# ============================================================
# 构建目标
# ============================================================

.PHONY: all clean flash size

all: $(TARGET).elf $(TARGET).hex $(TARGET).bin size

size: $(TARGET).elf
	@$(SIZE) $<

# --- 编译 C 文件 ---
%.o: %.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# --- 汇编 .s 文件 ---
%.o: %.s
	@echo "  AS    $<"
	@$(AS) $(CPU) -c $< -o $@

# --- 链接 ---
$(TARGET).elf: $(OBJS)
	@echo "  LD    $@"
	@$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

# --- 生成 HEX ---
$(TARGET).hex: $(TARGET).elf
	@echo "  HEX   $@"
	@$(OBJCOPY) -O ihex $< $@

# --- 生成 BIN ---
$(TARGET).bin: $(TARGET).elf
	@echo "  BIN   $@"
	@$(OBJCOPY) -O binary $< $@

# --- 烧录 (ST-Link) ---
flash: $(TARGET).bin
	st-flash write $(TARGET).bin 0x08000000

# --- 清理 ---
clean:
	@rm -f $(OBJS) $(TARGET).elf $(TARGET).hex $(TARGET).bin $(TARGET).map
	@echo "  Clean done."
