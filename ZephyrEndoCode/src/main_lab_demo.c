/*
 * Endonasal Robot Firmware - Interactive Lab Demo
 * Serial menu:
 *   1 = LEDs
 *   2 = DAC
 *   3 = Solenoids
 *   4 = ADC
 *   5 = ALL
 *   0 = STOP outputs
 *   h = menu
 *
 * Stop running demos with: s
 *
 * To use: rename to main.c and exclude main.c from build, or use build switch.
 * See PYTHON_INTEGRATION_README.md for switching between Lab Demo and Python integration.
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/uart.h>
#include <drivers/gpio.h>
#include <stm32f4xx_ll_bus.h>
#include "neopixel.h"
#include "solenoid.h"

extern int led_init(void);
extern void led_toggle(void);
extern void dac_init(void);
extern void adc_init(void);
extern void pwm_init(void);
extern void set_dac_channel(uint8_t channel, int value);
extern void i2c_adc_init(void);
extern void i2c_adc_scan(void);
extern int  i2c_adc_read_channel(uint8_t addr, uint8_t ch);
extern int  i2c_adc_probe_addr(uint8_t addr);

static const struct device *console_uart;
static uint8_t led_ready, neo_ready, dac_ready, sol_ready, adc_ready;
static uint8_t adc_addrs[4];
static uint8_t adc_addr_count;

/* Pressure sensor scaling
 * User-calibrated mapping:
 *   0..255 counts -> 0..60 PSI
 *   Example: 124/255 ~= 2.43V ~= 29.2 PSI (about mid-scale)
 */
#define PRESSURE_ZERO_COUNTS   0.0f
#define PRESSURE_FULL_COUNTS   255.0f
#define PRESSURE_FS_PSI        60.0f
#define ADC_VREF_V             5.0f

static void console_init_uart(void)
{
    console_uart = NULL;
#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_console), okay)
    const struct device *chosen = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    if (device_is_ready(chosen)) {
        console_uart = chosen;
    }
#endif
    if (!console_uart) console_uart = device_get_binding("UART_2");
    if (!console_uart) console_uart = device_get_binding("USART_2");
    if (!console_uart) console_uart = device_get_binding("UART_1");
    if (!console_uart) printk("# WARNING: console UART bind failed\n");
}

static int key_pressed(void)
{
    unsigned char ch;
    if (console_uart && uart_poll_in(console_uart, &ch) == 0) return (int)ch;
    return -1;
}

static void flush_input(void)
{
    while (key_pressed() >= 0) { }
}

static int stop_requested(void)
{
    int c = key_pressed();
    return (c == 's' || c == 'S');
}

static float adc_counts_to_psi(int raw)
{
    float psi = ((float)raw - PRESSURE_ZERO_COUNTS) *
                (PRESSURE_FS_PSI / (PRESSURE_FULL_COUNTS - PRESSURE_ZERO_COUNTS));
    if (psi < 0.0f) psi = 0.0f;
    if (psi > PRESSURE_FS_PSI) psi = PRESSURE_FS_PSI;
    return psi;
}

static float adc_counts_to_volts(int raw)
{
    if (raw < 0) raw = 0;
    if (raw > 255) raw = 255;
    return ((float)raw / 255.0f) * ADC_VREF_V;
}

static void print_menu(void)
{
    printk("\n# ============================================\n");
    printk("# Endonasal Robot - Full Test Profile\n");
    printk("# ============================================\n");
    printk("# 1 = LEDs\n");
    printk("# 2 = DAC\n");
    printk("# 3 = Solenoids\n");
    printk("# 4 = ADC\n");
    printk("# 5 = ALL\n");
    printk("# p = Pin Step (PB10, PB12, PB14, PB15)\n");
    printk("# m = Motor (A4988: EN=PA0, STEP=PC0, DIR=PC3)\n");
    printk("# 0 = STOP outputs\n");
    printk("# h = show menu\n");
    printk("# (single key menu; Enter not required)\n");
    printk("# ============================================\n# > ");
}

static void demo_leds(void)
{
    printk("\n# --- LED Demo ---\n");
    if (!led_ready) led_ready = led_init();
    if (led_ready) {
        for (int i = 0; i < 10; i++) { led_toggle(); k_msleep(100); }
    }
    if (!neo_ready) { neopixel_init(); neo_ready = 1; }
    printk("# Neopixel rainbow; press s to stop\n");
    flush_input();
    while (!stop_requested()) {
        neopixel_rainbow_step();
        k_msleep(30);
    }
    neopixel_clear();
}

