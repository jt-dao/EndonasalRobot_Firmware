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
SOFTWARE.
 */

/* main control thread 
*   1. read state data from A/D, quadrature, serial, etc (maybe from a queue)
*   2. calculate control
*   3. output control to D/A, PWM, etc
*   4. log state and control data
*   5. sleep briefly to allow other lower priority threads to run 
*   modelled after EE192 Spring 2021 FreeRTOS script, converted to Zephyr RTOS
*   May 2023 R. Fearing
*/
// Includes
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <zephyr.h>
#include <sys/printk.h>
#include <math.h>
#include "common.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/


/* size of stack area used by each thread */
#define STACKSIZE 4096
/* scheduling priority - larger = lower priority */
#define CONTROL_PRIORITY 10  // high for control task, but USART2 input should be higher
#define PI 3.14159265359
#define FREQ 100  // frequency in Hz
#define ENDONASAL

extern void printq_add(char *);
extern float get_time_float(void);
extern uint32_t get_time(void);
extern void print_time(void);
extern void set_pwm(int, uint32_t);
extern void generatePulses(int, int);
extern void stepOff(void);
extern void digitalOutputOff(uint32_t);
extern void digitalOutputOn(uint32_t);
extern void set_dac(int);
extern void set_dac_channel(uint8_t channel, int value);
extern uint16_t read_adc(int);
extern void set_solenoid(uint8_t channel, uint8_t state);
extern void neopixel_set_mode(uint8_t mode);
extern void neopixel_periodic(void);
extern void read_state(void);
extern void print_state(void);
extern int32_t print_wait; // delay time
extern void motor_cmd(int motor, int mode);
extern void motor_periodic(void);

// control thread define
K_THREAD_STACK_DEFINE(control_stack_area, STACKSIZE);
static struct k_thread control_thread_data;   // structure to hold kernel data about thread

// command queue define
/* command includes PFRQ1,2 and PWM1-12, so need 16 items minimum in queue*/
K_MSGQ_DEFINE(cmdq, sizeof(struct cmd_struct_def), 16, 16); // 16 items max, align on 16 bytes 

// structure for state data
extern struct state_data_t state_data;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
void control_thread(void);
void start_control(void);
void parse_cmd(char *, int);

void estop(void);

// turn all PWM, solenoids, motors, and DAC outputs off
void estop()
{ 
    generatePulses(1, (int) 0); 
    generatePulses(2, (int) 0); 
    set_pwm(1, (uint32_t) 0);
    set_pwm(2, (uint32_t) 0);
    set_pwm(3, (uint32_t) 0);
    set_pwm(4, (uint32_t) 0);
    set_pwm(5, (uint32_t) 0);
    set_pwm(6, (uint32_t) 0);
    set_pwm(7, (uint32_t) 0);
    set_pwm(8, (uint32_t) 0);
#ifdef ENDONASAL
    set_pwm(9, (uint32_t) 0);
    set_pwm(10, (uint32_t) 0);
    set_pwm(11, (uint32_t) 0);
    set_pwm(12, (uint32_t) 0);
#endif
    for (int i = 1; i <= 8; i++) {
        set_solenoid(i, 0);
    }
    for (int i = 0; i < 16; i++) {
        set_dac_channel((uint8_t)i, 0);
    }
}



void start_control()
{
    printk("# Starting control thread\n");
/* spawn control thread */
	k_tid_t tid = k_thread_create(&control_thread_data, control_stack_area,
			K_THREAD_STACK_SIZEOF(control_stack_area), 
            (k_thread_entry_t) control_thread, 
            NULL, NULL, NULL, CONTROL_PRIORITY, 0, K_FOREVER);

	k_thread_name_set(tid, "control_thread");
	k_thread_start(&control_thread_data);
    // printk("# Starting command queue\n");
    
}


static uint8_t dac_auto = 0; // 1 = cosine test wave on DAC ch0, 0 = Python/manual commands own DAC

void control_thread()
{   uint32_t time_stamp; // should be 64 bits
    char log[TEXT_LINE_LENGTH];
    int control; // control output
    struct cmd_struct_def cmd_struct; // local copy
    uint32_t neo_tick = 0;

    k_msleep(100); // some delay before starting control thread
    // give lower priority threads chance to run?

    time_stamp = get_time();  // only 32 bits need to fix to 64
    snprintf(log, sizeof(log), "# Starting control thread: at tick %d \n",
           (int) time_stamp);
    printq_add(log);
    
    while(1)  // main control loop
    {   // check without blocking for new command on queue
        if(k_msgq_get(&cmdq, &cmd_struct, K_NO_WAIT)==0)
        { /*
              printk("# new cmdq value found. Time %f cmd=%s value=%d\n", 
                    cmd_struct.time_stamp, cmd_struct.cmd, cmd_struct.value);
           */ 
            parse_cmd(cmd_struct.cmd, cmd_struct.value);
        } 
        #ifdef DEBUG_PRINT1
        else
        {   printk("# cmdq empty ");
        }
        #endif
        
        k_msleep(1);
        read_state();
        if (dac_auto) {
            control=(int)  1000*(1 + cos(2*PI*FREQ*state_data.time_stamp));
            set_dac(control);
        }

        if (++neo_tick >= 50) { neopixel_periodic(); neo_tick = 0; }
        motor_periodic();  // advances a motor running in continuous mode

        // avoid any print within main control loop
       
    }

}

