#!/usr/bin/env python
#
#   Plot fsk_demod statistic outputs.
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import json
import os
import sys
import time
import subprocess
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

_filename = sys.argv[1]

if len(sys.argv)>2:
	_sps = int(sys.argv[2])
	_x_label = "Seconds"
else:
	_sps = 1
	_x_label = "Sample Number"

_ebno = []
_fest1 = []
_fest2 = []
_ppm = []
_time = []


_f = open(_filename,'r')

for _line in _f:

	if _line[0] != '{':
		continue

	try:
		_data = json.loads(_line)
	except Exception as e:
		#print("Line parsing error: %s" % str(e))
		continue

	_ebno.append(_data['EbNodB'])
	_fest1.append(_data['f1_est'])
	_fest2.append(_data['f2_est'])
	_ppm.append(_data['ppm'])

	_time.append(_data['samples'])


_ebno_max = pd.Series(_ebno).rolling(10).max().dropna().tolist()


plt.figure()

plt.plot(_time[:len(_ebno_max)],_ebno_max)
plt.xlabel(_x_label)
plt.ylabel("Eb/N0 (dB)")
plt.title("Eb/N0")

plt.figure()

plt.plot(_time,_fest1, label="f1 est")
plt.plot(_time,_fest2, label="f2 est")
plt.legend()
plt.xlabel(_x_label)
plt.ylabel("Frequency (Hz)")
plt.title("Frequency Estimator Outputs")


plt.figure()
plt.plot(_time,_ppm)
plt.xlabel(_x_label)
plt.ylabel("PPM")
plt.title("Demod PPM Estimate")

plt.show()