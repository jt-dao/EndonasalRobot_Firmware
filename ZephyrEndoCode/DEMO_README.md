# Endonasal Robot — Demo Guide (New PCB)

---

## Prerequisites

- STM32 Nucleo **F446RE** connected via USB (ST-Link virtual COM port, **230400 baud**)
- PlatformIO (`pip install platformio`) **or** west + Zephyr SDK
- Python **3** with **`pyserial`** and **`numpy`**
- Run interactive scripts from **`ZephyrEndoCode/python_v2/`**

---

## Option A — Lab Demo (no Python host)

Self-contained serial menu for bench checks.

### 1. Flash lab demo firmware

```bash
cd ZephyrEndoCode
pio run -e lab_demo -t upload
```

### 2. Serial monitor (230400 baud)

```bash
pio device monitor -b 230400
```

Use **single keypresses — no Enter**.

### Menu

| Key | Demo |
|-----|------|
| `1` | LEDs — onboard blink, then NeoPixel rainbow (`s` to stop) |
| `2` | DAC — sub-keys: `m`/`z`/`f`/`r`/`q` |
| `3` | Solenoids — SOL1–SOL8 one at a time |
| `4` | ADC — live pressure; `r` raw (0–255), `p` PSI, `v` volts |
| `5` | All demos in sequence |
| `p` | Pin Step — manually toggle PB10/PB12/PB14/PB15 (SPI pins) |
| `m` | Motor — A4988 stepper test (EN=PA0, STEP=PC0, DIR=PC3) |
| `0` | Stop outputs |
| `h` | Menu |
| `s` | Stop demo, back to menu |

### Switch back to Python integration firmware

```bash
pio run -e python -t upload
```

---

## Option B — Python interactive mode (`run_stm_12_v2.py`)

Firmware **`main.c`** (PlatformIO **`env:python`**) listens on USART2 for text commands and streams telemetry.

### 1. Flash Python firmware

```bash
cd ZephyrEndoCode
pio run -e python -t upload
```

Always use **`python`** unless you intentionally want the lab menu (**`lab_demo`**).

### 2. Run the host script

```bash
cd ZephyrEndoCode/python_v2
python run_stm_12_v2.py
```

Ports: **`find_port.find_stm32_port()`** picks `usbmodem` / STM32 descriptions on macOS; you will be prompted if several match.

### Startup and `# STM32READY`

The MCU sends **`# STM32READY`** **once per boot** (via the print queue), after initialization and thread bring-up.

The script arms a handshake (`controlStop`) and expects that line while receiving on the serial thread. If the board **already finished booting** before the handshake is ready, that line is easy to miss — **press the black RESET** on the Nucleo after starting the script, or trigger a reset from Python:

- **`find_port.pulse_stlink_target_reset(ser)`** — toggles USB **DTR** (works on many ST-Link VCP setups). If nothing reboots, try  
  **`STM32_RESET_INVERT=1 python run_stm_12_v2.py`**  
  (see `find_port.py`).

Depending on script revision you may also see **`foo`** sent as a probe and **`# parse_cmd FOO unrecognized`** — that only proves RX works; **success is recognizing `# STM32READY`**.

Boot logs may include **`# I2C scan:`** and **`#   Found device at 0x48`** when the ADS7830 ACKs; if there is no device at **0x48**, you still get **`# Scan complete`** without that address line.

### 3. Commands at the prompt

| Command | Example | Effect |
|---------|---------|--------|
| `r <ch> <volts>` | `r 1 3.5` | Regulator channel (**1–16, 1-indexed**) → voltage setpoint (DAC scaled in firmware) |
| `r <16 voltages>` | `r 0 2.5 5 ...` | All 16 channels |
| `s <ch> <0\|1>` | `s 3 1` | Solenoid channel on/off |
| `s <8 values>` | `s 0 1 0 ...` | All solenoids |
| `dac <ch> <raw>` | `dac 1 32768` | Raw **16-bit** DAC (`DAC`/`DAC1`–`DAC16` on wire — see `NEW_PCB_README.md` for voltage span) |
| `neo <mode>` | `neo 2` | NeoPixel: **0** off, **1** DAC status (default), **2** rainbow |
| `m <motor> <mode>` | `m 5 1` | Stepper M1–M5 — mode **0**=off, **1**=one rotation, **2**=continuous |
| `p` | `p` | Print latest pressures (`p_0`–`p_7`) from `stateQ` |
| `prnwait <ms>` | `prnwait 100` | Telemetry period (**PRNWAIT** on MCU; default often **5000** ms until changed) |
| `estop` | `estop` | ESTOP — PWM/solenoids/DAC outputs cleared per firmware |
| `Ctrl+C` | — | Exit (closes serial in handler) |

