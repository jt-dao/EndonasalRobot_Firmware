# Endonasal Robot Firmware

Zephyr-based firmware for the STM32 Nucleo **F446RE** plus Python host tools.

**Current workflow (new PCB / v2):**

- Hands-on demo and Python UI: [`ZephyrEndoCode/DEMO_README.md`](ZephyrEndoCode/DEMO_README.md)
- Architecture, serial protocol, pin map: [`ZephyrEndoCode/NEW_PCB_README.md`](ZephyrEndoCode/NEW_PCB_README.md)
- Legacy v1 notes (COM port, `StateStruct.adc8`–`adc15`, etc.): section below and [`ZephyrEndoCode/README.md`](ZephyrEndoCode/README.md)

Pin assignments are summarized in `ZephyrEndoCode/include/pinouts-10-2.txt` where applicable.

---

### Legacy quick reference (v1-era docs)

Older experiments lived under `ZephyrEndoCode/python/` with hard-coded ports (e.g. `COM3`) and **`adc8`…`adc15`** naming for telemetry columns. The **default firmware today** uses **`python_v2/`**, **`find_port.py`**, and **`p_0`…`p_7`** (I2C pressure ADC). Do not mix assumptions without reading `NEW_PCB_README.md`.
