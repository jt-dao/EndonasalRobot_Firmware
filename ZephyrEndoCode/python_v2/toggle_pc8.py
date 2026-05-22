#!/usr/bin/env python3
"""
Simple PC8 toggle test
"""

import serial
import time

# Configure serial connection
ser = serial.Serial('/dev/tty.usbmodem1203', 230400, timeout=1)

print("=== PC8 TOGGLE TEST ===")
print("Sending 'toggle' command to toggle PC8...")

# Send toggle command
ser.write(b'toggle\n')
print("Sent: toggle")
time.sleep(0.1)

# Read response
response = ser.read_all()
if response:
    print(f"Response: {response.decode('utf-8', errors='ignore')}")

ser.close()
print("Test complete!") 