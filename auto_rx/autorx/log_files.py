#!/usr/bin/env python
#
#   radiosonde_auto_rx - Log File Utilities
#
#   Copyright (C) 2021  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import autorx
import autorx.config
import datetime
import glob
import io
import logging
import os.path
import time
import zipfile

import numpy as np

from dateutil.parser import parse
from autorx.utils import (
    short_type_lookup,
    short_short_type_lookup,
    readable_timedelta,
    strip_sonde_serial,
    position_info,
)
from autorx.geometry import GenericTrack, getDensity


def log_filename_to_stats(filename, quicklook=False):
    """ Attempt to extract information about a log file from a supplied filename """
    # Example log file name: 20210430-235413_IMET-89F2720A_IMET_401999_sonde.log
    # ./log/20200320-063233_R2230624_RS41_402500_sonde.log

    # Get a rough estimate of the number of lines of telemetry
    _filesize = os.path.getsize(filename)
    # Don't try and load files without data.
    if _filesize < 140:
        return None

    _lines = _filesize // 140 - 1

    if _lines <= 0:
        _lines = 1

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
        _short_type = short_short_type_lookup(_type)

        # Fourth field is the sonde frequency in kHz
        _freq = float(_fields[3]) / 1e3

        _output = {
            "datetime": _date_str2,
            "age": _time_delta,
            "serial": _serial,
            "type": _type_str,
            "short_type": _short_type,
            "freq": _freq,
            "lines": _lines,
        }

        if quicklook:
            try:
                _quick = log_quick_look(filename)
                if _quick:
                    _output["first"] = _quick["first"]
                    _output["last"] = _quick["last"]
                    _output["has_snr"] = _quick["has_snr"]
                    _output["max_range"] = int(max(_output["first"]["range_km"],_output["last"]["range_km"]))
                    _output["last_range"] = int(_output["last"]["range_km"])
                    _output["min_height"] = int(_output["last"]["alt"])
            except Exception as e:
                logging.error(f"Could not quicklook file {filename}: {str(e)}")

        return _output

    except Exception as e:
        logging.exception(f"Could not parse filename {_basename}", e)
        return None


def log_quick_look(filename):
    """ Attempt to read in the first and last line in a log file, and return the first/last position observed. """

    _filesize = os.path.getsize(filename)

    # Open the file and get the header line
    _file = open(filename, "r")
    _header = _file.readline()

    # Discard anything
    if "timestamp,serial,frame,lat,lon,alt" not in _header:
        return None

    _output = {}

    if "snr" in _header:
        _output["has_snr"] = True
    else:
        _output["has_snr"] = False

    try:
        # Naeive read of the first data line
        _first = _file.readline()
        _fields = _first.split(",")
        _first_datetime = _fields[0]
        _serial = _fields[1]
        _first_lat = float(_fields[3])
        _first_lon = float(_fields[4])
        _first_alt = float(_fields[5])
        _pos_info = position_info(
            (
                autorx.config.global_config["station_lat"],
                autorx.config.global_config["station_lon"],
                autorx.config.global_config["station_alt"],
            ),
            (_first_lat, _first_lon, _first_alt),
        )
        _output["first"] = {
            "datetime": _first_datetime,
            "lat": _first_lat,
            "lon": _first_lon,
            "alt": _first_alt,
            "range_km": _pos_info["straight_distance"] / 1000.0,
            "bearing": _pos_info["bearing"],
            "elevation": _pos_info["elevation"],
        }
    except Exception as e:
        # Couldn't read the first line, so likely no data.
        return None

    # Now we try and seek to near the end of the file.
    _seek_point = _filesize - 300
    if _seek_point < 0:
        # Don't bother trying to read the last line, it'll be the same as the first line.
        _output["last"] = _output["first"]
        return _output

    # Read in the rest of the file
    try:
        _file.seek(_seek_point)
        _remainder = _file.read()
        # Get the last line
        _last_line = _remainder.split("\n")[-2]
        _fields = _last_line.split(",")
        _last_datetime = _fields[0]
        _last_lat = float(_fields[3])
        _last_lon = float(_fields[4])
        _last_alt = float(_fields[5])
        _pos_info = position_info(
            (
                autorx.config.global_config["station_lat"],
                autorx.config.global_config["station_lon"],
                autorx.config.global_config["station_alt"],
            ),
            (_last_lat, _last_lon, _last_alt),
        )
        _output["last"] = {
            "datetime": _last_datetime,
            "lat": _last_lat,
            "lon": _last_lon,
            "alt": _last_alt,
            "range_km": _pos_info["straight_distance"] / 1000.0,
            "bearing": _pos_info["bearing"],
            "elevation": _pos_info["elevation"],
        }
        return _output
    except Exception as e:
        # Couldn't read in the last line for some reason.
        # Return what we have
        logging.error(f"Error reading last line of {filename}: {str(e)}")
        _output["last"] = _output["first"]
        return _output


