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

/* module to initialize some general purpose outputs 
PA12
PC8
PC9
PC13 
outputs are configured using push-pull (active high and active low)
*/
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include <zephyr.h>
#include <stdio.h>
#include "common.h"

void gpout_int(void);
void digitalOutputOff(uint32_t);
void digitalOutputOn(uint32_t);
extern void set_solenoid(uint8_t channel, uint8_t state);


void gpout_init(void)
{
/* Enable GPIO clock */
    // PA12 is now managed by solenoid.c (SOL5) — do NOT configure here
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);

    LL_GPIO_InitTypeDef GPIO_InitStruct;

    // PC13 output 4 (only remaining gpout pin on new PCB)
    GPIO_InitStruct.Pin =  LL_GPIO_PIN_13;
    GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    LL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    LL_GPIO_ResetOutputPin(GPIOC, Output4Pin);

    #ifdef DEBUG_PRINT
    printk("# End GP Output init. PC13 (PA12 owned by solenoid)\n");
    #endif
}


// general output bits — PA12 redirects to solenoid 5 on new PCB
void digitalOutputOn(uint32_t value)
{   switch(value)
    {   case 1:
            set_solenoid(5, 1);  // PA12 = SOL5 on new PCB
            break;
        case 4:
            LL_GPIO_SetOutputPin(GPIOC, Output4Pin);
            break;
        default: 
            printk("invalid channel for DigitalOutputOn\n");
            break;
    }
}

void digitalOutputOff(uint32_t value)
{   switch(value)
    {   case 1:
            set_solenoid(5, 0);  // PA12 = SOL5 on new PCB
            break;
        case 4:
            LL_GPIO_ResetOutputPin(GPIOC, Output4Pin);
            break;
        default: 
            printk("invalid channel for DigitalOutputOff\n");
            break;
    }
}
