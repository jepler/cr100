#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chargen.h"
#include "keyboard.h"
#include "pinout.h"

#include "RP2040.h"
#include "cmsis_compiler.h"
#include "hardware/clocks.h"
#include "hardware/structs/mpu.h"
#include "hardware/watchdog.h"
#include "pico.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdio/driver.h"
#include "pico/stdlib.h"

#include "vga_660x477_60.pio.h"

#include "lw_terminal_vt100.h"
#define DEBUG(...) ((void)0)

int pixels_sm;

#define WRITE_PIXDATA (pio0->txf[0] = pixels)
#define FIFO_WAIT                                                              \
    do { /* NOTHING */                                                         \
    } while (pio_sm_get_tx_fifo_level(pio0, 0) > 2)

#define FB_WIDTH_CHAR (132)
#define FB_HEIGHT_CHAR (53)
#define CHAR_X (5)
#define CHAR_Y (9)
#define FB_HEIGHT_PIXEL (FB_HEIGHT_CHAR * CHAR_Y)

struct lw_terminal_vt100 *vt100;

// declaring this static breaks it (why?)
void scan_convert(const uint32_t *restrict cptr32,
                  const uint16_t *restrict cgptr,
                  const uint16_t *restrict shade);
void __not_in_flash_func(scan_convert)(const uint32_t *restrict cptr32,
                                       const uint16_t *restrict cgptr,
                                       const uint16_t *restrict shade) {
#define READ_CHARDATA (ch = *cptr32++)
#define ONE_CHAR(in_shift, op, out_shift)                                      \
    do {                                                                       \
        chardata = cgptr[(ch >> (in_shift)) & 0xff];                           \
        mask = shade[(ch >> (8 + (in_shift))) & 7];                            \
        pixels op(shade[(ch >> (11 + (in_shift))) & 7] ^ (chardata & mask))    \
            out_shift;                                                         \
    } while (0)

    uint32_t ch;
    uint16_t chardata, mask;
    uint32_t pixels;

#define SIX_CHARS                                                              \
    do {                                                                       \
        READ_CHARDATA;                                                         \
        ONE_CHAR(0, =, << 20);                                                 \
        ONE_CHAR(16, |=, << 10);                                               \
        READ_CHARDATA;                                                         \
        ONE_CHAR(0, |=, );                                                     \
        WRITE_PIXDATA;                                                         \
        ONE_CHAR(16, =, << 20);                                                \
        READ_CHARDATA;                                                         \
        ONE_CHAR(0, |=, << 10);                                                \
        ONE_CHAR(16, |=, );                                                    \
        WRITE_PIXDATA;                                                         \
    } while (0)

    SIX_CHARS;
    SIX_CHARS;
    SIX_CHARS;
    FIFO_WAIT; /*  18 */

    SIX_CHARS;
    SIX_CHARS;
    SIX_CHARS;
    FIFO_WAIT; /*  36 */

    SIX_CHARS;
    SIX_CHARS;
    SIX_CHARS;
    FIFO_WAIT; /*  54 */

    SIX_CHARS;
    SIX_CHARS;
    SIX_CHARS;
    FIFO_WAIT; /*  72 */

    SIX_CHARS;
    SIX_CHARS;
    SIX_CHARS;
    FIFO_WAIT; /*  90 */

    SIX_CHARS;
    SIX_CHARS;
    SIX_CHARS;
    FIFO_WAIT; /* 108 */

    SIX_CHARS;
    SIX_CHARS;
    SIX_CHARS;
    FIFO_WAIT; /* 126 */

    SIX_CHARS; /* 132 */
}

uint16_t chargen[256 * CHAR_Y] = {
#include "5x9.h"
};

_Static_assert(FB_WIDTH_CHAR % 6 == 0);

lw_cell_t statusline[FB_WIDTH_CHAR];

