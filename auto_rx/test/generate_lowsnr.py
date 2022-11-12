#!/usr/bin/env python
#
#   Generate Noisy Sonde Samples, with a calibrated Eb/No
#
#   Run from ./scripts/ with
#   $ python generate_lowsnr.py
#
#   The generated files will end up in the 'generated' directory.
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import numpy as np
import os


# Where to find the samples files.
# These are all expected to be 96khz float (dtype='c8') files.
SAMPLE_DIR = "./samples"

# Directory to output generated files
GENERATED_DIR = "./generated"

# Range of Eb/N0 SNRs to produce.
# 10-20 dB seems to be the range where the demodulators fall over.
EBNO_RANGE = np.arange(5,20.5,0.5)

# Normalise the samples to +/- 1.0! 
# If we don't do this, bad things can happen later down the track...
NORMALISE = True

# List of samples
# [filename, baud_date, threshold, sample_rate]
# filename = string, without path
# baud_rate = integer
# threshold = threshold for calculating variance. Deterimined by taking 20*np.log10(np.abs(data)) and looking for packets.
# sample_rate = input file sample rate.

SAMPLES = [
    ['rs41_96k_float.bin', 4800, -20.0, 96000], 
    ['rs92_96k_float.bin', 4800, -100, 96000], # No threshold set, as signal is continuous.
    ['dfm09_96k_float.bin', 2500, -100, 96000], # Weird baud rate. No threshold set, as signal is continuous.
    ['m10_96k_float.bin', 9616, -10.0, 96000],  # Really weird baud rate.
    ['imet4_96k_float.bin', 1200, -10.0, 96000], # 1200 baud, but AFSK, so we expect 7-8 dB worse performance than the other sondes.
    ['imet54_96k_float.bin', 4800, -10.0, 96000], # 4800 baud GMSK
    ['rsngp_96k_float.bin', 2400, -100.0, 96000], # RS92-NGP - wider bandwidth.
    ['lms6-400_96k_float.bin', 4800, -100, 96000], # LMS6, 400 MHz variant. Continuous signal.
    ['mrz_96k_float.bin', 2400, -100, 96000], # MRZ Continuous signal.
    ['m20_96k_float.bin', 9600, -15, 96000], # M20, kind of continuous signal? residual carrier when not transmitting
    ['mts01_96k_float.bin', 1200, -20, 96000]
]



def load_sample(filename):
    _filename = os.path.join(SAMPLE_DIR, filename)
    return np.fromfile(_filename, dtype='c8')


def save_sample(data, filename):
    _filename = os.path.join(GENERATED_DIR, filename)
    # We have to make sure to convert to complex64..
    data.astype(dtype='c8').tofile(_filename)

    # TODO: Allow saving as complex s16 - see view solution here: https://stackoverflow.com/questions/47086134/how-to-convert-a-numpy-complex-array-to-a-two-element-float-array



def calculate_variance(data, threshold=-100.0):
    # Calculate the variance of a set of radiosonde samples.
    # Optionally use a threshold to limit the sample the variance 
    # is calculated over to ones that actually have sonde packets in them.

    _data_log = 20*np.log10(np.abs(data))

    return np.var(data[_data_log>threshold])


def add_noise(data, variance, baud_rate, ebno, fs=96000,  bitspersymbol=1.0):
    # Add calibrated noise to a sample.

    # Calculate Eb/No in linear units.
    _ebno = 10.0**((ebno)/10.0)

    # Calculate the noise variance we need to add
    _noise_variance = variance*fs/(baud_rate*_ebno*bitspersymbol)

    # Generate complex random samples
    _rand_i = np.sqrt(_noise_variance/2.0)*np.random.randn(len(data))
    _rand_q = np.sqrt(_noise_variance/2.0)*np.random.randn(len(data))

    _noisy = (data + (_rand_i + 1j*_rand_q))

    if NORMALISE:
        print("Normalised to 1.0")
        return _noisy/np.max(np.abs(_noisy))
    else:
        return _noisy




if __name__ == '__main__':

    for _sample in SAMPLES:
        # Extract the stuff we need from the entry.
        _source = _sample[0]
        _baud_rate = _sample[1]
        _threshold = _sample[2]
        _fs = _sample[3]

        print("Generating samples for: %s" % _source)

        # Read in source file.
        _data = load_sample(_source)

        # Calculate variance
        _var = calculate_variance(_data, _threshold)
        print("Calculated Variance: %.5f" % _var)

        # Now loop through the ebno's and generate the output.
        for ebno in EBNO_RANGE:
            _data_noise = add_noise(_data, variance=_var, baud_rate=_baud_rate, ebno=ebno, fs=_fs)

            _out_file = _source.split('.bin')[0] + "_%04.1fdB"%ebno + ".bin"

            save_sample(_data_noise, _out_file)
            print("Saved file: %s" % _out_file)






