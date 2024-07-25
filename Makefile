.PHONY: all
all: chargen.png build/cr100.uf2

.PHONY: clean
clean:
	rm -rf chargen.png build

chargen.png: chargen
	./$< | pnmtopng > $@

chargen: chargen.c 5x9.h Makefile
	gcc -g $< -o $@

build/Makefile:
	cmake -S . -B build

.PHONY: submodules
submodules:
	git submodule update --init && cd pico-sdk && git submodule update --init lib/tinyusb

.PHONY: uf2
uf2: build/cr100.uf2
	$(MAKE) -C build

.PHONY: build/cr100.uf2
build/cr100.uf2: build/Makefile

.PHONY: flash
flash: build/cr100.uf2
	_douf2 RPI-RP2 $< /dev/serial/by-id/usb-Raspberry_Pi_Pico_*-if00
