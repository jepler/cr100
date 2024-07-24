#!/usr/bin/env python
import sys

from enum import Enum, auto
from dataclasses import dataclass

class Polarity:
    Negative = 0
    Positive = 1

@dataclass(frozen=True)
class VideoMode:
    pixel_clock_khz: int

    visible_width: int
    hfront_porch: int
    hsync_pulse: int
    hback_porch: int
    hsync_polarity: Polarity

    @property
    def total_width(self):
        return self.visible_width + self.hfront_porch + self.hsync_pulse + self.hback_porch

    @property
    def line_rate_khz(self):
        return self.pixel_clock_khz / self.total_width

    @property
    def line_time_us(self):
        return 1000 / self.line_rate_khz


    visible_height: int
    vfront_porch: int
    vsync_pulse: int
    vback_porch: int
    vsync_polarity: Polarity

    @property
    def total_lines(self):
        return self.visible_height + self.vfront_porch + self.vsync_pulse + self.vback_porch

    @property
    def frame_rate_hz(self):
        return self.line_rate_khz / self.total_lines * 1000

    def frame_time_ms(self):
        return 1000 / self.frame_rate_hz

    def __repr__(self):
        return f"<VideoMode {self.visible_width}x{self.visible_height} {self.line_rate_khz:.2f}kHz/{self.frame_rate_hz:.2f}Hz pclk={self.pixel_clock_khz:.0f}KHz>"

def change_visible_pixels(mode_in, new_w):
    ratio = new_w  / mode_in.visible_width
    new_clock = mode_in.pixel_clock_khz * ratio
    new_hfront = round(mode_in.hfront_porch * ratio)
    new_hsync = round(mode_in.hsync_pulse * ratio)
    new_hback = round(mode_in.hback_porch * ratio)
    return VideoMode(
        new_clock,
        new_w,
        new_hfront,
        new_hsync,
        new_hback,
        mode_in.hsync_polarity,

        mode_in.visible_height,
        mode_in.vfront_porch,
        mode_in.vsync_pulse,
        mode_in.vback_porch,
        mode_in.vsync_polarity,
    )

def pio_hard_delay(instr, n, file):
    assert n > 0
    assert n < 128
    while n > 0:
        cycles = min(n, 32)
        print(f"    {instr} [{cycles-1}]", file=file)
        n -= cycles
    print(file=file)

def print_pio_hsync_program(program_name_base, mode, h_divisor, file=sys.stdout):
    net_khz = mode.pixel_clock_khz / h_divisor
    err = (mode.visible_width + mode.hfront_porch) % h_divisor
    print(f"""
; Horizontal sync program for {mode}
; PIO clock frequency = {mode.pixel_clock_khz:.1f}/{h_divisor}khz = {net_khz:.1f}
;
.program {program_name_base}_hsync
    pull block              ; Pull from FIFO to OSR (only happens once)

.wrap_target            ; Program wraps to here
; ACTIVE + FRONTPORCH {mode.visible_width} + {mode.hfront_porch} error {err}
    mov x, osr              ; Copy value from OSR to x scratch register
activeporch:
    jmp x-- activeporch  ; Remain high in active mode and front porch

""", file=file)


    cycles, err = divmod(mode.hsync_pulse + err, h_divisor)
    print(f"syncpulse: ; {mode.hsync_pulse}/{h_divisor} clocks [actual {cycles} error {err}]", file=file)
    pio_hard_delay(f"set pins, {mode.hsync_polarity:d}", cycles, file=file)

    cycles, err = divmod(mode.hback_porch + err, h_divisor)
    print(f"backporch: ; {mode.hback_porch}/{h_divisor} clocks [actual {cycles} error {err}]", file=file)
    pio_hard_delay(f"set pins, {not mode.hsync_polarity:d}", cycles - 1, file=file)
    print("    irq 0 [1]")
    print(".wrap", file=file)

