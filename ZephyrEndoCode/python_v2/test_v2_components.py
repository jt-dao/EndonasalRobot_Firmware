#!/usr/bin/env python3
"""
Comprehensive test script for V2 components
Tests solenoids, stepper motors, and ADC
"""

import serial
import serial.tools.list_ports
import sys
import find_port
import time

def test_solenoids_v2(ser):
    """Test V2 solenoid valves"""
    print("\n=== Testing V2 Solenoids ===")
    
    # Test individual solenoids
    for i in range(1, 9):
        print(f"Testing Solenoid {i}...")
        
        # Turn on
        command = f"SOL{i} 1\n".encode()
        ser.write(command)
        time.sleep(0.3)
        
        # Turn off
        command = f"SOL{i} 0\n".encode()
        ser.write(command)
        time.sleep(0.3)
    
    # Test all on/off
    print("Testing all solenoids ON...")
    for i in range(1, 9):
        command = f"SOL{i} 1\n".encode()
        ser.write(command)
    time.sleep(1)
    
    print("Testing all solenoids OFF...")
    for i in range(1, 9):
        command = f"SOL{i} 0\n".encode()
        ser.write(command)
    time.sleep(0.5)

def test_steppers_v2(ser):
    """Test V2 stepper motors"""
    print("\n=== Testing V2 Stepper Motors ===")
    
    # Test each stepper motor
    for i in range(1, 6):
        print(f"Testing Stepper Motor {i}...")
        
        # Forward motion
        command = f"PFRQ{i} 100\n".encode()
        ser.write(command)
        time.sleep(1)
        
        # Stop
        command = f"PFRQ{i} 0\n".encode()
        ser.write(command)
        time.sleep(0.5)
        
        # Reverse motion
        command = f"PFRQ{i} -100\n".encode()
        ser.write(command)
        time.sleep(1)
        
        # Stop
        command = f"PFRQ{i} 0\n".encode()
        ser.write(command)
        time.sleep(0.5)
    
    # Test multiple steppers simultaneously
    print("Testing multiple steppers...")
    for i in range(1, 6):
        command = f"PFRQ{i} 50\n".encode()
        ser.write(command)
    time.sleep(2)
    
    # Stop all
    for i in range(1, 6):
        command = f"PFRQ{i} 0\n".encode()
        ser.write(command)

def test_adc_v2(ser):
    """Test V2 I2C ADC"""
    print("\n=== Testing V2 I2C ADC ===")
    
    # Read ADC channels (if implemented)
    for i in range(4):
        print(f"Reading ADC Channel {i}...")
        command = f"IADC {i}\n".encode()
        ser.write(command)
        time.sleep(0.1)

def test_sequence_v2(ser):
    """Test coordinated sequence"""
    print("\n=== Testing Coordinated Sequence ===")
    
    # Sequence: solenoids on, steppers move, solenoids off
    print("Starting coordinated sequence...")
    
    # 1. Turn on solenoids 1, 3, 5, 7
    for i in [1, 3, 5, 7]:
        command = f"SOL{i} 1\n".encode()
        ser.write(command)
    time.sleep(0.5)
    
    # 2. Move steppers 1, 3, 5
    for i in [1, 3, 5]:
        command = f"PFRQ{i} 75\n".encode()
        ser.write(command)
    time.sleep(1)
    
    # 3. Stop steppers
    for i in [1, 3, 5]:
        command = f"PFRQ{i} 0\n".encode()
        ser.write(command)
    time.sleep(0.5)
    
    # 4. Turn off solenoids
    for i in [1, 3, 5, 7]:
        command = f"SOL{i} 0\n".encode()
        ser.write(command)
    time.sleep(0.5)
    
    # 5. Turn on solenoids 2, 4, 6, 8
    for i in [2, 4, 6, 8]:
        command = f"SOL{i} 1\n".encode()
        ser.write(command)
    time.sleep(0.5)
    
    # 6. Move steppers 2, 4
    for i in [2, 4]:
        command = f"PFRQ{i} -75\n".encode()
        ser.write(command)
    time.sleep(1)
    
    # 7. Stop steppers
    for i in [2, 4]:
        command = f"PFRQ{i} 0\n".encode()
        ser.write(command)
    time.sleep(0.5)
    
    # 8. Turn off all solenoids
    for i in range(1, 9):
        command = f"SOL{i} 0\n".encode()
        ser.write(command)
        

def main():
    """Main test function"""
    
    # Configure serial port
    ser = serial.Serial(find_port.find_stm32_port(), baudrate=230400)
    
    print("V2 Component Test Starting...")
    print("Waiting for STM32 to be ready...")
    
    # Wait for STM32 to be ready
    time.sleep(2)
    
    try:
        # Test each component
        test_solenoids_v2(ser)
        test_steppers_v2(ser)
        test_adc_v2(ser)
        test_sequence_v2(ser)
        
        print("\n=== All V2 Component Tests Complete ===")
        
    except KeyboardInterrupt:
        print("\nTest interrupted by user")
        
        # Emergency stop
        print("Performing emergency stop...")
        ser.write(b"ESTOP\n")
        
    finally:
        # Clean up
        ser.close()
        print("Serial connection closed")

if __name__ == "__main__":
    main() 