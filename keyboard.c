#include "keyboard.h"
#include "atkbd.pio.h"
#include "chargen.h"
#include "pinout.h"

#include "hardware/clocks.h"
#include "pico.h"
#include "pico/stdlib.h"

#define EOF (-1)
#define DEBUG(...) ((void)0)

static int pending_led_value;
static bool pending_led_flag;

static PIO kbd_pio;
static int kbd_sm;

static int ll_kbd_read_timeout(int timeout_us) {
    uint64_t deadline = time_us_64() + timeout_us;
    while (pio_sm_is_rx_fifo_empty(kbd_pio, kbd_sm)) {
        if (time_us_64() > deadline) {
            return EOF;
        }
    }
    return pio_sm_get_blocking(kbd_pio, kbd_sm);
}

static int kbd_read_timeout(int timeout_us) {
    int r = ll_kbd_read_timeout(timeout_us);
    if (r == EOF) {
        return EOF;
    }
    r = (r >> 22) & 0xff;
    return r; // todo: check parity, start & end bits!
}

static int parity(int x) {
    x ^= (x >> 4);
    x ^= (x >> 2);
    x ^= (x >> 1);
    return (x & 1) ^ 1;
}
static void kbd_write_blocking(int value) {
    value = value | (parity(value) << 8);
    pio_sm_put_blocking(kbd_pio, kbd_sm, ~value);
}

static bool expect(int expected, const char *msg) {
    int value = kbd_read_timeout(10000000);
    if (expected != value) {
        scrnprintf("%s: Expected 0x%02x, got 0x%02x\r\n", msg, expected, value);
        return false;
    }
    return true;
}

static bool write_expect_fa(int value, const char *msg) {
    pio_sm_clear_fifos(kbd_pio, kbd_sm);
    kbd_write_blocking(value);
    return expect(0xfa, msg);
}
bool keyboard_setup(PIO pio) {
    DEBUG("pre-waiting for keyboard to boot\r");
    sleep_ms(1600);

    gpio_init(KEYBOARD_DATA_PIN);
    gpio_init(KEYBOARD_DATA_PIN + 1);

    gpio_pull_up(KEYBOARD_DATA_PIN);
    gpio_pull_up(KEYBOARD_DATA_PIN + 1);

#if 0
    int i = 0, j = 1;
    while (!(gpio_get(KEYBOARD_DATA_PIN) && gpio_get(KEYBOARD_DATA_PIN + 1))) {
        if (i++ == j) {
            DEBUG("Waiting for keyboard to boot... %d ms so far\r", i);
            j *= 10;
        }
        sleep_ms(1);
    }

    sleep_ms(100);
#endif

    kbd_pio = pio;
    uint offset = pio_add_program(pio, &atkbd_program);
    kbd_sm = pio_claim_unused_sm(pio, true);
    atkbd_program_init(pio, kbd_sm, offset, KEYBOARD_DATA_PIN);

    bool ok = write_expect_fa(0xff, "reset keyboard");
    while (!ok) {
        sleep_ms(10000);
        write_expect_fa(0xff, "reset keyboard");
    }
    if (ok) {
        ok = expect(0xaa, "self-test result");
    }
    if (ok) {
        ok = write_expect_fa(0xf0, "set mode");
    }
    if (ok) {
        ok = write_expect_fa(0x03, "mode 3");
    }
    if (ok) {
        ok = write_expect_fa(0xfa, "all repeat");
    }
    return ok;
}

enum {
    LSHIFT = 1,
    LCTRL = 2,
    LALT = 4,
    RSHIFT = 8,
    RCTRL = 16,
    RALT = 32,
    MOD_CAPS = 64,
    MOD_NUM = 128,
    TOGGLING_MODIFIERS = MOD_CAPS | MOD_NUM
};
const char keyboard_modifiers[256] = {
    [0x12] = LSHIFT, [0x59] = RSHIFT, [0x11] = LCTRL,    [0x58] = RCTRL,
    [0x19] = LALT,   [0x39] = RALT,   [0x14] = MOD_CAPS, [0x76] = MOD_NUM,
};

enum SYMBOLS {
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    PRTSCR,
    SCRLCK,
    PAUSE,
    INSERT,
    DELETE,
    HOME,
    END,
    PAGEUP,
    PAGEDOWN,
    UPARROW,
    DOWNARROW,
    LEFTARROW,
    RIGHTARROW,
    MAX_SYMBOLS
};

