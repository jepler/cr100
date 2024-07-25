#!/usr/bin/env python
import sys

from enum import Enum, auto
from dataclasses import dataclass, replace

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

    @property
    def visible_line_time_us(self):
        return 1000 * self.visible_width / self.pixel_clock_khz

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

    @property
    def frame_time_ms(self):
        return 1000 / self.frame_rate_hz

    def __repr__(self):
        return f"<VideoMode {self.visible_width}x{self.visible_height} {self.line_rate_khz:.2f}kHz/{self.frame_rate_hz:.2f}Hz pclk={self.pixel_clock_khz:.0f}KHz hvis={self.visible_line_time_us:.2f}us>"

def change_visible_pixels(mode_in, new_w, new_clock=None):
    print(mode_in)
    if new_clock is None:
        new_clock = mode_in.pixel_clock_khz * new_w  / mode_in.visible_width
    new_mode = replace(mode_in,
        pixel_clock_khz = new_clock,
        visible_width = new_w,
    )
    print(new_mode)
    ratio = new_clock / mode_in.pixel_clock_khz
    new_visible_time = new_mode.visible_line_time_us
    new_line_counts = round(mode_in.total_width * ratio)
    print(new_line_counts, mode_in.total_width * ratio)
    new_pulse = round(mode_in.hsync_pulse * ratio)
    new_porch_counts = new_line_counts - new_pulse - new_w
    porch_ratio = mode_in.hfront_porch / (mode_in.hfront_porch + mode_in.hback_porch)
    new_front = round(new_porch_counts * porch_ratio)
    new_back = new_porch_counts - new_front
    print((mode_in.hfront_porch, mode_in.hsync_pulse, mode_in.hback_porch), "->", (new_front, new_pulse, new_back))
    new_mode = replace(
            new_mode,
            hfront_porch = new_front,
            hsync_pulse = new_pulse,
            hback_porch = new_back)
    print(new_mode)
    print()
    return new_mode

def pio_hard_delay(instr, n, file):
    assert n > 0
    assert n < 128
    while n > 0:
        cycles = min(n, 32)
        print(f"    {instr} [{cycles-1}]", file=file)
        n -= cycles
    print(file=file)

def print_pio_hsync_program(program_name_base, mode, h_divisor, cycles_per_pixel, file=sys.stdout):
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
    print("    irq 0 [1]", file=file)
    print(".wrap", file=file)

    print(f"""
% c-sdk {{
static inline void {program_name_base}_hsync_program_init(PIO pio, uint sm, uint offset, uint pin) {{

    pio_sm_config c = {program_name_base}_hsync_program_get_default_config(offset);

    // Map the state machine's SET pin group to one pin, namely the `pin`
    // parameter to this function.
    sm_config_set_set_pins(&c, pin, 1);
    sm_config_set_clkdiv_int_frac(&c, {cycles_per_pixel * h_divisor}, 0);
    // Set this pin's GPIO function (connect PIO to the pad)
    pio_gpio_init(pio, pin);
    // Set the pin direction to output at the PIO
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);

    // Load our configuration, and jump to the start of the program
    pio_sm_init(pio, sm, offset, &c);
    // Set the state machine running
    pio_sm_set_enabled(pio, sm, true);
}}

%}}
""", file=file)

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

def print_pio_vsync_program(program_name_base, mode, cycles_per_pixel, file=sys.stdout):
    print(f"""
.program {program_name_base}_vsync
.side_set 1 opt
; Vertical sync program for {mode}
;
    pull block                        ; Pull from FIFO to OSR (only once)

.wrap_target                      ; Program wraps to here
""", file=file)

    pio_yloop("wait 1 irq 0", mode.vfront_porch, "frontporch", f"{mode.vfront_porch} lines", file=file)

    pio_yloop(f"wait 1 irq 0 side {mode.vsync_polarity:d}", mode.vsync_pulse, "syncpulse", f"{mode.vsync_pulse} lines", file=file)

    pio_yloop(f"wait 1 irq 0 side {not mode.vsync_polarity:d}", mode.vback_porch, "backporch", f"{mode.vback_porch} lines", file=file)

    print(f"""
; ACTIVE
    mov x, osr                        ; Copy value from OSR to x scratch register
active:
    wait 1 irq 0                  ; Wait for hsync to go high
    irq 1                         ; Signal that we're in active mode
    jmp x-- active                ; Remain in active mode, decrementing counter

""", file=file)


    print(".wrap", file=file)

    print(f"""
% c-sdk {{
static inline void {program_name_base}_vsync_program_init(PIO pio, uint sm, uint offset, uint pin) {{

    pio_sm_config c = {program_name_base}_vsync_program_get_default_config(offset);

    // Map the state machine's SIDESET to one pin, namely the `pin`
    // parameter to this function.
    // sm_config_set_sideset(&c, 1, true, false);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_sideset(&c, 2, true, false);
    sm_config_set_clkdiv_int_frac(&c, {cycles_per_pixel}, 0);

    // Set this pin's GPIO function (connect PIO to the pad)
    pio_gpio_init(pio, pin);
    // Set the pin direction to output at the PIO
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);

    // Load our configuration, and jump to the start of the program
    pio_sm_init(pio, sm, offset, &c);
    // Set the state machine running
    pio_sm_set_enabled(pio, sm, true);

}}

%}}
""", file=file)



