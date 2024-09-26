#include "atkbd.pio.h"
#include "keyboard.h"
#include "pinout.h"

#include "pico.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"

extern void keyboard_setup(PIO pio) {
    uint offset = pio_add_program(pio, &atkbd_program);
    uint sm = pio_claim_unused_sm(pio, true);
    atkbd_program_init(pio, sm, offset, KEYBOARD_DATA_PIN);
}

extern void keyboard_poll(queue_t *q) {
}

extern void keyboard_leds(int value) {
}
