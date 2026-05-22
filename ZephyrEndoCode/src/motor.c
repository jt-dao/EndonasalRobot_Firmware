/* Stepper motor control for A4988 drivers (motors M1-M5).
 * Pins per include/pinouts-10-2.txt; EN_x = PA0 is a shared active-low enable.
 * Driven with LL GPIO (push-pull, low slew) — this overrides the analog mode
 * that adc_init() leaves on the shared PCx pins, so motor_init() must run after
 * adc_init() and qdec_init(). */

#include <zephyr.h>
#include <stm32f4xx_ll_gpio.h>
#include <stm32f4xx_ll_bus.h>

#define MOTOR_COUNT    5
#define MOTOR_STEPS    200
#define MOTOR_HALF_US  500     /* step half-period; full pulse = 2x */

#define MOTOR_EN_PORT  GPIOA
#define MOTOR_EN_PIN   LL_GPIO_PIN_0

struct motor_pins {
    GPIO_TypeDef *step_port;
    uint32_t      step_pin;
    GPIO_TypeDef *dir_port;
    uint32_t      dir_pin;
};

static const struct motor_pins motors[MOTOR_COUNT] = {
    { GPIOB, LL_GPIO_PIN_7,  GPIOC, LL_GPIO_PIN_11 }, /* M1: STEP PB7,  DIR PC11 */
    { GPIOC, LL_GPIO_PIN_13, GPIOC, LL_GPIO_PIN_10 }, /* M2: STEP PC13, DIR PC10 */
    { GPIOB, LL_GPIO_PIN_0,  GPIOC, LL_GPIO_PIN_12 }, /* M3: STEP PB0,  DIR PC12 */
    { GPIOC, LL_GPIO_PIN_2,  GPIOC, LL_GPIO_PIN_1  }, /* M4: STEP PC2,  DIR PC1  */
    { GPIOC, LL_GPIO_PIN_0,  GPIOC, LL_GPIO_PIN_3  }, /* M5: STEP PC0,  DIR PC3  */
};

void motor_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);

    LL_GPIO_InitTypeDef io = {0};
    io.Mode = LL_GPIO_MODE_OUTPUT;
    io.Speed = LL_GPIO_SPEED_FREQ_LOW;
    io.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    io.Pull = LL_GPIO_PULL_NO;
    io.Alternate = LL_GPIO_AF_0;

    io.Pin = MOTOR_EN_PIN;
    LL_GPIO_Init(MOTOR_EN_PORT, &io);
    LL_GPIO_SetOutputPin(MOTOR_EN_PORT, MOTOR_EN_PIN);  /* EN high = drivers disabled */

    for (int i = 0; i < MOTOR_COUNT; i++) {
        io.Pin = motors[i].step_pin;
        LL_GPIO_Init(motors[i].step_port, &io);
        LL_GPIO_ResetOutputPin(motors[i].step_port, motors[i].step_pin);
        io.Pin = motors[i].dir_pin;
        LL_GPIO_Init(motors[i].dir_port, &io);
        LL_GPIO_ResetOutputPin(motors[i].dir_port, motors[i].dir_pin);
    }
}

/* 0 = none; 1..5 = that motor is running continuously (advanced by motor_periodic) */
static int continuous_motor = 0;

/* One 200-pulse burst (~200ms) on motor m. Caller manages the EN line. */
static void motor_burst(const struct motor_pins *m)
{
    for (int x = 0; x < MOTOR_STEPS; x++) {
        LL_GPIO_SetOutputPin(m->step_port, m->step_pin);
        k_busy_wait(MOTOR_HALF_US);
        LL_GPIO_ResetOutputPin(m->step_port, m->step_pin);
        k_busy_wait(MOTOR_HALF_US);
    }
}

/* motor: 1..5.  mode: 0 = off, 1 = one rotation (blocking ~200ms), 2 = continuous.
 * Out-of-range motor is ignored. */
void motor_cmd(int motor, int mode)
{
    if (motor < 1 || motor > MOTOR_COUNT) {
        return;
    }
    const struct motor_pins *m = &motors[motor - 1];

    switch (mode) {
    case 0:  /* off */
        if (continuous_motor == motor) {
            continuous_motor = 0;
        }
        LL_GPIO_SetOutputPin(MOTOR_EN_PORT, MOTOR_EN_PIN);   /* disable drivers */
        break;
    case 1:  /* one rotation */
        continuous_motor = 0;
        LL_GPIO_ResetOutputPin(MOTOR_EN_PORT, MOTOR_EN_PIN); /* enable drivers */
        motor_burst(m);
        LL_GPIO_SetOutputPin(MOTOR_EN_PORT, MOTOR_EN_PIN);   /* disable drivers */
        break;
    case 2:  /* continuous — motor_periodic() advances it */
        LL_GPIO_ResetOutputPin(MOTOR_EN_PORT, MOTOR_EN_PIN); /* enable drivers */
        continuous_motor = motor;
        break;
    }
}

/* Call from the control loop: advances the continuously-running motor, if any. */
void motor_periodic(void)
{
    if (continuous_motor >= 1 && continuous_motor <= MOTOR_COUNT) {
        motor_burst(&motors[continuous_motor - 1]);
    }
}
