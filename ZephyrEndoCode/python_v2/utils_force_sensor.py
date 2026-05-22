import serial
import time
import argparse
import pickle
class ForceSensor:
	def __init__(self, port, baud_rate):
		self.port = port  
		self.baud_rate = baud_rate
		self.ser = serial.Serial(port, baud_rate, timeout=1)
	
	def read(self):
		# self.ser.flushInput()
		# self.ser.flushOutput()
		# flag = False
		while True:
			try:
				self.ser.write(b'?\r')
				time.sleep(0.03)
				if self.ser.in_waiting > 0:
					data = self.ser.readline().decode().rstrip()  # Read a line of data and remove any trailing characters
					try:
						value = float(data)
						return value
					except:
						pass
						# self.ser.write(b'?\r')
			except ValueError:
				print("Error: Invalid data received")
			except serial.SerialException as e:
				print("Serial Port Error:", str(e))
					
		# return float(data)
	
if __name__ == '__main__':
	result_folder = './data-raw/force/'
	parser = argparse.ArgumentParser()
	parser.add_argument('--output_file', type=str, default='test', help='Name of the output pickle file')
	parser.add_argument('--comment', default="")

	args = parser.parse_args()

	sensor = ForceSensor('COM5', 115200)
	
	import os
	import datetime
	file_name = args.output_file + datetime.datetime.now().strftime('_%Y%m%d_%H-%M-%S')
	file_name += "_{}".format(args.comment) if args.comment != "" else ""
	output_filename = file_name + '.pkl'

	output_filename = os.path.join(result_folder, output_filename)
	data = []

	start_time = time.time()
	try:
		while True:
			current_time = time.time() - start_time
			sensor_value = sensor.read()
			print(sensor_value)
			# Append timestamp and sensor value to the data array
			data.append([current_time, sensor_value])
			# Wait for a short interval
			time.sleep(0.05)  # Adjust as needed
	except KeyboardInterrupt:
		# Save data to a pickle file
		with open(output_filename, 'wb') as f:
			pickle.dump(data, f)
	
		print("Data saved to:", output_filename)
