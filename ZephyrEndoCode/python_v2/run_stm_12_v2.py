import numpy as np
import serial
import time
from time import sleep
import csv
import sys
import threading
from queue import Queue
from queue import LifoQueue
import queue
import pickle

is_camera_available = False
try:
    import cv2
    import cv2.aruco as aruco
    import utils_aruco
    aruco_detector = utils_aruco.ArucoDetector(camera_id=1)
    if aruco_detector.cap.isOpened():
        is_camera_available = True
    assert is_camera_available
except:
    pass

try:
    import utils_force_sensor
except ImportError:
    utils_force_sensor = None


sendQ = Queue()
stateQ = LifoQueue()  # keeping track of remote state
# always use newest data
start_time = time.time()

SystemCoreClock = 16000000
PWM_FREQUENCY = 2000
PWM_PERIOD = ((SystemCoreClock / PWM_FREQUENCY) - 1)*6

from serial.tools import list_ports
# list_ports.comports()  # Outputs list of available serial ports
print('ports being used:')
print([port.device for port in serial.tools.list_ports.comports()])
for port in serial.tools.list_ports.comports():
    print(f'{port.device}: {port.description}')
#### CONSTANTS ###########
data_file_name = '../Data/data.txt'
telemetry = False
numSamples = 20 # 1 kHz sampling in pid loop = 3 sec
INTERVAL = 0.1  # update rate for state information


rcvStop = threading.Event()
rcvStop.clear()
sendStop = threading.Event()
sendStop.clear()
controlStop = threading.Event()
controlStop.clear()
cameraStop = threading.Event()
cameraStop.clear()
forceStop = threading.Event()
forceStop.clear()
charStart = threading.Event()
charStart.clear()
charStop = threading.Event()
charStop.clear()

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
    all_ports = [p.device for p in serial.tools.list_ports.comports()]
    if not all_ports:
        raise RuntimeError('No serial ports found. Check USB connection.')
    print('# No STM32 port auto-detected. Available ports:')
    for i, p in enumerate(all_ports):
        print(f'#   [{i}] {p}')
    idx = int(input('# Select port index: '))
    return all_ports[idx]

ser = serial.Serial(find_stm32_port(), baudrate=230400)

class StateStruct():
    def __init__(self):
        self.time = 0.0
        self.hx711 = 0  # load cell
        self.qdec3 = 0  # quad decoder using timer 3
        self.qdec5 = 0  # quadrature decoder using timer 5
        self.p_0 = 0   # I2C ADC ch0-7  — ADS7830 @ 0x48 (0-255, 5V ref)
        self.p_1 = 0
        self.p_2 = 0
        self.p_3 = 0
        self.p_4 = 0
        self.p_5 = 0
        self.p_6 = 0
        self.p_7 = 0
        self.p_8 = 0   # I2C ADC ch8-15 — ADS7830 @ 0x49
        self.p_9 = 0
        self.p_10 = 0
        self.p_11 = 0
        self.p_12 = 0
        self.p_13 = 0
        self.p_14 = 0
        self.p_15 = 0


def generate_pattern1(start, end, inc):
    # Generate the list starting from start, incrementing by inc to end,
    # then decrementing by inc to start
    result = list(np.arange(start, end + inc, inc)) + list(np.arange(end - inc, start, -inc))
    return np.array(result)

def generate_pattern2(start, end, inc1, inc2):
    result = np.array([], dtype=float)
    for current_val in np.arange(start+inc1, end+inc1, inc1):
        result = np.concatenate((result, generate_pattern1(start, current_val, inc2)))
    return result

def convertADC(adc):
    return adc/4095*3.3

def convertI2CADC(raw):
    """ADS7830: 8-bit (0-255), 5V reference"""
    return raw / 255.0 * 5.0

def convertGagePressureSmall(voltage):
    """Map ADS7830 voltage to pressure using the lab-demo calibration.

    The current new-PCB calibration is 0.5..4.5 V -> 0..60 PSI.
    """
    psi = (voltage - 0.5) / (4.5 - 0.5) * 60.0
    return np.clip(psi, 0.0, 60.0)

def setDisplacement(displacement, maxDis):
    pressure = displacement/maxDis*10+5
    return pressure