def print_pio_pixel_program(program_name_base, mode, out_instr, cycles_per_pixel, file=sys.stdout):
    net_khz = cycles_per_pixel * mode.pixel_clock_khz
    assert cycles_per_pixel >= 2
    print(f"""
.program {program_name_base}_pixel
; Pixel generator program for {mode}
; PIO clock frequency = {cycles_per_pixel}×{mode.pixel_clock_khz}khz = {net_khz}
    pull block                  ; Pull from FIFO to OSR (only once)
    mov y, osr                  ; Copy value from OSR to y scratch register
    pull block                  ; Pull first pixel data

.wrap_target
    set pins, 0                   ; Zero RGB pins in blanking
    mov x, y                      ; Initialize counter variable

    wait 1 irq 1 [{cycles_per_pixel-1}] ; wait for vsync active mode

colorout:
    {out_instr} [{cycles_per_pixel-2}]
    jmp x-- colorout        ; Stay here thru horizontal active mode

.wrap""", file=file)

    print(f"""
% c-sdk {{
    enum {{ {program_name_base}_pixel_clock_khz = {mode.pixel_clock_khz}, {program_name_base}_sys_clock_khz = {cycles_per_pixel * mode.pixel_clock_khz} }};

static inline void {program_name_base}_pixel_program_init(PIO pio, uint sm, uint offset, uint pin, uint n_pin) {{

    pio_sm_config c = {program_name_base}_pixel_program_get_default_config(offset);

    // Map the state machine's OUT & SET pins
    sm_config_set_out_pins(&c, pin, n_pin);
    sm_config_set_set_pins(&c, pin, n_pin);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    // 15 pixels (30 bits) per word, left is MSB
    sm_config_set_out_shift(&c, false, true, 30);

    // Set this pin's GPIO function (connect PIO to the pad)
    for(uint i=0; i<n_pin; i++) {{
        pio_gpio_init(pio, pin + i);
    }}
    // Set the pin direction to output at the PIO
    pio_sm_set_consecutive_pindirs(pio, sm, pin, n_pin, true);

    // Load our configuration, and jump to the start of the program
    pio_sm_init(pio, sm, offset, &c);
    // Set the state machine running
    pio_sm_set_enabled(pio, sm, true);
}}

%}}
""", file=file)

mode_vga_640x480 = VideoMode(25_175, 640, 16, 96, 48, Polarity.Negative, 480, 10, 2, 33, Polarity.Negative)
mode_vga_660x480 = change_visible_pixels(mode_vga_640x480, 660, 26_000)

mode_vga_720x400 = VideoMode(28_321, 720, 18, 108, 54, Polarity.Negative, 400, 10, 2, 36, Polarity.Positive)
mode_vga_660x400 = change_visible_pixels(mode_vga_720x400, 660, 26_000)

if 1:
    print(mode_vga_640x480, 6*mode_vga_640x480.pixel_clock_khz)
    print(mode_vga_660x480, 6*mode_vga_660x480.pixel_clock_khz)

    print(mode_vga_720x400, 6*mode_vga_720x400.pixel_clock_khz)
    print(mode_vga_660x400, 6*mode_vga_660x400.pixel_clock_khz)

def print_all(mode, h_divisor=1, out_instr="out pins, 2", cycles_per_pixel=6, file=sys.stdout):
    program_name = f"vga_{mode.visible_width}x{mode.visible_height}_{mode.frame_rate_hz:.0f}"
    print_pio_hsync_program(program_name, mode, h_divisor, cycles_per_pixel, file=file)
    print("\n\n\n", file=file)
    print_pio_vsync_program(program_name, mode, cycles_per_pixel, file=file)
    print("\n\n\n", file=file)
    print_pio_pixel_program(program_name, mode, out_instr, cycles_per_pixel, file=file)

with open("vga_660x480_60.pio", "wt", encoding="utf-8") as f:
    print_all(mode_vga_660x480, file=f)

with open("vga_660x400_70.pio", "wt", encoding="utf-8") as f:
    print_all(mode_vga_660x400, file=f)
