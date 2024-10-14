#!/usr/bin/env python
#
#   radiosonde_auto_rx - Hamlib Rotator Control
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under MIT License
#
import logging
import socket
import time
import numpy as np

from queue import Queue
from threading import Thread, Lock
from autorx.utils import position_info


def read_rotator(rotctld_host="localhost", rotctld_port=4533, timeout=5):
    """ Attempt to read a position from a rotctld server.

    Args:
        rotctld_host (str): Hostname of a rotctld instance.
        rotctld_port (int): Port (TCP) of a rotctld instance.

    Returns:
        If successful:
            list: [azimuth, elevation]
        If unsuccessful:
            None
    """

    try:
        # Initialize the socket.
        _s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        _s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        _s.settimeout(timeout)

        # Attempt to connect to the rotctld server.
        _s.connect((rotctld_host, rotctld_port))

        # Send position request
        _s.send(b"p\n")

        # Attempt to receive reply.
        _reply = _s.recv(4096)

        # Split reply into lines
        _fields = _reply.decode("ascii").split("\n")
        # Check for an error response, indicated by 'RPRT' in the returned line.
        if "RPRT" in _fields[0]:
            logging.error("Rotator - rotctld reported error - %s" % _fields[0].strip())
            return None
        else:
            # Attempt to parse the lines as floating point numbers.
            _azimuth = float(_fields[0])
            _elevation = float(_fields[1])

            return [_azimuth, _elevation]

    except Exception as e:
        logging.error("Rotator - Error when reading rotator position - %s" % str(e))
        return None


def set_rotator(
    rotctld_host="localhost", rotctld_port=4533, azimuth=0.0, elevation=0.0, timeout=5
):
    """ Attempt to read a position from a rotctld server.

    Args:
        rotctld_host (str): Hostname of a rotctld instance.
        rotctld_port (int): Port (TCP) of a rotctld instance.
        azimuth (float): Target azimuth in degrees True
        elevation (float): Target elevation in degrees.

    Returns:
        If successful:
            True
        If unsuccessful:
            False
    """

    try:
        # Initialize the socket.
        _s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        _s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        _s.settimeout(timeout)

        # Attempt to connect to the rotctld server.
        _s.connect((rotctld_host, rotctld_port))

        # Clip the Azimuth and elevation to sane limits.
        _az = azimuth % 360.0
        _el = np.clip(elevation, 0, 90)

        # Send position set command
        _cmd = "P %.1f %.1f\n" % (_az, _el)
        _s.send(_cmd.encode("ascii"))

        # Attempt to receive reply.
        _reply = _s.recv(4096).decode("ascii")

        # Check for an 'OK' response, indicated by 'RPRT 0'
        if "RPRT 0" in _reply:
            return True
        else:
            # Anything else indicates an error.
            logging.error("Rotator - rotctld reported error: %s" % _reply.strip())
            return False

    except Exception as e:
        logging.error("Rotator - Error when setting rotator position - %s" % str(e))
        return False