static void demo_dac(void)
{
    printk("\n# --- DAC Demo (AD5679R) ---\n");
    if (!dac_ready) { dac_init(); dac_ready = 1; k_msleep(10); }

    printk("# DAC controls (single key):\n");
    printk("#   m = set all channels to 2.5V (known-good)\n");
    printk("#   c = set channel 0 only to 2.5V (scope one 24-clock frame)\n");
    printk("#   z = set all channels to 0V\n");
    printk("#   f = set all channels to 5.0V\n");
    printk("#   r = staircase ramp ch0..ch15 (0V..5V)\n");
    printk("#   q = return to main menu\n");
    flush_input();

    while (1) {
        int c = key_pressed();
        if (c < 0) { k_msleep(20); continue; }

        if (c == 'q' || c == 'Q') {
            break;
        } else if (c == 'm' || c == 'M') {
            for (int i = 0; i < 16; i++) {
                set_dac_channel((uint8_t)i, 32768);
                k_msleep(1);
            }
            printk("# All channels set to 2.5V\n");
        } else if (c == 'c' || c == 'C') {
            set_dac_channel(0, 32768);
            printk("# Channel 0 set to 2.5V\n");
        } else if (c == 'z' || c == 'Z') {
            for (int i = 0; i < 16; i++) {
                set_dac_channel((uint8_t)i, 0);
                k_msleep(1);
            }
            printk("# All channels set to 0V\n");
        } else if (c == 'f' || c == 'F') {
            for (int i = 0; i < 16; i++) {
                set_dac_channel((uint8_t)i, 65535);
                k_msleep(1);
            }
            printk("# All channels set to 5.0V\n");
        } else if (c == 'r' || c == 'R') {
            for (int i = 0; i < 16; i++) {
                uint16_t val = (uint16_t)((65535UL * i) / 15);
                set_dac_channel(i, val);
                printk("# ch%-2d = %.2fV\n", i, (val / 65535.0f) * 5.0f);
                k_msleep(1);
            }
        } else {
            printk("# DAC key: m=2.5V all, z=0V all, f=5V all, r=ramp, q=quit\n");
        }
    }
}

static void demo_solenoids(void)
{
    printk("\n# --- Solenoid Demo ---\n");
    if (!sol_ready) { solenoid_init(); sol_ready = 1; k_msleep(10); }
    printk("# Toggling each solenoid for 5 seconds; press s to stop\n");
    flush_input();
    for (int sol = 1; sol <= 8; sol++) {
        uint32_t elapsed_ms = 0;
        uint8_t state = 0;
        printk("# SOL%d toggling...\n", sol);
        while (elapsed_ms < 5000U) {
            if (stop_requested()) {
                for (int i = 1; i <= 8; i++) set_solenoid(i, 0);
                printk("# Solenoid demo stopped\n");
                return;
            }
            state = !state;
            set_solenoid(sol, state);
            printk("# SOL%d %s\n", sol, state ? "ON" : "OFF");
            k_msleep(500);
            elapsed_ms += 500U;
        }
        set_solenoid(sol, 0);
    }
    for (int i = 1; i <= 8; i++) set_solenoid(i, 0);
}

static void scan_adc_addrs_once(void)
{
    adc_addr_count = 0;
    for (uint8_t a = 0x48; a <= 0x4B; a++) {
        if (i2c_adc_probe_addr(a) == 1) adc_addrs[adc_addr_count++] = a;
    }
}

/* Must run after pwm_init(): TIM4 claims PB8/PB9 as CH3/CH4 (AF2). i2c_adc_init()
 * remuxes them to I2C1 (AF4). Without this, SCL/SDA stay timer pins and appear dead
 * at 0% PWM duty — same ordering as src/main.c. */
static void lab_i2c_bringup(void)
{
    i2c_adc_init();
    k_msleep(50);
    i2c_adc_scan();
    scan_adc_addrs_once();
    adc_ready = 1;
}