def list_log_files(quicklook=False):
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
        _entry = log_filename_to_stats(_file, quicklook=quicklook)
        if _entry:
            _output.append(_entry)

    return _output


def read_log_file(filename, skewt_decimation=10):
    """ Read in a log file """
    logging.debug(f"Attempting to read file: {filename}")

    # Open the file and get the header line
    _file = open(filename, "r")
    _header = _file.readline()

    # Initially assume a new style log file (> ~1.4.0)
    # timestamp,serial,frame,lat,lon,alt,vel_v,vel_h,heading,temp,humidity,pressure,type,freq_mhz,snr,f_error_hz,sats,batt_v,burst_timer,aux_data
    fields = {
        "datetime": "f0",
        "serial": "f1",
        "frame": "f2",
        "latitude": "f3",
        "longitude": "f4",
        "altitude": "f5",
        "vel_v": "f6",
        "vel_h": "f7",
        "heading": "f8",
        "temp": "f9",
        "humidity": "f10",
        "pressure": "f11",
        "type": "f12",
        "frequency": "f13",
        "snr": "f14",
        "sats": "f16",
        "batt": "f17",
    }

    if "other" in _header:
        # Older style log file
        # timestamp,serial,frame,lat,lon,alt,vel_v,vel_h,heading,temp,humidity,type,freq,other
        # 2020-06-06T00:58:09.001Z,R3670268,7685,-31.21523,137.68126,33752.4,5.9,2.1,44.5,-273.0,-1.0,RS41,401.501,SNR 5.4,FERROR -187,SATS 9,BATT 2.7
        fields = {
            "datetime": "f0",
            "serial": "f1",
            "frame": "f2",
            "latitude": "f3",
            "longitude": "f4",
            "altitude": "f5",
            "vel_v": "f6",
            "vel_h": "f7",
            "heading": "f8",
            "temp": "f9",
            "humidity": "f10",
            "type": "f11",
            "frequency": "f12",
        }
        # Only use a subset of the columns, as the number of columns can vary in this old format
        _data = np.genfromtxt(
            _file,
            dtype=None,
            encoding="ascii",
            delimiter=",",
            usecols=(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12),
        )

    else:
        # Grab everything
        _data = np.genfromtxt(_file, dtype=None, encoding="ascii", delimiter=",")

    _file.close()

    if _data.size == 1:
        # Deal with log files with only one entry cleanly.
        _data = np.array([_data])

    # Now we need to rearrange some data for easier use in the client
    _output = {"serial": strip_sonde_serial(_data[fields["serial"]][0])}

    # Path to display on the map
    _output["path"] = np.column_stack(
        (
            _data[fields["latitude"]],
            _data[fields["longitude"]],
            _data[fields["altitude"]],
        )
    ).tolist()
    _output["first"] = _output["path"][0]
    _output["first_time"] = _data[fields["datetime"]][0]
    _output["last"] = _output["path"][-1]
    _output["last_time"] = _data[fields["datetime"]][-1]
    _burst_idx = np.argmax(_data[fields["altitude"]])
    _output["burst"] = _output["path"][_burst_idx]
    _output["burst_time"] = _data[fields["datetime"]][_burst_idx]

    # Calculate first position info
    _pos_info = position_info(
        (
            autorx.config.global_config["station_lat"],
            autorx.config.global_config["station_lon"],
            autorx.config.global_config["station_alt"],
        ),
        _output["first"],
    )
    _output["first_range_km"] = _pos_info["straight_distance"] / 1000.0
    _output["first_bearing"] = _pos_info["bearing"]

    # Calculate last position info
    _pos_info = position_info(
        (
            autorx.config.global_config["station_lat"],
            autorx.config.global_config["station_lon"],
            autorx.config.global_config["station_alt"],
        ),
        _output["last"],
    )
    _output["last_range_km"] = _pos_info["straight_distance"] / 1000.0
    _output["last_bearing"] = _pos_info["bearing"]

    # TODO: Calculate data necessary for Skew-T plots
    if "pressure" in fields:
        _press = _data[fields["pressure"]]
    else:
        _press = None

    if "snr" in fields:
        _output["snr"] = _data[fields["snr"]].tolist()

    _output["skewt"] = calculate_skewt_data(
        _data[fields["datetime"]],
        _data[fields["latitude"]],
        _data[fields["longitude"]],
        _data[fields["altitude"]],
        _data[fields["temp"]],
        _data[fields["humidity"]],
        _press,
        decimation=skewt_decimation,
    )

    return _output


