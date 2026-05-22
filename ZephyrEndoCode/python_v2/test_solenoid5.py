#!/usr/bin/env python3
"""
Quick test script for Solenoid 5 only
"""

import serial
import time
import sys

def test_solenoid5():
    """Test only solenoid 5"""
    
    # Configure serial port
    try:
        ser = serial.Serial('/dev/tty.usbmodem1203', 115200, timeout=1)  # Mac
    except:
        try:
            ser = serial.Serial('COM3', 115200, timeout=1)  # Windows
        except:
            try:
                ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1)  # Linux
            except:
                print("Error: Could not open serial port")
                sys.exit(1)
    
    print("Testing Solenoid 5 only...")
    print("Waiting for STM32...")
    time.sleep(2)
    
    # Test solenoid 5 with multiple cycles
    for cycle in range(5):
        print(f"Cycle {cycle + 1}:")
        
        # Turn ON
        print("  SOL5 ON")
        ser.write(b"SOL5 1\n")
        time.sleep(1)
        
        # Turn OFF
        print("  SOL5 OFF")
        ser.write(b"SOL5 0\n")
        time.sleep(1)
    
    print("Solenoid 5 test complete!")
    ser.close()

if __name__ == "__main__":
    test_solenoid5() 