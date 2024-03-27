#!/usr/bin/env python
#
#   radiosonde_auto_rx - Radiosonde-Type Specific Functions
#
#   Copyright (C) 2019  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import datetime
import hashlib
from dateutil.parser import parse


def fix_datetime(datetime_str, local_dt_str=None):
    """
	Given a HH:MM:SS string from a telemetry sentence, produce a complete timestamp, using the current system time as a guide for the date.
	"""

    if local_dt_str is None:
        _now = datetime.datetime.now(datetime.timezone.utc)
    else:
        _now = parse(local_dt_str)

    # Are we in the rollover window?
    if _now.hour == 23 or _now.hour == 0:
        _outside_window = False
    else:
        _outside_window = True

    # Parsing just a HH:MM:SS will return a datetime object with the year, month and day replaced by values in the 'default'
    # argument.
    _imet_dt = parse(datetime_str, default=_now)

    if _outside_window:
        # We are outside the day-rollover window, and can safely use the current zulu date.
        return _imet_dt
    else:
        # We are within the window, and need to adjust the day backwards or forwards based on the sonde time.
        if _imet_dt.hour == 23 and _now.hour == 0:
            # Assume system clock running slightly fast, and subtract a day from the telemetry date.
            _imet_dt = _imet_dt - datetime.timedelta(days=1)

        elif _imet_dt.hour == 00 and _now.hour == 23:
            # System clock running slow. Add a day.
            _imet_dt = _imet_dt + datetime.timedelta(days=1)

        return _imet_dt


#
# 	iMet Radiosonde Functions
#


def imet_unique_id(telemetry, custom="SONDE", imet1=False):
    """
	Generate a 'unique' imet radiosonde ID based on the power-on time, frequency, and an optional location code.
	This requires the following fields be present in the telemetry dict:
		datetime_dt (datetime)  (will need to be generated above)
		frame (int) - Frame number
		freq_float (float) - Frequency in MHz, as a floating point number.
	"""

    _imet_dt = telemetry["datetime_dt"]

    if imet1:
        # iMet-1 sondes increment their frame counter TWICE every second, so we need to
        # compensate for this to be able to determine a power-on time.
        _frame = telemetry["frame"]//2
    else:
        # iMet-4 sondes increment the frame counter once per second.
        _frame = telemetry["frame"]

    # Determine power on time: Current time -  number of frames (one frame per second)
    _power_on_time = _imet_dt - datetime.timedelta(seconds=_frame)

    # Round frequency to the nearest 100 kHz (iMet sondes only have 100 kHz frequency steps)
    _freq = round(telemetry["freq_float"] * 10.0) / 10.0
    _freq = "%.3f MHz" % _freq

    # Now we generate a string to hash based on the power-on time, the rounded frequency, and the custom field.
    _temp_str = _power_on_time.strftime("%Y-%m-%dT%H:%M:%SZ") + _freq + custom

    # Calculate a SHA256 hash of the
    _hash = hashlib.sha256(_temp_str.encode("ascii")).hexdigest().upper()

    return "IMET-" + _hash[-8:]


#
# 	DFM Sonde Subtypes
#
def decode_dfm_subtype(subtype):
    """
	Decode a DFM subtype (0xB through 0xD) into a possible model number.
	NOTE: These are best guesses as to the relationship between subtype ID nibble
	and actual model number. Graw have said that sonde decoders should not rely on
	this nibble for identification.
	"""
    if "0x" not in subtype:
        return "DFM-Unknown"

    if subtype == "0x6":
        return "DFM06"
    elif subtype == "0x7":
        return "PS-15"
    elif subtype == "0xA":
        return "DFM09"
    elif subtype == "0xB":
        return "DFM17"
    elif subtype == "0xC":
        return "DFM09P"
    elif subtype == "0xD":
        return "DFM17"
    else:
        # Unknown subtype
        return "DFMx" + subtype[-1]


if __name__ == "__main__":

    # Testing scripts for the above.

    test_data = [
        {
            "datetime": "23:59:58",
            "frame": 50,
            "freq": "402.001 MHz",
            "local_dt": "2019-03-01T23:59:58Z",
        },
        {
            "datetime": "23:59:58",
            "frame": 50,
            "freq": "401.999 MHz",
            "local_dt": "2019-03-01T23:59:57Z",
        },
        {
            "datetime": "23:59:58",
            "frame": 50,
            "freq": "402.000 MHz",
            "local_dt": "2019-03-02T00:00:03Z",
        },
        {
            "datetime": "00:00:00",
            "frame": 52,
            "freq": "402.000 MHz",
            "local_dt": "2019-03-01T23:59:57Z",
        },
        {
            "datetime": "00:00:00",
            "frame": 52,
            "freq": "402.000 MHz",
            "local_dt": "2019-03-02T00:00:03Z",
        },
        {
            "datetime": "00:00:01",
            "frame": 53,
            "freq": "402.000 MHz",
            "local_dt": "2019-03-01T23:59:57Z",
        },
        {
            "datetime": "00:00:01",
            "frame": 53,
            "freq": "402.000 MHz",
            "local_dt": "2019-03-02T00:00:03Z",
        },
        {
            "datetime": "11:59:58",
            "frame": 42,
            "freq": "402.000 MHz",
            "local_dt": "2019-03-01T12:00:03Z",
        },
        {
            "datetime": "12:00:02",
            "frame": 46,
            "freq": "402.000 MHz",
            "local_dt": "2019-03-01T12:00:03Z",
        },
        # 20:12:05Z,IMET-E2596815,3935,33.29227,35.68153,9253.0,-42.8,62.7,iMet,401.998,SATS 10,BATT 5.0
        {
            "datetime": "20:12:05",
            "frame": 3935,
            "freq": "401.998 MHz",
            "local_dt": "2019-03-01T12:00:03Z",
        },
        # 20:12:06Z,IMET-E2596815,3937,33.29233,35.68149,9259.0,-42.9,62.1,iMet,401.998,SATS 11,BATT 5.0
        {
            "datetime": "20:12:06",
            "frame": 3937,
            "freq": "401.998 MHz",
            "local_dt": "2019-03-01T12:00:03Z",
        },
        # 20:13:03Z,IMET-F46CA05C,4049,33.29655,35.67995,9640.0,-45.9,51.2,iMet,401.999,SATS 10,BATT 5.0
        {
            "datetime": "20:13:03",
            "frame": 4049,
            "freq": "401.999 MHz",
            "local_dt": "2019-03-01T12:00:03Z",
        },
        # 20:13:04Z,IMET-F46CA05C,4051,33.29666,35.67995,9646.0,-46.0,51.1,iMet,401.999,SATS 10,BATT 5.0
        {
            "datetime": "20:13:04",
            "frame": 4051,
            "freq": "401.999 MHz",
            "local_dt": "2019-03-01T12:00:03Z",
        },
    ]

    for _test in test_data:
        _test["freq_float"] = float(_test["freq"].split(" ")[0])
        _test["datetime_dt"] = fix_datetime(
            _test["datetime"], local_dt_str=_test["local_dt"]
        )
        print(
            "Input Time: %s, Local Time: %s, Output Time: %s"
            % (
                _test["datetime"],
                _test["local_dt"],
                _test["datetime_dt"].strftime("%Y-%m-%dT%H:%M:%SZ"),
            )
        )
        _test["id"] = imet_unique_id(_test)
        print("Generated ID: %s" % _test["id"])
        print(" ")
