# Endonasal Robot Firmware — New PCB (v2 Hardware)

> **Operating guide first** (Quick Start → Serial Commands → State Output → NeoPixel test → Firmware Modes), then **reference / internals** (Architecture → Pin Mapping → Init Order → Pin Conflicts → Integration Notes → Build Notes).

---

## Quick Start

Build and flash **Python integration** firmware (not the lab demo):

```bash
cd ZephyrEndoCode
pio run -e python -t upload
pio device monitor -b 230400
```

Lab demo menu firmware:

```bash
pio run -e lab_demo -t upload
pio device monitor -b 230400
```

Or with west:
```bash
cd ZephyrEndoCode/zephyr
west build -b nucleo_f446re && west flash
```

Python (from repo root; port auto-detect in `find_port.py`):

```bash
cd ZephyrEndoCode/python_v2 && python run_stm_12_v2.py
```

See **`DEMO_README.md`** for the interactive prompt, handshake notes, and troubleshooting.

---

## Commands — Python Host Prompt

What you type at the `run_stm_12_v2.py` prompt (handled by `input_thread`). The host
translates these to wire commands — you do **not** type the uppercase wire names here.

| Type | Form | Effect |
|------|------|--------|
| `s` | `s <ch> <0\|1>` or `s <8 values>` | Solenoid(s). Channel **1-indexed**. Re-sends all SOL1-8 **and** DAC1-16. |
| `r` | `r <ch> <volts>` or `r <16 values>` | Regulator setpoint in **volts (0–5)**, channel **1-indexed**, clamped 0–5. Re-sends all DAC1-16 **and** SOL1-8. |
| `dac` | `dac <ch> <raw>` | Raw 16-bit DAC value; channel **1-indexed** (`dac 1` → wire `DAC1`). |
| `neo` | `neo <mode>` | NeoPixel mode: 0=off, 1=DAC status (default), 2=rainbow. |
| `m` | `m <motor> <mode>` | Stepper M1–M5; mode 0=off, 1=one rotation, 2=continuous. |
| `p` | `p` / `p <chip>` / `p <chip> <sensor>` | **Readback only** — prints pressures from `stateQ`; sends nothing to the MCU. Chip/sensor 1-indexed. |
| `estop` | `estop` | Sends `ESTOP`; also zeros the host's `regulator_vals`/`solenoid_vals`. |
| `prnwait` | `prnwait <ms>` | Telemetry print interval. |
| `start` | `start` | **Host-only** — sets the `charStart` experiment gate; sends nothing. |

## Commands — Raw Wire / MCU Protocol

What the firmware's `parse_cmd()` accepts directly. Usable from a raw serial monitor
(`pio device monitor`) or the lab demo — **not** from the host prompt above.

| Command | Example | Description |
|---------|---------|-------------|
| `SOL1`–`SOL8` | `SOL3 1` | Solenoid on (1) / off (0) |
| `DAC` | `DAC 32768` | Set DAC channel 0 (raw 16-bit) |
| `DAC1`–`DAC16` | `DAC5 32768` | Set DAC channel 1–16 (raw 16-bit) |
| `PWM1`–`PWM12` | `PWM5 1000` | Set PWM duty |
| `PFRQ1` / `PFRQ2` | `PFRQ1 500` | Legacy stepper pulse frequency |
| `STEPOFF` | `STEPOFF 0` | Legacy steppers off |
| `OUTON` / `OUTOFF` | `OUTON 1` | Legacy output (1=SOL5, 4=PC13) |
| `M` | `M 51` | Stepper M1-M5; value = motor×10 + mode (0=off, 1=rotate, 2=cont). Host `m 5 1` → `M 51` |
| `NEOPIX` | `NEOPIX 1` | LED mode: 0=off, 1=DAC status (default), 2=rainbow |
| `ESTOP` | `ESTOP 0` | All PWM + solenoids + DAC off |
| `PRNWAIT` | `PRNWAIT 100` | State print interval (ms) |

(See `control_thread.c` `parse_cmd()` for the full set, including `TIME`, `ADC`, `DACCH`, `DACTEST`, `DACAUTO`.)



---

## State Output (STM32 → Python)