class Rotator(object):
    """ Auto-RX Rotator Control Class

    Accepts telemetry dictionaries from a decoder, and turns a rotctld-connected rotator
    to point at the radiosonde.

    """

    # We require the following fields to be present in the input telemetry dict.
    REQUIRED_FIELDS = ["id", "lat", "lon", "alt", "type", "freq"]

    def __init__(
        self,
        station_position=(0.0, 0.0, 0.0),
        rotctld_host="localhost",
        rotctld_port=4533,
        rotator_update_rate=30,
        rotator_update_threshold=5.0,
        rotator_homing_enabled=False,
        rotator_homing_delay=10,
        rotator_home_position=[0.0, 0.0],
        azimuth_only=False
    ):
        """ Start a new Rotator Control object. 

        Args:
            station_position (tuple): The location of the receiving station, as a (lat, lon, alt) tuple.
            
            rotctld_host (str): The hostname where a rotctld instance is running.
            rotctld_port (int): The (TCP) port where a rotctld instance is running.
            
            rotator_update_rate (int): Update the current rotator position to the latest telemetry position every X seconds.
            rotator_update_threshold (float): Only update the rotator position if the new position is at least X degrees
                away from the current position in azimuth or elevation.

            rotator_homing_enabled (bool): If enabled, turn the rotator to a provided azimuth/elevation on startup,
                and whenever no telemetry has been observed for <rotator_homing_delay> minutes.
            rotator_homing_delay (int): Move the rotator to a home position if no telemetry is received within X minutes.
            rotator_home_position (tuple): Rotator home position, as an [azimuth, elevation] list, in degrees (true).
            azimuth_only (bool): If set, force all elevation data to 0.

        """

        # Create local copies of the init arguments.
        self.station_position = station_position
        self.rotctld_host = rotctld_host
        self.rotctld_port = rotctld_port
        self.rotator_update_rate = rotator_update_rate
        self.rotator_update_threshold = rotator_update_threshold
        self.rotator_homing_enabled = rotator_homing_enabled
        self.rotator_homing_delay = rotator_homing_delay
        self.rotator_home_position = rotator_home_position
        self.azimuth_only = azimuth_only

        # Latest telemetry.
        self.latest_telemetry = None
        self.latest_telemetry_time = 0
        self.last_telemetry_time = 0
        self.telem_lock = Lock()

        # Homing state
        self.rotator_homed = False

        # Input Queue.
        self.input_queue = Queue()

        # Start queue processing thread.
        self.rotator_thread_running = True
        self.rotator_thread = Thread(target=self.rotator_update_thread)
        self.rotator_thread.start()

        self.log_info("Started Rotator Thread")

    def add(self, telemetry):
        """ Add a telemetery dictionary to the input queue. """
        # Check the telemetry dictionary contains the required fields.
        for _field in self.REQUIRED_FIELDS:
            if _field not in telemetry:
                self.log_error("JSON object missing required field %s" % _field)
                return

        # Update the latest telemetry store.
        self.telem_lock.acquire()
        try:
            self.latest_telemetry = telemetry.copy()
            self.latest_telemetry_time = time.time()
        finally:
            self.telem_lock.release()

    def move_rotator(self, azimuth, elevation):
        """ Move the rotator to a new position, if the new position
            is further than <rotator_update_threshold> away from the current position
        """

        # Get current position
        _pos = read_rotator(
            rotctld_host=self.rotctld_host, rotctld_port=self.rotctld_port
        )

        # If we can't get the current position of the rotator, then we won't be able to move it either
        # May as well return immediately.
        if _pos == None:
            return False

        # Otherwise, compare the current position with the new position.
        # Modulo the azimuth with 360.0, in case we are in an overwind region.
        _curr_az = _pos[0] % 360.0
        _curr_el = _pos[1]

        _azimuth_diff = abs(azimuth - _curr_az)
        if (_azimuth_diff > 180.0):
            _azimuth_diff = abs(_azimuth_diff - 360.0)

        # For azimuth-only rotators, we force elevation to 0, and ignore any incoming elevation data
        # (which should be 0 anyway)
        if self.azimuth_only:
            _curr_el = 0.0
            elevation = 0.0

        if (_azimuth_diff > self.rotator_update_threshold) or (
            abs(elevation - _curr_el) > self.rotator_update_threshold
        ):
            # Move to the target position.
            self.log_info(
                "New rotator target is outside current antenna view (%.1f, %.1f +/- %.1f deg), moving rotator to %.1f, %.1f"
                % (
                    _curr_az,
                    _curr_el,
                    self.rotator_update_threshold,
                    azimuth,
                    elevation,
                )
            )
            return set_rotator(
                rotctld_host=self.rotctld_host,
                rotctld_port=self.rotctld_port,
                azimuth=azimuth,
                elevation=elevation,
            )
        else:
            # We are close enough to the target position, no need to move yet.
            self.log_debug(
                "New target is within current antenna view (%.1f, %.1f +/- %.1f deg), not moving rotator."
                % (_curr_az, _curr_el, self.rotator_update_threshold)
            )
            return True

    def home_rotator(self):
        """ Move the rotator to it's home position """
        if not self.rotator_homed:
            self.log_info("Moving rotator to home position.")
            self.move_rotator(
                azimuth=self.rotator_home_position[0],
                elevation=self.rotator_home_position[1],
            )
            self.rotator_homed = True

    def rotator_update_thread(self):
        """ Rotator updater thread """

        if self.rotator_homing_enabled:
            # Move rotator to 'home' position on startup.
            self.home_rotator()

        while self.rotator_thread_running:

            _telem = None
            _telem_time = None

            # Grab the latest telemetry data
            self.telem_lock.acquire()
            try:
                if self.latest_telemetry != None:
                    _telem = self.latest_telemetry.copy()
                    _telem_time = self.latest_telemetry_time
            finally:
                self.telem_lock.release()

            # Proceed if we have valid telemetry.
            if _telem != None:
                try:
                    # Check if the telemetry is very old.
                    _telem_age = time.time() - _telem_time

                    # If the telemetry is older than our homing delay, move to our home position.
                    if _telem_age > self.rotator_homing_delay * 60.0 and not self.rotator_homed:
                        self.home_rotator()

                    else:
                        # Check that the station position is not 0,0
                        if (self.station_position[0] == 0.0) and (
                            self.station_position[1] == 0.0
                        ):
                            self.log_error(
                                "Station position is 0,0 - not moving rotator."
                            )
                        # Check if this is a stale telemetry entry
                        elif self.latest_telemetry_time == self.last_telemetry_time:
                            self.log_debug(
                                "Telemetry received is not new, not moving rotator."
                            )
                        else:
                            # Otherwise, calculate the new azimuth/elevation.
                            _position = position_info(
                                self.station_position,
                                [_telem["lat"], _telem["lon"], _telem["alt"]],
                            )

                            # Move to the new position
                            self.move_rotator(
                                _position["bearing"], _position["elevation"]
                            )

                            self.rotator_homed = False

                except Exception as e:
                    self.log_error("Error handling new telemetry - %s" % str(e))

            # Update last telemetry time
            self.last_telemetry_time = self.latest_telemetry_time

            # Wait until the next update time.
            _i = 0
            while (_i < self.rotator_update_rate) and self.rotator_thread_running:
                time.sleep(1)
                _i += 1

    def update_station_position(self, lat, lon, alt):
        """ Update the internal station position record. Used when determining the station position by GPSD """
        self.station_position = (lat, lon, alt)

    def close(self):
        """ Close input processing thread. """
        self.rotator_thread_running = False

        if self.rotator_thread is not None:
            self.rotator_thread.join(60)
            if self.rotator_thread.is_alive():
                self.log_error("rotator control thread failed to join")

        self.log_debug("Stopped rotator control thread.")

    def running(self):
        """ Check if the logging thread is running.

        Returns:
            bool: True if the logging thread is running.
        """
        return self.rotator_thread_running

    def log_debug(self, line):
        """ Helper function to log a debug message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.debug("Rotator - %s" % line)

    def log_info(self, line):
        """ Helper function to log an informational message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.info("Rotator - %s" % line)

    def log_error(self, line):
        """ Helper function to log an error message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.error("Rotator - %s" % line)


if __name__ == "__main__":
    import sys

    _host = sys.argv[1]

    print(read_rotator(rotctld_host=_host))
    print(set_rotator(rotctld_host=_host, azimuth=0.0, elevation=0.0))