static int status_printf(const char *fmt, ...) {
    char buf[2 * FB_WIDTH_CHAR + 1];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    int attr = 0x300;
    int i, j;
    for (i = j = 0; j < FB_WIDTH_CHAR && buf[i]; i++) {
        int c = (unsigned char)buf[i];
        if (c < 32) {
            attr = c << 8;
        } else {
            statusline[j++] = c | attr;
        }
    }
    while (j < FB_WIDTH_CHAR) {
        statusline[j++] = 32 | attr;
    }
    return n;
}
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

// note: not in flash (referenced from core1 generator thread)
static uint16_t base_shade[] = {0,     0x554, 0xaa8, 0xffc, 0,     0x554,
                                0xaa8, 0xffc, 0,     0,     0,     0,
                                0xffc, 0xaa8, 0x554, 0x000, 0xffc, 0xaa8,
                                0x554, 0x000, 0xffc, 0xffc, 0xffc, 0xffc};

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

int frameno = 0;
int bell_frame_end = -1;
__attribute__((noreturn, noinline)) static void
__not_in_flash_func(core1_loop)(void) {
    while (true) {
        uint16_t *shade_ptr = frameno & 0x20 ? base_shade : base_shade + 4;
        if (bell_frame_end > frameno) {
            shade_ptr += 12;
        }
        for (int row = 0; row < FB_HEIGHT_CHAR; row++) {
            uint32_t *chardata =
                row == FB_HEIGHT_CHAR - 1
                    ? (uint32_t *)statusline
                    : (uint32_t *)lw_terminal_vt100_getline(vt100, row);
            for (int j = 0; j < CHAR_Y; j++) {
                scan_convert(chardata, &chargen[256 * j], shade_ptr);
            }
        }

        frameno += 1;
    }
}

static void visual_bell(void *_) { bell_frame_end = frameno + 15; }

static __attribute__((noreturn, noinline)) void
__not_in_flash_func(core1_entry)(void) {
    setup_vga();

    // Turn off flash access. After this, it will hard fault. Better than
    // messing up CIRCUITPY.
    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;
    MPU->RNR = 6; // 7 is used by pico-sdk stack protection.
    MPU->RBAR = XIP_MAIN_BASE | MPU_RBAR_VALID_Msk;
    MPU->RASR = MPU_RASR_XN_Msk | // Set execute never and everything else is
                                  // restricted.
                MPU_RASR_ENABLE_Msk |
                (0x1b << MPU_RASR_SIZE_Pos); // Size is 0x10000000 which masks
                                             // up to SRAM region.
    MPU->RNR = 7;

    core1_loop();
}
#endif

#define BG_ATTR(x) ((x) << 11)
#define FG_ATTR(x) ((x) << 8)

#define MAKE_ATTR(fg, bg) (((fg) ^ (((bg)*9) & 073)) << 8)

static int map_one(int i) { return (i > 0) + (i > 6); }

static lw_cell_t char_attr(void *user_data, const struct lw_parsed_attr *attr) {
    int fg = map_one(attr->fg);
    int bg = map_one(attr->bg);
    if (fg == bg && attr->fg != attr->bg) {
        fg = bg + 1;
    }
    if (attr->bold)
        fg = 3;
    if (attr->blink)
        fg ^= 4;
    if (attr->inverse) {
        return MAKE_ATTR(bg, fg);
    }
    return MAKE_ATTR(fg, bg);
}

queue_t keyboard_queue;

static void reset_cpu(void) {
    watchdog_reboot(0, SRAM_END, 0);
    watchdog_start_tick(12);

    while (true) {
        __wfi();
    }
}

int current_port;

#define COUNT_OF(x) ((sizeof(x) / sizeof((x)[0])))

uint baudrates[] = {300, 1200, 2400, 9600, 19200, 38400, 115200};
#define N_BAUDRATES (COUNT_OF(baudrates))

typedef struct {
    uint data_bits, stop_bits;
    uart_parity_t parity;
    const char *label;
} uart_config_t;

