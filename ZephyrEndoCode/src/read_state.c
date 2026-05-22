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



/* Raed all sensors that are part of state
* to avoid slowing down control thread, these should all be fast operations,
* with limited polling time. A/D converter is order 1 us
* sensor reading centralized here, to simplify control code

*/




#include "common.h"
#include <sys/printk.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/*******************************************************************************
 * Definitions
 ******************************************************************************/

extern float get_time_float(void);
extern uint32_t get_time(void);
extern void printq_add(char *);
extern uint16_t read_adc(int);
extern int i2c_adc_read_channel(uint8_t addr, uint8_t ch);
extern int32_t read_hx711(void);
extern int32_t read_qdec3();
extern int32_t read_qdec5();

#define I2C_ADC_ADDR    0x48   // ADS7830 #1 → p_0..p_7
#define I2C_ADC_ADDR2   0x49   // ADS7830 #2 → p_8..p_15
#define I2C_ADC_NUM_CH  8      // channels per chip
#define I2C_ADC_TOTAL_CH 16    // both chips combined
static uint8_t i2c_adc_round_robin = 0;  // round-robin index for I2C ADC reads
int32_t print_wait = PRINTWAIT;  // wait in time in ms
// #define PRINT_INTERVAL 3000 // interval for printing state

/* size of stack area used by each thread */
#define STACKSIZE 4096  // needs to be large because of printf
/* scheduling priority - larger = lower priority */
#define PRINT_STATE_PRIORITY 80  // relatively low for print_state task- was 120

// use switch for reducing length of printing for state vector
#undef FAST_STATE

// control thread define
K_THREAD_STACK_DEFINE(print_state_thread_stack_area, STACKSIZE);
static struct k_thread print_state_thread_data;   // structure to hold kernel data about thread

// structure for state data
struct state_data_t state_data;
struct state_data_t state_data1;

// mutex for state structure
K_MUTEX_DEFINE(state_mutex);

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

void read_state(void);
void print_state(void);
void start_print_state(void);
void print_state_thread(void);

void read_state()
{   float time_start, time_diff;
    int channel;
// not sure mutex is needed, read_state is only used by cotrol_thread
    //if (k_mutex_lock(&state_mutex, K_MSEC(100)) == 0) 
    if(1)
    {
    /* mutex successfully locked */
 
        time_start = get_time_float();  // in sec
        state_data.time_stamp = (float) time_start;

        // I2C ADC: read ONE channel per control loop iteration (round-robin)
        // across both ADS7830 chips. Each read is ~3ms (triple-read for mux
        // settling). Indices 0-7 are chip 0x48, 8-15 are chip 0x49; the full
        // 16-channel state vector refreshes every 16 iterations (~16ms).
        {   uint8_t idx = i2c_adc_round_robin;
            uint8_t addr = (idx < I2C_ADC_NUM_CH) ? I2C_ADC_ADDR : I2C_ADC_ADDR2;
            int val = i2c_adc_read_channel(addr, idx % I2C_ADC_NUM_CH);
            state_data.adc[idx] = (val >= 0) ? (uint16_t)val : 0;
            i2c_adc_round_robin = (idx + 1) % I2C_ADC_TOTAL_CH;
        }
        
        // load cell
        state_data.hx711 = read_hx711();
        
        // quadrature decoders
        state_data.qdec3 = read_qdec3();
        state_data.qdec5 = read_qdec5();     
     //   k_mutex_unlock(&state_mutex); 
    }
    else 
    {  printk("# error: read_state Cannot state_data.lock \n");
    }
#ifdef DEBUG_PRINT1
    time_diff= get_time_float()- time_start;  
	printk("# read state elapsed time:%6.3f us\n", 1e6*time_diff);
#endif

}