static void demo_adc(void)
{
    printk("\n# --- ADC Demo ---\n");
    if (!adc_ready) {
        lab_i2c_bringup();
    }
    if (adc_addr_count == 0) {
        printk("# No ADS7830 found (0x48..0x4B). Press s to return.\n");
        flush_input();
        while (!stop_requested()) k_msleep(20);
        return;
    }
    char mode = 'p'; /* r=raw, p=psi, v=volts */
    printk("# ADC view modes: r=raw, p=psi, v=volts, s=stop\n");
    printk("# Scale: 31 counts = 0 PSI, 230 counts = %.1f PSI\n", PRESSURE_FS_PSI);
    flush_input();
    while (1) {
        int c = key_pressed();
        if (c == 's' || c == 'S') break;
        if (c == 'r' || c == 'R') mode = 'r';
        if (c == 'p' || c == 'P') mode = 'p';
        if (c == 'v' || c == 'V') mode = 'v';

        printk("# mode=%c ", mode);
        for (int i = 0; i < adc_addr_count; i++) {
            uint8_t addr = adc_addrs[i];
            printk("# 0x%02X: ", addr);
            for (int ch = 0; ch < 8; ch++) {
                int v = i2c_adc_read_channel(addr, ch);
                if (v < 0) printk("ERR ");
                else if (mode == 'r') printk("%3d ", v);
                else if (mode == 'v') printk("%4.2f ", (double)adc_counts_to_volts(v));
                else printk("%5.1f ", (double)adc_counts_to_psi(v));
            }
            if (i < adc_addr_count - 1) printk(" | ");
        }
        printk("\n");
        k_msleep(500);
    }
}

static void stop_all(void)
{
    if (neo_ready) neopixel_clear();
    if (sol_ready) for (int i = 1; i <= 8; i++) set_solenoid(i, 0);
    if (dac_ready) for (int i = 0; i < 16; i++) set_dac_channel(i, 0);
    printk("# Outputs stopped.\n");
}

static void demo_pins(void)
{
    const struct device *gpiob = device_get_binding("GPIOB");
    if (!gpiob) {
        printk("# Error: GPIOB not found\n");
        return;
    }

    printk("\n# --- SPI Pin Stepper ---\n");
    printk("# 0 = Toggle PB10 (SCK)\n");
    printk("# 2 = Toggle PB12 (CS)\n");
    printk("# 4 = Toggle PB14 (MISO)\n");
    printk("# 5 = Toggle PB15 (MOSI)\n");
    printk("# q = Back to main menu\n");

    gpio_pin_configure(gpiob, 10, GPIO_OUTPUT_LOW);
    gpio_pin_configure(gpiob, 12, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpiob, 14, GPIO_OUTPUT_LOW);
    gpio_pin_configure(gpiob, 15, GPIO_OUTPUT_LOW);

    static uint8_t sck=0, cs=1, miso=0, mosi=0;
    
    flush_input();
    while (1) {
        int c = key_pressed();
        if (c < 0) { k_msleep(20); continue; }

        if (c == 'q' || c == 'Q') break;

        switch (c) {
            case '0': sck = !sck; gpio_pin_set(gpiob, 10, sck); printk("# PB10 (SCK)  -> %d\n", sck); break;
            case '2': cs = !cs;   gpio_pin_set(gpiob, 12, cs);  printk("# PB12 (CS)   -> %d\n", cs); break;
            case '4': miso = !miso; gpio_pin_set(gpiob, 14, miso); printk("# PB14 (MISO) -> %d\n", miso); break;
            case '5': mosi = !mosi; gpio_pin_set(gpiob, 15, mosi); printk("# PB15 (MOSI) -> %d\n", mosi); break;
        }
    }
}

/* A4988 stepper test: DIR high, 200 step pulses, 500ms pause.
 * EN active-low; STEP rising edge advances one microstep; DIR sampled on STEP edge.
 * Pins: EN=PA0, STEP=PC0, DIR=PC3.
 * Driven via LL GPIO (push-pull, low slew rate) rather than the Zephyr GPIO API:
 * the Zephyr path left the line floating, and adc_init() leaves PC0/PC3 in analog
 * mode — LL_GPIO_Init overrides both. */
