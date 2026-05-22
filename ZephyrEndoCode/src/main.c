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
LIABILITY, IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE. */


/* main.c - starts up threads in an orderly fashion
* then basically sleeps .
* started April 2023 by R. Fearing copying from EE192 2021 skeleton
* converting FreeRTOS to Zephyr RTOS
*
*  modified to have 12 PWM channel for pressure regulators
* and using PC6, PC7, PC8, PC9 for PWM
*/

/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
extern void printq_add(char *msg);
extern int led_init();
extern void led_toggle();
extern void dac_init(void);
extern void adc_init(void);
extern void adc_test(void);
extern void pwm_init(void);
extern void gpout_init(void);
extern void pwm_test(void);
extern void hx711_init(void);
extern void qdec_init(void);
extern void hx711_test(void);
extern void start_hx711(void);
extern void start_print_thread(void);
extern void start_heartbeat(void);
extern void thread_info(void);
extern void start_control(void);
extern void uart_interrupt_init();
extern void start_uart_input(void);
extern void start_print_state(void);
extern void solenoid_init(void);
extern void i2c_adc_init(void);
extern void i2c_adc_scan(void);
extern void neopixel_init(void);
extern void motor_init(void);


void main(void)
{
	long a = 0;
	char string[80];
	printk("# Endonasal v2 PCB - Python integration from main()\n");

	if(led_init())
		printk("# led_init: success. Wait for LED blink\n");
	else
		printk("# led_init: failed.\n");

	while(a < 50)
	{	led_toggle();
		k_msleep(SLEEP_TIME_MS/10);
		a = a + 1;
	}

	start_print_thread();
	printq_add("# printq test message- hello from main.c \n");
	snprintf(string, 80, "# Clock cycles per second %d\n",CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC);
	printq_add(string);
	k_msleep(250); // suspend main() to allow print to run

	/* Peripheral initialization
	 * Order matters: pwm_init() configures PC6-PC9, PB6-PB9, PA8-PA11 as
	 * timer alternate-function pins.  solenoid_init() and i2c_adc_init() must
	 * run AFTER pwm_init() so their GPIO config overrides the timer pin-mux
	 * for pins now used as solenoid outputs (PC5-PC9, PA11-PA12, PB6) and
	 * I2C1 (PB8-PB9).
	 */
	dac_init();       // AD5679R 16-ch SPI DAC
	adc_init();       // internal ADC (channels that don't conflict with solenoids)
	pwm_init();
	solenoid_init();  // 8-ch solenoid GPIO — overrides TIM8/TIM4/TIM1 on shared pins
	i2c_adc_init();   // ADS7830 I2C ADC — overrides TIM4 on PB8/PB9
	i2c_adc_scan();
	gpout_init();     // PC13 only (PA12 now owned by solenoid)
	neopixel_init();  // WS2812 on PB1
	hx711_init();
	qdec_init();
	motor_init();     // A4988 steppers M1-M5 — after adc/qdec so it owns PA0/PCx

	/* thread starting*/
	start_heartbeat();
	uart_interrupt_init();
	start_uart_input();
	start_hx711();
	start_control();
	start_print_state();
	k_msleep(250); // suspend main() to allow print to run
	thread_info();
	k_msleep(4*SLEEP_TIME_MS); // wait to see what time threads have used
	thread_info();
	printq_add("# STM32READY\n"); // python script should wait for this before starting

	while(1)
	{	k_msleep(5*SLEEP_TIME_MS);  // just wait so other threads can run
	}
}