def pressureToPWM(pressure):
    desired_voltage = pressure/67*5
    PWM = desired_voltage/3.3*PWM_PERIOD
    return PWM

def voltageToDAC(voltage):
    """Convert voltage (0-5V) to 16-bit DAC value (0-65535).
    AD5679R with 2.5V internal ref, 2x gain -> 0-5V output range."""
    return np.clip(voltage / 5.0 * 65535, 0, 65535).astype(int)

def makeSolenoidCmdString(s_vals):
    message_arr = []
    for i, val in enumerate(s_vals):
        message_arr.append(makeCmdString('SOL%d' % (i+1), int(val)))
    return message_arr

def makePressureCmdString(regulator_vals):
    dac_vals = voltageToDAC(regulator_vals)
    message_arr = []
    message = b''
    for i, dv in enumerate(dac_vals):
        message += makeCmdString('DAC{}'.format(i+1), dv)
        if (i+1)%4 == 0:
            message_arr.append(message)
            message = b''
            # print('msg', message)
            # print('arr', message_arr)
    if message:
        message_arr.append(message)
    return message_arr

def makePressureCmd():
    global regulator_vals, solenoid_vals
    # print('regular', regulator_vals)
    message_arr = makePressureCmdString(regulator_vals)
    for message in message_arr:
        sendQ.put(message+b'\n')
        time.sleep(0.05)
    if not (solenoid_vals is None):
        message_arr = makeSolenoidCmdString(solenoid_vals)
        for message in message_arr:
            sendQ.put(message+b'\n')
            time.sleep(0.05)

        #print(message)

offset = 0
def processLine(textLine,index):
    global offset
    newState = StateStruct()    
    #  "t=","hx711","qdec3","qdec5", "p_0"..."p_15"
    data = textLine.split(',')  # split on comma
    # print('data ', data)
    # print('textline ', textLine)
   # split first element on space delimiter to find #, which indicates a comment
    firstChar = data[0].split()
    if (firstChar[0] == '#') or (firstChar[0] == '***'):
            print(textLine)
             # check here for # STM32READY before enabling control_loop
            if(firstChar[1] in ('STM32READY', 'parse_cmd')):  
                controlStop.clear()   # only start control loop if get STM32READY message OR we send invalid command to recieve parse_cmd
    else:
        # print(textLine)
        temp=np.zeros(np.size(data))
        for i in range(0,np.size(data)):
            temp[i] = float(data[i])
        
        newState.time = temp[0]
        newState.hx711 = temp[1]  
        newState.qdec3 = temp[2]  
        newState.qdec5 = temp[3]  
        newState.p_0 = temp[4]
        newState.p_1 = temp[5]
        newState.p_2 = temp[6]
        newState.p_3 = temp[7]
        newState.p_4 = temp[8]
        newState.p_5 = temp[9]
        newState.p_6 = temp[10]
        newState.p_7 = temp[11]
        newState.p_8 = temp[12]
        newState.p_9 = temp[13]
        newState.p_10 = temp[14]
        newState.p_11 = temp[15]
        newState.p_12 = temp[16]
        newState.p_13 = temp[17]
        newState.p_14 = temp[18]
        newState.p_15 = temp[19]

        stateQ.put(newState)

        if newState.time == 0:
            offset = time.time()-t0

# thread receive state message from USB (STM32)
def rcvstate():
    print('Started rcvstate thread')
    writeFileHeader(data_file_name)
    fileout = open(data_file_name,'a') # append data to file
    if ser.isOpen():
        print("Serial open. Using port %s and baudrate %s" % (ser.name, ser.baudrate))
    else:
        print('serial open failed. Exiting.')
        exit()
   
    ser.reset_input_buffer() # get rid of accumulated inputs
    ser.flush()  # get rid of any extra outputs
    line = ser.readline()  # throw out first line read which is *** Booting Zephyr OS ***
    index = 0
    while not rcvStop.is_set():
        line = ser.readline()
        #print("raw line = %s" %(line))
        line = line.decode('ascii')   # read one \n terminated line, convert to string
        line = line.replace("\n","")  # replace extra line feed (leave \r in place)
 #       print('rcv index:%d. StateQ size %d\t%s' %(index,stateQ.qsize(),line))
        fileout.write(str(line))
        processLine(line, index)  # convert text to state values, and place in stateQ
