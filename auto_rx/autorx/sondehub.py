#!/usr/bin/env python
#
#   radiosonde_auto_rx - Sondehub DB Uploader
#
#   Uploads telemetry to the 'new' SondeHub ElasticSearch cluster,
#   in the new 'universal' format descried here:
#   https://github.com/projecthorus/radiosonde_auto_rx/wiki/Suggested-Universal-Sonde-Telemetry-Format
#
#   Copyright (C) 2021  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import autorx
import datetime
import glob
import gzip
import json
import logging
import os
import requests
import time
from threading import Thread
from email.utils import formatdate

try:
    # Python 2
    from Queue import Queue
except ImportError:
    # Python 3
    from queue import Queue


class SondehubUploader(object):
    """ Sondehub Uploader Class.

    Accepts telemetry dictionaries from a decoder, buffers them up, and then compresses and uploads
    them to the Sondehub Elasticsearch cluster.

    """

    # SondeHub API endpoint
    SONDEHUB_URL = "https://api.v2.sondehub.org/sondes/telemetry"
    SONDEHUB_STATION_POSITION_URL = "https://api.v2.sondehub.org/listeners"

    def __init__(
        self,
        upload_rate=30,
        upload_timeout=20,
        upload_retries=5,
        user_callsign="N0CALL",
        user_position=None,
        user_antenna="",
        contact_email="",
        user_position_update_rate=6,
    ):
        """ Initialise and start a Sondehub uploader
        
        Args:
            upload_rate (int): How often to upload batches of data.
            upload_timeout (int): Upload timeout.

        """

        self.upload_rate = upload_rate
        self.upload_timeout = upload_timeout
        self.upload_retries = upload_retries
        self.user_callsign = user_callsign
        self.user_position = user_position
        self.user_antenna = user_antenna
        self.contact_email = contact_email
        self.user_position_update_rate = user_position_update_rate

        if self.user_position is None:
            self.inhibit_upload = True
        else:
            self.inhibit_upload = False

        # Input Queue.
        self.input_queue = Queue()

        # Record of when we last uploaded a user station position to Sondehub.
        self.last_user_position_upload = 0

        try:
            # Python 2 check. Python 2 doesnt have gzip.compress so this will throw an exception.
            gzip.compress(b"\x00\x00")

            # Start queue processing thread.
            self.input_processing_running = True
            self.input_process_thread = Thread(target=self.process_queue)
            self.input_process_thread.start()

        except:
            logging.error(
                "Detected Python 2.7, which does not support gzip.compress. Sondehub DB uploading will be disabled."
            )
            self.input_processing_running = False

    def update_station_position(self, lat, lon, alt):
        """ Update the internal station position record. Used when determining the station position by GPSD """
        if self.inhibit_upload:
            # Don't update the internal position array if we aren't uploading our position.
            return
        else:
            self.user_position = (lat, lon, alt)

    def add(self, telemetry):
        """ Add a dictionary of telemetry to the input queue. 

        Args:
            telemetry (dict): Telemetry dictionary to add to the input queue.

        """

        # Attempt to reformat the data.
        _telem = self.reformat_data(telemetry)
        # self.log_debug("Telem: %s" % str(_telem))

        # Add it to the queue if we are running.
        if self.input_processing_running and _telem:
            self.input_queue.put(_telem)
        else:
            self.log_debug("Processing not running, discarding.")

    def reformat_data(self, telemetry):
        """ Take an input dictionary and convert it to the universal format """

        # Init output dictionary
        _output = {
            "software_name": "radiosonde_auto_rx",
            "software_version": autorx.__version__,
            "uploader_callsign": self.user_callsign,
            "uploader_position": self.user_position,
            "uploader_antenna": self.user_antenna,
            "time_received": datetime.datetime.utcnow().strftime(
                "%Y-%m-%dT%H:%M:%S.%fZ"
            ),
        }

        # Mandatory Fields
        # Datetime
        try:
            _output["datetime"] = telemetry["datetime_dt"].strftime(
                "%Y-%m-%dT%H:%M:%S.%fZ"
            )
        except Exception as e:
            self.log_error(
                "Error converting telemetry datetime to string - %s" % str(e)
            )
            self.log_debug("Offending datetime_dt: %s" % str(telemetry["datetime_dt"]))
            return None

        # Handling of different radiosonde types.
        # Unfortunately we've made things difficult for ourselves with how different sonde types are handled
        # in terms of serial number, type, subtype, etc.
        # Until all the decoders are aligned, we have to handle some sonde types differently.
        if telemetry["type"].startswith("RS41"):
            _output["manufacturer"] = "Vaisala"
            _output["type"] = "RS41"
            _output["serial"] = telemetry["id"]
            if "subtype" in telemetry:
                _output["subtype"] = telemetry["subtype"]

        elif telemetry["type"].startswith("RS92"):
            _output["manufacturer"] = "Vaisala"
            _output["type"] = "RS92"
            _output["serial"] = telemetry["id"]
            if "subtype" in telemetry:
                _output["subtype"] = telemetry["subtype"]

        elif telemetry["type"].startswith("DFM"):
            _output["manufacturer"] = "Graw"
            _output["type"] = "DFM"
            _output["subtype"] = telemetry["type"]
            _output["serial"] = telemetry["id"].split("-")[1]

        elif telemetry["type"].startswith("M10") or telemetry["type"].startswith("M20"):
            _output["manufacturer"] = "Meteomodem"
            _output["type"] = telemetry["type"]
            # Strip off leading M10- or M20-
            _output["serial"] = telemetry["id"][4:]

        elif telemetry["type"] == "LMS6":
            _output["manufacturer"] = "Lockheed Martin"
            if "LMSX" in telemetry["id"]:
                _output["type"] = "LMSX-400"
            else:
                _output["type"] = "LMS6-400"
            _output["serial"] = telemetry["id"].split("-")[1]

        elif telemetry["type"] == "MK2LMS":
            _output["manufacturer"] = "Lockheed Martin"
            _output["type"] = "LMS6-1680"
            _output["serial"] = telemetry["id"].split("-")[1]

        elif telemetry["type"] == "IMET":
            _output["manufacturer"] = "Intermet Systems"
            _output["type"] = "iMet-4"
            _output["serial"] = telemetry["id"].split("-")[1]

        elif telemetry["type"] == "IMET5":
            _output["manufacturer"] = "Intermet Systems"
            _output["type"] = "iMet-54"
            _output["serial"] = telemetry["id"].split("-")[1]

        elif telemetry["type"] == "MEISEI":
            _output["manufacturer"] = "Meisei"
            _output["type"] = telemetry["subtype"]
            _output["serial"] = telemetry["id"].split("-")[1]

        elif telemetry["type"] == "MRZ":
            _output["manufacturer"] = "Meteo-Radiy"
            _output["type"] = "MRZ"
            _output["serial"] = telemetry["id"][4:]

        else:
            self.log_error("Unknown Radiosonde Type %s" % telemetry["type"])
            return None

        # Frame Number
        _output["frame"] = telemetry["frame"]

        # Position
        _output["lat"] = telemetry["lat"]
        _output["lon"] = telemetry["lon"]
        _output["alt"] = telemetry["alt"]

        # Optional Fields
        if "temp" in telemetry:
            if telemetry["temp"] > -273.0:
                _output["temp"] = telemetry["temp"]

        if "humidity" in telemetry:
            if telemetry["humidity"] >= 0.0:
                _output["humidity"] = telemetry["humidity"]

        if "pressure" in telemetry:
            if telemetry["pressure"] >= 0.0:
                _output["pressure"] = telemetry["pressure"]

        if "vel_v" in telemetry:
            if telemetry["vel_v"] > -9999.0:
                _output["vel_v"] = telemetry["vel_v"]

        if "vel_h" in telemetry:
            if telemetry["vel_h"] > -9999.0:
                _output["vel_h"] = telemetry["vel_h"]

        if "heading" in telemetry:
            if telemetry["heading"] > -9999.0:
                _output["heading"] = telemetry["heading"]

        if "sats" in telemetry:
            _output["sats"] = telemetry["sats"]

        if "batt" in telemetry:
            if telemetry["batt"] >= 0.0:
                _output["batt"] = telemetry["batt"]

        if "aux" in telemetry:
            _output["xdata"] = telemetry["aux"]

        if "freq_float" in telemetry:
            _output["frequency"] = telemetry["freq_float"]

        if "bt" in telemetry:
            _output["burst_timer"] = telemetry["bt"]

        # Handle the additional SNR and frequency estimation if we have it
        if "snr" in telemetry:
            _output["snr"] = telemetry["snr"]

        if "f_centre" in telemetry:
            _freq = round(telemetry["f_centre"] / 1e3)
            _output["frequency"] = _freq / 1e3

        return _output

    def process_queue(self):
        """ Process data from the input queue, and write telemetry to log files.
        """
        self.log_info("Started Sondehub Uploader Thread.")

        while self.input_processing_running:

            # Process everything in the queue.
            _to_upload = []

            while self.input_queue.qsize() > 0:
                try:
                    _to_upload.append(self.input_queue.get_nowait())
                except Exception as e:
                    self.log_error("Error grabbing telemetry from queue - %s" % str(e))

            # Upload data!
            if len(_to_upload) > 0:
                self.upload_telemetry(_to_upload)

            # If we haven't uploaded our station position recently, re-upload it.
            if (
                time.time() - self.last_user_position_upload
            ) > self.user_position_update_rate * 3600:
                self.station_position_upload()

            # Sleep while waiting for some new data.
            for i in range(self.upload_rate):
                time.sleep(1)
                if self.input_processing_running == False:
                    break

        self.log_info("Stopped Sondehub Uploader Thread.")

    def upload_telemetry(self, telem_list):
        """ Upload an list of telemetry data to Sondehub """

        _data_len = len(telem_list)

        try:
            _start_time = time.time()
            _telem_json = json.dumps(telem_list).encode("utf-8")
            _compressed_payload = gzip.compress(_telem_json)
        except Exception as e:
            self.log_error(
                "Error serialising and compressing telemetry list for upload - %s"
                % str(e)
            )
            return

        _compression_time = time.time() - _start_time
        self.log_debug(
            "Pre-compression: %d bytes, post: %d bytes. %.1f %% compression ratio, in %.1f s"
            % (
                len(_telem_json),
                len(_compressed_payload),
                (len(_compressed_payload) / len(_telem_json)) * 100,
                _compression_time,
            )
        )

        _retries = 0
        _upload_success = False

        _start_time = time.time()

        while _retries < self.upload_retries:
            # Run the request.
            try:
                headers = {
                    "User-Agent": "autorx-" + autorx.__version__,
                    "Content-Encoding": "gzip",
                    "Content-Type": "application/json",
                    "Date": formatdate(timeval=None, localtime=False, usegmt=True),
                }
                _req = requests.put(
                    self.SONDEHUB_URL,
                    _compressed_payload,
                    # TODO: Revisit this second timeout value.
                    timeout=(self.upload_timeout, 6.1),
                    headers=headers,
                )
            except Exception as e:
                self.log_error("Upload Failed: %s" % str(e))
                return

            if _req.status_code == 200:
                # 200 is the only status code that we accept.
                _upload_time = time.time() - _start_time
                self.log_info(
                    "Uploaded %d telemetry packets to Sondehub in %.1f seconds."
                    % (_data_len, _upload_time)
                )
                _upload_success = True
                break

            elif _req.status_code == 500:
                # Server Error, Retry.
                _retries += 1
                continue

            else:
                self.log_error(
                    "Error uploading to Sondehub. Status Code: %d %s."
                    % (_req.status_code, _req.text)
                )
                break

        if not _upload_success:
            self.log_error("Upload failed after %d retries" % (_retries))

    def station_position_upload(self):
        """ 
        Upload a station position packet to SondeHub.

        This uses the PUT /listeners API described here:
        https://github.com/projecthorus/sondehub-infra/wiki/API-(Beta)
        
        """

        if self.inhibit_upload:
            # Position upload inhibited. Ensure user position is set to None, and continue upload of other info.
            self.log_debug("Sondehub station position upload inhibited.")
            self.user_position = None

        _position = {
            "software_name": "radiosonde_auto_rx",
            "software_version": autorx.__version__,
            "uploader_callsign": self.user_callsign,
            "uploader_position": self.user_position,
            "uploader_antenna": self.user_antenna,
            "uploader_contact_email": self.contact_email,
            "mobile": False,  # Hardcoded mobile=false setting - Mobile stations should be using Chasemapper.
        }

        _retries = 0
        _upload_success = False

        _start_time = time.time()

        while _retries < self.upload_retries:
            # Run the request.
            try:
                headers = {
                    "User-Agent": "autorx-" + autorx.__version__,
                    "Content-Type": "application/json",
                    "Date": formatdate(timeval=None, localtime=False, usegmt=True),
                }
                _req = requests.put(
                    self.SONDEHUB_STATION_POSITION_URL,
                    json=_position,
                    # TODO: Revisit this second timeout value.
                    timeout=(self.upload_timeout, 6.1),
                    headers=headers,
                )
            except Exception as e:
                self.log_error("Upload Failed: %s" % str(e))
                return

            if _req.status_code == 200:
                # 200 is the only status code that we accept.
                _upload_time = time.time() - _start_time
                self.log_info("Uploaded station information to Sondehub.")
                _upload_success = True
                break

            elif _req.status_code == 500:
                # Server Error, Retry.
                _retries += 1
                continue

            else:
                self.log_error(
                    "Error uploading station information to Sondehub. Status Code: %d %s."
                    % (_req.status_code, _req.text)
                )
                break

        if not _upload_success:
            self.log_error(
                "Station information upload failed after %d retries" % (_retries)
            )
            self.log_debug(f"Attempted to upload {json.dumps(_position)}")

        self.last_user_position_upload = time.time()

    def close(self):
        """ Close input processing thread. """
        self.input_processing_running = False

    def running(self):
        """ Check if the uploader thread is running. 

        Returns:
            bool: True if the uploader thread is running.
        """
        return self.input_processing_running

    def log_debug(self, line):
        """ Helper function to log a debug message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.debug("Sondehub Uploader - %s" % line)

    def log_info(self, line):
        """ Helper function to log an informational message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.info("Sondehub Uploader - %s" % line)

    def log_error(self, line):
        """ Helper function to log an error message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.error("Sondehub Uploader - %s" % line)


if __name__ == "__main__":
    # Test Script
    logging.basicConfig(
        format="%(asctime)s %(levelname)s:%(message)s", level=logging.DEBUG
    )
    _test = SondehubUploader()
    time.sleep(5)
    _test.close()
