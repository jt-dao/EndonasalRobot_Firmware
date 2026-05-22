# MRIRobotProject (ZephyrEndoCode)

Zephyr sample code: threads, queues, non-blocking print, logging. Pin usage is summarized under `include/` (e.g. `pinouts-10-2.txt`). Diagrams may live under `Doc/` when present.

**New PCB / daily workflow:** use **`python_v2/`**, **`pio run -e python`**, and read **`DEMO_README.md`** and **`NEW_PCB_README.md`** in this folder before relying on the steps below.

---

### Legacy workflow — STM32 with Python (pre–python_v2)
1. plug in stm32 board, and upload compiled ZephyrSampleCode to board
2. note USB COM port (windows) or dev/USB on Ubuntu/MacOS  (230.4 kBaud)
3. edit control-record.py to set appropriate port: ser = serial.Serial('COM3')
4. To run, hold reset on STM32, run control-record.py, and release reset on STM32 until
 get message ``Threads started. ctrl C to quit''. 
5. Note that comments from STM32 will be preceded by '#'
6. terminate program with ctrl-C. State data and comments will be stored in ./Data/data.txt as a comma separated file. Sort data in excel to make it easy to remove '#' debugging info.
7. In python, if StateQ size grows, it means Python can not keep up with state updates from STM32

### Python Interface to STM32
* state vector is sent every PRNWAIT ms over USB, but is updated on STM32 every millisecond. 
* class StateStruct() gives access to state variables:
* state.time  floating point for time stamp
* state.hx711  load cell (note this should be extended to add second load cell)
* state.qdec3, state.qdec5 = 0  quadrature decoders using timer 3 (16 bit) and timer 5 (32 bit)
* state.adc8 ... state.adc15  are 12 bit A/D channels 8 to 15

* commands can be sent to STM32 from Python using either
1. mutiple commands per message (saves time)
    * message = makeCmdString(command, value) + ...
    * sendQ.put(message)

2. single command per message 
    * makeCmd(command, value)  

* where command is a string of 7 characters or less of form 'PRNWAIT' with quotes
* and value should be an unsigned integer for DAC/PWM, or signed for PFRQ
* makeCmd('DAC', value)  output on digital-to-analog converter 1, 0...4095
* makeCmd('PWM1', value)  set PWM1 value, min=0 (check?) , max depends on PWM_FREQUENCY. At 2 kHz, max is about 48000
* ... makeCmd('PWM8', value)
* makeCmd('ESTOP', value) ignores value, sets pulse frequency output to zero, and PWM 1 to 8 to zero
* makeCmd('PFRQ1', value), makeCmd('PFRQ2', value): -20000 < value < 20000 sets frequency, with negative sign changing direction
* makeCmd('STEPOFF', value), ignores value, disables stepping motor so shaft is free to rotate
* makeCmd('PRNWAIT', value) set wait time (in ms) between sending updated state vector to Python 
* makeCmd('OUTOFF', chan)  turn off (=0, low) general purpose output chan={1,2,3,4}
* makeCmd('OUTON', chan)  turn on (=1,high) general purpose output chan={1,2,3,4}
* Python control_loop() can send a new command after new state vector is received from STM32


#### Setup/Modification (c code)
* `platformio.ini`: **`monitor_speed = 230400`** for current Nucleo integration (older notes may say 115200)
* ./include/common.h: INPUT_CMD_LENGTH 8, limits commands to 7 characters, and command value to 7 digits (no negative numbers)
* /include/common.h: #define PRINTWAIT 5000, initial state printing update (actual state updates are at 1 kHz). Send command PRNWAIT [tme in ms] to change this from Python after starting.
* to reduce number of debugging messages, recompile without /include/common.h: #define DEBUG_PRINT
* PWM frequency is set by PWM_FREQUENCY in pwm.c (default 2 kHz)

#### Setup/Modification (Python)
* set appropriate port, e.g.: ser = serial.Serial('COM3')
* choose data_file_name = '../Data/data.txt'  [this should be updated to make a unique name each time to avoid data over write]
* To change update rate, (will likely work at 100 ms, i.e. 10 Hz) change makeCmd('PRNWAIT', 500)   # set wait time for state update to be 500 ms
* update rate works at 40 ms, but not faster using 2xPFRQ and 12xPWM