const uart_config_t uart_configs[] = {
    {8, 1, UART_PARITY_NONE, "8N1"}, {8, 2, UART_PARITY_NONE, "8N2"},
    {8, 1, UART_PARITY_EVEN, "8E1"}, {8, 1, UART_PARITY_ODD, "8O1"},
    {7, 1, UART_PARITY_NONE, "7N1"}, {7, 2, UART_PARITY_NONE, "7N2"},
    {7, 1, UART_PARITY_EVEN, "7E1"}, {7, 1, UART_PARITY_ODD, "7O1"},
};
#define N_CONFIGS (COUNT_OF(uart_configs))

typedef struct uart_data {
    int uart, tx, rx, baud_idx, cfg_idx;
} uart_data_t;

uart_data_t uart_data[] = {
    {0, 0, 1},
    {0, 12, 13},
};
#define N_UARTS (COUNT_OF(uart_data))

static void uart_activate(void *data_in) {
    vt100->unicode = 0;
    uart_data_t *data = (uart_data_t *)data_in;
    uart_inst_t *inst = uart_get_instance(data->uart);
    uart_init(inst, baudrates[data->baud_idx]);
    const uart_config_t *config = &uart_configs[data->cfg_idx];
    uart_set_format(inst, config->data_bits, config->stop_bits, config->parity);
    uart_set_fifo_enabled(inst, true);
    gpio_set_function(data->rx, UART_FUNCSEL_NUM(inst, data->rx));
    gpio_set_function(data->tx, UART_FUNCSEL_NUM(inst, data->tx));
    gpio_pull_up(data->rx);
}

static void uart_deactivate(void *data_in) {
    uart_data_t *data = (uart_data_t *)data_in;
    gpio_init(data->rx);
    gpio_init(data->tx);
    gpio_pull_up(data->tx);
}

static int uart_getc_nonblocking(void *data_in) {
    uart_data_t *data = (uart_data_t *)data_in;
    uart_inst_t *inst = uart_get_instance(data->uart);
    if (!uart_is_readable(inst)) {
        return EOF;
    }
    return uart_getc(inst);
}

static void uart_putc_nonblocking(void *data_in, int c) {
    uart_data_t *data = (uart_data_t *)data_in;
    uart_inst_t *inst = uart_get_instance(data->uart);
    if (uart_is_writable(inst)) {
        uart_putc_raw(inst, c);
    }
}

static void uart_cycle_baud_rate(void *data_in) {
    uart_data_t *data = (uart_data_t *)data_in;
    uart_inst_t *inst = uart_get_instance(data->uart);
    data->baud_idx = (data->baud_idx + 1) % N_BAUDRATES;
    uart_set_baudrate(inst, baudrates[data->baud_idx]);
}

static void uart_cycle_settings(void *data_in) {
    uart_data_t *data = (uart_data_t *)data_in;
    uart_inst_t *inst = uart_get_instance(data->uart);
    data->cfg_idx = (data->cfg_idx + 1) % N_CONFIGS;
    const uart_config_t *config = &uart_configs[data->cfg_idx];
    uart_set_format(inst, config->data_bits, config->stop_bits, config->parity);
}

static void uart_describe(void *data_in, char *buf, size_t buflen) {
    uart_data_t *data = (uart_data_t *)data_in;
    const uart_config_t *config = &uart_configs[data->cfg_idx];
    snprintf(buf, buflen, "UART%d %5d %s", current_port,
             baudrates[data->baud_idx], config->label);
}

static void usb_activate(void *data) { vt100->unicode = 1; }

static void usb_deactivate(void *data) {}
static int usb_getc_nonblocking(void *data) {
    int c = getchar_timeout_us(1);
    return (c < 0) ? EOF : c;
}
static void usb_putc_nonblocking(void *data, int c) { stdio_putchar_raw(c); }

static void usb_cycle_baud_rate(void *data) {}
static void usb_cycle_settings(void *data) {}
static void usb_describe(void *data, char *buf, size_t buflen) {
    snprintf(buf, buflen, "%-15s", "USB");
}

