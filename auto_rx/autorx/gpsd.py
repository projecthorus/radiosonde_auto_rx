#!/usr/bin/env python3
# coding=utf-8
"""
GPS3 Code below sourced from https://github.com/wadda/gps3
and is licensed under the MIT license.
Modifications made by M.Jessop to make use of logging when reporting errors.

GPS3 (gps3.py) is a Python 2.7-3.5 GPSD interface (http://www.catb.org/gpsd)
Default host='127.0.0.1', port=2947, gpsd_protocol='json' in two classes.

1) 'GPSDSocket' creates a GPSD socket connection & request/retrieve GPSD output.
2) 'DataStream' Streamed gpsd JSON data literates it into python dictionaries.

Import          from gps3 import gps3
Instantiate     gpsd_socket = gps3.GPSDSocket()
                data_stream = gps3.DataStream()
Run             gpsd_socket.connect()
                gpsd_socket.watch()
Iterate         for new_data in gpsd_socket:
                    if new_data:
                        data_stream.unpack(new_data)
Use                     print('Altitude = ',data_stream.TPV['alt'])
                        print('Latitude = ',data_stream.TPV['lat'])

Consult Lines 144-ff for Attribute/Key possibilities.
or http://www.catb.org/gpsd/gpsd_json.html

Run human.py; python[X] human.py [arguments] for a human experience.
"""
from __future__ import print_function

import json
import logging
import select
import socket
import sys
import time
import traceback
from threading import Thread


GPSD_HOST = "127.0.0.1"  # gpsd
GPSD_PORT = 2947  # defaults
GPSD_PROTOCOL = "json"  # "


class GPSDSocket(object):
    """Establish a socket with gpsd, by which to send commands and receive data."""

    def __init__(self):
        self.streamSock = None
        self.response = None

    def connect(self, host=GPSD_HOST, port=GPSD_PORT):
        """Connect to a host on a given port.
        Arguments:
            host: default host='127.0.0.1'
            port: default port=2947
        """
        for alotta_stuff in socket.getaddrinfo(host, port, 0, socket.SOCK_STREAM):
            family, socktype, proto, _canonname, host_port = alotta_stuff
            try:
                self.streamSock = socket.socket(family, socktype, proto)
                self.streamSock.connect(host_port)
                self.streamSock.setblocking(False)
                return True
            except (OSError, IOError) as error:
                logging.error("GPSD - GPSDSocket connect exception: %s" % str(error))
                return False

    def watch(self, enable=True, gpsd_protocol=GPSD_PROTOCOL, devicepath=None):
        """watch gpsd in various gpsd_protocols or devices.
        Arguments:
            enable: (bool) stream data to socket
            gpsd_protocol: (str) 'json' | 'nmea' | 'rare' | 'raw' | 'scaled' | 'split24' | 'pps'
            devicepath: (str) device path - '/dev/ttyUSBn' for some number n or '/dev/whatever_works'
        Returns:
            command: (str) e.g., '?WATCH={"enable":true,"json":true};'
        """
        # N.B.: 'timing' requires special attention, as it is undocumented and lives with dragons.
        command = '?WATCH={{"enable":true,"{0}":true}}'.format(gpsd_protocol)

        if (
            gpsd_protocol == "rare"
        ):  # 1 for a channel, gpsd reports the unprocessed NMEA or AIVDM data stream
            command = command.replace('"rare":true', '"raw":1')
        if (
            gpsd_protocol == "raw"
        ):  # 2 channel that processes binary data, received data verbatim without hex-dumping.
            command = command.replace('"raw":true', '"raw",2')
        if not enable:
            command = command.replace(
                "true", "false"
            )  # sets -all- command values false .
        if devicepath:
            command = command.replace("}", ',"device":"') + devicepath + '"}'

        return self.send(command)

    def send(self, command):
        """Ship commands to the daemon
        Arguments:
            command: e.g., '?WATCH={{'enable':true,'json':true}}'|'?VERSION;'|'?DEVICES;'|'?DEVICE;'|'?POLL;'
        """
        # The POLL command requests data from the last-seen fixes on all active GPS devices.
        # Devices must previously have been activated by ?WATCH to be pollable.
        try:
            self.streamSock.send(bytes(command, encoding="utf-8"))
        except TypeError:
            self.streamSock.send(command)  # 2.7 chokes on 'bytes' and 'encoding='
        except (OSError, IOError) as error:  # MOE, LEAVE THIS ALONE!...for now.
            logging.error("GPSD - GPS3 send command fail with %s" % str(error))

    def __iter__(self):
        """banana"""  # <--- for scale
        return self

    def next(self, timeout=0):
        """Return empty unless new data is ready for the client.
        Arguments:
            timeout: Default timeout=0  range zero to float specifies a time-out as a floating point
        number in seconds.  Will sit and wait for timeout seconds.  When the timeout argument is omitted
        the function blocks until at least one file descriptor is ready. A time-out value of zero specifies
        a poll and never blocks.
        """
        try:
            waitin, _waitout, _waiterror = select.select(
                (self.streamSock,), (), (), timeout
            )
            if not waitin:
                return None
            else:
                gpsd_response = (
                    self.streamSock.makefile()
                )  # '.makefile(buffering=4096)' In strictly Python3
                self.response = gpsd_response.readline()
            return self.response

        except StopIteration as error:
            logging.error(
                "GPSD - The readline exception in GPSDSocket.next is %s" % str(error)
            )

    __next__ = next  # Workaround for changes in iterating between Python 2.7 and 3

    def close(self):
        """turn off stream and close socket"""
        if self.streamSock:
            self.watch(enable=False)
            self.streamSock.close()
        self.streamSock = None


