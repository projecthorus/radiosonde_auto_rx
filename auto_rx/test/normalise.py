#!/usr/bin/env python
#
#	Normalise a floating point sample to +/- 1.0
#
#	To check a file, run with:
#	python normalise.py filename.bin
#
#	Then, to actually normalise the file, run:
#	python normalise.py filename.bin confirm
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import numpy as np
import sys
import os

def load_sample(filename):
    return np.fromfile(filename, dtype='c8')


def save_sample(data, filename):
    data.astype(dtype='c8').tofile(filename)


_file = sys.argv[1]

try:
	_confirm = sys.argv[2]
	_confirm = True
except:
	_confirm = False

data = load_sample(_file)

_max_val = np.max(np.abs(data))
print("Maximum value: %.4f" % _max_val)

_normalised = data / _max_val

_max_val = np.max(np.abs(_normalised))
print("Maximum value (post-normalization): %.4f" % _max_val)

if _confirm:
	print("Saving normalised file.")
	save_sample(_normalised, _file)