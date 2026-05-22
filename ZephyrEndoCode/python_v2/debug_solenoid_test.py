#!/usr/bin/env python3
"""
Debug solenoid test - check if STM32 is responding
"""

import serial
import time

# Connect to STM32
ser = serial.Serial('/dev/tty.usbmodem1203', 115200, timeout=1)

print("=== DEBUG SOLENOID TEST ===")
print("Checking if STM32 is responding...")
print()

# Clear any existing data
ser.reset_input_buffer()

# Send a command and try to read response
print("Sending SOL5 1 command...")
ser.write(b"SOL5 1\n")

# Try to read any response
time.sleep(0.5)
if ser.in_waiting > 0:
    response = ser.read(ser.in_waiting)
    print(f"Response: {response}")
else:
    print("No response received")

print("Sending SOL5 0 command...")
ser.write(b"SOL5 0\n")

# Try to read any response
time.sleep(0.5)
if ser.in_waiting > 0:
    response = ser.read(ser.in_waiting)
    print(f"Response: {response}")
else:
    print("No response received")

print("Test complete!")
ser.close() 