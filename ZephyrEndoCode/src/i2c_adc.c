// Zephyr + STM32F4 LL I2C1 ADS7830 driver + I2C scan
#include <zephyr.h>
#include <sys/printk.h>

#include <stm32f4xx_ll_gpio.h>
#include <stm32f4xx_ll_bus.h>
#include <stm32f4xx_ll_i2c.h>
#include <stm32f4xx_ll_rcc.h>

/* ===================== CONFIG ===================== */
#define I2C_TIMEOUT_US        20000      // 20ms total timeout per step
#define I2C_SPEED_HZ          100000     // 100k

#define ADS7830_NUM_CH        8
#define ADS7830_VREF_V        5.0f       // External VREF voltage

// Two ADS7830 devices to read (per your request)
#define ADS7830_ADDR_A        0x48
#define ADS7830_ADDR_B        0x49

// ADS7830 single-ended command base:
// bit7 = 1 (single-ended)
// bits6..4 = channel select code
// bits3..2 = PD1..PD0 power-down mode
// choose PD=01 (ADC on, ext ref typical)
#define ADS7830_CMD_BASE      0x84

// PB8 SCL, PB9 SDA on I2C1 = AF4
#define I2C1_SCL_PIN          LL_GPIO_PIN_8
#define I2C1_SDA_PIN          LL_GPIO_PIN_9

/* ===================== HELPERS ===================== */

static inline int wait_flag(volatile uint32_t (*flag_fn)(I2C_TypeDef *),
                            I2C_TypeDef *i2c,
                            uint32_t timeout_us)
{
    while (!flag_fn(i2c)) {
        if (timeout_us-- == 0) return -1;
        k_usleep(1);
    }
    return 0;
}

// wrappers because LL flag checks are macros returning bool-like
static inline uint32_t FLAG_SB(I2C_TypeDef *i2c)   { return LL_I2C_IsActiveFlag_SB(i2c); }
static inline uint32_t FLAG_ADDR(I2C_TypeDef *i2c) { return LL_I2C_IsActiveFlag_ADDR(i2c); }
static inline uint32_t FLAG_TXE(I2C_TypeDef *i2c)  { return LL_I2C_IsActiveFlag_TXE(i2c); }
static inline uint32_t FLAG_BTF(I2C_TypeDef *i2c)  { return LL_I2C_IsActiveFlag_BTF(i2c); }
static inline uint32_t FLAG_RXNE(I2C_TypeDef *i2c) { return LL_I2C_IsActiveFlag_RXNE(i2c); }
static inline uint32_t FLAG_BUSY(I2C_TypeDef *i2c) { return LL_I2C_IsActiveFlag_BUSY(i2c); }

static inline void i2c_clear_errors(I2C_TypeDef *i2c)
{
    if (LL_I2C_IsActiveFlag_AF(i2c))   LL_I2C_ClearFlag_AF(i2c);
    if (LL_I2C_IsActiveFlag_BERR(i2c)) LL_I2C_ClearFlag_BERR(i2c);
    if (LL_I2C_IsActiveFlag_ARLO(i2c)) LL_I2C_ClearFlag_ARLO(i2c);
    if (LL_I2C_IsActiveFlag_OVR(i2c))  LL_I2C_ClearFlag_OVR(i2c);
}

// If bus is stuck BUSY, you can try a soft reset of I2C peripheral
static inline void i2c_soft_reset(I2C_TypeDef *i2c)
{
    LL_I2C_Disable(i2c);
    i2c_clear_errors(i2c);
    LL_I2C_Enable(i2c);
}

/* ===================== I2C INIT ===================== */

static void i2c1_init_ll(void)
{
    // Clocks
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1);

    // PB8/PB9 AF4 open drain + pullups
    LL_GPIO_InitTypeDef gi = {0};
    gi.Pin        = I2C1_SCL_PIN | I2C1_SDA_PIN;
    gi.Mode       = LL_GPIO_MODE_ALTERNATE;
    gi.Speed      = LL_GPIO_SPEED_FREQ_VERY_HIGH;
    gi.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
    gi.Pull       = LL_GPIO_PULL_UP;
    gi.Alternate  = LL_GPIO_AF_4; // I2C1/I2C2 AF4 on STM32F4
    LL_GPIO_Init(GPIOB, &gi);

    // Reset I2C then init
    LL_I2C_Disable(I2C1);

    // LL_I2C_Init computes timing from APB clock.
    // If Zephyr already configured clocks, this still works.
    LL_I2C_InitTypeDef ii = {0};
    LL_I2C_StructInit(&ii);
    ii.PeripheralMode  = LL_I2C_MODE_I2C;
    ii.ClockSpeed      = I2C_SPEED_HZ;
    ii.DutyCycle       = LL_I2C_DUTYCYCLE_2;
    ii.OwnAddress1     = 0x00;
    ii.TypeAcknowledge = LL_I2C_ACK;
    ii.OwnAddrSize     = LL_I2C_OWNADDRESS1_7BIT;
    LL_I2C_Init(I2C1, &ii);

    LL_I2C_Enable(I2C1);

    // Clear any leftover errors
    i2c_clear_errors(I2C1);
}

