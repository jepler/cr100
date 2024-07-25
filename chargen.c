#define _GNU_SOURCE
#include <stdint.h>

#if __has_include("pico/platform.h")
#include "pico/platform.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "vga_660x480_60.pio.h"

int pixels_sm;
#else
#define STANDALONE (1)
#define __not_in_flash_func(x) x
#define count_of(a) (sizeof(a)/sizeof((a)[0]))
#endif

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

#if STANDALONE
#define OUT_ARG_COMMA uint32_t * restrict vptr32,
#define WRITE_PIXDATA \
    (*vptr32++ = pixels)
#define FIFO_WAIT do { /* NOTHING */ } while(0)
#else
#define OUT_ARG_COMMA
#define WRITE_PIXDATA \
    (pio0->txf[0] = pixels)
#define FIFO_WAIT \
    do { /* NOTHING */ }  while(pio_sm_get_tx_fifo_level(pio0, 0) > 2)
#endif

// shade = array 0x0000, 0x5555, 0xaaaa, 0xffff
// each loop generates 5(10) pixels so our time budget is 25 (/ 30 / 35 / 40 OC) (50etc)
// cycles
#define FB_WIDTH_CHAR (132)
#define FB_HEIGHT_CHAR (53)
#define CHAR_X (5)
#define CHAR_Y (9)
#define FB_HEIGHT_PIXEL (FB_HEIGHT_CHAR * CHAR_Y)
void __not_in_flash_func(scan_convert)(const uint32_t * restrict cptr32, OUT_ARG_COMMA const uint16_t * restrict cgptr, const uint16_t * restrict shade) {
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

#if STANDALONE
void scan_to_pbmascii(const uint32_t *vram) {
    for(int i=0; i<FB_WIDTH_CHAR / 3; i++) {
        uint32_t v = vram[i];
if (i < 8)
    fprintf(stderr, "%08x ", v);
        for(int j=0; j<CHAR_X * 3; j++) {
            if(i || j) printf(" ");
            printf("%d", (v >> 30) & 3);
            v <<= 2;
        }
    }
fprintf(stderr, "\n");
    printf("\n");
}
#endif


#define BIT_TAKE_DEPOSIT(b, from, to) ((((b) >> from) & 1) << to)
#define CSWIZZLE(b) ( \
    BIT_TAKE_DEPOSIT((b), 0, 2) | \
    BIT_TAKE_DEPOSIT((b), 0, 3) | \
    BIT_TAKE_DEPOSIT((b), 1, 4) | \
    BIT_TAKE_DEPOSIT((b), 1, 5) | \
    BIT_TAKE_DEPOSIT((b), 2, 6) | \
    BIT_TAKE_DEPOSIT((b), 2, 7) | \
    BIT_TAKE_DEPOSIT((b), 3, 8) | \
    BIT_TAKE_DEPOSIT((b), 3, 9) | \
    BIT_TAKE_DEPOSIT((b), 4, 10) | \
    BIT_TAKE_DEPOSIT((b), 4, 11) | \
    0 )
#define CHAR(i, r1, r2, r3, r4, r5, r6, r7, r8, r9) \
    [i+0*256] = CSWIZZLE(r1), \
    [i+1*256] = CSWIZZLE(r2), \
    [i+2*256] = CSWIZZLE(r3), \
    [i+3*256] = CSWIZZLE(r4), \
    [i+4*256] = CSWIZZLE(r5), \
    [i+5*256] = CSWIZZLE(r6), \
    [i+6*256] = CSWIZZLE(r7), \
    [i+7*256] = CSWIZZLE(r8), \
    [i+8*256] = CSWIZZLE(r9) \

uint16_t chargen[256*CHAR_Y] = {
#include "5x9.h"
};

int cx, cy, attr = 0x300;

_Static_assert(FB_WIDTH_CHAR % 6 == 0);
uint32_t chardata32[FB_WIDTH_CHAR * FB_HEIGHT_CHAR / 2] = {
};

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
    uint offset = pio_add_program(pio, &vga_660x480_60_pixels_program);
    uint sm = pio_claim_unused_sm(pio, true);
    vga_660x480_60_pixels_program_init(pio, sm, offset, G0_PIN, 2);
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


int main() {
#if !STANDALONE
    set_sys_clock_khz(vga_660x480_60_sys_clock_khz, false);
    stdio_init_all();
#endif
    if(0)
        for (size_t i=0; i<count_of(chardata32); i++) { chardata32[i] = 0x08000800; }

    attr = 0x300;

    scrnprintf(
"An overclock to 140MHz is also very stable if you wanted 720 pixels Some classic text modes could\r\n"
"double the rightmost pixel of the 8-bit-wide font (typically, if the high bit of the character\r\n"
"number was set) instead of outputting 0, so you could get block graphics but they would be\r\n"
"slightly distorted worst with the \"shade\" characters but fine with almost everything else\n\r\n\r\n");

    for (int x = 0; x < 0x4000; x += 0x100) {
        attr = x;
        scrnprintf("01234567890 wasd il -uwu_", FB_WIDTH_CHAR, FB_HEIGHT_CHAR, CHAR_X, CHAR_Y);
        scrnprintf("AA BB AB BA gqgq ", FB_WIDTH_CHAR, FB_HEIGHT_CHAR, CHAR_X, CHAR_Y);
    }

#if STANDALONE
    uint32_t row32[FB_WIDTH_CHAR / 3];
    printf("P2 %d %d 3\n", FB_WIDTH_CHAR * CHAR_X, FB_HEIGHT_CHAR * CHAR_Y);
    for(int i=0; i<FB_HEIGHT_CHAR; i++) {
        for(int j=0; j<CHAR_Y; j++) {
            scan_convert(&chardata32[FB_WIDTH_CHAR * i / 2], row32,
                &chargen[256 * j], base_shade);
            scan_to_pbmascii(row32);
        }
    }
#endif

#if !STANDALONE
    printf("setup_vga()\n");
    setup_vga();
    multicore_launch_core1(core1_entry);
    attr = 0x300;
    while (true) {
        scrnprintf("%c", "AB " [random() % 3]);
    }
#endif

    return 0;
}