```
# header:  t=, hx711, qdec3, qdec5, p_0, p_1, ..., p_15
   1.2345,        0,     0,     0,  128,  130,  125,  127,  132,  129,  126,  131,  124,  123,  126,  125,  128,  127,  129,  126
```

- `p_0`–`p_7`: **ADS7830** @ **0x48**, **8-bit** raw (**0–255**), **5 V** VREF.
- `p_8`–`p_15`: **ADS7830** @ **0x49**, **8-bit** raw (**0–255**), **5 V** VREF.
- Python pressure display uses the lab-demo calibration: **0.5–4.5 V = 0–60 PSI**.
- **`read_state()`** updates **one** I2C channel per **1 ms** control iteration (**round-robin**); the full set **`p_0`…`p_15`** advances every ~**16 ms**, not a single simultaneous snapshot.
- Python **`python_v2/run_stm_12_v2.py`** maps columns into **`p_0`…`p_15`**. Legacy **`python/run_stm.py`** used **`adc8`…`adc15`** for **internal ADC** semantics — **do not reuse old scaling**.

---

## Full Python v2 Example Logs

Example terminal sessions from the repo root using the current Python host flow. Values are representative: port names, ADC readings, boot counters, and I2C scan output will vary by machine and hardware.

### Flash Python firmware

**Command:** `cd ZephyrEndoCode` then `pio run -e python -t upload`

```text
$ cd ZephyrEndoCode
$ pio run -e python -t upload
...
Environment    Status    Duration
-------------  --------  ------------
python         SUCCESS   00:00:14.832
```

### Start the Python host

**Command:** `cd python_v2` then `python run_stm_12_v2.py`

```text
$ cd python_v2
$ python run_stm_12_v2.py
ports being used:
['/dev/cu.usbmodem11403']
/dev/cu.usbmodem11403: STM32 STLink - ST-Link VCP
# Auto-selected port: /dev/cu.usbmodem11403
Data Logging for STM32, with USB connection
Started rcvstate thread
Serial open. Using port /dev/cu.usbmodem11403 and baudrate 230400
sendCMD: started thread
# *** Booting Zephyr OS build v3.x.x ***
# I2C scan:
#   Found device at 0x48
#   Found device at 0x49
# Scan complete
# STM32READY
Sending "foo" and waiting for "# parse_cmd ..." or "# STM32READY"
# parse_cmd FOO unrecognized
Received response on attempt 1
# No control_loop provided - interactive mode. Use input prompt to send commands.
  Type 'p' to read pressure/ADC, 'prnwait <ms>' to change telemetry rate.
Threads started. ctrl C to quit
```

### Set telemetry interval

**Command:** `prnwait 100`

```text
Enter values for regulators, solenoids, or motors: prnwait 100
```

### Read all pressure channels

**Command:** `p`

```text
Enter values for regulators, solenoids, or motors: p
t=7.412  hx711=0  qdec3=0  qdec5=0
--- ADC channel 1 ---
  ch1 s1 (p_0 ): raw=128  volts=2.510  psi=30.15
  ch1 s2 (p_1 ): raw=126  volts=2.471  psi=29.56
  ch1 s3 (p_2 ): raw=129  volts=2.529  psi=30.44
  ch1 s4 (p_3 ): raw=127  volts=2.490  psi=29.85
  ch1 s5 (p_4 ): raw=131  volts=2.569  psi=31.03
  ch1 s6 (p_5 ): raw=130  volts=2.549  psi=30.74
  ch1 s7 (p_6 ): raw=125  volts=2.451  psi=29.26
  ch1 s8 (p_7 ): raw=132  volts=2.588  psi=31.32
--- ADC channel 2 ---
  ch2 s1 (p_8 ): raw=124  volts=2.431  psi=28.97
  ch2 s2 (p_9 ): raw=123  volts=2.412  psi=28.68
  ch2 s3 (p_10): raw=126  volts=2.471  psi=29.56
  ch2 s4 (p_11): raw=125  volts=2.451  psi=29.26
  ch2 s5 (p_12): raw=128  volts=2.510  psi=30.15
  ch2 s6 (p_13): raw=127  volts=2.490  psi=29.85
  ch2 s7 (p_14): raw=129  volts=2.529  psi=30.44
  ch2 s8 (p_15): raw=126  volts=2.471  psi=29.56
```

### Read one ADC chip

**Command:** `p 1`

