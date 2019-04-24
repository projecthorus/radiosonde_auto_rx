#!/usr/bin/env python
#
#	Plot Radiosonde Sample file.
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import numpy as np
import matplotlib.pyplot as plt
import os.path
import sys
import argparse


parser = argparse.ArgumentParser()
parser.add_argument("-f", "--file", type=str, default="test.bin", help="File to plot.")
parser.add_argument("-t", "--type", type=str, default="c8", help="Type of data in file (c8 = complex64, u8=unsigned 8-bit, c16 = complex signed 16)")
parser.add_argument("-l", "--length", type=int, default=1000000, help="FFT Length (samples)")
parser.add_argument("-s", "--start", type=int, default=0, help="Offset into file (samples)")
parser.add_argument("--rate", type=int, default=-1, help="Optional sample rate, to plot a scale.")
parser.add_argument("--timeseries", action='store_true', default=False, help="Plot a 20log time series instead of a FFT.")
parser.add_argument("--title", type=str, default="", help="Optional plot title.")

args = parser.parse_args()



if args.type == 'c8':
	data = np.fromfile(args.file, dtype='c8')
elif args.type == 'u8':
	_data = np.fromfile(args.file, dtype=np.uint8).astype(np.float32) - 128
	data = _data.view(np.complex64)
elif args.type == 'c16':
	_data = np.fromfile(args.file, dtype=np.int16).astype(np.float32)
	data = _data.view(np.complex64)


if args.timeseries:
	_data_abs = 20*np.log10(np.abs(data[args.start:args.start+args.length]))
	plt.plot(_data_abs)
else:
	data_fft = 20*np.log10(np.fft.fftshift(np.abs(np.fft.fft(data[args.start:args.start+args.length]))))

	scale = np.fft.fftshift(np.fft.fftfreq(len(data_fft)))

	if args.rate != -1:
		scale = scale*args.rate

	plt.plot(scale, data_fft)
	plt.grid()
	plt.ylabel("Uncalibrated Power (dB)")

	if args.rate != -1:
		plt.xlabel("Frequency (Hz)")
	else:
		plt.xlabel("Frequency")

	if args.title == "":
		plt.title(os.path.basename(args.file))
	else:
		plt.title(args.title)

plt.show()