#define MOTOR_STEP_DELAY_US  500            /* half-period, flat (matches Arduino) */
#define MOTOR_EN_PIN         LL_GPIO_PIN_0   /* PA0 */
#define MOTOR_STEP_PIN       LL_GPIO_PIN_0   /* PC0 */
#define MOTOR_DIR_PIN        LL_GPIO_PIN_3   /* PC3 */
static void demo_motor(void)
{
    printk("\n# --- Motor Demo (A4988) ---\n");
    printk("# EN=PA0 (active low), STEP=PC0, DIR=PC3\n");
    printk("# DIR low, 200 step pulses, 500ms pause; press s to stop\n");

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);

    LL_GPIO_InitTypeDef io = {0};
    io.Mode = LL_GPIO_MODE_OUTPUT;
    /* LOW slew rate on purpose: VERY_HIGH gives ns-scale edges that ring on an
     * unterminated wire, and the A4988 STEP comparator counts the rings as extra
     * steps. Slow edges (like the Arduino's) keep a 1 kHz step train clean. */
    io.Speed = LL_GPIO_SPEED_FREQ_LOW;
    io.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    io.Pull = LL_GPIO_PULL_NO;
    io.Alternate = LL_GPIO_AF_0;

    io.Pin = MOTOR_EN_PIN;
    LL_GPIO_Init(GPIOA, &io);
    io.Pin = MOTOR_STEP_PIN | MOTOR_DIR_PIN;
    LL_GPIO_Init(GPIOC, &io);

    /* Read PC0's registers back to confirm the actual silicon config.
     * Expect: MODER=1 (output), OTYPER=0 (push-pull), OSPEEDR=0 (low), PUPDR=0 (none). */
    printk("# PC0 regs: MODER=%u OTYPER=%u OSPEEDR=%u PUPDR=%u\n",
           (unsigned)(GPIOC->MODER & 0x3),
           (unsigned)(GPIOC->OTYPER & 0x1),
           (unsigned)(GPIOC->OSPEEDR & 0x3),
           (unsigned)(GPIOC->PUPDR & 0x3));

    LL_GPIO_SetOutputPin(GPIOA, MOTOR_EN_PIN);     /* EN high = disabled */
    LL_GPIO_ResetOutputPin(GPIOC, MOTOR_STEP_PIN); /* STEP idle low */
    LL_GPIO_ResetOutputPin(GPIOC, MOTOR_DIR_PIN);  /* DIR low */

    LL_GPIO_ResetOutputPin(GPIOA, MOTOR_EN_PIN);   /* enable driver */

    flush_input();
    while (!stop_requested()) {
        LL_GPIO_ResetOutputPin(GPIOC, MOTOR_DIR_PIN);
        for (int x = 0; x < 200; x++) {
            if (stop_requested()) break;
            LL_GPIO_SetOutputPin(GPIOC, MOTOR_STEP_PIN);
            k_busy_wait(MOTOR_STEP_DELAY_US);
            LL_GPIO_ResetOutputPin(GPIOC, MOTOR_STEP_PIN);
            k_busy_wait(MOTOR_STEP_DELAY_US);
        }
        printk("# 200 steps\n");
        /* 500ms pause, but still responsive to stop */
        for (int i = 0; i < 10 && !stop_requested(); i++) k_msleep(50);
    }

    LL_GPIO_ResetOutputPin(GPIOC, MOTOR_STEP_PIN);
    LL_GPIO_SetOutputPin(GPIOA, MOTOR_EN_PIN);     /* disable driver */
    printk("# Motor demo stopped (EN released)\n");
}

static void demo_all(void)
{
    demo_leds();
    if (!dac_ready) { dac_init(); dac_ready = 1; k_msleep(10); }
    for (int i = 0; i < 16; i++) {
        set_dac_channel((uint8_t)i, 32768);
        k_msleep(1);
    }
    printk("# DAC set to 2.5V on all channels\n");
    demo_solenoids();
    demo_adc();
}

void main(void)
{
    console_init_uart();
    printk("\n# Endonasal Robot Firmware booting...\n");
    k_msleep(100);

    /* Same order as main.c: timers (PWM) configure shared pins first; then GPIO/I2C overrides. */
    dac_init();
    dac_ready = 1;
    adc_init();
    pwm_init();
    solenoid_init();
    sol_ready = 1;
    lab_i2c_bringup();

    print_menu();
    while (1) {
        int ch = key_pressed();
        if (ch < 0) { k_msleep(20); continue; }
        if (ch == '\r' || ch == '\n') continue;
        switch (ch) {
        case '1': demo_leds();      print_menu(); break;
        case '2': demo_dac();       print_menu(); break;
        case '3': demo_solenoids(); print_menu(); break;
        case '4': demo_adc();       print_menu(); break;
        case '5': demo_all();       print_menu(); break;
        case 'p': case 'P': demo_pins(); print_menu(); break;
        case 'm': case 'M': demo_motor(); print_menu(); break;
        case '0': stop_all();       print_menu(); break;
        case 'h': case 'H': case '?': print_menu(); break;
        default: printk("# Unknown command\n# > "); break;
        }
    }
}