const char *const symtab[MAX_SYMBOLS] = {
#define ENT(x, y) [x] = y
    ENT(F1, "\eOP"),         ENT(F2, "\eOQ"),        ENT(F3, "\eOR"),
    ENT(F4, "\eOS"),         ENT(F5, "\e[15~"),      ENT(F6, "\e[17~"),
    ENT(F7, "\e[18~"),       ENT(F8, "\e[19~"),      ENT(F9, "\e[20~"),
    ENT(F10, "\e[21~"),      ENT(F11, "\e[23~"),     ENT(F12, "\e[24~"),
    ENT(PRTSCR, "\ei"),      ENT(PAUSE, ""),         ENT(INSERT, "\e[2~"),
    ENT(DELETE, "\e[3~"),    ENT(UPARROW, "\e[A"),   ENT(DOWNARROW, "\e[B"),
    ENT(RIGHTARROW, "\e[C"), ENT(LEFTARROW, "\e[D"), ENT(HOME, "\e[H"),
    ENT(END, "\e[F"),        ENT(PAGEUP, "\e[5~"),   ENT(PAGEDOWN, "\e[6~"),
#undef ENT
};

#define BIT_SYM (0x2 << 14)
#define BIT_CMD (0x3 << 14)
#define MASK_CLASS (0x3 << 14)
#define CLASSIFY(x) (x & MASK_CLASS)
#define VALUE(x) (x & ~MASK_CLASS)
#define IS_SYM(x) (CLASSIFY(x) == BIT_SYM)
#define IS_CMD(x) (CLASSIFY(x) == BIT_CMD)
#define MAKE_SYM(x) (BIT_SYM | (int)(x))
#define MAKE_CMD(x) (BIT_CMD | (int)(x))
#define CHAR2(c, d) (int)c | (((int)d) << 8)
#define CHAR2(c, d) (int)c | (((int)d) << 8)
#define ALPHA(c) CHAR2(c, c ^ ('a' ^ 'A'))
#define NOMOD(c) CHAR2(c, c)

#define LO(x) (x & 0xff)
#define HI(x) ((x >> 8) & 0xff)

const uint32_t keyboard_codes[256] = {
    [0x08] = '\033',
    [0x07] = MAKE_SYM(F1),
    [0x0f] = MAKE_SYM(F2),
    [0x17] = MAKE_SYM(F3),
    [0x1f] = MAKE_SYM(F4),
    [0x27] = MAKE_SYM(F5),
    [0x2f] = MAKE_SYM(F6),
    [0x37] = MAKE_SYM(F7),
    [0x3f] = MAKE_SYM(F8),
    [0x47] = MAKE_SYM(F9),
    [0x4f] = MAKE_SYM(F10),
    [0x56] = MAKE_SYM(F11),
    [0x5e] = MAKE_SYM(F12),
    [0x57] = MAKE_SYM(PRTSCR),
    [0x5f] = MAKE_SYM(SCRLCK),
    [0x62] = MAKE_SYM(PAUSE),
    [0x67] = MAKE_SYM(INSERT),
    [0x64] = MAKE_SYM(DELETE),

    [0x0e] = CHAR2('`', '~'),
    [0x16] = CHAR2('1', '!'),
    [0x1e] = CHAR2('2', '@'),
    [0x26] = CHAR2('3', '#'),
    [0x25] = CHAR2('4', '$'),
    [0x2e] = CHAR2('5', '%'),
    [0x36] = CHAR2('6', '^'),
    [0x3d] = CHAR2('7', '&'),
    [0x3e] = CHAR2('8', '*'),
    [0x46] = CHAR2('9', '('),
    [0x45] = CHAR2('0', ')'),
    [0x4e] = CHAR2('-', '_'),
    [0x55] = CHAR2('=', '+'),
    [0x66] = '\010',

    [0x0d] = '\t',
    [0x15] = ALPHA('q'),
    [0x1d] = ALPHA('w'),
    [0x24] = ALPHA('e'),
    [0x2d] = ALPHA('r'),
    [0x2c] = ALPHA('t'),
    [0x35] = ALPHA('y'),
    [0x3c] = ALPHA('u'),
    [0x43] = ALPHA('i'),
    [0x44] = ALPHA('o'),
    [0x4d] = ALPHA('p'),
    [0x54] = CHAR2('[', '{'),
    [0x5b] = CHAR2(']', '}'),
    [0x5c] = CHAR2('\\', '|'),

    [0x1c] = ALPHA('a'),
    [0x1b] = ALPHA('s'),
    [0x23] = ALPHA('d'),
    [0x2b] = ALPHA('f'),
    [0x34] = ALPHA('g'),
    [0x33] = ALPHA('h'),
    [0x3b] = ALPHA('j'),
    [0x42] = ALPHA('k'),
    [0x4b] = ALPHA('l'),
    [0x4c] = CHAR2(';', ':'),
    [0x52] = CHAR2('\'', '"'),
    [0x5a] = '\n',

    [0x1a] = ALPHA('z'),
    [0x22] = ALPHA('x'),
    [0x21] = ALPHA('c'),
    [0x2a] = ALPHA('v'),
    [0x32] = ALPHA('b'),
    [0x31] = ALPHA('n'),
    [0x3a] = ALPHA('m'),
    [0x41] = CHAR2(',', '<'),
    [0x49] = CHAR2('.', '>'),
    [0x4a] = CHAR2('/', '?'),
    [0x63] = MAKE_SYM(UPARROW),

    [0x29] = ' ',
    [0x61] = MAKE_SYM(LEFTARROW),
    [0x6a] = MAKE_SYM(RIGHTARROW),
    [0x60] = MAKE_SYM(DOWNARROW),

    [0x6e] = MAKE_SYM(HOME),
    [0x65] = MAKE_SYM(END),
    [0x6f] = MAKE_SYM(PAGEUP),
    [0x6d] = MAKE_SYM(PAGEDOWN),
};