// handle commands from UDP, format command (8 characters), value (int)
void parse_cmd(char command[], int value)
{ char ucommand[INPUT_CMD_LENGTH];
    int i=0;
    char log[TEXT_LINE_LENGTH];
// last character is ucommand[INPUT_COMMAND_LENGTH-1] is \0 and should not be over written
    while( (i < INPUT_CMD_LENGTH-1) && (command[i] != '\0') )
    {  ucommand[i] = (char) toupper((int) command[i] ); // convert to upper case
        i++;
    }
    ucommand[i] = '\0';   // force last character to be end of string
#ifdef DEBUG_PRINT1
    snprintf(log, sizeof(log),"# ucommand = 0x[%x %x %x %x %x %x %x %x]\n", 
                ucommand[0],ucommand[1],ucommand[2],ucommand[3],
                ucommand[4],ucommand[5],ucommand[6],ucommand[7]); 
    printq_add(log);  
#endif 
//    printk("\n# In parse_command. command:%s value %d\n", ucommand, value);
    if (strcmp(ucommand,"TIME") == 0) 
    {   print_time(); 
        return;
    }
    if(strcmp(ucommand,"ESTOP") == 0)
    {   estop();
        snprintf(log, sizeof(log),"# Emergency Stop received\n"); 
        printq_add(log);
        return;
    }
    if(strcmp(ucommand,"PFRQ1") == 0)
    {   generatePulses(1, (uint32_t) value); 
        return;
    }
    if(strcmp(ucommand,"PFRQ2") == 0)
    {    generatePulses(2, (uint32_t) value); 
        return;
    }
    if(strcmp(ucommand,"STEPOFF") == 0)
    {   stepOff(); 
        return;
    }
    if(strcmp(ucommand,"PWM1") == 0)
    {    set_pwm(1, (uint32_t) value); return; }
    if(strcmp(ucommand,"PWM2") == 0)
    {    set_pwm(2, (uint32_t) value); return; }
    if(strcmp(ucommand,"PWM3") == 0)
    {    set_pwm(3, (uint32_t) value); return; }
    if(strcmp(ucommand,"PWM4") == 0)
    {    set_pwm(4, (uint32_t) value); return; }
    if(strcmp(ucommand,"PWM5") == 0)
    {    set_pwm(5, (uint32_t) value); return;}
    if(strcmp(ucommand,"PWM6") == 0)
    {    set_pwm(6, (uint32_t) value);return; }
    if(strcmp(ucommand,"PWM7") == 0)
    {    set_pwm(7, (uint32_t) value); return;}
    if(strcmp(ucommand,"PWM8") == 0)
    {    set_pwm(8, (uint32_t) value);return; }
 #ifdef ENDONASAL
    if(strcmp(ucommand,"PWM9") == 0)
    {    set_pwm(9, (uint32_t) value); return;}
    if(strcmp(ucommand,"PWM10") == 0)
    {    set_pwm(10, (uint32_t) value);return; }
    if(strcmp(ucommand,"PWM11") == 0)
    {    set_pwm(11, (uint32_t) value); return;}
    if(strcmp(ucommand,"PWM12") == 0)
    {    set_pwm(12, (uint32_t) value);return; }
 #endif
    if (strcmp(ucommand, "DAC") == 0)
    {    dac_auto = 0; set_dac(value); return;}
    if(strcmp(ucommand,"DAC1") == 0)
    {    dac_auto = 0; set_dac_channel(0, value); return; }
    if(strcmp(ucommand,"DAC2") == 0)
    {    dac_auto = 0; set_dac_channel(1, value); return; }
    if(strcmp(ucommand,"DAC3") == 0)
    {    dac_auto = 0; set_dac_channel(2, value); return; }
    if(strcmp(ucommand,"DAC4") == 0)
    {    dac_auto = 0; set_dac_channel(3, value); return; }
    if(strcmp(ucommand,"DAC5") == 0)
    {    dac_auto = 0; set_dac_channel(4, value); return; }
    if(strcmp(ucommand,"DAC6") == 0)
    {    dac_auto = 0; set_dac_channel(5, value); return; }
    if(strcmp(ucommand,"DAC7") == 0)
    {    dac_auto = 0; set_dac_channel(6, value); return; }
    if(strcmp(ucommand,"DAC8") == 0)
    {    dac_auto = 0; set_dac_channel(7, value); return; }
    if(strcmp(ucommand,"DAC9") == 0)
    {    dac_auto = 0; set_dac_channel(8, value); return; }
    if(strcmp(ucommand,"DAC10") == 0)
    {    dac_auto = 0; set_dac_channel(9, value); return; }
    if(strcmp(ucommand,"DAC11") == 0)
    {    dac_auto = 0; set_dac_channel(10, value); return; }
    if(strcmp(ucommand,"DAC12") == 0)
    {    dac_auto = 0; set_dac_channel(11, value); return; }
    if(strcmp(ucommand,"DAC13") == 0)
    {    dac_auto = 0; set_dac_channel(12, value); return; }
    if(strcmp(ucommand,"DAC14") == 0)
    {    dac_auto = 0; set_dac_channel(13, value); return; }
    if(strcmp(ucommand,"DAC15") == 0)
    {    dac_auto = 0; set_dac_channel(14, value); return; }
    if(strcmp(ucommand,"DAC16") == 0)
    {    dac_auto = 0; set_dac_channel(15, value); return; }
    if (strcmp(ucommand, "ADC") == 0)
    {    read_adc(8); return;}
    if (strcmp(ucommand, "OUTOFF") == 0)
    {   digitalOutputOff((uint32_t) value);
        return; }
    if (strcmp(ucommand, "OUTON") == 0)
    {   digitalOutputOn((uint32_t) value);
        return;}
    if(strcmp(ucommand,"SOL1") == 0)
    {    set_solenoid(1, (uint8_t) value); return; }
    if(strcmp(ucommand,"SOL2") == 0)
    {    set_solenoid(2, (uint8_t) value); return; }
    if(strcmp(ucommand,"SOL3") == 0)
    {    set_solenoid(3, (uint8_t) value); return; }
    if(strcmp(ucommand,"SOL4") == 0)
    {    set_solenoid(4, (uint8_t) value); return; }
    if(strcmp(ucommand,"SOL5") == 0)
    {    set_solenoid(5, (uint8_t) value); return; }
    if(strcmp(ucommand,"SOL6") == 0)
    {    set_solenoid(6, (uint8_t) value); return; }
    if(strcmp(ucommand,"SOL7") == 0)
    {    set_solenoid(7, (uint8_t) value); return; }
    if(strcmp(ucommand,"SOL8") == 0)
    {    set_solenoid(8, (uint8_t) value); return; }
    if (strcmp(ucommand, "DACCH") == 0)
    {    dac_auto = 0; set_dac_channel((uint8_t)(value >> 16), value & 0xFFFF); return; }
    if (strcmp(ucommand, "DACTEST") == 0)
    {
        dac_auto = 0;
        if (value == 0) {
            for (int i = 0; i < 16; i++) set_dac_channel((uint8_t)i, 0);
        } else if (value == 1) {
            for (int i = 0; i < 16; i++) set_dac_channel((uint8_t)i, 32768);
        } else if (value == 2) {
            for (int i = 0; i < 16; i++) set_dac_channel((uint8_t)i, 65535);
        } else {
            for (int i = 0; i < 16; i++) set_dac_channel((uint8_t)i, (uint16_t)(i * 4369));
        }
        return;
    }
    if (strcmp(ucommand, "DACAUTO") == 0)
    {    dac_auto = (value != 0); return; }
    if (strcmp(ucommand, "PRNWAIT") == 0)
    {    snprintf(log, sizeof(log),"# PRNWAIT changed from %d to %d ms \n",
         print_wait, value);
         printq_add(log);
         print_wait = value;
         return;
    }              // should extend to choose channel
    if (strcmp(ucommand, "NEOPIX") == 0)
    {    neopixel_set_mode((uint8_t) value); return; }
    if (strcmp(ucommand, "M") == 0)
    {   /* serial parser merges "m 1 2" into value=12; decode the two digits */
        int mot  = value / 10;
        int mode = value % 10;
        if (mot < 1 || mot > 5 || mode > 2) {
            snprintf(log, sizeof(log),
                     "# M: bad args (use m <1-5> <0=off|1=rotate|2=cont>)\n");
        } else {
            motor_cmd(mot, mode);
            snprintf(log, sizeof(log),"# M%d mode=%d\n", mot, mode);
        }
        printq_add(log);
        return;
    }

    // if no successful message send error to log
    snprintf(log, sizeof(log),"# parse_cmd %s unrecognized\n", ucommand); 
    printq_add(log);
    
/*    if (strcmp(ucommand, "KD") == 0)
    {    set_control_gain(1, value); }
    if (strcmp(ucommand, "RUN") == 0)
    {   run_control(); }
    */
}
