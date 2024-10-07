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

enum { SHIFT=1, CTRL=2, ALT=3 };
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
    [0x15] = "q",
    [0x1d] = "w",
    [0x24] = "e",
    [0x2d] = "r",
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
