#!/usr/bin/env python3
"""
Simple serial monitor to check STM32 output
"""

import serial
import time

# Configure serial connection
ser = serial.Serial('/dev/tty.usbmodem1203', 230400, timeout=1)

print("=== SERIAL MONITOR ===")
print("Listening for 5 seconds...")
print("Press Ctrl+C to stop")

try:
    start_time = time.time()
    while time.time() - start_time < 5:
        if ser.in_waiting > 0:
            data = ser.read_all()
            if data:
                print(f"Received: {data.decode('utf-8', errors='ignore')}")
        time.sleep(0.1)
except KeyboardInterrupt:
    print("\nStopped by user")

ser.close()
print("Monitor complete!") 