#!/usr/bin/env python
#
#   Investigate dft_detect correlation outputs.
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
#   NOTE: The modifications to dft_detect which enable this script to work have been reverted, and so
#   this script will not work unless the modifications are re-applied.
#

import glob
import argparse
import os
import sys
import time
import subprocess


#
# Processing Types
#
processing_type = {}

# DFT_Detect
_sample_fs = 96000.0
_fm_rate = 22000
#_fm_rate = 15000
# Calculate the necessary conversions
_rtlfm_oversampling = 8.0 # Viproz's hacked rtl_fm oversamples by 8x.
_shift = -2.0*_fm_rate/_sample_fs # rtl_fm tunes 'up' by rate*2, so we need to shift the signal down by this amount.

_resample = (_fm_rate*_rtlfm_oversampling)/_sample_fs

if _resample != 1.0:
    # We will need to resample.
    _resample_command = "csdr convert_f_s16 | ./tsrc - - %.4f | csdr convert_s16_f |" % _resample
    _shift = (-2.0*_fm_rate)/(_sample_fs*_resample)
else:
    _resample_command = ""

_demod_command = "| %s csdr shift_addition_cc %.5f 2>/dev/null | csdr convert_f_u8 |" % (_resample_command, _shift)
_demod_command += " ./rtl_fm_stdin -M fm -f 401000000 -F9 -s %d  2>/dev/null|" % (int(_fm_rate))
_demod_command += " sox -t raw -r %d -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - highpass 20 2>/dev/null |" % int(_fm_rate)

processing_type['dft_detect_rtlfm'] = {
    'demod': _demod_command,
    'decode': "../dft_detect --debug 2>/dev/null",
    # Grep out the line containing the detected sonde type.
    "post_process" : "",
    'files' : "./generated/*.bin"
}


def run_analysis(mode, file_mask=None, shift=0.0, verbose=False, log_output = None):


    _mode = processing_type[mode]

    # If we are not supplied with a file mask, use the defaults.
    if file_mask is None:
        file_mask = _mode['files']

    # Get the list of files.
    _file_list = glob.glob(file_mask)
    if len(_file_list) == 0:
        print("No files found matching supplied path.")
        return

    # Sort the list of files.
    _file_list.sort()

    _first = True

    # Calculate the frequency offset to apply, if defined.
    _shiftcmd = "| csdr shift_addition_cc %.5f 2>/dev/null" % (shift/96000.0)


    _results = {}

    # Iterate over the files in the supplied list.
    for _file in _file_list:
    	print("Processing: %s" % _file)
        # Generate the command to run.
        _cmd = "cat %s "%_file 

        # Add in an optional frequency error if supplied.
        if shift != 0.0:
            _cmd += _shiftcmd

        # Add on the rest of the demodulation and decoding commands.
        _cmd += _mode['demod'] + _mode['decode'] + _mode['post_process']


        if _first:
            print("Command: %s" % _cmd)
            _first = False

        # Run the command.
        try:
            _start = time.time()
            _output = subprocess.check_output(_cmd, shell=True, stderr=None)
        except Exception as e:
	        if e.returncode >= 2:
	            _output = e.output
	        else:
	            continue


        _runtime = time.time() - _start

        try:

	        # Split out the SNR from the filename.
	        _snr = float(_file.split('_')[-1].split('dB.bin')[0])
	        _results[_snr] = {}

        	_lines = _output.split('\n')

        	for _line in _lines:
        		_fields = _line.split(',')
        		_type = _fields[0]
        		_score = _fields[1]

        		_results[_snr][_type] = float(_score)

        except Exception as e:
        	print("Error - %s" % str(e))
        	pass


    print(_results)

    _snrs = _results.keys()
    _snrs.sort()

    _types = ['RS41', 'RS92', 'DFM9', 'M10', 'LMS6']

    _header = "snr," + ','.join(_types)
    print(_header)
    for _snr in _snrs:
    	_line = "%.1f," % _snr

    	for _type in _types:
    		_line += "%.4f," % _results[_snr][_type]

    	print(_line)



if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-m", "--mode", type=str, default="dft_detect_rtlfm", help="Operation mode.")
    parser.add_argument("-f", "--files", type=str, default=None, help="Glob-path to files to run over.")
    parser.add_argument("-v", "--verbose", action='store_true', default=False, help="Show additional debug info.")
    parser.add_argument("--shift", type=float, default=0.0, help="Shift the signal-under test by x Hz. Default is 0.")
    args = parser.parse_args()

    # Check the mode is valid.
    if args.mode not in processing_type:
        print("Error - invalid operating mode.")
        print("Valid Modes: %s" % str(processing_type.keys()))
        sys.exit(1)

    run_analysis(args.mode, args.files, shift=args.shift, verbose=args.verbose)

