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

/* driver interface use user LED
* connected to GPIO PA5 on STM32 F446RE nucleo board
*/

/* common definitions for controller code */
#include <zephyr.h>
// change this for longer lines in log file (allocated at compile time)
#define TEXT_LINE_LENGTH 256 // allow 256 characters per log message (fits 16-ch telemetry)
#define INPUT_CMD_LENGTH 8 // allow 7 characters + '\0' for text cmd+value

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1
// initial value for delay between state updates
#define PRINTWAIT 5000


/* for debugging printk messages*/
#define DEBUG_PRINT
// #define DEBUG_PRINT1 // more detailed debugging

// structure for commands coming from UART interface
struct cmd_struct_def {
    float time_stamp;
    char cmd[INPUT_CMD_LENGTH];
    int value;
};  

struct state_data_t {
    float time_stamp;
    uint16_t adc[16]; // 16 I2C ADC channels — ADS7830 @ 0x48 → 0-7, @ 0x49 → 8-15 (0-255 range)
    
    int32_t hx711;
    int32_t qdec3; // quadrature decoder using timer 3
    int32_t qdec5; // quadrature decoder using timer 5
};



/* When set: PB13–PB15 must not be reconfigured by pwm stepper helpers — PB14/PB15 are SPI2
 * MISO/MOSI for the AD5679R. pwm_init() used to force them GPIO after dac_init(), which broke DAC. */
#define ENDONASAL_HAS_SPI_DAC 1

/* GPIO Port B pin definitions
GPIOB Pin 13, and 14 are direction for each motor
* GPIOB Pin 15 is used for stepping motor free (unenergized=1)
*/
#define HX711_DOUT LL_GPIO_PIN_5
#define StepMotor1DirPin LL_GPIO_PIN_13
#define StepMotor2DirPin LL_GPIO_PIN_14
#define StepMotorFreePin LL_GPIO_PIN_15
// PA12 = SOL5 on new PCB (managed by solenoid.c, legacy alias kept for gpout.c redirect)
#define Output1Pin LL_GPIO_PIN_12
// PC8 = SOL2, PC9 = SOL1 on new PCB (managed by solenoid.c)
//#define Output2Pin LL_GPIO_PIN_8
//#define Output3Pin LL_GPIO_PIN_9
// PC13 output 4 (only remaining gpout pin)
#define Output4Pin LL_GPIO_PIN_13