```text
Enter values for regulators, solenoids, or motors: p 1
t=8.118  hx711=0  qdec3=0  qdec5=0
--- ADC channel 1 ---
  ch1 s1 (p_0 ): raw=128  volts=2.510  psi=30.15
  ch1 s2 (p_1 ): raw=126  volts=2.471  psi=29.56
  ch1 s3 (p_2 ): raw=129  volts=2.529  psi=30.44
  ch1 s4 (p_3 ): raw=127  volts=2.490  psi=29.85
  ch1 s5 (p_4 ): raw=131  volts=2.569  psi=31.03
  ch1 s6 (p_5 ): raw=130  volts=2.549  psi=30.74
  ch1 s7 (p_6 ): raw=125  volts=2.451  psi=29.26
  ch1 s8 (p_7 ): raw=132  volts=2.588  psi=31.32
```

### Read one pressure sensor

**Command:** `p 2 3`

```text
Enter values for regulators, solenoids, or motors: p 2 3
t=8.817  hx711=0  qdec3=0  qdec5=0
  ch2 s3 (p_10): raw=126  volts=2.471  psi=29.56
```

### Read all pressure sensors
**Command:** `p`

```text
Enter values for regulators, solenoids, or motors: p
t=15.923  hx711=0  qdec3=0  qdec5=0
--- ADC channel 1 ---
  ch1 s1 (p_0 ): raw=121  volts=2.373  psi=28.09
  ch1 s2 (p_1 ): raw=122  volts=2.392  psi=28.38
  ch1 s3 (p_2 ): raw=120  volts=2.353  psi=27.79
  ch1 s4 (p_3 ): raw=121  volts=2.373  psi=28.09
  ch1 s5 (p_4 ): raw=123  volts=2.412  psi=28.68
  ch1 s6 (p_5 ): raw=122  volts=2.392  psi=28.38
  ch1 s7 (p_6 ): raw=121  volts=2.373  psi=28.09
  ch1 s8 (p_7 ): raw=122  volts=2.392  psi=28.38
--- ADC channel 2 ---
  ch2 s1 (p_8 ): raw=120  volts=2.353  psi=27.79
  ch2 s2 (p_9 ): raw=121  volts=2.373  psi=28.09
  ch2 s3 (p_10): raw=120  volts=2.353  psi=27.79
  ch2 s4 (p_11): raw=121  volts=2.373  psi=28.09
  ch2 s5 (p_12): raw=122  volts=2.392  psi=28.38
  ch2 s6 (p_13): raw=121  volts=2.373  psi=28.09
  ch2 s7 (p_14): raw=120  volts=2.353  psi=27.79
  ch2 s8 (p_15): raw=121  volts=2.373  psi=28.09
```

### Set one regulator

**Command:** `r 1 2.5`

```text
Enter values for regulators, solenoids, or motors: r 1 2.5
[2.5 0.  0.  0.  0.  0.  0.  0.  0.  0.  0.  0.  0.  0.  0.  0. ]
```

### Set all regulators

**Command:** `r 0 0.5 1.0 1.5 2.0 2.5 3.0 3.5 4.0 4.5 5.0 4.5 4.0 3.5 3.0 2.5`

```text
Enter values for regulators, solenoids, or motors: r 0 0.5 1.0 1.5 2.0 2.5 3.0 3.5 4.0 4.5 5.0 4.5 4.0 3.5 3.0 2.5
[0.  0.5 1.  1.5 2.  2.5 3.  3.5 4.  4.5 5.  4.5 4.  3.5 3.  2.5]
```

### Set one raw DAC channel

**Command:** `dac 1 32768`

```text
Enter values for regulators, solenoids, or motors: dac 1 32768
DAC1 -> 32768
```

### Set one solenoid

**Command:** `s 3 1`

```text
Enter values for regulators, solenoids, or motors: s 3 1
[0. 0. 1. 0. 0. 0. 0. 0.]
```

### Set all solenoids

**Command:** `s 0 1 0 1 0 1 0 1`

```text
Enter values for regulators, solenoids, or motors: s 0 1 0 1 0 1 0 1
[0. 1. 0. 1. 0. 1. 0. 1.]
```

### Switch NeoPixel modes

**Commands:** `neo 2`, then `neo 1`