def pio_yloop(instr, n, label, comment, file):
    assert n <= 65
    if n < 3:
        print(f"{label}: ; {comment}", file=file)
        for i in range(n):
            print(f"    {instr}", file=file)
    elif n <= 33:
        print(f"    set y, {n-1 if n <= 32 else 31}", file=file)
        print(f"{label}: ; {comment}", file=file)
        print(f"    {instr}", file=file)
        print(f"    jmp y--, {label}", file=file)
        if n == 33:
            print(f"    {instr}", file=file)
    elif n <= 65:
        print(f"    set y, {(n-2)//2}", file=file)
        print(f"{label}: ; {comment}", file=file)
        print(f"    {instr}", file=file)
        print(f"    {instr}", file=file)
        print(f"    jmp y--, {label}", file=file)
        if n & 1:
            print(f"    {instr}", file=file)
    print(file=file)

def print_pio_vsync_program(program_name_base, mode, file=sys.stdout):
    print(f"""
.program {program_name_base}_vsync
.side_set 1 opt
; Vertical sync program for {mode}
;
    pull block                        ; Pull from FIFO to OSR (only once)

.wrap_target                      ; Program wraps to here
; ACTIVE
    mov x, osr                        ; Copy value from OSR to x scratch register
active:
    wait 1 irq 0                  ; Wait for hsync to go high
    irq 1                         ; Signal that we're in active mode
    jmp x-- active                ; Remain in active mode, decrementing counter

""", file=file)


    pio_yloop("wait 1 irq 0", mode.vfront_porch, "frontporch", f"{mode.vfront_porch} lines", file=file)
    print(f"    set pins, {mode.vsync_polarity:d}", file=file)

    pio_yloop("wait 1 irq 0", mode.vsync_pulse, "syncpulse", f"{mode.vsync_pulse} lines", file=file)

    pio_yloop(f"wait 1 irq 0 side {not mode.vsync_polarity:d}", mode.vback_porch, "backporch", f"{mode.vback_porch} lines", file=file)

    print(".wrap", file=file)


def print_pio_pixel_program(program_name_base, mode, out_instr, cycles_per_pixel, file=sys.stdout):
    net_khz = cycles_per_pixel * mode.pixel_clock_khz
    assert cycles_per_pixel >= 2
    print(f"""
.program {program_name_base}_pixels
; Pixel generator program for {mode}
; PIO clock frequency = {cycles_per_pixel}×{mode.pixel_clock_khz}khz = {net_khz}
    pull block                  ; Pull from FIFO to OSR (only once)
    mov y, osr                  ; Copy value from OSR to y scratch register

.wrap_target
    set pins, 0                   ; Zero RGB pins in blanking
    mov x, y                      ; Initialize counter variable

    wait 1 irq 1 [{cycles_per_pixel-1}] ; wait for vsync active mode

colorout:
    {out_instr} [{cycles_per_pixel-2}]
    jmp x-- colorout        ; Stay here thru horizontal active mode

.wrap""")


mode_vga_640x480 = VideoMode(25_175, 640, 16, 96, 48, Polarity.Negative, 480, 10, 2, 33, Polarity.Negative)
mode_vga_660x480 = change_visible_pixels(mode_vga_640x480, 660)

mode_vga_720x400 = VideoMode(28_321, 720, 18, 108, 54, Polarity.Negative, 400, 10, 2, 36, Polarity.Positive)
mode_vga_660x400 = change_visible_pixels(mode_vga_720x400, 660)

if 0:
    print(mode_vga_640x480, 6*mode_vga_640x480.pixel_clock_khz)
    print(mode_vga_660x480, 6*mode_vga_660x480.pixel_clock_khz)

    print(mode_vga_720x400, 6*mode_vga_720x400.pixel_clock_khz)
    print(mode_vga_660x400, 6*mode_vga_660x400.pixel_clock_khz)

def print_all(mode, h_divisor=2, out_instr="out pins, 4", cycles_per_pixel=6):
    program_name = f"vga_{mode.visible_width}x{mode.visible_height}_{mode.frame_rate_hz:.0f}"
    print_pio_hsync_program(program_name, mode, h_divisor)
    print("\n\n\n")
    print_pio_vsync_program(program_name, mode)
    print("\n\n\n")
    print_pio_pixel_program(program_name, mode, out_instr, cycles_per_pixel)

print_all(mode_vga_660x480, h_divisor=1)
