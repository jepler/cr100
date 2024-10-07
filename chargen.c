#define _GNU_SOURCE
#include <stdint.h>

#include "pinout.h"
#include "keyboard.h"
#include "chargen.h"

#include "pico.h"
#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "pico/multicore.h"
#include "hardware/structs/mpu.h"
#include "hardware/clocks.h"
#include "cmsis_compiler.h"
#include "RP2040.h"

#include "vga_660x477_60.pio.h"

#include "lw_terminal_vt100.h"

int pixels_sm;

#define WRITE_PIXDATA \
    (pio0->txf[0] = pixels)
#define FIFO_WAIT \
    do { /* NOTHING */ }  while(pio_sm_get_tx_fifo_level(pio0, 0) > 2)

#define FB_WIDTH_CHAR (132)
#define FB_HEIGHT_CHAR (53)
#define CHAR_X (5)
#define CHAR_Y (9)
#define FB_HEIGHT_PIXEL (FB_HEIGHT_CHAR * CHAR_Y)

struct lw_terminal_vt100 *vt100;

// declaring this static breaks it (why?)
void scan_convert(const uint32_t * restrict cptr32, const uint16_t * restrict cgptr, const uint16_t * restrict shade);
void __not_in_flash_func(scan_convert)(const uint32_t * restrict cptr32, const uint16_t * restrict cgptr, const uint16_t * restrict shade) {
#define READ_CHARDATA \
    (ch = *cptr32++)
#define ONE_CHAR(in_shift, op, out_shift) \
    do { \
        chardata = cgptr[(ch>>(in_shift)) & 0xff]; \
        mask = shade[(ch>>(8 + (in_shift)))&7]; \
        pixels op (shade[(ch >>(11 + (in_shift)))&7] ^ (chardata & mask)) out_shift; \
    } while(0)
 
    uint32_t ch;
    uint16_t chardata, mask;
    uint32_t pixels;

#define SIX_CHARS \
    do { \
        READ_CHARDATA; \
        ONE_CHAR(0, =, << 20); \
        ONE_CHAR(16, |=, << 10); \
        READ_CHARDATA; \
        ONE_CHAR(0, |=, ); \
        WRITE_PIXDATA; \
        ONE_CHAR(16, =, << 20); \
        READ_CHARDATA; \
        ONE_CHAR(0, |=, << 10); \
        ONE_CHAR(16, |=, ); \
        WRITE_PIXDATA; \
    } while(0)

    SIX_CHARS; SIX_CHARS; SIX_CHARS; FIFO_WAIT; /*  18 */ \
    SIX_CHARS; SIX_CHARS; SIX_CHARS; FIFO_WAIT; /*  36 */ \
    SIX_CHARS; SIX_CHARS; SIX_CHARS; FIFO_WAIT; /*  54 */ \
    SIX_CHARS; SIX_CHARS; SIX_CHARS; FIFO_WAIT; /*  72 */ \
    SIX_CHARS; SIX_CHARS; SIX_CHARS; FIFO_WAIT; /*  90 */ \
    SIX_CHARS; SIX_CHARS; SIX_CHARS; FIFO_WAIT; /* 108 */ \
    SIX_CHARS; SIX_CHARS; SIX_CHARS; FIFO_WAIT; /* 126 */ \
    SIX_CHARS; /* 132 */ \
}

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

uint16_t chargen[256*CHAR_Y] = {
#include "5x9.h"
};

int cx, cy, attr = 0x300;

_Static_assert(FB_WIDTH_CHAR % 6 == 0);

int scrnprintf(const char *fmt, ...) {
    char *ptr;
    va_list ap;
    va_start(ap, fmt);
    int n = vasprintf(&ptr, fmt, ap);
    va_end(ap);
    lw_terminal_vt100_read_buf(vt100, ptr, n);
    free(ptr);
    return n;
}

static
uint16_t base_shade[] = {0, 0x554, 0xaa8, 0xffc, 0, 0x554, 0xaa8, 0xffc, 0, 0, 0, 0};

#if !STANDALONE
static void setup_vga_hsync(PIO pio) {
    uint offset = pio_add_program(pio, &vga_660x477_60_hsync_program);
    uint sm = pio_claim_unused_sm(pio, true);
    vga_660x477_60_hsync_program_init(pio, sm, offset, HSYNC_PIN);
}

