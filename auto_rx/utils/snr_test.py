#!/usr/bin/env python
#
# Demodulator Performance Testing
# Really simple testing of how the demodulators cope with added white noise.
#
# Copy in the relevant demod binaries to this directory.
# Run with: python snr_test.py -f test_file.bin -d RS92
#
# The input test file should be the FM demodulated signal, with the sox post-processing.
# For example:
#     RS92: rtl_fm -p 0 -M fm -F9 -s 12k -f 400500000 | sox -t raw -r 12k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - highpass 20 lowpass 2500 2>/dev/null > test_file.bin
#     RS41: rtl_fm -p 0 -M fm -F9 -s 15k -f 405500000 | sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - lowpass 2600 2>/dev/null > test_file.bin
#
# I'm unsure how comparable the RS92 and RS41 results are. Take the results with a healthy lump of salt.
# At the very least, this should be useful to compare performance of different demod revisions.
#

import sys
import os
import numpy as np
import argparse
import subprocess

# Demodulator calls. Replace as appropriate.
RS92_DEMOD = "./rs92ecc --crc --ecc --vel"
RS41_DEMOD = "./rs41ecc --crc --ecc --ptu"

# Noise level range (gaussian distribution, standard deviation referred to full scale), in dB.
# Both demods seem to fall over by -15 dB of added noise.
NOISE_LEVELS = np.arange(-30.0, -15, 1.0)

TEMP_FILENAME = 'temp.bin'

def read_file(filename):
	''' Read in file and convert to floating point '''
	data = np.fromfile(filename,dtype='u1')
	header = data[:44] # This is a bit of a hack. The RS demods want a wave header, so we store this for later writeout.
	data = (data[44:].astype('float') - 128.0) / 128.0

	return (data,header)


def write_file(filename, data, header):
	'''  Convert an array of floats to uint8 and write to a file '''

	data = (data*128.0)+128.0
	data = data.astype('u1')
	f = open(filename,'wb')
	f.write(header.tobytes())
	f.write(data.tobytes())
	f.close()

def add_noise(data, noise_level):
	''' Add white noise to a file '''
	noise_level_linear = 10**(noise_level/20.0)
	noise = np.random.normal(scale=noise_level_linear, size=data.shape)

	return data + noise

def run_demod(filename, demod='RS92'):
	if demod == 'RS92':
		demod_bin = RS92_DEMOD
	else:
		demod_bin = RS41_DEMOD

	demod_command = "cat %s | %s" % (filename, demod_bin)

	# Run demod.
	with open(os.devnull, 'w') as devnull:
		output = subprocess.check_output(demod_command, shell=True, stderr=devnull)


	if demod == 'RS92':
		# RS92 demod just gives us one line per frame.
		return len(output.split('\n'))
	else:
		# RS41 demod gives us a lot more...
		frames = 0
		for _line in output.split('\n'):
			if _line != '':
				if _line[0] == '[':
					frames += 1
		return frames


if __name__ == '__main__':
	# Command line arguments. 
	parser = argparse.ArgumentParser()
	parser.add_argument("-f", "--filename", type=str, help="Input file. Assumed to be unsigned 8-bit, 48 kHz file.")
	parser.add_argument("-d", "--demod", type=str, help="Demodulator to test, either RS92 or RS41.")
	args = parser.parse_args()

	print("Reading: %s" % args.filename)
	# Read in input file.
	(data,header) = read_file(args.filename)
	print("Samples: %d" % len(data))

	for noise_lvl in NOISE_LEVELS:
		temp_data = add_noise(data, noise_lvl)
		write_file(TEMP_FILENAME, temp_data, header)

		frame_count = run_demod(TEMP_FILENAME, args.demod)
		print("%f dB: Frames Recovered: %d" % (noise_lvl,frame_count))