```text
Enter values for regulators, solenoids, or motors: neo 2
NEOPIX mode -> 2

Enter values for regulators, solenoids, or motors: neo 1
NEOPIX mode -> 1
```

### Run and stop one motor

**Commands:** `m 5 1`, then `m 5 0`

```text
Enter values for regulators, solenoids, or motors: m 5 1
M5 mode -> 1

Enter values for regulators, solenoids, or motors: m 5 0
M5 mode -> 0
```

### Start experiment logging gate
Import experiments as seen in Python V1--no experiments currently made for V2.

**Command:** `start`

```text
Enter values for regulators, solenoids, or motors: start
```

### Emergency stop

**Command:** `estop`

```text
Enter values for regulators, solenoids, or motors: estop
ESTOP sent
```

### Shut down the host

**Command:** `Ctrl+C`

```text
^C
User Keyboard Interrupt
rcvstate: Closing file
rcvstate: Closing serial in 2 seconds
sendCmd: finished thread
rcvstate: finished thread
```

### Inspect the recorded data file

**Command:** `sed -n '1,5p' ../Data/data.txt`

```text
$ sed -n '1,5p' ../Data/data.txt
"Data file recorded 2026/5/21  14:03:11"
" time  hx711, qdec3, qdec5, p_0, p_1, p_2, p_3, p_4, p_5, p_6, p_7, p_8, p_9, p_10, p_11, p_12, p_13, p_14, p_15"
7.412,0,0,0,128,126,129,127,131,130,125,132,124,123,126,125,128,127,129,126
7.512,0,0,0,128,126,129,127,131,130,125,132,124,123,126,125,128,127,129,126
7.612,0,0,0,128,126,129,127,131,130,125,132,124,123,126,125,128,127,129,126
```

---

## Testing the NeoPixel DAC Status Display

### What to expect

On boot, all 16 LEDs are **off** (all DAC channels start at 0). As DAC values are set, each LED shows a smooth continuous color corresponding to the exact voltage — every one of the 65535 non-zero DAC steps maps to a unique color and brightness.

The spectrum sweeps hue **violet-blue → cyan → green → yellow → orange → red** while brightness simultaneously increases, so higher pressure looks visually "hotter" in both color and intensity:

| DAC Value | Voltage | Hue | Brightness | Appearance |
|-----------|---------|-----|------------|------------|
| 0 | 0 V | — | off | Off |
| ~5460 | ~0.4 V | 183 | 30 | Dim blue |
| ~16384 | ~1.25 V | 150 | 38 | Cyan |
| ~27300 | ~2.1 V | 117 | 47 | Teal-green |
| ~38200 | ~2.9 V | 83 | 57 | Yellow-green |
| ~49150 | ~3.75 V | 50 | 66 | Orange-yellow |
| ~60000 | ~4.6 V | 17 | 76 | Orange-red |
| 65535 | 5.0 V | 0 | 80 | Bright red |

### Quick test from Python

```python
import serial, time

port = '/dev/tty.usbmodem...'   # replace with your port
ser = serial.Serial(port, 230400, timeout=2)

# wait for firmware ready
while b'STM32READY' not in ser.readline():
    pass

def send(cmd, val):
    ser.write(f'{cmd} {val}\n'.encode())
    time.sleep(0.15)

# ramp DAC channel 0 (LED 0) from off → full scale
for v in [0, 13650, 27300, 40950, 54600, 65535]:
    send('DAC1', v)
    time.sleep(0.5)

# set a few channels at different levels to see multi-LED display
send('DAC1',  13650)   # LED 0 → blue   (~1.0 V)
send('DAC2',  27300)   # LED 1 → green  (~2.1 V)
send('DAC3',  54600)   # LED 2 → orange (~4.2 V)
send('DAC4',  65535)   # LED 3 → red    (5.0 V)

# turn off neopixels
send('NEOPIX', 0)

# switch to rainbow mode
send('NEOPIX', 2)

# back to DAC status mode
send('NEOPIX', 1)
```

### Quick test from serial monitor

```
DAC1 32768     → LED 0 turns yellow-green (~2.5 V)
DAC2 65535     → LED 1 turns red (5.0 V)
DAC3 0         → LED 2 turns off
NEOPIX 0       → all LEDs off
NEOPIX 1       → restore DAC status display
ESTOP 0        → all outputs off, all LEDs clear
```

