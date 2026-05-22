/* WS2812 Neopixel Driver for STM32F4
 * Bit-bang approach using direct register access
 * Uses PB1 for data output
 * Zephyr runs STM32F446 at 84MHz
 */

#include <zephyr.h>
#include <stm32f4xx_ll_gpio.h>
#include <stm32f4xx_ll_bus.h>
#include "neopixel.h"

#define LED_COUNT   16

static uint8_t colors[LED_COUNT][3] = {{0}};
static uint8_t cycle_position = 0;
static uint8_t hue_offset = 0;
static uint8_t neo_mode = 1; /* 0=off, 1=DAC status (default), 2=rainbow */

extern uint16_t get_dac_value(uint8_t channel);

// Direct register access for speed
#define GPIOB_BSRR  (*(volatile uint32_t*)0x40020418)
#define PIN1_SET    (1 << 1)
#define PIN1_RESET  (1 << 17)

// At 84MHz, 1 cycle = 11.9ns
// WS2812B timing (relaxed):
//   T0H = 400ns  (~34 cycles)
//   T1H = 800ns  (~67 cycles)  
//   T0L = 850ns  (~71 cycles)
//   T1L = 450ns  (~38 cycles)
// Total bit = ~1.25us

// NOPs for timing - each NOP = 1 cycle = 11.9ns at 84MHz
#define NOP1  __NOP()
#define NOP5  NOP1; NOP1; NOP1; NOP1; NOP1
#define NOP10 NOP5; NOP5
#define NOP20 NOP10; NOP10

static inline __attribute__((always_inline)) void send_bit(uint8_t bit)
{
    if (bit) {
        // Send 1: high ~800ns, low ~450ns
        GPIOB_BSRR = PIN1_SET;
        NOP20; NOP20; NOP10; NOP10; NOP5; // ~67 cycles = ~800ns
        GPIOB_BSRR = PIN1_RESET;
        NOP20; NOP10; NOP5; // ~35 cycles, rest is function overhead
    } else {
        // Send 0: high ~400ns, low ~850ns
        GPIOB_BSRR = PIN1_SET;
        NOP20; NOP10; // ~30 cycles = ~360ns + overhead
        GPIOB_BSRR = PIN1_RESET;
        NOP20; NOP20; NOP20; NOP5; // ~65 cycles, rest is overhead
    }
}

static void send_byte(uint8_t byte)
{
    send_bit(byte & 0x80);
    send_bit(byte & 0x40);
    send_bit(byte & 0x20);
    send_bit(byte & 0x10);
    send_bit(byte & 0x08);
    send_bit(byte & 0x04);
    send_bit(byte & 0x02);
    send_bit(byte & 0x01);
}

void ws2812_show(uint8_t (*colors)[3])
{
    // Disable interrupts for precise timing
    unsigned int key = irq_lock();
    
    for (int i = 0; i < LED_COUNT; i++) {
        // WS2812 expects GRB order
        send_byte(colors[i][1]);  // Green
        send_byte(colors[i][0]);  // Red
        send_byte(colors[i][2]);  // Blue
    }
    
    irq_unlock(key);
    
    // Reset pulse (>50us low)
    k_busy_wait(80);
}

void neopixel_init(void)
{
    // Enable GPIOB clock
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
    
    // Configure PB1 as output, very high speed
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_1, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_1, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinOutputType(GPIOB, LL_GPIO_PIN_1, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_1, LL_GPIO_PULL_NO);
    
    // Start low
    GPIOB_BSRR = PIN1_RESET;
    
    // Initial reset pulse
    k_msleep(1);
    
    // Clear all LEDs
    neopixel_clear();
}

// Effect functions

void neopixel_clear(void)
{
    for (int i = 0; i < LED_COUNT; i++) {
        colors[i][0] = 0;
        colors[i][1] = 0;
        colors[i][2] = 0;
    }
    ws2812_show(colors);
}

void neopixel_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < LED_COUNT; i++) {
        colors[i][0] = r;
        colors[i][1] = g;
        colors[i][2] = b;
    }
    ws2812_show(colors);
}

void neopixel_set_led(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index < LED_COUNT) {
        colors[index][0] = r;
        colors[index][1] = g;
        colors[index][2] = b;
        ws2812_show(colors);
    }
}

// Cycle red through LEDs - call repeatedly to animate
void neopixel_cycle_red_step(void)
{
    // Clear all LEDs
    for (int i = 0; i < LED_COUNT; i++) {
        colors[i][0] = 0;
        colors[i][1] = 0;
        colors[i][2] = 0;
    }
    
    // Light current position red
    colors[cycle_position][0] = 50;  // Red
    
    ws2812_show(colors);
    
    // Advance position
    cycle_position = (cycle_position + 1) % LED_COUNT;
}

// Convert HSV to RGB (h: 0-255, s: 0-255, v: 0-255)
static void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }
    
    uint8_t region = h / 43;
    uint8_t remainder = (h - (region * 43)) * 6;
    
    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
    
    switch (region) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

// Rainbow effect - call repeatedly to animate
void neopixel_rainbow_step(void)
{
    for (int i = 0; i < LED_COUNT; i++) {
        uint8_t hue = hue_offset + (i * 256 / LED_COUNT);
        hsv_to_rgb(hue, 255, 40, &colors[i][0], &colors[i][1], &colors[i][2]);
    }
    ws2812_show(colors);
    
    hue_offset += 2;
}

/*
 * Smooth analog spectrum: every DAC value maps to a unique (hue, brightness)
 * pair across the 0-5V output range (AD5679R, 2.5V ref, 2x gain).
 *
 * Hue sweeps 160 (blue) → 0 (red) as pressure rises.
 * Brightness scales 20 → 80 so higher pressure is visually brighter.
 * val=0 shows dim blue so all 16 channels are always visible.
 */
void neopixel_update_dac_status(void)
{
    for (int i = 0; i < LED_COUNT; i++) {
        uint16_t val = get_dac_value((uint8_t)i);
        uint8_t hue    = (uint8_t)((uint32_t)(65535 - val) * 160 / 65535);
        uint8_t bright = 20 + (uint8_t)((uint32_t)val * 60 / 65535);
        hsv_to_rgb(hue, 255, bright,
                   &colors[i][0], &colors[i][1], &colors[i][2]);
    }
    ws2812_show(colors);
}

void neopixel_set_mode(uint8_t mode)
{
    neo_mode = mode;
    if (mode == 0) {
        neopixel_clear();
    }
}

uint8_t neopixel_get_mode(void)
{
    return neo_mode;
}

/* Called periodically from the control thread to refresh LEDs based on mode */
void neopixel_periodic(void)
{
    switch (neo_mode) {
    case 1:
        neopixel_update_dac_status();
        break;
    case 2:
        neopixel_rainbow_step();
        break;
    default:
        break;
    }
}