// CAUTION: printf for floats takes lots of stack space
// so calling thread needs to have enough stack space.
void print_state()
{   char log[TEXT_LINE_LENGTH];

// need to use lock so do not get partial (mixed) ints/floats

    snprintf(log, sizeof(log),
            "t=%8.3f hx711=%d  qdec3,5 %6d%6d  i2c0-15 %5d%5d%5d%5d%5d%5d%5d%5d%5d%5d%5d%5d%5d%5d%5d%5d\n",
             state_data.time_stamp,  state_data.hx711, state_data.qdec3, state_data.qdec5,
             state_data.adc[0], state_data.adc[1],state_data.adc[2],state_data.adc[3],
             state_data.adc[4], state_data.adc[5], state_data.adc[6], state_data.adc[7],
             state_data.adc[8], state_data.adc[9],state_data.adc[10],state_data.adc[11],
             state_data.adc[12], state_data.adc[13], state_data.adc[14], state_data.adc[15]);
    printq_add(log);
}


void print_state_thread()
{   char log[TEXT_LINE_LENGTH];
     uint32_t time_stamp; // should be 64 bits 

    time_stamp = get_time();  // only 32 bits need to fix to 64
        snprintf(log, sizeof(log), "# Starting print_state thread: at tick %d (%d ms PRNWAIT)\n",
           (int) time_stamp, print_wait);
    printq_add(log);
    #ifdef FAST_STATE
    snprintf(log, sizeof(log),
                "# %7s,%9s,%6s,%6s\n",
              "t=","hx711","qdec3","qdec5");
    #else
    snprintf(log, sizeof(log),
            "# %7s,%9s,%6s,%6s,%5s, %5s, %5s, %5s, %5s, %5s, %5s, %5s, %5s, %5s, %5s, %5s, %5s, %5s, %5s, %5s\n",
              "t=","hx711","qdec3","qdec5",
              "p_0","p_1","p_2","p_3","p_4","p_5","p_6","p_7",
              "p_8","p_9","p_10","p_11","p_12","p_13","p_14","p_15");
    #endif
    printq_add(log);

    while(1)
    {   
        // need to use lock so do not get partial (mixed) ints/floats
        if (k_mutex_lock(&state_mutex, K_MSEC(100)) == 0) 
        {
            memcpy(&state_data1, &state_data, sizeof(state_data)); // make quick copy for printing 
            k_mutex_unlock(&state_mutex); 
        }
        else 
        {  printk("# error: print_state Cannot state_data.lock \n");
        }
        #ifdef FAST_STATE
        snprintf(log, sizeof(log),
            "%9.4f,%9d,%6d,%6d\n", 
             state_data1.time_stamp,  state_data1.hx711, state_data1.qdec3, state_data1.qdec5);
        #else
        snprintf(log, sizeof(log),
            "%9.4f,%9d,%6d,%6d,%5d, %5d, %5d, %5d, %5d, %5d, %5d, %5d, %5d, %5d, %5d, %5d, %5d, %5d, %5d, %5d\n",
             state_data1.time_stamp,  state_data1.hx711, state_data1.qdec3, state_data1.qdec5,
             state_data1.adc[0], state_data1.adc[1],state_data1.adc[2],state_data1.adc[3],
             state_data1.adc[4], state_data1.adc[5], state_data1.adc[6], state_data1.adc[7],
             state_data1.adc[8], state_data1.adc[9],state_data1.adc[10],state_data1.adc[11],
             state_data1.adc[12], state_data1.adc[13], state_data1.adc[14], state_data1.adc[15]);
        #endif
        printq_add(log);
        k_msleep(print_wait); // should include execuion time of rest of this thread to make loop time accurate- maybe 2 ms?
    }
}


void start_print_state()
{
    printk("# Starting print_state thread\n");
/* spawn thread */
	k_tid_t tid = k_thread_create(&print_state_thread_data, print_state_thread_stack_area,
			K_THREAD_STACK_SIZEOF(print_state_thread_stack_area), 
            (k_thread_entry_t) print_state_thread, 
            NULL, NULL, NULL, PRINT_STATE_PRIORITY, 0, K_FOREVER);

	k_thread_name_set(tid, "print_state");
	k_thread_start(&print_state_thread_data);
    // printk("# Starting command queue\n");
    
}
