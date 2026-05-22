import os
import serial.tools.list_ports
import time


def pulse_stlink_target_reset(port, hold_s=0.12):
    """Pulse USB-serial DTR to reset the STM32 via ST-Link VCP (typical on Nucleo).

    If your board does not reboot, set env STM32_RESET_INVERT=1 to flip pulse polarity.
    """
    invert = os.environ.get("STM32_RESET_INVERT", "").strip() in ("1", "true", "yes")
    low, high = (True, False) if invert else (False, True)
    try:
        port.dtr = low
        time.sleep(hold_s)
        port.dtr = high
        time.sleep(hold_s)
    except (AttributeError, OSError, IOError):
        pass


def find_stm32_port():
    """Auto-detect STM32 Nucleo USB serial port. Falls back to manual entry."""
    candidates = [p.device for p in serial.tools.list_ports.comports()
                if 'usbmodem' in p.device.lower() or 'nucleo' in (p.description or '').lower()
                or 'stm32' in (p.description or '').lower()]
    if len(candidates) == 1:
        print(f'# Auto-selected port: {candidates[0]}')
        return candidates[0]
    if len(candidates) > 1:
        print('# Multiple candidate ports found:')
        for i, p in enumerate(candidates):
            print(f'#   [{i}] {p}')
        idx = int(input('# Select port index: '))
        return candidates[idx]
    # No candidates — let user pick from full list
    all_ports = [p.device for p in serial.tools.list_ports.comports()]
    if not all_ports:
        raise RuntimeError('No serial ports found. Check USB connection.')
    print('# No STM32 port auto-detected. Available ports:')
    for i, p in enumerate(all_ports):
        print(f'#   [{i}] {p}')
    idx = int(input('# Select port index: '))
    return all_ports[idx]