#define _GNU_SOURCE
#include <stdint.h>

#include "pinout.h"
#include "keyboard.h"

#include "pico.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

#include "vga_660x477_60.pio.h"

#include "vt.h"

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

void scroll_terminal() {

    memmove(chardata32, chardata32 + FB_WIDTH_CHAR / 2, FB_WIDTH_CHAR * (FB_HEIGHT_CHAR - 1) * 2);
    uint32_t mask = attr | (attr << 16);
    for(size_t i=0; i<FB_WIDTH_CHAR / 2; i++) {
        chardata32[(FB_HEIGHT_CHAR-1) * FB_WIDTH_CHAR / 2 + i] = mask;
    }
}
void increase_y() {
    cy = (cy + 1);
    if (cy == FB_HEIGHT_CHAR) {
        scroll_terminal();
        cy = FB_HEIGHT_CHAR - 1;
    }
}

int writefn(void *cookie, const char *data, int n) {
    uint16_t *chardata = cookie;
    for(; n; data++, n--) {
        switch(*data) {
            case '\r':
                cx = 0;
                break;
            case '\n':
                increase_y();
                break;
            default:
                if(*data >= 32) {
                    if(cx == FB_WIDTH_CHAR) {
                        cx = 0;
                        increase_y();
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

int saved_attr;
void show_cursor() {
    int xx = cx == FB_WIDTH_CHAR ? FB_WIDTH_CHAR - 1 : cx;
    saved_attr = readattr(xx, cy);
    setattr(xx, cy, saved_attr ^ BG_ATTR(7));
}

void hide_cursor() {
    int xx = cx == FB_WIDTH_CHAR ? FB_WIDTH_CHAR - 1 : cx;
    setattr(xx, cy, saved_attr);
}

#define MAKE_ATTR(fg, bg) ((fg) ^ (((bg) * 9) & 073))

esc_state vt_st;

void invert_screen() {
    for(size_t i=0; i<count_of(chardata32); i++) { chardata32[i] ^= 0x18001800; }
}

void clear_eol(int ps) {
    size_t start = cx + cy * FB_WIDTH_CHAR, end = (cx+1) * FB_WIDTH_CHAR;
    if(ps == 1) { end = start+1; }
    if(ps > 0) { start = cy * FB_WIDTH_CHAR; }
    uint16_t *chardata = (void*)chardata32;
    for(size_t i = start; i< end; i++) { chardata[i] = 32 | attr; }
}

void clear_screen(int ps) {
    size_t start = cx + cy * FB_WIDTH_CHAR, end = FB_HEIGHT_CHAR * FB_WIDTH_CHAR;
    if(ps == 1) { end = start+1; }
    if(ps > 0) { start = 0; }
    uint16_t *chardata = (void*)chardata32;
    for(size_t i = start; i< end; i++) { chardata[i] = 32 | attr; }
}

void cursor_left() {
    if(cx > 0) cx -= 1;
}

void cursor_position(esc_state *st) {
    // param 1 is row (cy), 1-bsaed
    if(st->esc_param[1] > 0 && st->esc_param[1] < FB_WIDTH_CHAR) {
        cx = st->esc_param[1] - 1;
    }
    // param 1 is column (cx), 1-bsaed
    if(st->esc_param[0] > 0 && st->esc_param[0] < FB_HEIGHT_CHAR) {
        cy = st->esc_param[0] - 1;
    }
}

int map_one(int i) {
    return (i > 0) + (i > 6);
}

void char_attr(esc_state *st) {
    int new_fg = 2;
    int new_bg = 0;

    for(int i= 0; i<count_of(st->esc_param); i++) {
        int p = st->esc_param[i];
        if (30 <= p && p <= 37) new_fg = map_one(p - 30);
        if (90 <= p && p <= 97) new_fg = map_one(p - 90);
        if (40 <= p && p <= 47) new_fg = map_one(p - 40);
        if (100 <= p && p <= 107) new_fg = map_one(p - 100);
    }
    attr = MAKE_ATTR(new_fg, new_bg) << 8;
}

int main() {
#if !STANDALONE
    set_sys_clock_khz(vga_660x477_60_sys_clock_khz, false);
    stdio_init_all();
#endif

    scrnprintf(
"(line 0)\r\n"
"CR100 terminal demo...\r\n"
);

    for(int bg = 0; bg < 8; bg++) {
        for(int fg = 0; fg < 8; fg++) {
            attr = MAKE_ATTR(fg, bg) << 8;
            scrnprintf(" %o%o ", bg, fg);
            attr = 0x300;
            scrnprintf(" ");
        }
        scrnprintf("\r\n");
    }

    multicore_launch_core1(core1_entry);
    keyboard_setup();

    attr = 0x300;
    show_cursor();
    while (true) {
        int c = getchar();
        if (c == EOF) { continue; }

        vt_action action = vt_process_code(&vt_st, c);

        if (action == NO_OUTPUT) { continue; }

        hide_cursor();
        switch(action) {
            case NO_OUTPUT:
                __builtin_unreachable();

            case PRINTABLE:
                scrnprintf("%c", c);
                if(0 && c == '\r')
                    scrnprintf("\n");
                break;

            case BELL:
                invert_screen();
                sleep_ms(100);
                invert_screen();
                break;

            case CLEAR_EOL:
                clear_eol(vt_st.esc_param[0]);
                break;

            case CLEAR_SCREEN:
                clear_screen(vt_st.esc_param[0]);
                break;

            case CURSOR_LEFT:
                cursor_left();
                break;

            case CURSOR_POSITION:
                cursor_position(&vt_st);
                break;

            case CHAR_ATTR:
                char_attr(&vt_st);
                break;
        }
        show_cursor();
    }

    return 0;
}
