#!/usr/bin/env python
#
#   radiosonde_auto_rx - OziMux Output
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#

import json
import logging
import socket
import time
from queue import Queue
from threading import Thread


class OziUploader(object):
    """ Push radiosonde telemetry out via UDP broadcast to the Horus Chase-Car Utilities

    Uploads to:
        - OziMux / OziPlotter (UDP Broadcast, port 8942 by default) - Refer here for information on the data format:
            https://github.com/projecthorus/oziplotter/wiki/3---Data-Sources
        - "Payload Summary" (UDP Broadcast, on port 55672 by default)
            Refer here for information: https://github.com/projecthorus/horus_utils/wiki/5.-UDP-Broadcast-Messages#payload-summary-payload_summary

    NOTE: This class is designed to only handle telemetry from a single radiosonde at a time. It should only be operated in a
    single-SDR configuration, where only one sonde is tracked at a time.

    """

    # We require the following fields to be present in the incoming telemetry dictionary data
    REQUIRED_FIELDS = [
        "frame",
        "id",
        "datetime",
        "lat",
        "lon",
        "alt",
        "temp",
        "type",
        "freq",
        "freq_float",
        "datetime_dt",
    ]

    # Extra fields we can pass on to other programs.
    EXTRA_FIELDS = ["bt", "humidity", "pressure", "sats", "batt", "snr", "fest", "f_centre", "ppm", "subtype", "sdr_device_idx", "vel_v", "vel_h", "aux"]

    def __init__(
        self,
        ozimux_host="<broadcast>",
        ozimux_port=None,
        payload_summary_host="<broadcast>",
        payload_summary_port=None,
        update_rate=5,
        station="auto_rx",
    ):
        """ Initialise an OziUploader Object.

        Args:
            ozimux_host (str): UDP host to push ozimux/oziplotter messages to.
            ozimux_port (int): UDP port to push ozimux/oziplotter messages to. Set to None to disable.
            payload_summary_host (str): UDP host to push payload summary messages to.
            payload_summary_port (int): UDP port to push payload summary messages to. Set to None to disable.
            update_rate (int): Time in seconds between updates.
        """

        self.ozimux_host = ozimux_host
        self.ozimux_port = ozimux_port
        self.payload_summary_host = payload_summary_host
        self.payload_summary_port = payload_summary_port
        self.update_rate = update_rate
        self.station = station

        # Input Queue.
        self.input_queue = Queue()

        # Start the input queue processing thread.
        self.input_processing_running = True
        self.input_thread = Thread(target=self.process_queue)
        self.input_thread.start()

        self.log_info("Started OziMux / Payload Summary Exporter")

    def send_ozimux_telemetry(self, telemetry):
        """ Send a packet of telemetry into the network in OziMux/OziPlotter-compatible format.

        Args:
            telemetry (dict): Telemetry dictionary to send.

        """

        _short_time = telemetry["datetime_dt"].strftime("%H:%M:%S")
        _sentence = "TELEMETRY,%s,%.5f,%.5f,%d\n" % (
            _short_time,
            telemetry["lat"],
            telemetry["lon"],
            telemetry["alt"],
        )

        try:
            _ozisock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

            # Set up socket for broadcast, and allow re-use of the address
            _ozisock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            _ozisock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            # Under OSX we also need to set SO_REUSEPORT to 1
            try:
                _ozisock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
            except:
                pass

            try:
                _ozisock.sendto(
                    _sentence.encode("ascii"), (self.ozimux_host, self.ozimux_port)
                )
            # Catch any socket errors, that may occur when attempting to send to a broadcast address
            # when there is no network connected. In this case, re-try and send to localhost instead.
            except socket.error as e:
                self.log_debug(
                    "Send to broadcast address failed, sending to localhost instead."
                )
                _ozisock.sendto(
                    _sentence.encode("ascii"), ("127.0.0.1", self.ozimux_port)
                )

            _ozisock.close()

        except Exception as e:
            self.log_error("Failed to send OziMux packet: %s" % str(e))

    def send_payload_summary(self, telemetry):
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
                "station": self.station,
                "callsign": telemetry["id"],
                "latitude": telemetry["lat"],
                "longitude": telemetry["lon"],
                "altitude": telemetry["alt"],
                "speed": _speed,
                "heading": _heading,
                "time": _short_time,
                "comment": "Radiosonde",
                # Additional fields specifically for radiosondes
                "model": telemetry["type"],
                "freq": telemetry["freq"],
                "temp": telemetry["temp"],
                "frame": telemetry["frame"],
            }

            # Add in any extra fields we may care about.
            for _field in self.EXTRA_FIELDS:
                if _field in telemetry:
                    packet[_field] = telemetry[_field]

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
                    json.dumps(packet).encode("ascii"),
                    (self.payload_summary_host, self.payload_summary_port),
                )
            # Catch any socket errors, that may occur when attempting to send to a broadcast address
            # when there is no network connected. In this case, re-try and send to localhost instead.
            except socket.error as e:
                self.log_debug(
                    "Send to broadcast address failed, sending to localhost instead."
                )
                _s.sendto(
                    json.dumps(packet).encode("ascii"),
                    ("127.0.0.1", self.payload_summary_port),
                )

            _s.close()

        except Exception as e:
            self.log_error("Error sending Payload Summary: %s" % str(e))

    def process_queue(self):
        """ Process packets from the input queue.

        This thread handles packets from the input queue (provided by the decoders)
        Packets are sorted by ID, and a dictionary entry is created. 

        """

        while self.input_processing_running:

            if self.input_queue.qsize() > 0:
                # Dump the queue, keeping the most recent element.
                while not self.input_queue.empty():
                    _telem = self.input_queue.get()

                # Send!
                if self.ozimux_port != None:
                    self.send_ozimux_telemetry(_telem)

                if self.payload_summary_port != None:
                    self.send_payload_summary(_telem)

            time.sleep(self.update_rate)

    def add(self, telemetry):
        """ Add a dictionary of telemetry to the input queue. 

        Args:
            telemetry (dict): Telemetry dictionary to add to the input queue.

        """

        # Check the telemetry dictionary contains the required fields.
        for _field in self.REQUIRED_FIELDS:
            if _field not in telemetry:
                self.log_error("JSON object missing required field %s" % _field)
                return

        # Add it to the queue if we are running.
        if self.input_processing_running:
            self.input_queue.put(telemetry)
        else:
            self.log_error("Processing not running, discarding.")

    def close(self):
        """ Shutdown processing thread. """
        self.log_debug("Waiting for processing thread to close...")
        self.input_processing_running = False

        if self.input_thread is not None:
            self.input_thread.join(60)
            if self.input_thread.is_alive():
                self.log_error("ozimux input thread failed to join")

    def log_debug(self, line):
        """ Helper function to log a debug message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.debug("OziMux - %s" % line)

    def log_info(self, line):
        """ Helper function to log an informational message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.info("OziMux - %s" % line)

    def log_error(self, line):
        """ Helper function to log an error message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.error("OziMux - %s" % line)
