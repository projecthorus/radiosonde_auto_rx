import time
import serial

# the SDR devices are attached to USB hub, with power routed though
# switches that are disabled when serial device is opened. So this script
# basically removes power from RTL-SDR devices for 3 seconds, then
# restores the power and waits for 3 more seconds for the USB subsystem to
# find them again

p=serial.Serial("/dev/tty.usbserial-A5XK3RJT",9600)
time.sleep(3)
p.close()
time.sleep(3)
