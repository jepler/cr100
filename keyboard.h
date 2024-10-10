#pragma once

#include "hardware/pio.h"
#include "pico/util/queue.h"

extern bool keyboard_setup(PIO pio);
extern void keyboard_poll(queue_t *q);
extern void keyboard_set_leds(int value);
extern void atkbd_program_init(PIO pio, int sm, int offset, int base_pin);
enum { LED_NUM = 4, LED_CAPS = 2 };
extern int keyboard_leds;
