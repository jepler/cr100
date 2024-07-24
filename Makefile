.PHONY: all
all: chargen.png build/cr100.uf2

.PHONY: clean
clean:
	rm chargen.s chargen.png build

chargen.png: chargen
	./$< | pnmtopng > $@

chargen: chargen.c Makefile
	gcc -g $< -o $@

build/Makefile:
	cmake -S . -B build

.PHONY: build/cr100.uf2
build/cr100.uf2: build/Makefile
	$(MAKE) -C build

.PHONY: flash
flash: build/cr100.uf2
	_douf2 RPI-RP2 $< /dev/serial/by-id/usb-Raspberry_Pi_Pico_*-if00
