# Endonasal Robot Firmware — New PCB (v2 Hardware)

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

## Architecture

```
Python host (230400 baud)
  │  sends: PWM1-12, SOL1-8, OUTON/OFF, DAC, DACCH, ESTOP, PRNWAIT, PFRQ1/2, STEPOFF, M
  │  receives: "# STM32READY", state lines (t=,hx711,qdec3,qdec5,p_0..p_7)
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
- 16 channels, 16-bit (0-65535), 0-24V output
- `set_dac(value)` → channel 0
- `set_dac_channel(ch, value)` → any channel

### Neopixel (neopixel.c)

- PB1, bit-bang, 16 LEDs (WS2812B)
- **LED N mirrors DAC channel N** (0-indexed) with a continuous analog color spectrum across the full 0-24V range
- Every DAC step (1 of 65535) maps to a unique hue + brightness: violet-blue (low) → cyan → green → yellow → orange → bright red (24V)
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

## Serial Commands (Python → STM32)

| Command | Example | Description |
|---------|---------|-------------|
| `SOL1`–`SOL8` | `SOL3 1` | Solenoid on (1) / off (0) |
| `PWM1`–`PWM12` | `PWM5 1000` | Set PWM duty |
| `DAC` | `DAC 32768` | Set DAC channel 0 |
| `OUTON` / `OUTOFF` | `OUTON 1` | Legacy output (1=SOL5, 4=PC13) |
| `ESTOP` | `ESTOP 0` | All PWM + solenoids off |
| `PRNWAIT` | `PRNWAIT 100` | State print interval (ms) |
| `PFRQ1` / `PFRQ2` | `PFRQ1 500` | Stepper pulse frequency |
| `STEPOFF` | `STEPOFF 0` | Steppers off |
| `NEOPIX` | `NEOPIX 1` | LED mode: 0=off, 1=DAC status (default), 2=rainbow |
| `M` | `M 51` | Stepper M1-M5; value = motor×10 + mode (0=off, 1=one rotation, 2=continuous). Host `m 5 1` → `M 51` |

## Testing the NeoPixel DAC Status Display

### What to expect

On boot, all 16 LEDs are **off** (all DAC channels start at 0). As DAC values are set, each LED shows a smooth continuous color corresponding to the exact voltage — every one of the 65535 non-zero DAC steps maps to a unique color and brightness.

The spectrum sweeps hue **violet-blue → cyan → green → yellow → orange → red** while brightness simultaneously increases, so higher pressure looks visually "hotter" in both color and intensity:

| DAC Value | Voltage | Hue | Brightness | Appearance |
|-----------|---------|-----|------------|------------|
| 0 | 0 V | — | off | Off |
| ~5460 | ~2 V | 183 | 30 | Dim blue |
| ~16384 | ~6 V | 150 | 38 | Cyan |
| ~27300 | ~10 V | 117 | 47 | Teal-green |
| ~38200 | ~14 V | 83 | 57 | Yellow-green |
| ~49150 | ~18 V | 50 | 66 | Orange-yellow |
| ~60000 | ~22 V | 17 | 76 | Orange-red |
| 65535 | 24 V | 0 | 80 | Bright red |

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
send('DAC1',  13650)   # LED 0 → blue   (~5 V)
send('DAC2',  27300)   # LED 1 → green  (~10 V)
send('DAC3',  54600)   # LED 2 → orange (~20 V)
send('DAC4',  65535)   # LED 3 → red    (24 V)

# turn off neopixels
send('NEOPIX', 0)

# switch to rainbow mode
send('NEOPIX', 2)

# back to DAC status mode
send('NEOPIX', 1)
```

### Quick test from serial monitor

```
DAC1 32768     → LED 0 turns yellow-green (~12 V)
DAC2 65535     → LED 1 turns red (24 V)
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

## State Output (STM32 → Python)

```
# header:  t=, hx711, qdec3, qdec5, p_0, p_1, p_2, p_3, p_4, p_5, p_6, p_7
   1.2345,        0,     0,     0,  128,  130,  125,  127,  132,  129,  126,  131
```

- `p_0`–`p_7`: **ADS7830** @ **0x48**, **8-bit** raw (**0–255**), **5 V** VREF.
- **`read_state()`** updates **one** I2C channel per **1 ms** control iteration (**round-robin**); the full set **`p_0`…`p_7`** advances every ~**8 ms**, not a single simultaneous snapshot.
- Python **`python_v2/run_stm_12_v2.py`** maps columns into **`p_0`…`p_7`**. Legacy **`python/run_stm.py`** used **`adc8`…`adc15`** for **internal ADC** semantics — **do not reuse old scaling**.

---

## Two Firmware Modes

| Mode | PlatformIO env | Main sources |
|------|----------------|--------------|
| **Python integration** | **`pio run -e python`** | `src/main.c` (+ UART cmd queue, `run_stm_12_v2.py`) |
| **Lab demo menu** | **`pio run -e lab_demo`** | `src/main_lab_demo.c` (see `platformio.ini` **`src_filter`**) |

Switch by **re-flashing** the env above; no need to rename `main.c` unless you maintain a custom CMake flow outside PlatformIO.

---

## Integration notes vs legacy `README.md` / `python/`

| Area | Legacy v1 mental model | New PCB |
|------|-------------------------|---------|
| Host scripts | `python/*.py`, fixed **`COM3`** | **`python_v2/`**, **`find_port.py`** |
| Telemetry ADC fields | **`adc8`…`adc15`**, 12-bit MCU ADC | **`p_0`…`p_7`**, I2C **0–255** |
| **`OUTON`/`OUTOFF`** | GPIO channel numbers | Pin remap — see **Known Pin Conflicts** |
| Handshake | Reset-release rituals vary | **`# STM32READY`** once per boot — see **`DEMO_README.md`** |

---

## Build Notes

- **`zephyr/CMakeLists.txt`** globs `src/*.c` and excludes **`main_lab_demo.c`**, `*.backup`, `*.good` (PlatformIO may additionally filter sources).
- **`zephyr/prj.conf`**: **`CONFIG_CONSOLE_SUBSYS=y`**. **`CONFIG_CONSOLE_GETCHAR`** is **not** enabled — **`uart_getline.c`** supplies **`console_getchar()`** on USART2.
- Dead / duplicate sources may live under **`src_dead/`** when present.

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

## Known Pin Conflicts (Resolved)

| Pin | Old use | New use | Resolution |
|-----|---------|---------|------------|
| PA12 | `gpout.c` Output1 | SOL5 | `gpout.c` removed PA12 config; `OUTON 1` redirects to `set_solenoid(5,1)` |
| PC5 | Internal ADC IN15 | SOL4 | `solenoid_init()` reconfigures; state reads I2C ADC instead |
| PC8 | gpout Output2 (commented out) | SOL2 | Already disabled in old code |
| PC9 | gpout Output3 (commented out) | SOL1 | Already disabled in old code |
| PB1 | Internal ADC IN9 | Neopixel data | `neopixel_init()` now called in Python mode; no ADC conflict |
| PA0 | `read_qdec.c` TIM5 encoder input | Stepper shared enable (EN, motor.c) | `motor_init()` runs after `qdec_init()` and claims PA0 as the A4988 EN; quadrature on PA0 is disabled — matches `pinouts-10-2.txt` (PA0 = EN_x) |
