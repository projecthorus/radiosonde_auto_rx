#!/usr/bin/env python
#
#   radiosonde_auto_rx
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
__version__ = "20190316"

# Global Variables

# RTLSDR Usage Register - This dictionary holds information about each SDR and its currently running Decoder / Scanner
#   Key = SDR device index / ID
#   'device_idx': {
#       'in_use' (bool) : True if the SDR is currently in-use by a decoder or scanner.
#       'task' (class)  : If this SDR is in use, a reference to the task.
#       'bias' (bool)   : True if the bias-tee should be enabled on this SDR, False otherwise.
#       'ppm' (int)     : The PPM offset for this SDR.
#       'gain' (float)  : The gain setting to use with this SDR. A setting of -1 turns on hardware AGC.    
#   }
#
#
sdr_list = {}

# Currently running task register.
#   Keys will either be 'SCAN' (only one scanner shall be running at a time), or a sonde frequency in MHz.
#   Each element contains:
#       'task' : (class) Reference to the currently running task.
#       'device_idx' (str): The allocated SDR.
#
task_list = {}
