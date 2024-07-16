#include <stdint.h>
/*
RAM usage (5x9 font, 2bpp, 128x53 -> 640x480):

128 * 53 * 2 = 13568b character buffer (char + attr)
128 * 2 * 3  = 768b line buffers
256 * 9      = 2304b character RAM

character RAM format:
[row 0 of each character]
[row 1 of each character]
[etc]

character row data format: interleaves pixel order, right justified
ADBECFxx
A B C
 D E F

convert character row to line buffer:
*/

// shade = array 0x0000, 0x5555, 0xaaaa, 0xffff
// each loop generates 5 pixels so our time budget is 25 (/ 30 / 35 / 40 OC)
// cycles
#define FB_WIDTH_CHAR (128)
#define FB_HEIGHT_CHAR (53)
#define CHAR_X (5)
#define CHAR_Y (9)
#define FB_HEIGHT_PIXEL (FB_HEIGHT_CHAR * CHAR_Y)
void scan_convert(const uint16_t *cptr, uint16_t *vptr, const uint8_t *cgptr, const uint16_t *shade) {
    for(int i=0; i<FB_WIDTH_CHAR; i++) {
        int16_t ch = *cptr++;
        int8_t chardata = cgptr[ch & 0xff];
        ch >>= 8;
        int16_t mask = shade[ch & 7]; // 0123 = 4 colors 4567 = blink colors
        ch >>= 3;
        int16_t odd = chardata & 0x55; odd = odd | (odd << 1);
        int16_t even = (chardata & 0xaa) << 5; even = even | (even << 1);
        int16_t pixels = shade[ch] ^ ((odd | even) & mask);
        *vptr++ = pixels;
    }
}

#if defined(__linux)
#define STANDALONE
#endif

#if defined(STANDALONE)
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

void scan_to_pbmascii(const uint16_t *vram) {
    for(int i=0; i<FB_WIDTH_CHAR; i++) {
        int v = vram[i];
        for(int j=0; j<CHAR_X; j++) {
            if(i || j) printf(" ");
            printf("%d", (v >> 6) & 3);
            v <<= 2;
        }
    }
    printf("\n");
}

int8_t chargen[256*CHAR_Y] = {
};

int cx, cy, attr = 0x300;

int writefn(void *cookie, const char *data, int n) {
    uint16_t *cram = cookie;
    for(; n; data++, n--) {
        switch(*data) {
            case '\r':
                cx = 0;
                break;
            case '\n':
                cy = (cy + 1) % FB_HEIGHT_CHAR;
                break;
            default:
                if(*data > 32) {
                    if(cx == FB_WIDTH_CHAR) {
                        cy = (cy + 1) % FB_HEIGHT_CHAR;
                        cx = 0;
                    }
                    cram[cx + cy * FB_WIDTH_CHAR] = *data | attr;
                    cx ++;
                }
        }
    }
}

uint16_t cram[FB_WIDTH_CHAR * FB_HEIGHT_CHAR];

int scrnprintf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    writefn(cram, buf, strlen(buf));
}

int main() {
    for(int j=0; j<256*CHAR_Y; j++) {
        chargen[j] = j % 255;
    }
    for(int j=0; j<CHAR_Y; j++) {
        chargen[256 * j] = 0;
        chargen[256 * j + 32] = 0;
    }

    uint16_t vram[FB_WIDTH_CHAR];
    uint16_t base_shade[] = {0, 0x5555, 0xaaaa, 0xffff, 0, 0x5555, 0xaaaa, 0xffff, 0, 0, 0, 0};
    memset(cram, 0, sizeof(cram));
    scrnprintf("%d x %d character mode\r\n%d x %d font\r\n", FB_WIDTH_CHAR, FB_HEIGHT_CHAR, CHAR_X, CHAR_Y);
    attr = 0x100;
    scrnprintf("attributes\r\n", FB_WIDTH_CHAR, FB_HEIGHT_CHAR, CHAR_X, CHAR_Y);
    attr = 0x200;
    scrnprintf("attributes\r\n", FB_WIDTH_CHAR, FB_HEIGHT_CHAR, CHAR_X, CHAR_Y);
    attr = 0xb00;
    scrnprintf("more attributes\r\n", FB_WIDTH_CHAR, FB_HEIGHT_CHAR, CHAR_X, CHAR_Y);

    printf("P2 %d %d 3\n", FB_WIDTH_CHAR * CHAR_X, FB_HEIGHT_CHAR * CHAR_Y);
    for(int i=0; i<FB_HEIGHT_CHAR; i++) {
        for(int j=0; j<CHAR_Y; j++) {
            scan_convert(&cram[FB_WIDTH_CHAR * i], vram,
                &chargen[256 * j], base_shade);
            scan_to_pbmascii(vram);
        }
    }
}
#endif