### Notes

- The LED strip must be connected to **PB1** (3.3 V signal level; use a level shifter to 5 V for longer strips)
- LEDs refresh at ~10 Hz — changes appear within one refresh cycle (~100 ms)
- `ESTOP` clears all LEDs regardless of mode; send `NEOPIX 1` afterward to re-enable

---

## Two Firmware Modes

| Mode | PlatformIO env | Main sources |
|------|----------------|--------------|
| **Python integration** | **`pio run -e python`** | `src/main.c` (+ UART cmd queue, `run_stm_12_v2.py`) |
| **Lab demo menu** | **`pio run -e lab_demo`** | `src/main_lab_demo.c` (see `platformio.ini` **`src_filter`**) |

Switch by **re-flashing** the env above; no need to rename `main.c` unless you maintain a custom CMake flow outside PlatformIO.

---

# Reference / Internals

## Architecture

```
Python host (230400 baud)
  │  sends: PWM1-12, SOL1-8, OUTON/OFF, DAC, DACCH, ESTOP, PRNWAIT, PFRQ1/2, STEPOFF, M
  │  receives: "# STM32READY", state lines (t=,hx711,qdec3,qdec5,p_0..p_15)
  ▼
STM32F446RE (Zephyr RTOS)
  ├── main.c              → init peripherals, start threads, send # STM32READY
  ├── control_thread.c    → parse commands from cmdq, drive outputs, read state
  ├── read_state.c        → read I2C ADC (pressure), HX711, QDEC; print state
  ├── read_serial.c       → UART input → cmdq
  ├── uart_printq.c       → UART output queue
  ├── solenoid.c          → 8-ch GPIO solenoid control
  ├── dac.c               → AD5679R 16-ch SPI DAC
  ├── i2c_adc.c           → ADS7830 I2C ADC (8-bit, 0-255)
  ├── pwm.c               → 12-ch PWM
  ├── gpout.c             → PC13 general output (**PA12** is **SOL5**, not gpout)
  ├── read_adc.c          → internal STM32 ADC (channels 8-14, not used in state)
  ├── read_hx711.c        → HX711 load cell
  ├── read_qdec.c         → quadrature decoders (timer 3/5)
  ├── timing.c            → get_time(), get_time_float()
  ├── heartbeat.c         → LED heartbeat
  ├── neopixel.c          → WS2812 (PB1), heat-map of DAC ch0-15 pressure levels
  └── motor.c             → A4988 stepper drivers, motors M1-M5
```

---

## New PCB Pin Mapping

### Solenoids (solenoid.c)

| Channel | Pin | Port |
|---------|-----|------|
| SOL1 | PC9 | GPIOC |
| SOL2 | PC8 | GPIOC |
| SOL3 | PC6 | GPIOC |
| SOL4 | PC5 | GPIOC |
| SOL5 | PA12 | GPIOA |
| SOL6 | PC7 | GPIOC |
| SOL7 | PB6 | GPIOB |
| SOL8 | PA11 | GPIOA |

### I2C ADC — ADS7830 (i2c_adc.c)

- Bus: I2C1 (PB8=SCL, PB9=SDA)
- Address 0x48 (and optionally 0x49)
- 8 channels, 8-bit (0-255), 5V VREF
- Triple-read per channel for mux settling

### DAC — AD5679R (dac.c)

- Bus: SPI2 (PB10=SCK, PB14=MISO, PB15=MOSI, PB12=CS)
- 16 channels, 16-bit (0-65535), 0-5V output (2.5V internal ref, 2x gain)
- `set_dac(value)` → channel 0
- `set_dac_channel(ch, value)` → any channel

### Neopixel (neopixel.c)

- PB1, bit-bang, 16 LEDs (WS2812B)
- **LED N mirrors DAC channel N** (0-indexed) with a continuous analog color spectrum across the full 0-5V range
- Every DAC step (1 of 65535) maps to a unique hue + brightness: violet-blue (low) → cyan → green → yellow → orange → bright red (5V)
- Brightness also scales with value so high pressure is visually "hotter" even between neighboring colors
- DAC = 0 → LED off (regulator inactive)
- Refreshes at ~10 Hz from the control thread
- Mode 1 (DAC status) is the default on boot

