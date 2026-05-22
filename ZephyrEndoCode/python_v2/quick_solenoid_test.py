#!/usr/bin/env python3
"""
Quick solenoid test with longer delays for observation
"""

import serial
import time
import find_port

# Configure serial connection

ser = serial.Serial(find_port.find_stm32_port(), baudrate=230400)
print("=== SOLENOID 2 (PC8) TEST - 10 CYCLES ===")
print("Watch for solenoid movement or listen for clicks!")

# Test solenoid 2 (PC8) for 10 cycles
for cycle in range(1, 11):
    print(f"Cycle {cycle}/10:")
    
    # Turn solenoid 2 ON
    ser.write(b'SOL2 1\n')
    print("  Turning SOL2 ON...")
    time.sleep(0.5)
    
    # Turn solenoid 2 OFF
    ser.write(b'SOL2 0\n')
    print("  Turning SOL2 OFF...")
    time.sleep(0.5)

print("Test complete! 10 cycles finished.")
ser.close() 