import serial
import time

try:
    s = serial.Serial('/dev/ttyACM0', 9600, timeout=1)
    time.sleep(2) # wait for reset
    end_time = time.time() + 7
    while time.time() < end_time:
        if s.in_waiting > 0:
            line = s.readline().decode('utf-8', 'ignore').strip()
            if line:
                print(line)
except Exception as e:
    print(f"Error: {e}")
