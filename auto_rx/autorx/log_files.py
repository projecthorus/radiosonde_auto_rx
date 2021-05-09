#!/usr/bin/env python
#
#   radiosonde_auto_rx - Log File Utilities
#
#   Copyright (C) 2021  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import autorx
import datetime
import glob
import logging
import os.path
import time

import numpy as np

from dateutil.parser import parse
from autorx.utils import short_type_lookup, readable_timedelta, strip_sonde_serial


def log_filename_to_stats(filename):
    """ Attempt to extract information about a log file from a supplied filename """
    # Example log file name: 20210430-235413_IMET-89F2720A_IMET_401999_sonde.log
    # ./log/20200320-063233_R2230624_RS41_402500_sonde.log

    _basename = os.path.basename(filename)

    _now_dt = datetime.datetime.now(datetime.timezone.utc)

    try:
        _fields = _basename.split("_")

        # First field is the date/time the sonde was first received.
        _date_str = _fields[0] + "Z"
        _date_dt = parse(_date_str)

        # Calculate age
        _age_td = _now_dt - _date_dt
        _time_delta = readable_timedelta(_age_td)

        # Re-format date
        _date_str2 = _date_dt.strftime("%Y-%m-%dT%H:%M:%SZ")

        # Second field is the serial number, which may include a sonde type prefix.
        _serial = strip_sonde_serial(_fields[1])

        # Third field is the sonde type, in 'shortform'
        _type = _fields[2]
        _type_str = short_type_lookup(_type)

        # Fourth field is the sonde frequency in kHz
        _freq = float(_fields[3])/1e3

        return {
            "datetime": _date_str2, 
            "age": _time_delta,
            "serial": _serial, 
            "type": _type_str, 
            "freq":_freq
        }

    except Exception as e:
        logging.exception(f"Could not parse filename {_basename}", e)
        return None

    
def list_log_files():
    """ Look for all sonde log files within the logging directory """

    # Output list, which will contain one object per log file, ordered by time
    _output = []

    # Search for file matching the expected log file name
    _log_mask = os.path.join(autorx.logging_path, "*_sonde.log")
    _log_files = glob.glob(_log_mask)

    # Sort alphanumerically, which will result in the entries being date ordered
    _log_files.sort()
    # Flip array so newest is first.
    _log_files.reverse()

    for _file in _log_files:
        _entry = log_filename_to_stats(_file)
        if _entry:
            _output.append(_entry)

    return _output



def read_log_file(filename):
    """ Read in a log file """
    return {}
    


def read_log_by_serial(serial):
    """ Attempt to read in a log file for a particular sonde serial number """


    # Search in the logging directory for a matching serial number
    _log_mask = os.path.join(autorx.logging_path, f"*_{serial}_*_sonde.log")
    _matching_files = glob.glob(_log_mask)
    
    # No matching entries found
    if len(_matching_files) == 0:
        return {}

    return {}
    


if __name__ == "__main__":
    import sys

    print(list_log_files())

    if len(sys.argv) > 1:
        print(f"Attempting to read serial: {sys.argv[1]}")
        print(read_log_by_serial(sys.argv[1]))