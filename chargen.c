#define _GNU_SOURCE
#include <stdint.h>

#if __has_include(<pico/platform.h>)
#include "pico/platform.h"
#else
#define STANDALONE
#define __not_in_flash_func(x) x
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

// shade = array 0x0000, 0x5555, 0xaaaa, 0xffff
// each loop generates 5(10) pixels so our time budget is 25 (/ 30 / 35 / 40 OC) (50etc)
// cycles
#define FB_WIDTH_CHAR (132)
#define FB_HEIGHT_CHAR (53)
#define CHAR_X (5)
#define CHAR_Y (9)
#define FB_HEIGHT_PIXEL (FB_HEIGHT_CHAR * CHAR_Y)
void __not_in_flash_func(scan_convert)(const uint32_t * restrict cptr32, uint32_t * restrict vptr32, const uint16_t * restrict cgptr, const uint16_t * restrict shade) {
#define READ_CHARDATA \
    (ch = *cptr32++)
#define ONE_CHAR(in_shift, op, out_shift) \
    do { \
        chardata = cgptr[(ch>>(in_shift)) & 0xff]; \
        mask = shade[(ch>>(8 + (in_shift)))&7]; \
        pixels op (shade[(ch >>(11 + (in_shift)))&7] ^ (chardata & mask)) out_shift; \
    } while(0)
#define WRITE_PIXDATA \
    (*vptr32++ = pixels)
 
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

    SIX_CHARS; SIX_CHARS; SIX_CHARS; SIX_CHARS; // 24
    SIX_CHARS; SIX_CHARS; SIX_CHARS; SIX_CHARS; // 48
    SIX_CHARS; SIX_CHARS; SIX_CHARS; SIX_CHARS; // 72
    SIX_CHARS; SIX_CHARS; SIX_CHARS; SIX_CHARS; // 96
    SIX_CHARS; SIX_CHARS; SIX_CHARS; SIX_CHARS; // 120
    SIX_CHARS; SIX_CHARS;
}

#if defined(STANDALONE)
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

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
    CHAR('A', 
            0b00000,
            0b01100,
            0b10010,
            0b10010,
            0b11110,
            0b10010,
            0b10010,
            0b10010,
            0b00000),
    CHAR('B', 
            0b00000,
            0b11100,
            0b10010,
            0b10010,
            0b11100,
            0b10010,
            0b10010,
            0b11100,
            0b00000),
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
}

int scrnprintf(const char *fmt, ...) {
    char *ptr;
    va_list ap;
    va_start(ap, fmt);
    int n = vasprintf(&ptr, fmt, ap);
    va_end(ap);
    writefn((void*)chardata32, ptr, n);
    free(ptr);
}

int main() {
#if 0
    for(int j=0; j<256*CHAR_Y; j++) {
        chargen[j] = j % 255;
    }
    for(int j=0; j<CHAR_Y; j++) {
        chargen[256 * j] = 0;
        chargen[256 * j + 32] = 0;
    }
#endif

    uint32_t row32[FB_WIDTH_CHAR / 3];
    uint16_t base_shade[] = {0, 0x554, 0xaa8, 0xffc, 0, 0x554, 0xaa8, 0xffc, 0, 0, 0, 0};
    memset(chardata32, 0, sizeof(chardata32));
    for (attr = 0; attr < 0x4000; attr += 0x100) {
        scrnprintf("AA BB AB BA ABBA ", FB_WIDTH_CHAR, FB_HEIGHT_CHAR, CHAR_X, CHAR_Y);
        scrnprintf("AA BB AB BA ABBA ", FB_WIDTH_CHAR, FB_HEIGHT_CHAR, CHAR_X, CHAR_Y);
    }
#if 0
    scrnprintf("%d x %d character mode\r\n%d x %d font\r\n", FB_WIDTH_CHAR, FB_HEIGHT_CHAR, CHAR_X, CHAR_Y);
    attr = 0x100;
    scrnprintf("attributes\r\n", FB_WIDTH_CHAR, FB_HEIGHT_CHAR, CHAR_X, CHAR_Y);
    attr = 0x200;
    scrnprintf("attributes\r\n", FB_WIDTH_CHAR, FB_HEIGHT_CHAR, CHAR_X, CHAR_Y);
    attr = 0xb00;
    scrnprintf("more attributes\r\n", FB_WIDTH_CHAR, FB_HEIGHT_CHAR, CHAR_X, CHAR_Y);
#endif

    printf("P2 %d %d 3\n", FB_WIDTH_CHAR * CHAR_X, FB_HEIGHT_CHAR * CHAR_Y);
    for(int i=0; i<FB_HEIGHT_CHAR; i++) {
        for(int j=0; j<CHAR_Y; j++) {
            scan_convert(&chardata32[FB_WIDTH_CHAR * i / 2], row32,
                &chargen[256 * j], base_shade);
            scan_to_pbmascii(row32);
        }
    }
}
#endif