/* ===================== LOW LEVEL TRANSACTIONS ===================== */

// Safe probe: START + address(write) + STOP.
// Returns 1 if ACK, 0 if NACK, -1 on timeout/hard error.
static int i2c_probe_7bit(uint8_t addr)
{
    // If bus stuck, try to recover
    if (FLAG_BUSY(I2C1)) {
        i2c_soft_reset(I2C1);
        k_usleep(100);
    }

    i2c_clear_errors(I2C1);

    // START
    LL_I2C_GenerateStartCondition(I2C1);
    if (wait_flag(FLAG_SB, I2C1, I2C_TIMEOUT_US) != 0) return -1;

    // Send address + WRITE
    LL_I2C_TransmitData8(I2C1, (addr << 1) | 0x00);

    // Wait for ADDR (ACK) OR AF (NACK)
    uint32_t t = I2C_TIMEOUT_US;
    while (!LL_I2C_IsActiveFlag_ADDR(I2C1) &&
           !LL_I2C_IsActiveFlag_AF(I2C1))
    {
        if (t-- == 0) {
            LL_I2C_GenerateStopCondition(I2C1);
            return -1;
        }
        k_usleep(1);
    }

    if (LL_I2C_IsActiveFlag_AF(I2C1)) {
        // NACK
        LL_I2C_ClearFlag_AF(I2C1);
        LL_I2C_GenerateStopCondition(I2C1);
        return 0;
    }

    // ACK
    LL_I2C_ClearFlag_ADDR(I2C1);
    LL_I2C_GenerateStopCondition(I2C1);
    return 1;
}

// Write 1 byte then repeated-start read 1 byte.
// Returns 0 on success, -1 on failure.
static int i2c_write1_read1(uint8_t addr, uint8_t wbyte, uint8_t *rbyte)
{
    if (!rbyte) return -1;

    // Recover if stuck
    if (FLAG_BUSY(I2C1)) {
        i2c_soft_reset(I2C1);
        k_usleep(100);
    }

    i2c_clear_errors(I2C1);

    // ---------- START + WRITE address ----------
    LL_I2C_AcknowledgeNextData(I2C1, LL_I2C_ACK);

    LL_I2C_GenerateStartCondition(I2C1);
    if (wait_flag(FLAG_SB, I2C1, I2C_TIMEOUT_US) != 0) return -1;

    LL_I2C_TransmitData8(I2C1, (addr << 1) | 0x00);
    if (wait_flag(FLAG_ADDR, I2C1, I2C_TIMEOUT_US) != 0) return -1;
    LL_I2C_ClearFlag_ADDR(I2C1);

    // ---------- write command byte ----------
    LL_I2C_TransmitData8(I2C1, wbyte);
    // wait byte fully transferred (BTF preferred, TXE alone can be early)
    if (wait_flag(FLAG_BTF, I2C1, I2C_TIMEOUT_US) != 0) return -1;

    // ADS7830 needs conversion time (typically < 1ms, but add delay for safety)
    k_usleep(1000);  // 1ms conversion delay

    // ---------- REPEATED START + READ address ----------
    LL_I2C_GenerateStartCondition(I2C1);
    if (wait_flag(FLAG_SB, I2C1, I2C_TIMEOUT_US) != 0) return -1;

    LL_I2C_TransmitData8(I2C1, (addr << 1) | 0x01);
    if (wait_flag(FLAG_ADDR, I2C1, I2C_TIMEOUT_US) != 0) return -1;

    // --------- 1-BYTE READ SEQUENCE (STM32F4) ---------
    // For 1 byte: NACK next data, clear ADDR, generate STOP, wait RXNE, read
    LL_I2C_AcknowledgeNextData(I2C1, LL_I2C_NACK);
    LL_I2C_ClearFlag_ADDR(I2C1);
    LL_I2C_GenerateStopCondition(I2C1);

    if (wait_flag(FLAG_RXNE, I2C1, I2C_TIMEOUT_US) != 0) return -1;
    *rbyte = LL_I2C_ReceiveData8(I2C1);

    // restore ACK for next transaction
    LL_I2C_AcknowledgeNextData(I2C1, LL_I2C_ACK);

    return 0;
}