### Steppers — A4988 (motor.c)

Shared active-low enable: **EN = PA0**. Pins per `include/pinouts-10-2.txt`.

| Motor | STEP | DIR |
|-------|------|-----|
| M1 | PB7 | PC11 |
| M2 | PC13 | PC10 |
| M3 | PB0 | PC12 |
| M4 | PC2 | PC1 |
| M5 | PC0 | PC3 |

- Driven via LL GPIO (push-pull, low slew); `motor_init()` overrides the analog mode `adc_init()` leaves on the shared PCx pins.
- One rotation = 200 step pulses at a 500µs half-period (~1 kHz). Continuous mode is advanced by `motor_periodic()` in the control loop.

---

## Init order (`main.c`) — matters for pin mux

Order is **intentional**: **`pwm_init()`** first configures timer alternate-function pins; **`solenoid_init()`** and **`i2c_adc_init()`** then override shared pins for GPIO / **I2C1 (PB8/PB9)**.

1. `led_init()` + short blink loop  
2. **`start_print_thread()`** — print queue / UART output path early  
3. **`dac_init()`** — AD5679R SPI  
4. **`adc_init()`** — internal ADC (not filling **`p_*`** in telemetry)  
5. **`pwm_init()`** — timer AF on shared pins  
6. **`solenoid_init()`** — SOL GPIO overrides PWM/USB-facing pins as needed  
7. **`i2c_adc_init()`** + **`i2c_adc_scan()`** — ADS7830 on **I2C1**  
8. **`gpout_init()`** — **PC13**  
9. **`neopixel_init()`** — **PB1**  
10. **`hx711_init()`**, **`qdec_init()`**, **`motor_init()`** — `motor_init()` runs last so it owns PA0/PCx after `adc_init`/`qdec_init`  
11. Threads: **`uart_interrupt_init`**, **`start_uart_input`**, **`start_hx711`**, **`start_control`**, **`start_print_state`**, etc.  
12. **`printq_add("# STM32READY\n")`** after brief delays — Python host should sync after reset as described in **`DEMO_README.md`**

---

## Known Pin Conflicts (Resolved)

| Pin | Old use | New use | Resolution |
|-----|---------|---------|------------|
| PA12 | `gpout.c` Output1 | SOL5 | `gpout.c` removed PA12 config; `OUTON 1` redirects to `set_solenoid(5,1)` |
| PC5 | Internal ADC IN15 | SOL4 | `solenoid_init()` reconfigures; state reads I2C ADC instead |
| PC8 | gpout Output2 (commented out) | SOL2 | Already disabled in old code |
| PC9 | gpout Output3 (commented out) | SOL1 | Already disabled in old code |
| PB1 | Internal ADC IN9 | Neopixel data | `neopixel_init()` now called in Python mode; no ADC conflict |
| PA0 | `read_qdec.c` TIM5 encoder input | Stepper shared enable (EN, motor.c) | `motor_init()` runs after `qdec_init()` and claims PA0 as the A4988 EN; quadrature on PA0 is disabled — matches `pinouts-10-2.txt` (PA0 = EN_x) |

---

## Integration notes vs legacy `README.md` / `python/`

| Area | Legacy v1 mental model | New PCB |
|------|-------------------------|---------|
| Host scripts | `python/*.py`, fixed **`COM3`** | **`python_v2/`**, **`find_port.py`** |
| Telemetry ADC fields | **`adc8`…`adc15`**, 12-bit MCU ADC | **`p_0`…`p_15`**, I2C **0–255** |
| **`OUTON`/`OUTOFF`** | GPIO channel numbers | Pin remap — see **Known Pin Conflicts** |
| Handshake | Reset-release rituals vary | **`# STM32READY`** once per boot — see **`DEMO_README.md`** |

---

## Build Notes

- **`zephyr/CMakeLists.txt`** globs `src/*.c` and excludes **`main_lab_demo.c`**, `*.backup`, `*.good` (PlatformIO may additionally filter sources).
- **`zephyr/prj.conf`**: **`CONFIG_CONSOLE_SUBSYS=y`**. **`CONFIG_CONSOLE_GETCHAR`** is **not** enabled — **`uart_getline.c`** supplies **`console_getchar()`** on USART2.
- Dead / duplicate sources may live under **`src_dead/`** when present.
