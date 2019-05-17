#!/usr/bin/env python
#
#   Calculate the SNR at which the PER of a modem passes a user-defined threshold.
#
#   Copyright (C) 2019  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#

import glob
import argparse
import os.path
import sys
import time
import subprocess
import numpy as np



def read_csv(filename):
	_f = open(filename,'r')

	_snrs = []
	_packets = []


	for _line in _f:

		try:
			_fields = _line.split(',')
			_file = _fields[0]
			_snr = float(_file.split('_')[-1].split('dB.bin')[0])
			_packet_count = int(_fields[1])

			_snrs.append(_snr)
			_packets.append(_packet_count)
		except Exception as e:
			print("Error - %s" % str(e))

	return (np.array(_snrs), np.array(_packets))


def calculate_per_thrshold(snrs, packets, expected_packets, threshold=0.5):

	# Convert packet count to PER.
	_per = 1.0 - packets/float(expected_packets)
	
	# Interpolate the incoming SNR vs packet data out to 0.05 dB steps.
	_snr_range = np.arange(snrs[0], snrs[-1], 0.05)
	_per_interp = np.interp(_snr_range, snrs, _per) - 0.5

	# Find the point the PER crosses below the threshold.
	_threshold_snr = _snr_range[np.where(np.diff(np.sign(_per_interp)))[0]]

	return _threshold_snr




if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("filename", type=str, help="CSV file to parse.")
    parser.add_argument("--packets", type=int, default=119, help="Number of packets expected.")
    parser.add_argument("--threshold", type=float, default=0.5, help="PER Threshold for comparison.")
    args = parser.parse_args()


    (snrs, packets) = read_csv(args.filename)

    _snr = calculate_per_thrshold(snrs, packets, expected_packets=args.packets, threshold=args.threshold)

    print("%s, %.2f" % (os.path.basename(args.filename),_snr))
