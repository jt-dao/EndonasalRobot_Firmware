#ifndef NEOPIXEL_H
#define NEOPIXEL_H

#include <stdint.h>

#define LED_COUNT   16

// Low-level driver functions
void neopixel_init(void);
void ws2812_show(uint8_t (*colors)[3]);

// Effect functions (callable from main)
void neopixel_clear(void);
void neopixel_set_all(uint8_t r, uint8_t g, uint8_t b);
void neopixel_set_led(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void neopixel_cycle_red_step(void);  // Call repeatedly to animate
void neopixel_rainbow_step(void);      // Call repeatedly to animate

#endif // NEOPIXEL_H