### 4. What you should see

- Rapid LED blink during boot, then heartbeat.
- NeoPixels in DAC-status mode reflect regulator/DAC levels (when **`neo 1`**).
- Rows appended to **`../Data/data.txt`** (comma-separated telemetry).

### 5. Quick hardware check

```
neo 2
neo 1
r 1 2.5
r 1 0
s 1 1
s 1 0
estop
```

---

## Option C — Automated experiment (`control_loop`)

Pass a **`control_loop(q_output, result_folder)`** into **`try_main`**. Wait until **`controlStop`** is cleared after **`# STM32READY`** before assuming the MCU is gated ready:

```python
# python_v2/my_experiment.py
from run_stm_12_v2 import try_main, controlStop, stateQ
import queue
import time

def control_loop(q_output, result_folder):
    while controlStop.is_set():
        time.sleep(0.05)
    while True:
        if not stateQ.empty():
            state = stateQ.get()
            # state.time, state.hx711, state.qdec3, state.qdec5, state.p_0 .. state.p_7
            ...
        time.sleep(0.04)

if __name__ == "__main__":
    try_main(control_loop, queue.Queue(), None)
```

---

## Migrating from v1 (`python/` folder)

| Topic | v1 habit | v2 / new PCB |
|--------|-----------|----------------|
| Host code | `python/run_stm_12_v2.py`, hard-coded **`COM3`** | **`python_v2/`**, **`find_port.py`** |
| Telemetry names | **`adc8`…`adc15`** (internal ADC mindset) | **`p_0`…`p_7`** — **ADS7830**, **0–255**, **5 V ref** |
| Scaling | e.g. **12-bit → 3.3 V** helpers | Re-derive pressure/voltage; not interchangeable |
| Snapshot | All ADC channels updated together | **Round-robin** I2C read (~**8 ms** to refresh all **`p_*`** at **1 ms** loop) |
| Outputs | **`OUTON`/`OUTOFF`** channel numbers | Pin remap — e.g. **`OUTON 1`** → **SOL5** (see **`NEW_PCB_README.md`**) |
| Extras | — | **`SOL1`–`SOL8`**, **`NEOPIX`**, **`DAC1`–`DAC16`** |

---

## Troubleshooting

| Symptom | What to try |
|---------|-------------|
| No COM port | Cable; **`pio device list`** |
| **`ModuleNotFoundError: serial`** | Install **`pyserial`** in the environment you use to run **`python`** |
| Handshake / no **`# STM32READY`** | Black **RESET** after starting the script; **`STM32_RESET_INVERT=1`** with DTR pulse; confirm **`pio run -e python`** (not **`lab_demo`**) |
| **`# parse_cmd FOO unrecognized`** | Expected if the script probes with **`foo`**; not sufficient by itself for handshake |
| **`# parse_cmd INPUT_T` / `READ` unrecognized** | Often **harmless** UART noise / fragmented parse at connect |
| **`UnicodeDecodeError`** in receive thread | Non-ASCII on the line — use **`errors='replace'`** in **`decode`** if you patch the script |
| No **`Found device at 0x48`** on I2C scan | Wiring **PB8/PB9**, sensor power, wrong **I2C address** |
| NeoPixels dark | **PB1**; after **`estop`**, send **`neo 1`** |
| Solenoid silent | **24 V** supply; **`NEW_PCB_README`** SOL pin table |
| Telemetry slow | **`prnwait 100`** (10 Hz) |

---

## Related files

- **`NEW_PCB_README.md`** — architecture, serial command table, pin map, state CSV layout  
- **`platformio.ini`** — **`python`** vs **`lab_demo`** sources  
