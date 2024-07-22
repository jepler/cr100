.PHONY: all
all: chargen.png chargen.s

CFLAGS_arm = -Os -march=armv6-m -mthumb -mabi=aapcs -mcpu=cortex-m0plus -msoft-float -mfloat-abi=soft \
	-isystem /home/jepler/src/pico-sdk/src/rp2_common/pico_platform/include/ \
	-isystem /home/jepler/src/pico-sdk/src/rp2040/hardware_regs/include/ \
	-isystem /home/jepler/src/pico-sdk/src/common/pico_base/include/ \

chargen.s: chargen.c
	arm-none-eabi-gcc-12.2.1 $(CFLAGS_arm) -S -o $@ $<

chargen.png: chargen
	./$< | pnmtopng > $@

chargen: chargen.c
	gcc -g $< -o $@