static void setup_vga_vsync(PIO pio) {
    uint offset = pio_add_program(pio, &vga_660x477_60_vsync_program);
    uint sm = pio_claim_unused_sm(pio, true);
    vga_660x477_60_vsync_program_init(pio, sm, offset, VSYNC_PIN);
}

static int setup_vga_pixels(PIO pio) {
    uint offset = pio_add_program(pio, &vga_660x477_60_pixel_program);
    uint sm = pio_claim_unused_sm(pio, true);
    vga_660x477_60_pixel_program_init(pio, sm, offset, G0_PIN, 2);
    return sm;
}

static void setup_vga(void) {
    pixels_sm = setup_vga_pixels(pio0);
    assert(pixels_sm == 0);
    setup_vga_vsync(pio0);
    setup_vga_hsync(pio0);
}

__attribute__((noreturn,noinline))
static void __not_in_flash_func(core1_loop)(void) {
    int frameno = 0;
    while(true) {
        for(int row = 0; row < FB_HEIGHT_CHAR; row++) {
            uint32_t *chardata = (uint32_t*)lw_terminal_vt100_getline(vt100, row);
            for(int j=0; j<CHAR_Y; j++) {
                scan_convert(chardata,
                    &chargen[256 * j], frameno & 0x20 ? base_shade : base_shade + 4);
            }
        }
        
        frameno += 1;
    }
}

static
__attribute__((noreturn,noinline))
void __not_in_flash_func(core1_entry)(void) {
    setup_vga();

    // Turn off flash access. After this, it will hard fault. Better than messing
    // up CIRCUITPY.
    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;
    MPU->RNR = 6; // 7 is used by pico-sdk stack protection.
    MPU->RBAR = XIP_MAIN_BASE | MPU_RBAR_VALID_Msk;
    MPU->RASR = MPU_RASR_XN_Msk | // Set execute never and everything else is restricted.
        MPU_RASR_ENABLE_Msk |
        (0x1b << MPU_RASR_SIZE_Pos);         // Size is 0x10000000 which masks up to SRAM region.                                                                                         
    MPU->RNR = 7;                                                                            

    core1_loop();
}
#endif

#define BG_ATTR(x) ((x) << 11)
#define FG_ATTR(x) ((x) << 8)

#define MAKE_ATTR(fg, bg) (((fg) ^ (((bg) * 9) & 073)) << 8)

static int map_one(int i) {
    return (i > 0) + (i > 6);
}

static lw_cell_t char_attr(void *user_data, const struct lw_parsed_attr *attr) {
    int fg = map_one(attr->fg);
    int bg = map_one(attr->bg);
    if(attr->bold) fg = 3;
    if(attr->blink) fg ^= 4;
    if(attr->inverse) {
        return MAKE_ATTR(bg, fg);
    }
    return MAKE_ATTR(fg, bg);
}

queue_t keyboard_queue;

static
int stdio_kbd_in_chars(char *buf, int length) {
    int rc = 0;
    int code;
    keyboard_poll(&keyboard_queue);
    while (length && queue_try_remove(&keyboard_queue, &code)) {
        *buf++ = code;
        length--;
        rc++;
    }
    return (rc == 0) ? PICO_ERROR_NO_DATA : rc;
}

static stdio_driver_t stdio_kbd = {
    .in_chars = stdio_kbd_in_chars,
};

int main(void) {
#if !STANDALONE
    set_sys_clock_khz(vga_660x477_60_sys_clock_khz, false);
    stdio_init_all();
#endif

    vt100 = lw_terminal_vt100_init(NULL, NULL, char_attr, FB_WIDTH_CHAR, FB_HEIGHT_CHAR);
    multicore_launch_core1(core1_entry);

    scrnprintf(
"(line 0)\r\n"
"CR100 terminal demo...\r\n"
);

    if (keyboard_setup(pio1)) {
        queue_init(&keyboard_queue, sizeof(int), 64);
        stdio_set_driver_enabled(&stdio_kbd, true);
    }

    while (true) {
        int c = getchar();
        if (c == EOF) { continue; }
        char cc = c;
        lw_terminal_vt100_read_buf(vt100, &cc, 1);
    }

    return 0;
}
