#pragma once

#include "pico/util/queue.h"

extern void keyboard_setup();
extern void keyboard_poll(queue_t *q);
extern void keyboard_leds(int value);