#        sleep(INTERVAL)   #don't delay, otherwise serial will be out of sync with control thread
        index +=1
        time.sleep(0.001)  # give up thread for other threads to run
    print("rcvstate: Closing file")
    fileout.close()
    print("rcvstate: Closing serial in 2 seconds")
    time.sleep(2)
    ser.close() # should be in receive thread so it closes after reading whole line
    print('rcvstate: finished thread')

# thread for sending commands over USB to STM32
# use queue so can send commands from multiple sources
def sendCmd():
   i=0
   print('sendCMD: started thread') 
   while not sendQ.empty():
       message = sendQ.get()  # flush any initial message command queue
   while not sendStop.is_set():
       time.sleep(0.001)   # give other threads time to run
       if not sendQ.empty():
       # get message if any from command queue
           message = sendQ.get()
#           print('sendCmd %d: message=%s' % (i, message))
           ser.write(message)   # send text to STM32, format is command word in text followed by short
           i=i+1 
   print('sendCmd: finished thread')

def makeCmd(command, value):
    textval = "{0}".format("%-6d" % value)  # convert command value to string, left justify
    messageString = command.encode('utf8') + b' '+ textval.encode('utf8') +b'\n'
    sendQ.put(messageString)
    
def makeCmdString(command, value):
    textval = "{0}".format("%-6d" % value)  # convert command value to string, left justify
    messageString = command.encode('utf8') + b' '+ textval.encode('utf8')
    return(messageString)
    
def camera_thread(q_output):
    if not is_camera_available:
        print("Camera is not available.")
        return
    
    while not cameraStop.is_set():
        if charStart.is_set():
            corners, ids = aruco_detector.detect_frame()
            dumpQ(q_output, 'camera', 'time', time.time() - t0, time.time() - t0,)
        time.sleep(0.01)
    aruco_detector.terminate()
    print('camera saved')

def force_thread(force_sensor, q_output):
    while not forceStop.is_set():
        if charStart.is_set():
            f = force_sensor.read()
            dumpQ(q_output, 'force_sensor', 'force', f, time.time()-t0)
        time.sleep(0.05)
    print('force exited')
def dumpQ(q, component, name, value, time):
    q.put((component,name,value,time))

