#define _GNU_SOURCE
#include <stdint.h>

#include "pico/platform.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "vga_660x480_60.pio.h"

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
        FIFO_WAIT; \
    } while(0)

#define ONE_SCAN \
    do { \
        SIX_CHARS; SIX_CHARS; SIX_CHARS; FIFO_WAIT; /*  18 */ \
        SIX_CHARS; SIX_CHARS; SIX_CHARS; FIFO_WAIT; /*  36 */ \
        SIX_CHARS; SIX_CHARS; SIX_CHARS; FIFO_WAIT; /*  54 */ \
        SIX_CHARS; SIX_CHARS; SIX_CHARS; FIFO_WAIT; /*  72 */ \
        SIX_CHARS; SIX_CHARS; SIX_CHARS; FIFO_WAIT; /*  90 */ \
        SIX_CHARS; SIX_CHARS; SIX_CHARS; FIFO_WAIT; /* 108 */ \
        SIX_CHARS; SIX_CHARS; SIX_CHARS; FIFO_WAIT; /* 126 */ \
        SIX_CHARS; FIFO_WAIT; /* 132 */ \
    } while (0)

    ONE_SCAN;
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
uint32_t chardata32[FB_WIDTH_CHAR * FB_HEIGHT_CHAR / 2];

int writefn(void *cookie, const char *data, int n) {
    lw_terminal_vt100_read_buf(vt100, data, n);
    return n;
}

int scrnprintf(const char *fmt, ...) {
    char *ptr;
    va_list ap;
    va_start(ap, fmt);
    int n = vasprintf(&ptr, fmt, ap);
    va_end(ap);
    writefn((void*)chardata32, ptr, n);
    free(ptr);
    return n;
}

uint16_t base_shade[] = {0, 0x554, 0xaa8, 0xffc, 0, 0x554, 0xaa8, 0xffc, 0, 0, 0, 0};

#if !STANDALONE
#define HSYNC_PIN (16)
#define VSYNC_PIN (17)
#define G0_PIN (9) // "green 3" on VGA pico demo base

static void setup_vga_hsync(PIO pio) {
    uint offset = pio_add_program(pio, &vga_660x480_60_hsync_program);
    uint sm = pio_claim_unused_sm(pio, true);
    vga_660x480_60_hsync_program_init(pio, sm, offset, HSYNC_PIN);
    pio_sm_put_blocking(pio, sm, 660+16-1);
}

static void setup_vga_vsync(PIO pio) {
    uint offset = pio_add_program(pio, &vga_660x480_60_vsync_program);
    uint sm = pio_claim_unused_sm(pio, true);
    vga_660x480_60_vsync_program_init(pio, sm, offset, VSYNC_PIN);
    pio_sm_put_blocking(pio, sm, 477-1);
}

static int setup_vga_pixels(PIO pio) {
    uint offset = pio_add_program(pio, &vga_660x480_60_pixel_program);
    uint sm = pio_claim_unused_sm(pio, true);
    vga_660x480_60_pixel_program_init(pio, sm, offset, G0_PIN, 2);
    pio_sm_put_blocking(pio, sm, 660-1);
    return sm;
}

static void setup_vga() {
    pixels_sm = setup_vga_pixels(pio0);
    assert(pixels_sm == 0);
    setup_vga_vsync(pio0);
    setup_vga_hsync(pio0);
}

void __not_in_flash_func(scan_convert_static_row)(int32_t pixels) {
#undef SIX_CHARS
#define SIX_CHARS do { WRITE_PIXDATA; WRITE_PIXDATA; } while(0)
    ONE_SCAN;
}

void __not_in_flash_func(core1_entry)() {
    int frameno = 0;
    setup_vga();

    // Something causes a 1-fifo-entry shift. I failed to diagnose it so this
    // terrible workaround is put into place: it shifts out a whole screen
    // minus one FIFO entry.  *sob*
    // (also the top text row is not visible on my test LCD but who knows why
    // this could be. we'll get it on real HW and find out what's what.)
    {
        uint32_t pixels = 0;
        for(int i=0; i<21; i++) {
            WRITE_PIXDATA;
            WRITE_PIXDATA;
            WRITE_PIXDATA;
            WRITE_PIXDATA;
            WRITE_PIXDATA;
            WRITE_PIXDATA;
            FIFO_WAIT;
        }
        WRITE_PIXDATA;
        WRITE_PIXDATA;
        WRITE_PIXDATA;
        WRITE_PIXDATA;
        WRITE_PIXDATA;
        FIFO_WAIT;
        for(int i=0; i<476; i++) {
            scan_convert_static_row(0);
        }
    }
    while(true) {
        for(int row = 0; row < FB_HEIGHT_CHAR; row++) {
            for(int j=0; j<CHAR_Y; j++) {
                scan_convert(&chardata32[FB_WIDTH_CHAR * row / 2],
                    &chargen[256 * j], frameno & 0x20 ? base_shade : base_shade + 4);
            }
        }
        
        frameno += 1;
    }
}
#endif

#define BG_ATTR(x) ((x) << 11)
#define FG_ATTR(x) ((x) << 8)

#define MAKE_ATTR(fg, bg) (((fg) ^ (((bg) * 9) & 073)) << 8)

void invert_screen() {
    for(size_t i=0; i<count_of(chardata32); i++) { chardata32[i] ^= 0x18001800; }
}

int map_one(int i) {
    return (i > 0) + (i > 6);
}

lw_cell_t char_attr(void *user_data, const struct lw_parsed_attr *attr) {
    int fg = map_one(attr->fg);
    int bg = map_one(attr->bg);
    if(attr->bold) fg = 3;
    if(attr->blink) fg ^= 4;
    if(attr->inverse) {
        return MAKE_ATTR(bg, fg);
    }
    return MAKE_ATTR(fg, bg);
}


int main() {
#if !STANDALONE
    set_sys_clock_khz(vga_660x480_60_sys_clock_khz, false);
    stdio_init_all();
#endif

    vt100 = lw_terminal_vt100_init(NULL, NULL, NULL, FB_WIDTH_CHAR, FB_HEIGHT_CHAR);
    multicore_launch_core1(core1_entry);

    scrnprintf(
"(line 0)\r\n"
"CR100 terminal demo...\r\n"
);

    while (true) {
        int c = getchar();
        if (c == EOF) { continue; }

        char buf[2] = { c, 0 };
        lw_terminal_vt100_read_str(vt100, buf);
    }

    return 0;
}
