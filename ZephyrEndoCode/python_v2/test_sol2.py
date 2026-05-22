#!/usr/bin/env python3
"""
Test SOL2 command with current firmware
"""

import serial
import time

# Configure serial connection
ser = serial.Serial('/dev/tty.usbmodem1203', 230400, timeout=1)

print("=== SOL2 COMMAND TEST ===")

# Test SOL2 ON
print("Sending: SOL2 1")
ser.write(b'SOL2 1\n')
time.sleep(0.1)

# Read response
response = ser.read_all()
if response:
    print(f"Response: {response.decode('utf-8', errors='ignore')}")

# Test SOL2 OFF
print("Sending: SOL2 0")
ser.write(b'SOL2 0\n')
time.sleep(0.1)

# Read response
response = ser.read_all()
if response:
    print(f"Response: {response.decode('utf-8', errors='ignore')}")

ser.write(b't')
time.sleep(0.1)
data = ser.read_all()
ser.close()

print("Test complete!") 