class DataStream(object):
    """Retrieve JSON Object(s) from GPSDSocket and unpack it into respective
    gpsd 'class' dictionaries, TPV, SKY, etc. yielding hours of fun and entertainment.
    """

    packages = {
        "VERSION": {"release", "proto_major", "proto_minor", "remote", "rev"},
        "TPV": {
            "alt",
            "climb",
            "device",
            "epc",
            "epd",
            "eps",
            "ept",
            "epv",
            "epx",
            "epy",
            "lat",
            "lon",
            "mode",
            "speed",
            "tag",
            "time",
            "track",
        },
        "SKY": {"satellites", "gdop", "hdop", "pdop", "tdop", "vdop", "xdop", "ydop"},
        # Subset of SKY: 'satellites': {'PRN', 'ss', 'el', 'az', 'used'}  # is always present.
        "GST": {
            "alt",
            "device",
            "lat",
            "lon",
            "major",
            "minor",
            "orient",
            "rms",
            "time",
        },
        "ATT": {
            "acc_len",
            "acc_x",
            "acc_y",
            "acc_z",
            "depth",
            "device",
            "dip",
            "gyro_x",
            "gyro_y",
            "heading",
            "mag_len",
            "mag_st",
            "mag_x",
            "mag_y",
            "mag_z",
            "pitch",
            "pitch_st",
            "roll",
            "roll_st",
            "temperature",
            "time",
            "yaw",
            "yaw_st",
        },
        # 'POLL': {'active', 'tpv', 'sky', 'time'},
        "PPS": {
            "device",
            "clock_sec",
            "clock_nsec",
            "real_sec",
            "real_nsec",
            "precision",
        },
        "TOFF": {"device", "clock_sec", "clock_nsec", "real_sec", "real_nsec"},
        "DEVICES": {"devices", "remote"},
        "DEVICE": {
            "activated",
            "bps",
            "cycle",
            "mincycle",
            "driver",
            "flags",
            "native",
            "parity",
            "path",
            "stopbits",
            "subtype",
        },
        # 'AIS': {}  # see: http://catb.org/gpsd/AIVDM.html
        "ERROR": {"message"},
    }  # TODO: Full suite of possible GPSD output

    def __init__(self):
        """Potential data packages from gpsd for a generator of class attribute dictionaries"""
        for package_name, dataset in self.packages.items():
            _emptydict = {key: "n/a" for key in dataset}
            setattr(self, package_name, _emptydict)

        self.DEVICES["devices"] = {
            key: "n/a" for key in self.packages["DEVICE"]
        }  # How does multiple listed devices work?
        # self.POLL = {'tpv': self.TPV, 'sky': self.SKY, 'time': 'n/a', 'active': 'n/a'}

    def unpack(self, gpsd_socket_response):
        """Sets new socket data as DataStream attributes in those initialised dictionaries
        Arguments:
            gpsd_socket_response (json object):
        Provides:
        self attribute dictionaries, e.g., self.TPV['lat'], self.SKY['gdop']
        Raises:
        AttributeError: 'str' object has no attribute 'keys' when the device falls out of the system
        ValueError, KeyError: most likely extra, or mangled JSON data, should not happen, but that
        applies to a lot of things.
        """
        try:
            fresh_data = json.loads(
                gpsd_socket_response
            )  # The reserved word 'class' is popped from JSON object class
            package_name = fresh_data.pop(
                "class", "ERROR"
            )  # gpsd data package errors are also 'ERROR'.
            package = getattr(
                self, package_name, package_name
            )  # packages are named for JSON object class
            for key in package.keys():
                package[key] = fresh_data.get(
                    key, "n/a"
                )  # Restores 'n/a' if key is absent in the socket response

        except AttributeError:  # 'str' object has no attribute 'keys'
            logging.error(
                "GPSD Parser - There is an unexpected exception in DataStream.unpack."
            )
            return

        except (ValueError, KeyError) as error:
            logging.error("GPSD Parser - Other Error - %s" % str(error))
            return


