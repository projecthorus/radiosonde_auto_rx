#!/usr/bin/env python
#
# Radiosonde Auto RX Tools
#
# 2017-04 Mark Jessop <vk5qi@rfhead.net>
#
import numpy as np
from StringIO import StringIO
from findpeaks import *
from os import system
import sys

# Sonde Search Configuration Parameters
MIN_FREQ = 400.4e6
MAX_FREQ = 403.5e6
SEARCH_STEP = 500
MIN_FREQ_DISTANCE = 10000  	# Expect a minimum distance of 10 kHz between sondes.
MIN_SNR = 10 				# Only takes peaks that are a minimum of 10dB above the noise floor.


def read_rtl_power(filename):
	''' Read in frequency samples from a single-shot log file produced by rtl_power'''

	# Output buffers.
	freq = np.array([])
	power = np.array([])

	freq_step = 0


	# Open file.
	f = open(filename,'r')

	# rtl_power log files are csv's, with the first 6 fields in each line describing the time and frequency scan parameters
	# for the remaining fields, which contain the power samples. 

	for line in f:
		# Split line into fields.
		fields = line.split(',')

		if len(fields) < 6:
			raise Exception("Invalid number of samples in input file - corrupt?")

		start_date = fields[0]
		start_time = fields[1]
		start_freq = float(fields[2])
		stop_freq = float(fields[3])
		freq_step = float(fields[4])
		n_samples = int(fields[5])

		freq_range = np.arange(start_freq,stop_freq,freq_step)
		samples = np.loadtxt(StringIO(",".join(fields[6:])),delimiter=',')

		# Add frequency range and samples to output buffers.
		freq = np.append(freq, freq_range)
		power = np.append(power, samples)

	f.close()
	return (freq, power, freq_step)



if __name__ == "__main__":
	# Run rtl_power, with a timeout
	# rtl_power -f 400400000:403500000:800 -i20 -1 log_power.csv
	rtl_power_cmd = "timeout 30 rtl_power -f %d:%d:%d -i20 -1 log_power.csv" % (MIN_FREQ, MAX_FREQ, SEARCH_STEP)
	print("Running: %s" % rtl_power_cmd)
	ret_code = system(rtl_power_cmd)
	if ret_code == 1:
		print("rtl_power call failed!")
		sys.exit(1)

	# Read in result
	(freq, power, step) = read_rtl_power('log_power.csv')

	# Rough approximation of the noise floor of the received power spectrum.
	power_nf = np.mean(power)

	# Detect peaks.
	peak_indices = detect_peaks(power, mph=(power_nf+MIN_SNR), mpd=(MIN_FREQ_DISTANCE/step))

	if len(peak_indices) == 0:
		print("No peaks found!")
		sys.exit(1)

	peak_frequencies = freq[peak_indices]


	print("Peaks found at: %s" % str(peak_frequencies/1e6))



