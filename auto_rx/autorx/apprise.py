#!/usr/bin/env python
#
#   radiosonde_auto_rx - Apprise Notifications
#
#   Copyright (C) 2026 Mark Jessop
#   Released under GNU GPL v3 or later

import autorx
import datetime
import logging
import time
from pathlib import Path
from queue import Queue
from threading import Thread
from .config import read_auto_rx_config
from .utils import position_info, strip_sonde_serial
from .geometry import GenericTrack


class AppriseNotification(object):
    """ Radiosonde Apprise Notification Class.

    Accepts telemetry dictionaries from a decoder, and sends notifictions for various events.
    Incoming telemetry is processed via a queue, so this object should be thread safe.

    """

    # We require the following fields to be present in the input telemetry dict.
    REQUIRED_FIELDS = ["id", "lat", "lon", "alt", "type", "freq"]

    def __init__(
        self,
        apprise_urls = [],
        subject=None,
        nearby_landing_subject=None,
        station_position=None,
        launch_notifications=True,
        landing_notifications=True,
        encrypted_sonde_notifications=True,
        landing_range_threshold=50,
        landing_altitude_threshold=1000,
        landing_descent_trip=10,
    ):
        """ Init a new Apprise Notification Thread """
        self.apprise_urls = apprise_urls
        self.subject = subject
        self.nearby_landing_subject = nearby_landing_subject 
        self.station_position = station_position
        self.launch_notifications = launch_notifications
        self.landing_notifications = landing_notifications
        self.encrypted_sonde_notifications = encrypted_sonde_notifications
        self.landing_range_threshold = landing_range_threshold
        self.landing_altitude_threshold = landing_altitude_threshold
        self.landing_descent_trip = landing_descent_trip

        # Dictionary to track sonde IDs
        self.sondes = {}

        self.max_age = 3600 * 2  # Only store telemetry for 2 hours

        # Input Queue.
        self.input_queue = Queue()

        # Try and initialise Apprise
        import apprise

        self.apasset = apprise.AppriseAsset(
            app_id=f"radiosonde_auto_rx {autorx.__version__}",
            app_desc="radiosonde_auto_rx notifications",
            app_url="https://github.com/projecthorus/radiosonde_auto_rx",
            image_path_mask=str(Path(__file__).resolve().with_name("autorx_256x256.png")),
            image_url_mask="https://raw.githubusercontent.com/projecthorus/radiosonde_auto_rx/refs/heads/master/autorx_256x256.png",
            theme="default")
        self.apobj = apprise.Apprise(asset=self.apasset)

        for _url in self.apprise_urls:
            try:
                self.apobj.add(_url)
                self.log_info(f"Added notification url {_url.split(':')[0]}")
            except:
                self.log_error(f"Error adding {_url.split(':')[0]} notification url.")

        # Start queue processing thread.
        self.input_processing_running = True
        self.input_thread = Thread(target=self.process_queue)
        self.input_thread.start()

        self.log_info(f"Started Apprise Notifier Thread")

    def add(self, telemetry):
        """ Add a telemetery dictionary to the input queue. """
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

    def process_queue(self):
        """ Process packets from the input queue. """
        while self.input_processing_running:

            # Process everything in the queue.
            while self.input_queue.qsize() > 0:
                try:
                    _telem = self.input_queue.get_nowait()
                    self.process_telemetry(_telem)

                except Exception as e:
                    self.log_error("Error processing telemetry dict - %s" % str(e))

            # Sleep while waiting for some new data.
            time.sleep(2)

            self.clean_telemetry_store()

    def process_telemetry(self, telemetry):
        """ Process a new telemmetry dict, and send notifications if it is a new sonde. """
        _id = telemetry["id"]

        if _id not in self.sondes:
            self.sondes[_id] = {
                "last_time": time.time(),
                "descending_trip": 0,
                "ascent_trip": False,
                "descent_notified": False,
                "track": GenericTrack(max_elements=20),
            }

            # Add initial position to the track info.
            self.sondes[_id]["track"].add_telemetry(
                {
                    "time": telemetry["datetime_dt"],
                    "lat": telemetry["lat"],
                    "lon": telemetry["lon"],
                    "alt": telemetry["alt"],
                }
            )

            if "encrypted" in telemetry:
                if telemetry["encrypted"] and self.encrypted_sonde_notifications:
                    try:
                        # This is a new Encrypted Radiosonde, send an email.
                        msg = "Encrypted Radiosonde Detected:\n"
                        msg += "\n"

                        if "subtype" in telemetry:
                            telemetry["type"] = telemetry["subtype"]

                        msg += "Serial:    %s\n" % _id
                        msg += "Type:      %s\n" % telemetry["type"]
                        msg += "Frequency: %s\n" % telemetry["freq"]
                        msg += "Time Detected: %sZ\n" % telemetry["datetime_dt"].isoformat()

                        # Construct subject
                        _subject = self.subject
                        _subject = _subject.replace("<id>", telemetry["id"])
                        _subject = _subject.replace("<type>", telemetry["type"])
                        _subject = _subject.replace("<freq>", telemetry["freq"])

                        if "encrypted" in telemetry:
                            if telemetry["encrypted"] == True:
                                _subject += " - ENCRYPTED SONDE"

                        self.send_notifications(subject=_subject, message=msg)

                    except Exception as e:
                        self.log_error("Error sending E-mail - %s" % str(e))

            elif self.launch_notifications:

                try:
                    # This is a new sonde. Send the email.
                    msg = "Sonde launch detected:\n"
                    msg += "\n"

                    msg += "Serial:    %s\n" % _id
                    msg += "Type:      %s\n" % telemetry["type"]
                    msg += "Frequency: %s\n" % telemetry["freq"]
                    msg += "Position:  %.5f,%.5f\n" % (
                        telemetry["lat"],
                        telemetry["lon"],
                    )
                    msg += "Altitude:  %d m\n" % round(telemetry["alt"])

                    if self.station_position != None:
                        _relative_position = position_info(
                            self.station_position,
                            (telemetry["lat"], telemetry["lon"], telemetry["alt"]),
                        )
                        msg += "Range:     %.1f km\n" % (
                            _relative_position["straight_distance"] / 1000.0
                        )
                        msg += "Bearing:   %d degrees True\n" % int(
                            _relative_position["bearing"]
                        )

                    msg += "\n"
                    msg += "https://sondehub.org/%s\n" % strip_sonde_serial(_id)
                    msg += "https://sondehub.org/card/%s\n" % strip_sonde_serial(_id)

                    # Construct subject
                    _subject = self.subject
                    _subject = _subject.replace("<id>", telemetry["id"])
                    _subject = _subject.replace("<type>", telemetry["type"])
                    _subject = _subject.replace("<freq>", telemetry["freq"])

                    self.send_notifications(subject=_subject, message=msg)

                except Exception as e:
                    self.log_error("Error sending E-mail - %s" % str(e))

        else:
            # Update track data.
            _sonde_state = self.sondes[_id]["track"].add_telemetry(
                {
                    "time": telemetry["datetime_dt"],
                    "lat": telemetry["lat"],
                    "lon": telemetry["lon"],
                    "alt": telemetry["alt"],
                }
            )
            # Update last seen time, so we know when to clean out this sondes data from memory.
            self.sondes[_id]["last_time"] = time.time()

            # We have seen this sonde recently. Let's check it's descending...

            if self.sondes[_id]["descent_notified"] == False and _sonde_state:

                # Set a flag if the sonde has passed above the landing altitude threshold.
                # This is used along with the descending trip to trigger a landing email notification.
                if (telemetry["alt"] > self.landing_altitude_threshold):
                    self.sondes[_id]["ascent_trip"] = True 

                # If the sonde is below our threshold altitude, *and* is descending at a reasonable rate, increment.
                if (telemetry["alt"] < self.landing_altitude_threshold) and (
                    _sonde_state["ascent_rate"] < -2.0
                ):
                    self.sondes[_id]["descending_trip"] += 1

                if (self.sondes[_id]["descending_trip"] > self.landing_descent_trip) and self.sondes[_id]["ascent_trip"]:
                    # We've seen this sonde descending for enough time now AND we have also seen it go above the landing threshold,
                    # so it's likely been on a flight and isnt just bouncing around on the ground.
                    # Note that we've passed the descent threshold, so we shouldn't analyze anything from this sonde anymore.
                    self.sondes[_id]["descent_notified"] = True

                    self.log_debug("Sonde %s triggered descent threshold." % _id)

                    # Let's check if it's within our notification zone.

                    if self.station_position != None:
                        _relative_position = position_info(
                            self.station_position,
                            (telemetry["lat"], telemetry["lon"], telemetry["alt"]),
                        )

                        _range = _relative_position["straight_distance"] / 1000.0
                        self.log_debug(
                            "Descending sonde is %.1f km away from station location"
                            % _range
                        )

                        if (
                            _range < self.landing_range_threshold
                            and self.landing_notifications
                        ):
                            self.log_info(
                                "Landing sonde %s triggered range threshold." % _id
                            )

                            msg = "Nearby sonde landing detected:\n\n"

                            msg += "Serial:    %s\n" % _id
                            msg += "Type:      %s\n" % telemetry["type"]
                            msg += "Frequency: %s\n" % telemetry["freq"]
                            msg += "Position:  %.5f,%.5f\n" % (
                                telemetry["lat"],
                                telemetry["lon"],
                            )
                            msg += "Altitude:  %d m\n" % round(telemetry["alt"])

                            msg += "Range:     %.1f km (Threshold: %.1fkm)\n" % (
                                _relative_position["straight_distance"] / 1000.0,
                                self.landing_range_threshold,
                            )
                            msg += "Bearing:   %d degrees True\n" % int(
                                _relative_position["bearing"]
                            )

                            msg += "\n"
                            msg += "https://sondehub.org/%s\n" % strip_sonde_serial(_id)
                            msg += (
                                "https://sondehub.org/card/%s\n"
                                % strip_sonde_serial(_id)
                            )

                            # Construct subject
                            _subject = self.nearby_landing_subject
                            _subject = _subject.replace("<id>", _id)
                            _subject = _subject.replace("<type>", telemetry["type"])
                            _subject = _subject.replace("<freq>", telemetry["freq"])

                            self.send_notifications(subject=_subject, message=msg)

                    else:
                        # No station position to work with! Bomb out at this point
                        return

    def send_notifications(
        self, subject="radiosonde_auto_rx Station Notification", message="Foobar"
    ):
        """ Generic Apprise notification function, for sending error messages. """
        try:
            msg = "radiosonde_auto_rx Notification Message:\n"
            msg += "Timestamp: %s (local)\n" % datetime.datetime.now().isoformat()
            msg += message
            msg += "\n"

            # Construct subject
            _subject = subject

            self.log_debug("Title: %s" % _subject)
            self.log_debug("Message: %s" % msg)

            try:
                self.apobj.notify(
                    title=_subject,
                    body=msg
                )
            except Exception as e:
                self.log_error(f"Error sending notification: {str(e)}")

            self.log_info("Notifications sent.")
        except Exception as e:
            self.log_error("Error sending notification - %s" % str(e))

        pass

    def clean_telemetry_store(self):
        """ Remove any old data from the telemetry store """

        _now = time.time()
        _telem_ids = list(self.sondes.keys())
        for _id in _telem_ids:
            # If the most recently telemetry is older than self.max_age, remove all data for
            # that sonde from the local store.
            if (_now - self.sondes[_id]["last_time"]) > self.max_age:
                self.sondes.pop(_id)
                self.log_debug("Removed Sonde #%s from archive." % _id)

    def close(self):
        """ Close input processing thread. """
        self.log_debug("Waiting for processing thread to close...")
        self.input_processing_running = False

        if self.input_thread is not None:
            self.input_thread.join(60)
            if self.input_thread.is_alive():
                self.log_error("Apprise notification input thread failed to join")

    def running(self):
        """ Check if the logging thread is running.

        Returns:
            bool: True if the logging thread is running.
        """
        return self.input_processing_running

    def log_debug(self, line):
        """ Helper function to log a debug message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.debug("Apprise Notifications - %s" % line)

    def log_info(self, line):
        """ Helper function to log an informational message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.info("Apprise Notifications - %s" % line)

    def log_error(self, line):
        """ Helper function to log an error message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.error("Apprise Notifications - %s" % line)


if __name__ == "__main__":
    # Test Script - Send an example notification using the settings in station.cfg
    import sys

    logging.basicConfig(
        format="%(asctime)s %(levelname)s:%(message)s", level=logging.DEBUG
    )

    # Read in the station config, which contains the email settings.
    config = read_auto_rx_config("station.cfg", no_sdr_test=True)

    # Start up an apprise notifification object.
    _apprise_notification = AppriseNotification(
        apprise_urls=config["apprise_urls"],
        subject=config["apprise_subject"],
        nearby_landing_subject=config["apprise_nearby_landing_subject"],
        station_position=(-10.0, 10.0, 0.0,),
        landing_notifications=True,
        launch_notifications=True,
    )

    # Wait a second..
    time.sleep(1)

    if len(sys.argv) > 1:
        _apprise_notification.send_notifications(message="This is a test notification message")
        time.sleep(1)

    # Add in a packet of telemetry, which will cause the email notifier to send an email.
    print("Testing launch alert.")
    _apprise_notification.add(
        {
            "id": "N1234557",
            "frame": 10,
            "lat": -10.0,
            "lon": 10.0,
            "alt": 10000,
            "temp": 1.0,
            "type": "RS41",
            "freq": "401.520 MHz",
            "freq_float": 401.52,
            "heading": 0.0,
            "vel_h": 5.1,
            "vel_v": -5.0,
            "datetime_dt": datetime.datetime.now(datetime.timezone.utc),
        }
    )

    time.sleep(10)

    print("Testing encrypted sonde alert.")
    _apprise_notification.add(
        {
            "id": "R1234557",
            "frame": 10,
            "lat": 0.0,
            "lon": 0.0,
            "alt": 0,
            "temp": 1.0,
            "type": "RS41",
            "subtype": "RS41-SGM",
            "freq": "401.520 MHz",
            "freq_float": 401.52,
            "heading": 0.0,
            "vel_h": 5.1,
            "vel_v": -5.0,
            "datetime_dt": datetime.datetime.now(datetime.timezone.utc),
            "encrypted": True
        }
    )

    # Wait a little bit before shutting down.
    time.sleep(5)

    _test = {
        "id": "N1234557",
        "frame": 11,
        "lat": -10.01,
        "lon": 10.01,
        "alt": 1100,
        "temp": 1.0,
        "type": "RS41",
        "freq": "401.520 MHz",
        "freq_float": 401.52,
        "heading": 0.0,
        "vel_h": 5.1,
        "vel_v": -5.0,
        "datetime_dt": datetime.datetime.now(datetime.timezone.utc),
    }

    print("Testing landing alert.")
    for i in range(30):
        _tosubmit = _test.copy()
        _apprise_notification.add(_tosubmit)
        _test["alt"] = _test["alt"] - 10.0
        _test["lat"] = _test["lat"] + 0.001
        _test["lon"] = _test["lon"] + 0.001
        _test["datetime_dt"] = datetime.datetime.now(datetime.timezone.utc)
        time.sleep(1)

    time.sleep(60)

    _apprise_notification.close()
