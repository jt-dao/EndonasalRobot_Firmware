/* MIT License

Copyright (c) 2024 Regents of The Regents of the University of California

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE. */

/* Solenoid Control Module
 * Controls 8 solenoid valves using GPIO pins
 * SOL1: PC9, SOL2: PC8, SOL3: PC6, SOL4: PC5
 * SOL5: PA12, SOL6: PC7, SOL7: PB6, SOL8: PA11
 */

#include <zephyr.h>
#include <stm32f4xx_ll_gpio.h>
#include <stm32f4xx_ll_bus.h>
#include <stm32f4xx_ll_rcc.h>
#include "solenoid.h"

// Solenoid states (0 = off, 1 = on)
static uint8_t solenoid_states[8] = {0, 0, 0, 0, 0, 0, 0, 0};

void solenoid_init(void)
{
    // Disable USB OTG to free PA12 (SOL5) and PA11 (SOL8)
    LL_AHB2_GRP1_DisableClock(LL_AHB2_GRP1_PERIPH_OTGFS);
    
    // Enable GPIO clocks
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);

    // Configure GPIO pins as outputs
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
    GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
    GPIO_InitStruct.Alternate = LL_GPIO_AF_0;  // GPIO mode

    // Configure PA12 and PA11 (SOL5, SOL8)
    GPIO_InitStruct.Pin = SOL5_PIN | SOL8_PIN;
    LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // Configure PB6 (SOL7) with pulldown to avoid startup floating-high
    GPIO_InitStruct.Pin = SOL7_PIN;
    GPIO_InitStruct.Pull = LL_GPIO_PULL_DOWN;
    LL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    LL_GPIO_ResetOutputPin(GPIOB, SOL7_PIN);

    // Configure PC5-PC9 (SOL1-SOL4, SOL6)
    GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
    GPIO_InitStruct.Pin = SOL1_PIN | SOL2_PIN | SOL3_PIN | SOL4_PIN | SOL6_PIN;
    LL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // Initialize all solenoids to OFF
    for (int i = 0; i < 8; i++) {
        set_solenoid(i + 1, 0);
    }
}

void set_solenoid(uint8_t channel, uint8_t state)
{
    if (channel < 1 || channel > 8) {
        return;
    }

    solenoid_states[channel - 1] = state;

    switch (channel) {
        case 1: // SOL1 - PC9
            if (state) {
                LL_GPIO_SetOutputPin(GPIOC, SOL1_PIN);
            } else {
                LL_GPIO_ResetOutputPin(GPIOC, SOL1_PIN);
            }
            break;
        case 2: // SOL2 - PC8
            if (state) {
                LL_GPIO_SetOutputPin(GPIOC, SOL2_PIN);
            } else {
                LL_GPIO_ResetOutputPin(GPIOC, SOL2_PIN);
            }
            break;
        case 3: // SOL3 - PC6
            if (state) {
                LL_GPIO_SetOutputPin(GPIOC, SOL3_PIN);
            } else {
                LL_GPIO_ResetOutputPin(GPIOC, SOL3_PIN);
            }
            break;
        case 4: // SOL4 - PC5
            if (state) {
                LL_GPIO_SetOutputPin(GPIOC, SOL4_PIN);
            } else {
                LL_GPIO_ResetOutputPin(GPIOC, SOL4_PIN);
            }
            break;
        case 5: // SOL5 - PA12
            if (state) {
                LL_GPIO_SetOutputPin(GPIOA, SOL5_PIN);
            } else {
                LL_GPIO_ResetOutputPin(GPIOA, SOL5_PIN);
            }
            break;
        case 6: // SOL6 - PC7
            if (state) {
                LL_GPIO_SetOutputPin(GPIOC, SOL6_PIN);
            } else {
                LL_GPIO_ResetOutputPin(GPIOC, SOL6_PIN);
            }
            break;
        case 7: // SOL7 - PB6
            if (state) {
                LL_GPIO_SetOutputPin(GPIOB, SOL7_PIN);
            } else {
                LL_GPIO_ResetOutputPin(GPIOB, SOL7_PIN);
            }
            break;
        case 8: // SOL8 - PA11
            if (state) {
                LL_GPIO_SetOutputPin(GPIOA, SOL8_PIN);
            } else {
                LL_GPIO_ResetOutputPin(GPIOA, SOL8_PIN);
            }
            break;
    }
}

void set_all_solenoids(uint8_t states[8])
{
    for (int i = 0; i < 8; i++) {
        set_solenoid(i + 1, states[i]);
    }
}

uint8_t get_solenoid(uint8_t channel)
{
    if (channel < 1 || channel > 8) {
        return 0;
    }
    return solenoid_states[channel - 1];
}