bool pending_release;
int current_modifiers = 0;

static void queue_add_data(queue_t *q, int data) {
    (void)queue_try_add(q, &data);
}

static void queue_add_str(queue_t *q, const char *s) {
    while (*s)
        queue_add_data(q, *s++);
}

static void queue_handle_event(queue_t *q, bool release, int value) {
    int modifiers = keyboard_modifiers[value];
    DEBUG("queue_handle_event release=%d value=%d modifiers=%d\r\n", release,
          value, modifiers);
    if (modifiers) {
        if (release) {
            if (modifiers & TOGGLING_MODIFIERS) {
                current_modifiers ^= modifiers;
                keyboard_set_leds(
                    ((current_modifiers & MOD_NUM) ? LED_NUM : 0) |
                    ((current_modifiers & MOD_CAPS) ? LED_CAPS : 0));
            } else {
                current_modifiers &= ~modifiers;
            }
        } else {
            if (modifiers & TOGGLING_MODIFIERS) {
                /* NOTHING */
            } else {
                current_modifiers |= modifiers;
            }
        }
        return;
    }
    if (release) {
        return;
    }

    bool is_shift = current_modifiers & (LSHIFT | RSHIFT);
    bool is_ctrl = current_modifiers & (LCTRL | RCTRL);
    bool is_alt = current_modifiers & (LALT | RALT);
    bool is_caps = current_modifiers & (MOD_CAPS);

    int kc = keyboard_codes[value];

    DEBUG("kc=%04x is_[scaX] = %d %d %d %d\r\n", kc, is_shift, is_ctrl, is_alt,
          is_caps);

    if (!kc) {
        scrnprintf("\r\nUn-mapped key: 0x%02x\r\n", value);
        return;
    }

    if (IS_CMD(kc)) {
        queue_add_data(q, kc);
        return;
    }

    if (IS_SYM(kc)) {
        int sym = VALUE(kc);
        if (is_ctrl && is_alt) {
            if (sym == DELETE) {
                queue_add_data(q, CMD_REBOOT);
            }
            if (sym == F1) {
                queue_add_data(q, CMD_SWITCH_PORT);
            }
            if (sym == F2) {
                queue_add_data(q, CMD_SWITCH_RATE);
            }
            if (sym == F3) {
                queue_add_data(q, CMD_SWITCH_SETTINGS);
            }
            return;
        }
        queue_add_str(q, symtab[kc & 0x7fff]);
        return;
    }

    int c = LO(kc);
    // DEBUG("c=%d HI=%d is_shift=%d\n", c, HI(kc), is_shift);
    if (HI(kc) && is_shift)
        c = HI(kc);
#define CTRLABLE(c) (c >= 64 && c <= 127)
    if (is_ctrl && CTRLABLE(c)) {
        c = c & 0x1f;
    }
    if (is_ctrl && c == 010) {
        c = 0377; // ctrl-backspace = RUBOUT
    }
#define IS_ALPHA(c) ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
    if (is_caps && IS_ALPHA(c)) {
        c ^= ('a' ^ 'A');
    }
    if (is_alt) {
        queue_add_data(q, '\033');
    }
    queue_add_data(q, c);
}

void keyboard_poll(queue_t *q) {
    int value = kbd_read_timeout(0);

    if (value == EOF) {
        return;
    }
    DEBUG("keyboard_poll %02x\r\n", value);
    if (value == 0xfa) {
        if (pending_led_flag) {
            kbd_write_blocking(pending_led_value);
            pending_led_flag = false;
        }
    } else if (value == 0xf0) {
        pending_release = true;
    } else if (!(value & 0x80)) {
        queue_handle_event(q, pending_release, value);
        pending_release = false;
    }
}

int keyboard_leds;

void keyboard_set_leds(int value) {
    keyboard_leds = value;
    // return;
    if (value != pending_led_value) {
        pending_led_value = value;
        pending_led_flag = true;
        kbd_write_blocking(0xed);
    }
}
