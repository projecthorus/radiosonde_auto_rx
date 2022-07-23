#!/usr/bin/env python
#
#   Plot the PER peformance of the auto_rx decode chains
#	as calculated by test_demod.py
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
#	Result CSVs are expected to be located in ./results/
#

import glob
import argparse
import os
import sys
import time
import subprocess
import numpy as np
import matplotlib.pyplot as plt


CSV_DIR = "./results/"

# sonde_types = {
# 	'RS41': {'csv':'rs41_rtlfm.txt', 'packets': 120, 'color': 'C0'},
# 	'RS92': {'csv':'rs92_rtlfm.txt', 'packets': 120, 'color': 'C1'},
# 	'DFM09': {'csv':'dfm_rtlfm.txt', 'packets': 96, 'color': 'C2'},
# 	'M10': {'csv':'m10_rtlfm.txt', 'packets': 120, 'color': 'C3'},
# }


# soft-decision decoding, with a +0.25 Fs signal centre.
# sonde_types = {
# 	'RS41': {'csv':'rs41_fsk_demod_soft.txt', 'packets': 118, 'color': 'C0'},
# 	'RS92': {'csv':'rs92_fsk_demod_soft.txt', 'packets': 120, 'color': 'C1'},
# 	'RS92-NGP': {'csv':'rs92ngp_fsk_demod_soft.txt', 'packets': 120, 'color': 'C2'},
# 	'DFM09': {'csv':'dfm_fsk_demod_soft.txt', 'packets': 96, 'color': 'C3'},
# 	'M10': {'csv':'m10_fsk_demod_soft.txt', 'packets': 120, 'color': 'C4'},
# 	'LMS6-400': {'csv':'lms6-400_fsk_demod_soft.txt', 'packets': 120, 'color': 'C5'},
# 	'MRZ': {'csv':'mrz_fsk_demod_soft.txt', 'packets': 105, 'color': 'C6'},
# 	'iMet-54': {'csv':'imet54_fsk_demod_soft.txt', 'packets': 240, 'color': 'C7'},
# }

# soft-decision decoding, with a 0Hz signal centre.
sonde_types = {
	'RS41': {'csv':'rs41_fsk_demod_soft_centre.txt', 'packets': 118, 'color': 'C0'},
	'RS92': {'csv':'rs92_fsk_demod_soft_centre.txt', 'packets': 120, 'color': 'C1'},
	'RS92-NGP': {'csv':'rs92ngp_fsk_demod_soft_centre.txt', 'packets': 120, 'color': 'C2'},
	'DFM09': {'csv':'dfm_fsk_demod_soft_centre.txt', 'packets': 96, 'color': 'C3'},
	'M10': {'csv':'m10_fsk_demod_soft_centre.txt', 'packets': 120, 'color': 'C4'},
	'LMS6-400': {'csv':'lms6-400_fsk_demod_soft_centre.txt', 'packets': 120, 'color': 'C5'},
	'MRZ': {'csv':'mrz_fsk_demod_soft_centre.txt', 'packets': 105, 'color': 'C6'},
	'iMet-54': {'csv':'imet54_fsk_demod_soft_centre.txt', 'packets': 240, 'color': 'C7'},
	'M20': {'csv':'m20_fsk_demod_soft_centre.txt', 'packets': 120, 'color': 'C8'},
	'iMet-4': {'csv':'imet4_iq.txt', 'packets': 218, 'color': 'C9'},
}


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


# Sort list of sonde types.
_types = list(sonde_types.keys())
_types.sort()


plt.figure(figsize=(10,5))

for _type in _types:

	_filename = os.path.join(CSV_DIR, sonde_types[_type]['csv'])

	(_snr, _packets) = read_csv(_filename)

	_per = 1.0 - _packets/float(np.max(_packets))

	plt.plot(_snr, _per, color=sonde_types[_type]['color'], label=_type)

plt.legend()
plt.grid()
plt.ylabel("Packet Error Rate")
plt.xlabel("Eb/No (dB)")
plt.title("auto_rx Decode Chain Performance - fsk_demod soft-decision 0 Hz")
plt.show()