def input_thread(q_output):
    global regulator_vals,solenoid_vals, start_characterization
    while True:
        try:
            input_values = input("Enter values for regulators, solenoids, or motors: ")
            val = input_values.split()
            if not val:
                continue
            type = str(val[0])
            numericValues = [float(val) for val in input_values.split()[1:]]
            numericValues = np.array(numericValues)
            if type == 's':
                if len(numericValues) == N_SOLENOID:
                    solenoid_vals = np.copy(numericValues)
                elif len(numericValues) == 2:
                    solenoid_vals[int(numericValues[0])-1] = numericValues[1]
                print(solenoid_vals)
                makePressureCmd()
            elif type == 'r':
                max_voltage = 5.0
                if len(numericValues) == N_REGULATOR:
                    is_within_range = np.all((numericValues >= 0) & (numericValues <= max_voltage))
                    if is_within_range:
                        regulator_vals = np.copy(numericValues)
                elif len(numericValues) == 2:
                    channel = int(numericValues[0]) - 1
                    if 0 <= channel < N_REGULATOR:
                        if numericValues[1] >= 0 and numericValues[1] <= max_voltage:
                            regulator_vals[channel] = numericValues[1]
                print(regulator_vals)
                makePressureCmd()
                for i, val in enumerate(regulator_vals):
                    dumpQ(q_output, 'regulator', 'DAC{}'.format(i+1), val, time.time()-t0)
            elif type == 'dac':
                makeCmd('DAC%d' % int(numericValues[0]), int(numericValues[1]))
                print('DAC%d -> %d' % (int(numericValues[0]), int(numericValues[1])))
            elif type == 'neo':
                makeCmd('NEOPIX', int(numericValues[0]))
                print('NEOPIX mode -> %d' % int(numericValues[0]))
            elif type == 'm':
                if len(numericValues) == 2:
                    motor = int(numericValues[0])
                    mode = int(numericValues[1])
                    if 1 <= motor <= 5 and 0 <= mode <= 2:
                        # firmware decodes value as motor*10 + mode
                        makeCmd('M', motor * 10 + mode)
                        print('M%d mode -> %d' % (motor, mode))
                    else:
                        print('motor must be 1-5, mode 0=off/1=rotate/2=cont')
                else:
                    print('usage: m <motor 1-5> <0=off|1=rotate|2=cont>')
            elif type == 'estop':
                makeCmd('ESTOP', 0)
                regulator_vals[:] = 0
                solenoid_vals[:] = 0
                print('ESTOP sent')
            elif type == 'prnwait':
                makeCmd('PRNWAIT', int(numericValues[0]))
            elif type == 'p':
                # p              -> all 16, with a break between the two ADC chips
                # p <chip>       -> all 8 sensors of that chip (chip 1 or 2)
                # p <chip> <sen> -> one sensor (chip 1-2, sensor 1-8); both 1-indexed
                if not stateQ.empty():
                    state = stateQ.get()
                    print('t=%.3f  hx711=%d  qdec3=%d  qdec5=%d'
                          % (state.time, state.hx711, state.qdec3, state.qdec5))

                    def show(flat):
                        raw = getattr(state, 'p_%d' % flat)
                        volts = convertI2CADC(raw)
                        psi = convertGagePressureSmall(volts)
                        psi_str = '%.2f' % psi if psi is not None else 'N/A'
                        print('  ch%d s%d (p_%-2d): raw=%3d  volts=%.3f  psi=%s'
                              % (flat // 8 + 1, flat % 8 + 1, flat, raw, volts, psi_str))

                    if len(numericValues) >= 2:
                        chip, sensor = int(numericValues[0]), int(numericValues[1])
                        if chip in (1, 2) and 1 <= sensor <= 8:
                            show((chip - 1) * 8 + (sensor - 1))
                        else:
                            print('usage: p <chip 1-2> <sensor 1-8>')
                    elif len(numericValues) == 1:
                        chip = int(numericValues[0])
                        if chip in (1, 2):
                            print('--- ADC channel %d ---' % chip)
                            for s in range(8):
                                show((chip - 1) * 8 + s)
                        else:
                            print('usage: p <chip 1-2> [sensor 1-8]')
                    else:
                        for chip in (1, 2):
                            print('--- ADC channel %d ---' % chip)
                            for s in range(8):
                                show((chip - 1) * 8 + s)
                else:
                    print('stateQ empty — no telemetry yet. Try: prnwait 100')
            elif type == 'start':
                charStart.set()
            else:
                print("Invalid number or type of values")

        except ValueError:
            print("Invalid input. Please enter numeric values.")

N_REGULATOR = 16
N_SOLENOID = 8
regulator_vals = np.array([0.] * N_REGULATOR)
solenoid_vals = np.array([0.] * N_SOLENOID)
start_characterization = 0


    
def writeFileHeader(dataFileName):
    fileout = open(dataFileName,'w')
    #write out parameters in format which can be imported to Excel
    today = time.localtime()
    date = str(today.tm_year)+'/'+str(today.tm_mon)+'/'+str(today.tm_mday)+'  '
    date = date + str(today.tm_hour) +':' + str(today.tm_min)+':'+str(today.tm_sec)
    fileout.write('"Data file recorded ' + date + '"\n')
    fileout.write('" time  hx711, qdec3, qdec5, p_0, p_1, p_2, p_3, p_4, p_5, p_6, p_7,'
                  ' p_8, p_9, p_10, p_11, p_12, p_13, p_14, p_15"\n')
    fileout.close()

# debug version- debugger has trouble with threads
def main_test():
    print("Data Logging for STM32, with USB connection- test threads\n")
    rcvStop.clear()
    rcvstate()   # run directly for debugging outside thread
    
def main(control_loop, q_output, result_folder, use_force=False):
    print("Data Logging for STM32, with USB connection\n")
    stateThread = threading.Thread(group=None, target=rcvstate, name="stateThread")
    stateThread.daemon = False  # want clean file close
    rcvStop.clear()
    stateThread.start()
    time.sleep(5)  # give time to catch up with printing

# =============================================================================
#   be ready to send commands when control thread starts  
    sendThread =threading.Thread(group=None, target=sendCmd, name="sendThread")
    sendThread.daemon = False  # want clean file close
    sendStop.clear()
    sendThread.start()
    time.sleep(2) # give time before control thread starts
    
    # handshake: set controlStop and send "foo" until we see a "# foo ..." or "# STM32READY" line
    def _wait_cleared(evt, timeout):
        end = time.time() + timeout
        while time.time() < end:
            if not evt.is_set():
                return True
            time.sleep(0.01)
        return False

    print('Sending "foo" and waiting for "# parse_cmd ..." or "# STM32READY"')
    controlStop.set()  # make sure the control thread waits until handshake completes
    max_attempts = 5
    for attempt in range(max_attempts):
        sendQ.put(b'foo\n')
        if _wait_cleared(controlStop, 2.0):
            print('Received response on attempt', attempt+1)
            break
        else:
            print('No response received, retry', attempt+1)
    else:
        print('No "# parse_cmd" or "# STM32READY" response after retries')
    #

# =============================================================================
    controlStop.set()  # only start control loop if get STM32READY message
    if control_loop is not None:
        controlThread =threading.Thread(group=None, target=control_loop, args=(q_output,result_folder), name="controlThread")
        controlThread.daemon = False  # want clean file close
        # controlStop.clear()   
        # for debugging, start control_loop() outside thread
        # control_loop()
        controlThread.start()
    else:
        controlThread = None
        makeCmd('PRNWAIT', 100)   # set telemetry rate to 100ms for interactive mode
        print("# No control_loop provided — interactive mode. Use input prompt to send commands.")
        print("  Type 'p' to read pressure/ADC, 'prnwait <ms>' to change telemetry rate.")
# =============================================================================    
    if is_camera_available:
        cameraThread = threading.Thread(group=None, target=camera_thread, name="cameraThread", args=(q_output,))
        cameraStop.clear()
        cameraThread.start()
# =============================================================================
    if use_force and utils_force_sensor is not None:
        force_sensor = utils_force_sensor.ForceSensor('COM4', 115200)
        forceThread = threading.Thread(group=None, target=force_thread, name="forceThread", args=(force_sensor, q_output))
        forceStop.clear()
        forceThread.start()

# =============================================================================
    userThread = threading.Thread(target=input_thread, args=(q_output,), daemon=True)
    userThread.start()

    print('Threads started. ctrl C to quit')
    # print(threading.enumerate())
    # Loop infinitely waiting for commands or until the user types quit or ctrl-c
    while True:
         #### if keyboard input is needed, it should be in thread to avoid blocking
        # Read keyboard input from the user
# =============================================================================
#         if (sys.version_info > (3, 0)):
#             message = input('')  # Python 3 compatibility
#         else:
#             message = raw_input('')  # Python 2 compatibility
# #        print('input message=%s' %(message))
#         # If user types quit then lets exit and close the socket
#         if 'quit' in message:
#             print("begin quit")
#             rcvStop.set()  # set stop variable
#             break
#         else:
#             sendQ.put(message.encode('utf8')+b'\n\r')
# =============================================================================
        time.sleep(0.5)  # give threads time to run
    print('Quit keyboard input loop')
    
  
    print("End of main. Closing threads")
    sendStop.set()
    rcvStop.set()  # set stop variable for thread
    controlStop.set()
    sleep(1.0) # wait for threads to close
    stateThread.join()   # wait for termination of state thread    
    sendThread.join()
    if controlThread is not None:
        controlThread.join()
#    exit()      
#    sys.exit()
    
t0 = time.time()
#Provide a try-except over the whole main function
# for clean exit. 
def try_main(*args, **kwargs):
    try:
        main(*args, **kwargs)
    except KeyboardInterrupt as e:
        print('Keyboard Interrupt!')
        sendStop.set()
        rcvStop.set()  # set stop variable for thread
        cameraStop.set()
        forceStop.set()
        controlStop.set()
        sleep(3.0) # wait for threads to close
        ser.close()
    except OSError as error:
        print(error)     # the exception instance
        print(error.args)      # arguments stored in .args
        print("IO Error.")
        ser.close()
    finally:
        ser.close()
        print("normal exit")
        # should also close file
 #       exit()
if __name__ == '__main__':
    q_output = queue.Queue()
    try_main(None, q_output, None)
    # q_output_list = []
    # while not q_output.empty():
        # q_output_list.append(q_output.get())

    # with open("queue.pickle", "wb") as f:
        # pickle.dump(q_output_list, f)
        