def calculate_skewt_data(
    datetime,
    latitude,
    longitude,
    altitude,
    temperature,
    humidity,
    pressure=None,
    decimation=5,
):
    """ Work through a set of sonde data, and produce a dataset suitable for plotting in skewt-js """

    # A few basic checks initially

    # Don't bother to plot data with not enough data points.
    if len(datetime) < 10:
        return []

    # Figure out if we have any ascent data at all.
    _burst_idx = np.argmax(altitude)

    if _burst_idx == 0:
        # We only have descent data.
        return []

    if altitude[0] > 20000:
        # No point plotting SkewT plots for data only gathered above 10km altitude...
        return []

    _skewt = []

    # Make sure we start on index one.
    i = -1*decimation + 1

    while i < _burst_idx:
        i += decimation
        try:
            if temperature[i] < -260.0:
                # If we don't have any valid temp data, just skip this point
                # to avoid doing un-necessary calculations
                continue

            _time_delta = (parse(datetime[i]) - parse(datetime[i - 1])).total_seconds()
            if _time_delta == 0:
                continue

            _old_pos = (latitude[i - 1], longitude[i - 1], altitude[i - 1])
            _new_pos = (latitude[i], longitude[i], altitude[i])

            _pos_delta = position_info(_old_pos, _new_pos)

            _speed = _pos_delta["great_circle_distance"] / _time_delta
            _bearing = (_pos_delta["bearing"] + 180.0) % 360.0

            if pressure is None:
                _pressure = getDensity(altitude[i], get_pressure=True) / 100.0
            elif pressure[i] < 0.0:
                _pressure = getDensity(altitude[i], get_pressure=True) / 100.0
            else:
                _pressure = pressure[i]

            _temp = temperature[i]

            if humidity[i] >= 0.0:
                _rh = humidity[i]

                _dp = (
                    243.04
                    * (np.log(_rh / 100) + ((17.625 * _temp) / (243.04 + _temp)))
                    / (
                        17.625
                        - np.log(_rh / 100)
                        - ((17.625 * _temp) / (243.04 + _temp))
                    )
                )
            else:
                _dp = -999.0

            if np.isnan(_dp):
                continue

            _skewt.append(
                {
                    "press": _pressure,
                    "hght": altitude[i],
                    "temp": _temp,
                    "dwpt": _dp,
                    "wdir": _bearing,
                    "wspd": _speed,
                }
            )

            # Only produce data up to 50hPa (~20km alt), which is the top of the skewt plot.
            # We *could* go above this, but the data becomes less useful at those altitudes.
            if _pressure < 50.0:
                break

        except Exception as e:
            print(str(e))

        # Continue through the data..

    return _skewt


def read_log_by_serial(serial, skewt_decimation=25):
    """ Attempt to read in a log file for a particular sonde serial number """

    # Search in the logging directory for a matching serial number
    _log_mask = os.path.join(autorx.logging_path, f"*_*{serial}_*_sonde.log")
    _matching_files = glob.glob(_log_mask)

    # No matching entries found
    if len(_matching_files) == 0:
        return {}
    else:
        try:
            data = read_log_file(_matching_files[0], skewt_decimation=skewt_decimation)
            return data
        except Exception as e:
            logging.exception(f"Error reading file for serial: {serial}", e)
            return {}


def zip_log_files(serial_list=None):
    """ Take a list of serial numbers and find and zip all related log files """

    if serial_list is None:
        # Get all log files.
        # Search for file matching the expected log file name
        _log_mask = os.path.join(autorx.logging_path, "*_sonde.log")
        _log_files = glob.glob(_log_mask)
    else:
        # Have been provided a list of log files.
        _log_files = []
        for _serial in serial_list:
            _log_mask = os.path.join(autorx.logging_path, f"*_*{_serial}_*_sonde.log")
            _matching_files = glob.glob(_log_mask)

            if len(_matching_files) >= 1:
                _log_files.append(_matching_files[0])

    logging.debug(f"Log Files - Zipping up {len(_log_files)} log files.")

    # Perform the zipping
    data = io.BytesIO()
    with zipfile.ZipFile(data, compression=zipfile.ZIP_DEFLATED, mode="w") as z:
        for f_name in _log_files:
            z.write(f_name, arcname=os.path.basename(f_name))
    logging.debug(f"Log Files - Resultant zip file is {data.tell()/1024768} MiB.")
    # Seek back to the start
    data.seek(0)

    # Return the BytesIO object
    return data


if __name__ == "__main__":
    import sys
    import json

    logging.basicConfig(
        format="%(asctime)s %(levelname)s:%(message)s", level=logging.DEBUG
    )

    _start = time.time()
    _no_quicklook = list_log_files()
    _stop = time.time()
    print(f"No Quicklook: {_stop-_start}")

    _start = time.time()
    _quicklook = list_log_files(quicklook=True)
    _stop = time.time()
    print(f"Quicklook: {_stop-_start}")

    # Test out the zipping function
    _serial = []
    for x in range(5):
        _serial.append(_quicklook[x]["serial"])

    print(_serial)
    _start = time.time()
    _zip = zip_log_files(serial_list=_serial)
    _stop = time.time()
    print(f"Zip 5 logs: {_stop - _start}")

    _start = time.time()
    _zip = zip_log_files()
    _stop = time.time()
    print(f"Zip all logs: {_stop - _start}")