typedef struct {
    void *data;
    void (*activate)(void *data);
    void (*deactivate)(void *data);
    int (*getc_nonblocking)(void *data);
    void (*putc_nonblocking)(void *data, int c);
    void (*cycle_baud_rate)(void *data);
    void (*cycle_settings)(void *data);
    void (*describe)(void *data, char *buf, size_t buflen);
} port_descr_t;
static const port_descr_t ports[] = {
    {
        .data = NULL,
        .activate = usb_activate,
        .deactivate = usb_deactivate,
        .getc_nonblocking = usb_getc_nonblocking,
        .putc_nonblocking = usb_putc_nonblocking,
        .cycle_baud_rate = usb_cycle_baud_rate,
        .cycle_settings = usb_cycle_settings,
        .describe = usb_describe,
    },
    {
        .data = &uart_data[0],
        .activate = uart_activate,
        .deactivate = uart_deactivate,
        .getc_nonblocking = uart_getc_nonblocking,
        .putc_nonblocking = uart_putc_nonblocking,
        .cycle_baud_rate = uart_cycle_baud_rate,
        .cycle_settings = uart_cycle_settings,
        .describe = uart_describe,
    },
    {
        .data = &uart_data[1],
        .activate = uart_activate,
        .deactivate = uart_deactivate,
        .getc_nonblocking = uart_getc_nonblocking,
        .putc_nonblocking = uart_putc_nonblocking,
        .cycle_baud_rate = uart_cycle_baud_rate,
        .cycle_settings = uart_cycle_settings,
        .describe = uart_describe,
    },
};
#define NUM_PORTS (COUNT_OF(ports))

#define CURRENT_PORT (ports[current_port])

static bool status_refresh = true;
static void refresh_status(void) { status_refresh = true; }

static void port_deactivate(void) {
    CURRENT_PORT.deactivate(CURRENT_PORT.data);
}

static void port_activate(void) { CURRENT_PORT.activate(CURRENT_PORT.data); }

static int port_getc(void) {
    return CURRENT_PORT.getc_nonblocking(CURRENT_PORT.data);
}

static void port_putc(int c) {
    return CURRENT_PORT.putc_nonblocking(CURRENT_PORT.data, c);
}

static void port_cycle_baud_rate(void) {
    refresh_status();
    CURRENT_PORT.cycle_baud_rate(CURRENT_PORT.data);
}

static void port_cycle_settings(void) {
    refresh_status();
    CURRENT_PORT.cycle_settings(CURRENT_PORT.data);
}

static char *port_describe(void) {
    static char buf[24];
    CURRENT_PORT.describe(CURRENT_PORT.data, buf, sizeof(buf));
    return buf;
}

static void switch_port(void) {
    port_deactivate();
    current_port = (current_port + 1) % NUM_PORTS;
    refresh_status();
    port_activate();
}

static int stdio_kbd_in_chars(char *buf, int length) {
    int rc = 0;
    int code;
    keyboard_poll(&keyboard_queue);
    while (length && queue_try_remove(&keyboard_queue, &code)) {
        DEBUG("code=%04x\r\n", code);
        if ((code & 0xc000) == 0xc000) {
            switch (code) {
            case CMD_SWITCH_RATE:
                port_cycle_baud_rate();
                break;
            case CMD_SWITCH_SETTINGS:
                port_cycle_settings();
                break;
            case CMD_SWITCH_PORT:
                switch_port();
                break;
            case CMD_REBOOT:
                reset_cpu();
            }
            continue;
        }
        *buf++ = code;
        length--;
        rc++;
    }
    return (rc == 0) ? PICO_ERROR_NO_DATA : rc;
}

static int kbd_getc_nonblocking(void) {
    char c;
    int result = stdio_kbd_in_chars(&c, 1);
    if (result == PICO_ERROR_NO_DATA) {
        return EOF;
    }
    return c;
}

#if 0
static stdio_driver_t stdio_kbd = {
    .in_chars = stdio_kbd_in_chars,
};
#endif

static void master_write(void *user_data, void *buffer_in, size_t len) {
    const char *buffer = buffer_in;
    for (; len--; buffer++) {
        port_putc(*buffer);
    }
}

