#define _GNU_SOURCE
#include <stdint.h>

#include "pico/platform.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "vga_660x480_60.pio.h"

int pixels_sm;

/*
RAM usage (5x9 font, 2bpp, 132x53 -> 660x480):

128 * 53 * 2 = 13568b character buffer (char + attr)
128 * 2 * 3  = 768b line buffers
256 * 9      = 2304b character RAM

character RAM format:
[row 0 of each character]
[row 1 of each character]
[etc]

character row data format: interleaves pixel order, right justified
xxxxAABBCCDDEExx

convert character row to line buffer:
*/

#define WRITE_PIXDATA \
    (pio0->txf[0] = pixels)
#define FIFO_WAIT \
    do { /* NOTHING */ }  while(pio_sm_get_tx_fifo_level(pio0, 0) > 2)

// shade = array 0x0000, 0x5555, 0xaaaa, 0xffff
// each loop generates 5(10) pixels so our time budget is 25 (/ 30 / 35 / 40 OC) (50etc)
// cycles
#define FB_WIDTH_CHAR (132)
#define FB_HEIGHT_CHAR (53)
#define CHAR_X (5)
#define CHAR_Y (9)
#define FB_HEIGHT_PIXEL (FB_HEIGHT_CHAR * CHAR_Y)
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

int readchar(int cx, int cy) {
    uint16_t *chardata = (void*)chardata32;
    int i = cx + cy * FB_WIDTH_CHAR;
    return chardata[i] & 0xff;
}
int readattr(int cx, int cy) {
    uint16_t *chardata = (void*)chardata32;
    int i = cx + cy * FB_WIDTH_CHAR;
    return chardata[i] & 0xff00;
}

void setchar(int cx, int cy, int ch) {
    uint16_t *chardata = (void*)chardata32;
    int i = cx + cy * FB_WIDTH_CHAR;
    chardata[i] = (chardata[i] & 0xff00) | ch;
}
void setattr(int cx, int cy, int attr) {
    uint16_t *chardata = (void*)chardata32;
    int i = cx + cy * FB_WIDTH_CHAR;
    chardata[i] = (chardata[i] & 0xff) | attr;
}

int writefn(void *cookie, const char *data, int n) {
    uint16_t *chardata = cookie;
    for(; n; data++, n--) {
        switch(*data) {
            case '\r':
                cx = 0;
                break;
            case '\n':
                cy = (cy + 1) % FB_HEIGHT_CHAR;
                break;
            default:
                if(*data >= 32) {
                    if(cx == FB_WIDTH_CHAR) {
                        cy = (cy + 1) % FB_HEIGHT_CHAR;
                        cx = 0;
                    }
                    chardata[cx + cy * FB_WIDTH_CHAR] = *data | attr;
                    cx ++;
                }
        }
    }
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
    pio_sm_put_blocking(pio, sm, 480-1);
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

volatile int core1_data = 0;
void __not_in_flash_func(core1_entry)() {
    // volatile uint32_t *dptr = &pio0->txf[0];
    while(false) {
        scan_convert_static_row(0xf000000);
        scan_convert_static_row(0xff00000);
        scan_convert_static_row(0xffff000);
        scan_convert_static_row(0);
    }

    int frameno = 0;
    while(true) {
        scan_convert_static_row(0x5555555);
        for(int row = 0; row < FB_HEIGHT_CHAR; row++) {
            for(int j=0; j<CHAR_Y; j++) {
                scan_convert(&chardata32[FB_WIDTH_CHAR * row / 2],
                    &chargen[256 * j], frameno & 0x20 ? base_shade : base_shade + 4);
            }
        }
        scan_convert_static_row(0);
        scan_convert_static_row(0x5555555);
        
        core1_data += 1;
        frameno += 1;
    }
}
#endif

#define BG_ATTR(x) ((x) << 11)
#define FG_ATTR(x) ((x) << 8)

int saved_attr;
void hide_cursor() {
    int xx = cx == FB_WIDTH_CHAR ? FB_WIDTH_CHAR - 1 : cx;
    saved_attr = readattr(xx, cy);
    setattr(xx, cy, saved_attr ^ BG_ATTR(7));
}

void show_cursor() {
    int xx = cx == FB_WIDTH_CHAR ? FB_WIDTH_CHAR - 1 : cx;
    setattr(xx, cy, saved_attr);
}


int main() {
#if !STANDALONE
    set_sys_clock_khz(vga_660x480_60_sys_clock_khz, false);
    stdio_init_all();
#endif

    scrnprintf(
"\r\n"
"CR100 terminal demo...\r\n"
);

    printf("setup_vga()\n");
    setup_vga();
    multicore_launch_core1(core1_entry);
    attr = 0x300;
    show_cursor();
    while (true) {
        int c = getchar();
        if (c != EOF) {
            hide_cursor();
            if(c == '\n')
                scrnprintf("\r");
            scrnprintf("%c", c);
            show_cursor();
        }
    }

    return 0;
}
