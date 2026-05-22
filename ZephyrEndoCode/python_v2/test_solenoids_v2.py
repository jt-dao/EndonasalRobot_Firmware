#!/usr/bin/env python3
"""
Test script for V2 solenoid valves
Tests all 8 solenoids individually and in sequence
"""

import serial
import time
import sys

def test_solenoids():
    """Test all 8 solenoids"""
    
    # Configure serial port - adjust as needed
    try:
        ser = serial.Serial('/dev/tty.usbmodem1203', 230400, timeout=1)  # Mac - detected port
    except:
        try:
            ser = serial.Serial('COM3', 230400, timeout=1)  # Windows
        except:
            try:
                ser = serial.Serial('/dev/ttyACM0', 230400, timeout=1)  # Linux
            except:
                print("Error: Could not open serial port")
                print("Available ports:")
                import glob
                ports = glob.glob('/dev/tty.*') + glob.glob('COM*')
                for port in ports:
                    print(f"  {port}")
                print("Please check your COM port and try again")
                sys.exit(1)
    
    print("V2 Solenoid Test Starting...")
    print("Waiting for STM32 to be ready...")
    
    # Wait for STM32 to be ready
    time.sleep(2)
    
    # Test individual solenoids
    print("\nTesting individual solenoids...")
    for i in range(1, 9):
        print(f"Testing Solenoid {i}...")
        
        # Turn on
        command = f"SOL{i} 1\n".encode()
        ser.write(command)
        time.sleep(0.5)
        
        # Turn off
        command = f"SOL{i} 0\n".encode()
        ser.write(command)
        time.sleep(0.5)
    
    # Test all solenoids on
    print("\nTesting all solenoids ON...")
    for i in range(1, 9):
        command = f"SOL{i} 1\n".encode()
        ser.write(command)
    time.sleep(2)
    
    # Test all solenoids off
    print("Testing all solenoids OFF...")
    for i in range(1, 9):
        command = f"SOL{i} 0\n".encode()
        ser.write(command)
    time.sleep(1)
    
    # Test alternating pattern
    print("\nTesting alternating pattern...")
    for cycle in range(3):
        print(f"Cycle {cycle + 1}")
        
        # Even solenoids on
        for i in [2, 4, 6, 8]:
            command = f"SOL{i} 1\n".encode()
            ser.write(command)
        time.sleep(1)
        
        # All off
        for i in range(1, 9):
            command = f"SOL{i} 0\n".encode()
            ser.write(command)
        time.sleep(0.5)
        
        # Odd solenoids on
        for i in [1, 3, 5, 7]:
            command = f"SOL{i} 1\n".encode()
            ser.write(command)
        time.sleep(1)
        
        # All off
        for i in range(1, 9):
            command = f"SOL{i} 0\n".encode()
            ser.write(command)
        time.sleep(0.5)
    
    print("\nSolenoid test complete!")
    ser.close()

if __name__ == "__main__":
    test_solenoids() 