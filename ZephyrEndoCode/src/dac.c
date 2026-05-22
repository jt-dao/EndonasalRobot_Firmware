/*
 * GPIO bit-banged DAC (AD5679R) driver for Zephyr
 * 16-channel, 16-bit DAC with internal reference
 * AD5679RBCPZ-1: zero-scale power-up, 2.5V internal ref
 * STM32F446 Nucleo-64: SCK=PB10, MISO=PB14, MOSI=PB15, CS=PB12
 *
 * Hardware config: VDD=5V, VLOGIC=3.3V, GAIN=VLOGIC (0-5V output)
 * Internal reference defaults ON at power-up (Table 18: DB0=0 enables).
 *
 * Serial write: SCLK idles low, MSB first, 24 bits per SYNC frame.
 * Each command is 24 bits (3 bytes) per SYNC frame.
 *
 * 24-bit frame (datasheet Figure 59):
 *   [DB23:DB20] = Command (C3-C0)
 *   [DB19:DB16] = Address (A3-A0)
 *   [DB15:DB0]  = Data (D15-D0)
 */

#include <zephyr.h>
#include <device.h>
#include <drivers/gpio.h>
#include <sys/printk.h>
#include <errno.h>
#include <stm32f4xx_ll_gpio.h>
#include <stm32f4xx_ll_bus.h>
#include "common.h"

#define DAC_GPIO_PORT "GPIOB"
#define SCK_PIN 10
#define CS_PIN 12
#define MOSI_PIN 15
#define DAC_BIT_DELAY_US 10

/* AD5679R Commands (datasheet Table 10)
 * Command bits C3-C0 go into the upper nibble of byte 0.
 * Address bits A3-A0 go into the lower nibble of byte 0.
 */
#define AD5679_CMD_NOP              0x00   // 0000: No operation
#define AD5679_CMD_WRITE_INPUT      0x10   // 0001: Write to Input Register n (LDAC dependent)
#define AD5679_CMD_UPDATE_DAC       0x20   // 0010: Update DAC Register n from Input Register n
#define AD5679_CMD_WRITE_UPDATE     0x30   // 0011: Write to and update DAC Channel n
#define AD5679_CMD_POWER_DOWN       0x40   // 0100: Power down/power up
#define AD5679_CMD_LDAC_MASK        0x50   // 0101: LDAC mask register
#define AD5679_CMD_RESET            0x60   // 0110: Software reset (addr=0, data=0x1234)
#define AD5679_CMD_INT_REF_SETUP    0x70   // 0111: Internal reference setup
#define AD5679_CMD_DCEN             0x80   // 1000: Daisy-chain enable
#define AD5679_CMD_READBACK         0x90   // 1001: Readback enable
#define AD5679_CMD_WRITE_ALL_INPUT  0xA0   // 1010: Write all input registers
#define AD5679_CMD_WRITE_ALL_UPDATE 0xB0   // 1011: Write and update all DAC registers

#define AD5679_MAX_VALUE        65535  // 16-bit DAC
#define AD5679_NUM_CHANNELS     16

static const struct device *dac_gpio;

static uint16_t dac_values[AD5679_NUM_CHANNELS];

/* Forward declarations */
void set_dac_channel(uint8_t channel, int value);

static inline void dac_sck(int state)
{
    gpio_pin_set(dac_gpio, SCK_PIN, state);
}

static inline void dac_mosi(int state)
{
    gpio_pin_set(dac_gpio, MOSI_PIN, state);
}

static inline void dac_cs(int state)
{
    gpio_pin_set(dac_gpio, CS_PIN, state);
}

static void dac_send_byte(uint8_t byte)
{
    for (int bit = 7; bit >= 0; bit--) {
        dac_mosi((byte >> bit) & 0x01);
        k_busy_wait(DAC_BIT_DELAY_US);
        dac_sck(1);
        k_busy_wait(DAC_BIT_DELAY_US);
        dac_sck(0);
        k_busy_wait(DAC_BIT_DELAY_US);
    }
}

/* Send a single 24-bit command to the DAC as one low-SYNC frame */
static int dac_send_cmd(uint8_t byte0, uint8_t byte1, uint8_t byte2)
{
    if (!dac_gpio) {
        return -ENODEV;
    }

    dac_sck(0);
    dac_cs(0);
    k_busy_wait(DAC_BIT_DELAY_US);

    dac_send_byte(byte0);
    dac_send_byte(byte1);
    dac_send_byte(byte2);

    k_busy_wait(DAC_BIT_DELAY_US);
    dac_cs(1);
    k_busy_wait(DAC_BIT_DELAY_US);
    return 0;
}

void dac_init(void)
{
    dac_gpio = device_get_binding(DAC_GPIO_PORT);
    if (!dac_gpio) {
        printk("# DAC GPIO device not found\n");
        return;
    }

    gpio_pin_configure(dac_gpio, SCK_PIN, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(dac_gpio, MOSI_PIN, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(dac_gpio, CS_PIN, GPIO_OUTPUT_ACTIVE | GPIO_PULL_UP);

    /* Force Very High Speed mode for SPI pins using LL to ensure sharp edges */
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = LL_GPIO_PIN_10 | LL_GPIO_PIN_12 | LL_GPIO_PIN_15;
    GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
    GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    dac_sck(0);
    dac_mosi(0);
    dac_cs(1);
    k_msleep(10);

    /* Reset and Configure */
    dac_send_cmd(AD5679_CMD_RESET, 0x12, 0x34);           // Software Reset
    k_msleep(10);
    dac_send_cmd(AD5679_CMD_INT_REF_SETUP, 0x00, 0x00);  // Internal reference enabled (DB0=0)
    dac_send_cmd(AD5679_CMD_LDAC_MASK, 0xFF, 0xFF);      // Ignore LDAC pin (Update on 24th clock)
    dac_send_cmd(AD5679_CMD_POWER_DOWN, 0x00, 0x00);     // Power Up Bank 0 (0-7)
    dac_send_cmd(AD5679_CMD_POWER_DOWN | 0x08, 0x00, 0x00); // Power Up Bank 1 (8-15)

    printk("# AD5679R: Internal ref enabled, LDAC masked.\n");
}

void set_dac(int value)
{
    set_dac_channel(0, value);
}

void set_dac_channel(uint8_t channel, int value)
{
    if (channel >= AD5679_NUM_CHANNELS) {
        printk("# Invalid channel %d (max %d)\n", channel, AD5679_NUM_CHANNELS - 1);
        return;
    }

    if (value < 0) value = 0;
    if (value > AD5679_MAX_VALUE) value = AD5679_MAX_VALUE;

    dac_values[channel] = (uint16_t)value;

    /*
     * Write and update DAC Channel n (Command 0011, datasheet p.25):
     * byte0 = [C3:C0][A3:A0] = 0x30 | channel
     * byte1 = D15..D8
     * byte2 = D7..D0
     */
    uint8_t cmd = AD5679_CMD_WRITE_UPDATE | (channel & 0x0F);
    int ret = dac_send_cmd(cmd, (value >> 8) & 0xFF, value & 0xFF);
    if (ret != 0) {
        printk("# SPI DAC ch%d write failed: %d\n", channel, ret);
    }
}

void set_all_dac_channels(uint16_t values[AD5679_NUM_CHANNELS])
{
    for (int i = 0; i < AD5679_NUM_CHANNELS; i++) {
        set_dac_channel(i, values[i]);
    }
}

uint16_t get_dac_value(uint8_t channel)
{
    if (channel >= AD5679_NUM_CHANNELS) return 0;
    return dac_values[channel];
}
