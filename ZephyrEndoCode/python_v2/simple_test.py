#!/usr/bin/env python3
"""
Simple test to verify serial communication
"""

import serial
import time

# Configure serial connection
ser = serial.Serial('/dev/tty.usbmodem1203', 230400, timeout=1)

print("=== SIMPLE SERIAL TEST ===")
print("Sending TIME command to check communication...")

# Send TIME command to verify communication
ser.write(b'TIME\n')
time.sleep(0.1)

# Read any response
response = ser.read_all()
if response:
    print(f"Response: {response.decode('utf-8', errors='ignore')}")
else:
    print("No response received")

print("\nSending SOL2 commands...")

# Test solenoid 2 once
ser.write(b'SOL2 1\n')
print("Sent: SOL2 1")
time.sleep(0.1)

# Read any debug output
response = ser.read_all()
if response:
    print(f"Debug output: {response.decode('utf-8', errors='ignore')}")

ser.write(b'SOL2 0\n')
print("Sent: SOL2 0")
time.sleep(0.1)

# Read any debug output
response = ser.read_all()
if response:
    print(f"Debug output: {response.decode('utf-8', errors='ignore')}")

ser.close()
print("Test complete!") 