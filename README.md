# cr100 - vintage style terminal with rp2040

I was inspired to create this vintage style terminal software when a friend bought and
fixed a 9" IBM paperwhite monitor.

Ultimately, this software is intended to be used with a PS/2 keyboard and some kind of
text based session over a UART or RS232 serial connection. However, it's not done yet.

Because the pixel resolution of the image is unconventional (660 pixels across), use
on LCD displays is not recommended. (that said, it does KINDA work for testing)

## Features

 * 132x53 text mode (660x480, VGA 640x480@60Hz compatible timing), including 1 status line
 * 4 brightness levels
 * foreground & background colors for each cell
 * blinking text
 * vt1xx-like terminal, use cr100 terminal entry for best compatibility
 * Extremely minimal UTF-8 support, enabled when the port is USB
   * Supports the "VT100 graphics characters" at their corresponding code points

## Building

You need a system that is set up for developing embedded software, including the ARM
embedded toolchain. Check the pico-sdk docs for more help with that. The software was
developed on Debian Linux.

To build, run `make submodules && make uf2`.

The `make flash` routine depends on scripts on the developer's system, not bundled here.

## Pinout
Read the source :)

Final pinout plan (RP2040 GPIO numbering):
 * 0/1/2/3: UART 0 with optional flow control OR
 * 12/13/14/15: UART 0 with optional flow control (sw selectable)

 * 4/5/6/7: UART 1 with optional flow control OR
 * 8/9/10/11: UART 1 with optional flow control (sw selectable)

 * 16/17/18/19: VGA
 * 20/21: PS2

## DAC resistors

On the test monitor 510Ω & 680Ω resistances give good luminance levels and are close to the VGA voltage spec.

## Inspiration; Software & Hardware I Studied

The most detailed & accessible account of generating VGA signals was [V. Hunter
Adams's PIO Assembly VGA Driver for RP2040 (Raspberry Pi
Pico)](https://vanhunteradams.com/Pico/VGA/VGA.html). I studied it closely when
creating my own PIO programs in this repository.

[The Pimoroni VGA Demo
Base](https://shop.pimoroni.com/products/pimoroni-pico-vga-demo-base?variant=32369520672851)
was a convenient board to prototype with. However, it lacks a convenient way to add
the 4 (or 6, for flow control) additional digital I/O.

[tinyvga.com](http://www.tinyvga.com/vga-timing) had classic VGA timing numbers, as did [Martin Hinner's's VGA timings page](http://martin.hinner.info/vga/timing.html).

## Character generation theory

The code uses a fully unrolled loop to generate each scanline of text. Code to
do so is running continuously on "core 1", leaving the main core free for all
other activities.

The character generator RAM contains horizontally doubled character images
bit-shifted in such a way as to avoid a few runtime shifts. The "horizontal
doubling" is done so that the character's bits can be masked with a repeating
"00", "01", "10" or "11" bit pattern to create the 4 luminance levels.

The character generator is arranged with the first scan of all 256 characters
together, followed by the second scan, and so forth.

Because the font is 5 pixels wide, every 6 characters produce 60 bits. These
are placed into 2 30-bit values and sent to the PIO FIFO as 2 32-bit values.
Every 12 characters (6 FIFO values) the FIFO is allowed to drain to 2 entries.
The timings work out so that no DMA buffer is required, simplifying the code.

Background color XORs with foreground color, this saved time. It will be
necessary to account for this in the terminal emulator.

Blink is accomplished by using one of two different tables for the color
mapping. As a consequence, the background color can also blink.

There's no hardware provision for a cursor; modifying the under-cursor
character's attribute is the anticipated way to create the cursor effect.

The CPU is mildly overclocked (156MHz) so that 6 CPU cycles are available for
each output pixel, or 30 cycles for each character on average (plus a little
margin because the first 12 characters are loaded when the scanline starts)

Why 132 characters? Some old VT-style terminals had a 132 character mode. 132
is a multiple of 6, the number of characters generated at once. It was a nice
coincidence and only required fudging VGA timings slightly.

## Timing accuracy

Heirloom VGA 640x480@60Hz:
 * 25.175MHz dot clock
 * 640 visible pixels, 800 total pixels = 31.47kHz horizontal rate
 * 480 visible lines, 525 total lines = 59.92Hz vertical rate

My 660x477@60Hz mode:
 * 26.000MHz dot clock
 * 660 visible pixels, 826 total pixels = 31.48kHz horizontal rate (<1% high)
 * 477 visible lines, 525 total lines = 59.96Hz vertical rate (<1% high)

I don't expect there to be any problem for an old monitor to sync to this signal.

## Installing the terminal entry

`make install-termcap` or `sudo make install-termcap` (to install systemwide, best if enabling getty).

## Enabling getty on Linux

Find the serial number of your device and create a udev rule for it (in a file like `/etc/udev/rules.d/97-cr100.rules`) that gives it a consistent name in /dev:
```
KERNEL=="ttyACM[0-9]*", ENV{ID_SERIAL}=="Raspberry_Pi_Pico_CR100_E6...33", ENV{ID_USB_INTERFACE_NUM}=="00", SYMLINK+="ttyCR0", ENV{SYSTEMD_WANTS}+="serial-getty@ttyCR0.service"
```

Create the service file, editing it to change `$TERM` to `vt100-w` (or `cr100` if installed), and then enable the service:
```
cp /usr/lib/systemd/system/serial-getty@.service /etc/systemd/system/serial-getty@ttyCR0.service
vi /etc/systemd/system/serial-getty@ttyCR0.service
systemctl enable serial-getty@ttyCR0.service
```

Restart udev:
```
systemctl restart udev
```

Plug in the Pico board. You should get a login prompt on the terminal. If not, well, you get to debug USB & systemd now. These steps sort of worked on my Debian Bookworm system: but literally they work every other time you start the device. :-/

## Hot Keys

 * CTRL+ALT+F1: Cycle connections (USB/UART1/UART2)
 * CTRL+ALT+F2: Cycle baud rates (UART only)
 * CTRL+ALT+F2: Cycle data format (UART only)
 * CTRL+ALT+DELETE: Reboot the firmware

## License

All my original code is available under the MIT license.

The `adafruit_bitmap_font` library copy is MIT licensed by the original authors.

The font is a descendant of a font "Copyright 1989 X Consortium" made by a
friend back in the 90s. A more complete license declaration is not available,
but I believe this font, like the bulk of the X Window System, is MIT licensed
by the original author.