class GPSDAdaptor(object):
    """ Connect to a GPSD instance, and pass data onto a callback function """

    def __init__(
        self, hostname="127.0.0.1", port=2947, callback=None, update_decimation=30
    ):
        """
        Initialize a GPSAdaptor object.

        This class uses the GPSDSocket class to connect to a GPSD instance,
        and then formats all received data appropriately and passes it on to chasemapper.

        Args:
            hostname (str): Hostname of where GPSD is listening.
            port (int): GPSD listen port (default = 2947)
            callback (function): Callback to pass appropriately formatted dictionary data to.
            update_decimation (int): Only pass updates to the callback every X samples.
        """

        self.hostname = hostname
        self.port = port
        self.callback = callback

        self.update_decimation = update_decimation
        self.update_counter = 0

        self.gpsd_thread_running = False
        self.gpsd_thread = None
        self.start()

    def start(self):
        """ Start the GPSD thread """
        if self.gpsd_thread != None:
            return
        else:
            self.gpsd_thread_running = True
            self.gpsd_thread = Thread(target=self.gpsd_process_thread)
            self.gpsd_thread.start()

    def close(self):
        """ Stop the GPSD thread. """
        self.gpsd_thread_running = False
        # Wait for the thread to close.
        if self.gpsd_thread != None:
            self.gpsd_thread.join(60)
            if self.gpsd_thread.is_alive():
                logging.error("GPS thread failed to join")

    def send_to_callback(self, data):
        """
        Send the current GPS data snapshot onto the callback function,
        if one exists.
        """

        # Attempt to pass it onto the callback function.
        if self.callback != None:
            try:
                self.callback(data)
            except Exception as e:
                traceback.print_exc()
                logging.error("GPSD - Error Passing data to callback - %s" % str(e))

    def gpsd_process_thread(self):
        """ Attempt to connect to a GPSD instance, and read position information """
        while self.gpsd_thread_running:

            # Attempt to connect.
            _gpsd_socket = GPSDSocket()
            _data_stream = DataStream()
            _success = _gpsd_socket.connect(host=self.hostname, port=self.port)

            # If we could not connect, wait and try again.
            if not _success:
                logging.error(
                    "GPSD - Connect failed. Waiting 10 seconds before re-trying."
                )
                time.sleep(10)
                continue

            # Start watching for data.
            _gpsd_socket.watch(gpsd_protocol="json")
            logging.info("GPSD - Connected to GPSD instance at %s" % self.hostname)

            while self.gpsd_thread_running:
                # We should be getting GPS data every second.
                # If this isn't the case, we should close the connection and re-connect.
                _gpsd_data = _gpsd_socket.next(timeout=10)

                if _gpsd_data == None or _gpsd_data == "":
                    logging.error("GPSD - No data received. Attempting to reconnect.")

                    # Break out of this loop back to the connection loop.
                    break
                else:
                    # Attempt to parse the data.
                    _data_stream.unpack(_gpsd_data)

                    # Extract the Time-Position-Velocity report.
                    # This will have fields as defined in: http://www.catb.org/gpsd/gpsd_json.html
                    _TPV = _data_stream.TPV
                    if _TPV["lat"] == "n/a" or _TPV["lon"] == "n/a":
                        # No position data. Continue.
                        continue
                    else:
                        # Produce output data structure.

                        if _TPV["speed"] != "n/a":
                            _speed = _TPV["speed"]
                        else:
                            _speed = 0.0

                        if _TPV["alt"] != "n/a":
                            _alt = _TPV["alt"]
                        else:
                            _alt = 0.0

                        _gps_state = {
                            "type": "GPS",
                            "latitude": _TPV["lat"],
                            "longitude": _TPV["lon"],
                            "altitude": _alt,
                            "speed": _speed,
                            "valid": True,
                        }

                        if self.update_counter % self.update_decimation == 0:
                            self.send_to_callback(_gps_state)

                        self.update_counter += 1

            # Close the GPSD connection.
            try:
                _gpsd_socket.close()
            except Exception as e:
                logging.error("GPSD - Error when closing connection: %s" % str(e))


if __name__ == "__main__":

    def print_dict(data):
        print(data)

    logging.basicConfig(
        format="%(asctime)s %(levelname)s:%(message)s", level=logging.DEBUG
    )

    _gpsd = GPSDAdaptor(callback=print_dict)
    time.sleep(30)
    _gpsd.close()

    # gpsd_socket = GPSDSocket()
    # data_stream = DataStream()
    # gpsd_socket.connect()
    # gpsd_socket.watch()
    # for new_data in gpsd_socket:
    #     if new_data:
    #         data_stream.unpack(new_data)
    #         print(data_stream.TPV)
    #         #print(data_stream.SKY)
