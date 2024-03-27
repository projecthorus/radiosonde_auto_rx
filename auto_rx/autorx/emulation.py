#!/usr/bin/env python
#
#   Auto-RX - Log File Replay
#
#   Usage:
#   python3 -m autorx.emulation [-h] [-s SPEED] [-p PORT] log
#
#   positional arguments:
#   log                   auto_rx log file to replay.
#
#   optional arguments:
#   -h, --help            show this help message and exit
#   -s SPEED, --speed SPEED
#                            Replay speedup multiple (Default: 1.0)
#   -p PORT, --port PORT  Payload Summary UDP Output Port (Default: 55673)
#
#   Copyright (C) 2021  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#

import argparse
import json
import logging
import socket
import sys
import time
import datetime
import traceback
from dateutil.parser import parse


def send_payload_summary(telemetry, port=55673):
    """ Send a payload summary message into the network via UDP broadcast.

    Args:
    telemetry (dict): Telemetry dictionary to send.

    """

    try:
        # Prepare heading & speed fields, if they are provided in the incoming telemetry blob.
        if "heading" in telemetry.keys():
            _heading = telemetry["heading"]
        else:
            _heading = -1

        if "vel_h" in telemetry.keys():
            _speed = telemetry["vel_h"] * 3.6
        else:
            _speed = -1

        # Generate 'short' time field.
        _short_time = telemetry["datetime_dt"].strftime("%H:%M:%S")

        packet = {
            "type": "PAYLOAD_SUMMARY",
            "station": "Replay",
            "callsign": telemetry["id"],
            "latitude": telemetry["lat"],
            "longitude": telemetry["lon"],
            "altitude": telemetry["alt"],
            "speed": _speed,
            "heading": _heading,
            "time": _short_time,
            "comment": "Replay",
            # Additional fields specifically for radiosondes
            "model": telemetry["type"],
            "freq": telemetry["freq"],
            "temp": telemetry["temp"],
            "frame": telemetry["frame"],
        }

        # Set up our UDP socket
        _s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        _s.settimeout(1)
        # Set up socket for broadcast, and allow re-use of the address
        _s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        _s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        # Under OSX we also need to set SO_REUSEPORT to 1
        try:
            _s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except:
            pass

        try:
            _s.sendto(
                json.dumps(packet).encode("ascii"), ("<broadcast>", port),
            )
        # Catch any socket errors, that may occur when attempting to send to a broadcast address
        # when there is no network connected. In this case, re-try and send to localhost instead.
        except socket.error as e:
            logging.debug(
                "Send to broadcast address failed, sending to localhost instead."
            )
            _s.sendto(
                json.dumps(packet).encode("ascii"), ("127.0.0.1", port),
            )

        _s.close()

    except Exception as e:
        logging.debug("Error sending Payload Summary: %s" % str(e))


def emulate_telemetry(filename, port=55673, speed=1.0):
    """
    Read in a auto_rx log file and emit payload summary messages.
    """

    _f = open(filename, "r")
    # Skip header line
    _f.readline()

    # Use the current time as the start time for our telemetry sentences.
    _first_time = True

    # Log file format...
    # timestamp,serial,frame,lat,lon,alt,vel_v,vel_h,heading,temp,humidity,pressure,type,freq_mhz,snr,f_error_hz,sats,batt_v,burst_timer,aux_data
    # 0                       1        2    3         4         5      6   7   8     9      10   11   12   13      14   15  16 17 18 19
    # 2020-12-29T23:29:40.000Z,S1050212,2138,-34.96582,138.53122,4496.8,8.4,6.4,157.1,-273.0,-1.0,-1.0,RS41,401.500,23.6,562,8,3.0,-1,-1

    # Read in first datetime line
    _line = _f.readline()
    _fields = _line.split(",")
    _telemetry_datetime = parse(_fields[0])

    _current_datetime = datetime.datetime.now(datetime.timezone.utc)

    for _line in _f:
        _fields = _line.split(",")

        _time = parse(_fields[0])
        _id = _fields[1]
        _frame = int(_fields[2])
        _lat = float(_fields[3])
        _lon = float(_fields[4])
        _alt = float(_fields[5])
        _type = _fields[12]
        _freq = _fields[13]
        _temp = float(_fields[9])
        _vel_h = float(_fields[7])
        _heading = float(_fields[8])

        # Get Delta in time between telemetry lines.
        _time_delta = _time - _telemetry_datetime
        _telemetry_datetime = _time

        # Calculate our delay before emitting this packet.
        _delay_time = _time_delta.seconds * (1.0 / speed)

        # Increment the emitted datetime field.
        _current_datetime = _current_datetime + _time_delta

        # Delay
        print("Sleeping for %.1f seconds." % _delay_time)
        time.sleep(_delay_time)

        # Format telemetry data into a dictionary.
        _telemetry = {
            "datetime_dt": _current_datetime,
            "id": _id,
            "lat": _lat,
            "lon": _lon,
            "alt": _alt,
            "vel_h": _vel_h,
            "heading": _heading,
            "type": _type,
            "temp": _temp,
            "freq": _freq,
            "frame": _frame,
        }

        send_payload_summary(_telemetry, port=port)

        print(_telemetry)


if __name__ == "__main__":

    logging.basicConfig(
        format="%(asctime)s %(levelname)s:%(message)s", level=logging.DEBUG
    )

    # Command line arguments.
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-s",
        "--speed",
        type=float,
        default=1.0,
        help="Replay speedup multiple (Default: 1.0)",
    )
    parser.add_argument(
        "-p",
        "--port",
        type=int,
        default=55673,
        help="Payload Summary UDP Output Port (Default: 55673)",
    )
    parser.add_argument(
        "log", help="auto_rx log file to replay.",
    )
    args = parser.parse_args()

    emulate_telemetry(args.log, port=args.port, speed=args.speed)