static int map_unicode(void *user_data, int n, lw_cell_t *attr) {
    struct lw_terminal_vt100 *vt100 = (struct lw_terminal_vt100 *)user_data;
    if (n >= 0x1fb00 && n <= 0x1fb3b) {
        n = n - 0x1fb00 +
            129; // 1fb00 is sextant-1, which lives at position 129
        if (n >= 169)
            n++; // the 135 and 246 sextants are elsewhere
        if (n >= 149)
            n++;
        if (n >= 160) { // half of sextants are inverted
            struct lw_parsed_attr tmp_attr = vt100->parsed_attr;
            tmp_attr.inverse = !tmp_attr.inverse;
            n ^= 0x3f;
            *attr = vt100->encode_attr(vt100, &tmp_attr);
        }
        return n;
    }
    switch (n) {
    case 9608: {
        struct lw_parsed_attr tmp_attr = vt100->parsed_attr;
        tmp_attr.inverse = !tmp_attr.inverse;
        *attr = vt100->encode_attr(vt100, &tmp_attr);
        return 32; // FULL BLOCK U+2588
    }
    case 9612:
        return 149; // LEFT HALF BLOCK U+258c
    case 9616: {
        struct lw_parsed_attr tmp_attr = vt100->parsed_attr;
        tmp_attr.inverse = !tmp_attr.inverse;
        *attr = vt100->encode_attr(vt100, &tmp_attr);
        return 149; // RIGHT HALF BLOCK U+2590
    }
    case 9670:
        return 1;
    case 9618:
        return 2;
    case 9225:
        return 3;
    case 9228:
        return 4;
    case 9229:
        return 5;
    case 9226:
        return 6;
    case 9252:
        return 9;
    case 9227:
        return 10;
    case 9496:
        return 11;
    case 9488:
        return 12;
    case 9484:
        return 13;
    case 9492:
        return 14;
    case 9532:
        return 15;
    case 9146:
        return 16;
    case 9147:
        return 17;
    case 9472:
        return 18;
    case 9148:
        return 19;
    case 9149:
        return 20;
    case 9500:
        return 21;
    case 9508:
        return 22;
    case 9524:
        return 23;
    case 9516:
        return 24;
    case 9474:
        return 25;
    case 8804:
        return 26;
    case 8805:
        return 27;
    case 960:
        return 28;
    case 8800:
        return 29;
    }
    return '?';
}

static int old_keyboard_leds;
int main(void) {
#if !STANDALONE
    set_sys_clock_khz(vga_660x477_60_sys_clock_khz, false);
    stdio_init_all();
#endif
    for (int i = 0; i < N_UARTS; i++) {
        gpio_init(uart_data[i].tx);
        gpio_pull_up(uart_data[i].tx);
    }

    vt100 = lw_terminal_vt100_init(NULL, NULL, master_write, char_attr,
                                   FB_WIDTH_CHAR, FB_HEIGHT_CHAR - 1);
    vt100->map_unicode = map_unicode;
    vt100->do_bell = visual_bell;
    multicore_launch_core1(core1_entry);

    scrnprintf(" \r");

    port_activate();

    queue_init(&keyboard_queue, sizeof(int), 64);
    if (!keyboard_setup(pio1)) {
        scrnprintf("KEYBOARD INIT FAILED\r\n");
    }

    scrnprintf("\033[H\033[J\r\n ** \033[1mCR100 Terminal \033[7m READY \033[m "
               "**\r\n\r\n");

    while (true) {
        int c = port_getc();
        if (c > 0) {
            char cc = c;
            lw_terminal_vt100_read_buf(vt100, &cc, 1);
        }
        c = kbd_getc_nonblocking();
        if (c != EOF) {
            port_putc(c);
        }
        if (keyboard_leds != old_keyboard_leds) {
            status_refresh = true;
            old_keyboard_leds = keyboard_leds;
        }

        if (status_refresh) {
            status_printf("\3%s\3 \2 %s %s", port_describe(),
                          keyboard_leds & LED_CAPS ? "\22 CAPS \2" : "      ",
                          keyboard_leds & LED_NUM ? "\22 NUM \2" : "     ");
            status_refresh = false;
        }
    }

    return 0;
}
