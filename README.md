# cr100 - vintage style terminal with rp2040

I was inspired to create this vintage style terminal software when a friend bought and
fixed a 9" IBM paperwhite monitor.

Ultimately, this software is intended to be used with a PS/2 keyboard and some kind of
text based session over a UART or RS232 serial connection. However, it's not done yet.

Because the pixel resolution of the image is unconventional (660 pixels across), use
on LCD displays is not recommended. (that said, it does KINDA work for testing)

## Features

 * 132x53 text mode (660x480, VGA 640x480@60Hz compatible timing)
 * 4 brightness levels
 * foreground & background colors for each cell
 * blinking text

## TODO

 * ANSI-style escape codes
 * UART & PS/2 I/O

## Building

You need a system that is set up for developing embedded software, including the ARM
embedded toolchain. Check the pico-sdk docs for more help with that. The software was
developed on Debian Linux.

To build, run `make submodules && make uf2`.

The `make flash` routine depends on scripts on the developer's system, not bundled here.

## Pinout
 * 9, 10: luminance channel. Use a resistor network to convert to VGA levels.
 * 16: HSYNC
 * 17: VSYNC

## DAC resistors

Some doodling indicates that, assuming the I/O level as 3.3v, resistors of 390Ω
and 820Ω should be readily available and give not terrible voltages topping out around
0.7v across the VGA 75 ohm load resistor.

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
 * 25.125MHz dot clock
 * 640 visible pixels, 800 total pixels = 31.40kHz horizontal rate
 * 480 visible lines, 525 total lines = 59.92Hz vertical rate

My 660x480@60Hz mode:
 * 26MHz dot clock
 * 660 visible pixels, 825 total pixels = 31.78kHz horizontal rate (1.4% high)
 * 480 visible lines, 525 total lines = 60.03Hz vertical rate (1.4% high)

I don't expect there to be any problem for an old monitor to sync to this signal.

# License

All my original code is available under the MIT license.

The `adafruit_bitmap_font` library copy is MIT licensed by the original authors.

The font is a descendant of a font "Copyright 1989 X Consortium" made by a
friend back in the 90s. A more complete license declaration is not available,
but I believe this font, like the bulk of the X Window System, is MIT licensed
by the original author.
