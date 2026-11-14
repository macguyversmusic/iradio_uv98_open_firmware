LIB     := lib/at32f423
DEV     := $(LIB)/cmsis/cm4/device_support
CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

CPU     := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS    := -DAT32F423RCT7_7 -DUSE_STDPERIPH_DRIVER
INC     := -Isrc -I$(LIB)/cmsis/cm4/core_support -I$(DEV) -I$(LIB)/drivers/inc
CFLAGS  := $(CPU) $(DEFS) $(INC) -Os -g -std=gnu11 -Wall -Wextra -ffunction-sections -fdata-sections
LDFLAGS := $(CPU) -T rt880_app.ld -Wl,--gc-sections -Wl,-Map=build/openfw.map --specs=nano.specs --specs=nosys.specs

SRCS := src/main.c $(DEV)/system_at32f423.c \
        $(addprefix $(LIB)/drivers/src/at32f423_,crm.c gpio.c usart.c spi.c dac.c misc.c wdt.c adc.c)
ASRC := $(DEV)/startup/gcc/startup_at32f423.s
OBJS := $(addprefix build/,$(notdir $(SRCS:.c=.o) $(ASRC:.s=.o)))

vpath %.c src $(DEV) $(LIB)/drivers/src
vpath %.s $(DEV)/startup/gcc

# Image size matches the stock firmware files: 0x08002800-0x0803FFFF.
APP_END := 0x08040000

all: build/uv98plus_openfw.bin

# src/font.h and src/fonts_aa.h are included in the repository. "make font" regenerates them
# (needs python3-pil and fonts-dejavu-core).
font:
	python3 -I tools/mkfont.py src/font.h
	python3 -I tools/mkfont_aa.py src/fonts_aa.h

build:
	mkdir -p build

build/main.o: src/font.h src/fonts_aa.h

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.s | build
	$(CC) $(CPU) -c $< -o $@

build/openfw.elf: $(OBJS) rt880_app.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

build/uv98plus_openfw.bin: build/openfw.elf
	$(OBJCOPY) -O binary --gap-fill 0x00 --pad-to $(APP_END) $< $@
	python3 -I tools/check_image.py $@

.PHONY: all font