/* ===================== ADS7830 DRIVER ===================== */

static uint8_t adc_inited = 0;

// ADS7830 channel encoding map (single-ended) from datasheet table:
// CH0=0, CH1=4, CH2=1, CH3=5, CH4=2, CH5=6, CH6=3, CH7=7
static const uint8_t ads7830_ch_map[8] = {0, 4, 1, 5, 2, 6, 3, 7};

static void ads7830_init(void)
{
    i2c1_init_ll();
    adc_inited = 1;
}

// Read ONE channel with TRIPLE READ (discard first 2, keep last).
// This lets the MUX + S/H settle before taking the real reading.
// Returns [0..255] or -1 on error.
static int ads7830_read_channel_addr(uint8_t addr, uint8_t ch)
{
    if (!adc_inited || ch >= ADS7830_NUM_CH) return -1;

    uint8_t cmd = ADS7830_CMD_BASE | (ads7830_ch_map[ch] << 4);
    uint8_t d;

    // Read 1: send command, discard result (MUX switching)
    if (i2c_write1_read1(addr, cmd, &d) != 0) return -1;

    // Read 2: let S/H settle, discard
    if (i2c_write1_read1(addr, cmd, &d) != 0) return -1;

    // Read 3: final stable value
    if (i2c_write1_read1(addr, cmd, &d) != 0) return -1;

    return (int)d;
}

// Read all channels into values[8] for a given ADS7830 address.
// Returns 0 success, -1 fail.
static int ads7830_read_all_addr(uint8_t addr, uint8_t *values)
{
    if (!adc_inited || !values) return -1;

    for (int i = 0; i < ADS7830_NUM_CH; i++) {
        int v = ads7830_read_channel_addr(addr, i);
        if (v < 0) return -1;
        values[i] = (uint8_t)v;
    }
    return 0;
}

/* ===================== I2C SCANNER ===================== */

static void i2c_scan(void)
{
    printk("# I2C scan:\n");
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        int ok = i2c_probe_7bit(addr);
        if (ok == 1) {
            printk("#   Found device at 0x%02X\n", addr);
        }
    }
    printk("# Scan complete\n");
}

/* ===================== PUBLIC API ===================== */
/* Wrappers so other modules (e.g. main.c) can use the I2C ADC */

void i2c_adc_init(void)
{
    ads7830_init();
}

void i2c_adc_scan(void)
{
    i2c_scan();
}

int i2c_adc_read_channel(uint8_t addr, uint8_t ch)
{
    return ads7830_read_channel_addr(addr, ch);
}

int i2c_adc_read_all(uint8_t addr, uint8_t *values)
{
    return ads7830_read_all_addr(addr, values);
}

int i2c_adc_probe_addr(uint8_t addr)
{
    return i2c_probe_7bit(addr);
}

/* ===================== STANDALONE MAIN (unused in demo) ===================== */

void main2(void)
{
    uint8_t values_a[8] = {0};
    uint8_t values_b[8] = {0};

    ads7830_init();
    k_msleep(50);

    printk("# ADS7830 ready - reading all 8 channels on 0x48 and 0x49\n");

    while (1) {
        int ok_a = ads7830_read_all_addr(ADS7830_ADDR_A, values_a);
        int ok_b = ads7830_read_all_addr(ADS7830_ADDR_B, values_b);

        if (ok_a == 0) {
            printk("# 0x%02X: ", ADS7830_ADDR_A);
            for (int i = 0; i < 8; i++) printk("%3d ", values_a[i]);
            printk("\n");
        } else {
            printk("# 0x%02X: read error\n", ADS7830_ADDR_A);
        }

        if (ok_b == 0) {
            printk("# 0x%02X: ", ADS7830_ADDR_B);
            for (int i = 0; i < 8; i++) printk("%3d ", values_b[i]);
            printk("\n");
        } else {
            printk("# 0x%02X: read error\n", ADS7830_ADDR_B);
        }

        k_msleep(500);
    }
}
