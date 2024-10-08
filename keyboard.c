#include "atkbd.pio.h"
#include "keyboard.h"
#include "pinout.h"
#include "chargen.h"

#include "pico.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"

#define EOF (-1)

static int pending_led_value;
static bool pending_led_flag;

static PIO kbd_pio;
static int kbd_sm;

static int ll_kbd_read_timeout(int timeout_us) {
    uint64_t deadline = time_us_64() + timeout_us;
    while (pio_sm_is_rx_fifo_empty(kbd_pio, kbd_sm)) {
        if(time_us_64() > deadline) { return EOF; }
    }
    return pio_sm_get_blocking(kbd_pio, kbd_sm);
}

static int kbd_read_timeout(int timeout_us) {
    int r = ll_kbd_read_timeout(timeout_us);
    if (r == EOF) { return EOF; }
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
    scrnprintf("sm put %03x\r\n", value);
    pio_sm_put_blocking(kbd_pio, kbd_sm, value);
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
    gpio_init(KEYBOARD_DATA_PIN);
    gpio_init(KEYBOARD_DATA_PIN + 1);

    gpio_pull_down(KEYBOARD_DATA_PIN);
    gpio_pull_down(KEYBOARD_DATA_PIN + 1);

    while(!(gpio_get(KEYBOARD_DATA_PIN) && gpio_get(KEYBOARD_DATA_PIN + 1)))
        scrnprintf("Waiting for keyboard to boot...\r\n");
    
    sleep_ms(10);

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

enum { SHIFT=1, CTRL=2, ALT=4 };
const char keyboard_modifiers[256] = {
    [0x12] = SHIFT,
    [0x59] = SHIFT,
    [0x11] = CTRL,
    [0x58] = CTRL,
    [0x19] = ALT,
    [0x39] = ALT,
};

const char *keyboard_codes[256] = {
    [0x08] = "ESC",
    [0x07] = "F1",
    [0x0f] = "F2",
    [0x17] = "F3",
    [0x1f] = "F4",
    [0x27] = "F4",
    [0x2f] = "F4",
    [0x37] = "F4",
    [0x3f] = "F4",
    [0x47] = "F4",
    [0x4f] = "F10",
    [0x56] = "F11",
    [0x5e] = "F12",
    [0x76] = "NUMLOCK",
    [0x57] = "PRTSCR",
    [0x5f] = "SCRLCK",
    [0x62] = "PAUSE",
    [0x67] = "INSERT",
    [0x64] = "DELETE",
    
    [0x0e] = "`",
    [0x16] = "1",
    [0x1e] = "2",
    [0x26] = "3",
    [0x25] = "4",
    [0x2e] = "5",
    [0x36] = "6",
    [0x3d] = "7",
    [0x3e] = "8",
    [0x46] = "9",
    [0x45] = "0",
    [0x4e] = "-",
    [0x55] = "=",
    [0x66] = "\010",

    [0x0d] = "\t",
    [0x15] = "q",
    [0x1d] = "w",
    [0x24] = "e",
    [0x2d] = "r",
    [0x2c] = "t",
    [0x35] = "y",
    [0x3c] = "u",
    [0x43] = "i",
    [0x44] = "o",
    [0x4d] = "p",
    [0x54] = "[",
    [0x5b] = "]",
    [0x5c] = "\\",

    [0x14] = "CAPS",
    [0x1c] = "a",
    [0x1b] = "s",
    [0x23] = "d",
    [0x2b] = "f",
    [0x34] = "g",
    [0x33] = "h",
    [0x3b] = "j",
    [0x42] = "k",
    [0x4b] = "l",
    [0x4c] = ";",
    [0x52] = "'",
    [0x5a] = "\n",

    [0x1a] = "z",
    [0x22] = "x",
    [0x21] = "c",
    [0x2a] = "v",
    [0x32] = "b",
    [0x31] = "n",
    [0x3a] = "m",
    [0x41] = ",",
    [0x49] = ".",
    [0x4a] = "/",
    [0x63] = "uparrow",

    [0x29] = " ",
    [0x61] = "leftarrow",
    [0x6a] = "rightarrow",
    [0x60] = "downarrow",


    [0x6e] = "home",
    [0x65] = "end",
    [0x6f] = "pgup",
    [0x6d] = "pgdn",
};

bool pending_release;
int current_modifiers = 0;

static
void queue_add_str(queue_t *q, bool release, int value) {
    int modifiers = keyboard_modifiers[value];
    if (modifiers) {
        if(release) current_modifiers &= ~modifiers;
        else current_modifiers |= modifiers;
        scrnprintf("\r\nModifiers -> 0x%02x\r\n", current_modifiers); 
        return;
    }
    if(release) { return; }

    const char *ptr = keyboard_codes[value];
    if (!ptr) {
        scrnprintf("\r\nUn-mapped key: 0x%02x\r\n", value); 
        return;
    }
    while(*ptr) {
        int data = *ptr++;
        queue_try_add(q, &data);
    }
}

void keyboard_poll(queue_t *q) {
    int value = kbd_read_timeout(0);
    if (value == EOF) {
        return;
    }
    else if (value == 0xfa) {
        if(pending_led_flag) {
            kbd_write_blocking(pending_led_value);
            pending_led_flag = false;
        }
    } else if (value == 0xf0) {
        pending_release = true;
    } else {
        queue_add_str(q, pending_release, value);
        pending_release = false;
    }
}

void keyboard_leds(int value) {
    if (value != pending_led_value) {
        pending_led_value = value;
        pending_led_flag = true;
        kbd_write_blocking(0xed);
    }
}
