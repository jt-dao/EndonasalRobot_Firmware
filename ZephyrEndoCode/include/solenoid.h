#ifndef SOLENOID_H
#define SOLENOID_H

#include <stdint.h>
#include <stm32f4xx_ll_gpio.h>

// Solenoid pin definitions
#define SOL1_PIN LL_GPIO_PIN_9  // PC9
#define SOL2_PIN LL_GPIO_PIN_8  // PC8
#define SOL3_PIN LL_GPIO_PIN_6  // PC6
#define SOL4_PIN LL_GPIO_PIN_5  // PC5
#define SOL5_PIN LL_GPIO_PIN_12 // PA12
#define SOL6_PIN LL_GPIO_PIN_7  // PC7
#define SOL7_PIN LL_GPIO_PIN_6  // PB6
#define SOL8_PIN LL_GPIO_PIN_11 // PA11

// Initialize solenoid control module
void solenoid_init(void);

// Set individual solenoid (channel 1-8, state 0=off, 1=on)
void set_solenoid(uint8_t channel, uint8_t state);

// Set all solenoids at once (states array of 8 values)
void set_all_solenoids(uint8_t states[8]);

// Get current state of solenoid (returns 0 or 1)
uint8_t get_solenoid(uint8_t channel);

#endif // SOLENOID_H
