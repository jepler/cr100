.PHONY: all
all: uf2

.PHONY: clean
clean:
	rm -rf build dist

build/Makefile:
	cmake -S . -B build

.PHONY: submodules
submodules: pico-sdk/lib/tinyusb/repository.yml

pico-sdk/lib/tinyusb/repository.yml:
	git submodule update --init && cd pico-sdk && git submodule update --init lib/tinyusb

.PHONY: uf2
uf2: | build/Makefile
	$(MAKE) -C build

.PHONY: flash
flash: uf2
	$(MAKE) -C build
	_douf2 RPI-RP2 build/cr100.uf2 /dev/serial/by-id/usb-Raspberry_Pi_Pico_*-if00

# Note: use `sudo install-terminfo` or similar to install systemwide.
# tic writes to the systemwide database if permitted, otherwise to the per-user
# database.
install-terminfo:
	tic -v cr100.terminfo

.PHONY: dist
dist: uf2
	rm -rf dist
	mkdir -p dist/bin dist/terminfo
	cp build/cr100.uf2 dist/bin/
	cp build/cr100.elf dist/bin
	tic -o dist/terminfo cr100.terminfo
