#!/usr/bin/env python
#
#   radiosonde_auto_rx - Habitat Exporter
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import crcmod
import datetime
import logging
import random
import requests
import time
import traceback
import json
from base64 import b64encode
from hashlib import sha256
from queue import Queue
from threading import Thread, Lock
from . import __version__ as auto_rx_version

# These get replaced out after init
url_habitat_uuids = ""
url_habitat_db = ""
habitat_url = ""

# CRC16 function
def crc16_ccitt(data):
    """
    Calculate the CRC16 CCITT checksum of *data*.
    (CRC16 CCITT: start 0xFFFF, poly 0x1021)

    Args:
        data (str): String to be CRC'd. The string will be encoded to ASCII prior to CRCing.

    Return:
        str: Resultant checksum as two bytes of hexadecimal.

    """
    crc16 = crcmod.predefined.mkCrcFun("crc-ccitt-false")
    # Encode to ASCII.
    _data_ascii = data.encode("ascii")
    return hex(crc16(_data_ascii))[2:].upper().zfill(4)


def sonde_telemetry_to_sentence(telemetry, payload_callsign=None, comment=None):
    """ Convert a telemetry data dictionary into a UKHAS-compliant telemetry sentence.
    
    Args:
        telemetry (dict): A sonde telemetry dictionary. Refer to the description in the autorx.decode.SondeDecoder docs.
        payload_callsign (str): If supplied, override the callsign field with this string.
        comment (str): Optional data to add to the comment field of the output sentence.

    Returns:
        str: UKHAS-compliant telemetry sentence for uploading to Habitat


    """
    # We only want HH:MM:SS for uploading to habitat.
    _short_time = telemetry["datetime_dt"].strftime("%H:%M:%S")

    if payload_callsign is None:
        # If we haven't been supplied a callsign, we generate one based on the serial number.
        _callsign = "RS_" + telemetry["id"]
    else:
        _callsign = payload_callsign

    _sentence = "$$%s,%d,%s,%.5f,%.5f,%d,%.1f,%.1f,%.1f" % (
        _callsign,
        telemetry["frame"],
        _short_time,
        telemetry["lat"],
        telemetry["lon"],
        int(telemetry["alt"]),  # Round to the nearest metre.
        telemetry["vel_h"],
        telemetry["temp"],
        telemetry["humidity"],
    )

    if "f_centre" in telemetry:
        # We have an estimate of the sonde's centre frequency from the modem, use this in place of
        # the RX frequency.
        # Round to 1 kHz
        _freq = round(telemetry["f_centre"] / 1000.0)
        # Convert to MHz.
        _freq = "%.3f MHz" % (_freq / 1e3)
    else:
        # Otherwise, use the normal frequency.
        _freq = telemetry["freq"]

    # Add in a comment field, containing the sonde type, serial number, and frequency.
    _sentence += ",%s %s %s" % (telemetry["type"], telemetry["id"], _freq)

    # Add in pressure data, if valid (not -1)
    if telemetry["pressure"] > 0.0:
        _sentence += " %.1fhPa" % telemetry["pressure"]

    # Check for Burst/Kill timer data, and add in.
    if "bt" in telemetry:
        if (telemetry["bt"] != -1) and (telemetry["bt"] != 65535):
            _sentence += " BT %s" % time.strftime(
                "%H:%M:%S", time.gmtime(telemetry["bt"])
            )

    # Add in battery voltage, if the field is valid (e.g. not -1)
    if telemetry["batt"] > 0.0:
        _sentence += " %.1fV" % telemetry["batt"]

    # Add on any custom comment data if provided.
    if comment != None:
        comment = comment.replace(",", "_")
        _sentence += " " + comment

    _checksum = crc16_ccitt(_sentence[2:])
    _output = _sentence + "*" + _checksum + "\n"
    return _output


#
# Functions for uploading a listener position to Habitat.
# Derived from https://raw.githubusercontent.com/rossengeorgiev/hab-tools/master/spot2habitat_chase.py
#
callsign_init = False

uuids = []


def check_callsign(callsign, timeout=10):
    """
    Check if a payload document exists for a given callsign. 

    This is done in a bit of a hack-ish way at the moment. We just check to see if there have
    been any reported packets for the payload callsign on the tracker.
    This should really be replaced with the correct call into the habitat tracker.

    Args:
        callsign (str): Payload callsign to search for.
        timeout (int): Timeout for the search, in seconds. Defaults to 10 seconds.

    Returns:
        bool: True if callsign has been observed within the last 6 hour, False otherwise.
    """

    _url_check_callsign = "http://legacy-snus.habhub.org/tracker/datanew.php?mode=6hours&type=positions&format=json&max_positions=10&position_id=0&vehicle=%s"

    logging.debug("Habitat - Checking if %s has been observed recently..." % callsign)
    # Perform the request
    _r = requests.get(_url_check_callsign % callsign, timeout=timeout)

    try:
        # Read the response in as JSON
        _r_json = _r.json()

        # Read out the list of positions for the requested callsign
        _positions = _r_json["positions"]["position"]

        # If there is at least one position returned, we assume there is a valid payload document.
        if len(_positions) > 0:
            logging.info(
                "Habitat - Callsign %s already present in Habitat DB, not creating new payload doc."
                % callsign
            )
            return True
        else:
            # Otherwise, we don't, and go create one.
            return False

    except Exception as e:
        # Handle errors with JSON parsing.
        logging.error(
            "Habitat - Unable to request payload positions from legacy-snus.habhub.org - %s"
            % str(e)
        )
        return False


# Keep an internal cache for which payload docs we've created so we don't spam couchdb with updates
payload_config_cache = {}


def ISOStringNow():
    return "%sZ" % datetime.datetime.utcnow().isoformat()


def initPayloadDoc(
    serial, description="Meteorology Radiosonde", frequency=401.5, timeout=20
):
    """Creates a payload in Habitat for the radiosonde before uploading"""
    global url_habitat_db

    payload_data = {
        "type": "payload_configuration",
        "name": serial,
        "time_created": ISOStringNow(),
        "metadata": {"description": description},
        "transmissions": [
            {
                "frequency": frequency,
                "modulation": "RTTY",
                "mode": "USB",
                "encoding": "ASCII-8",
                "parity": "none",
                "stop": 2,
                "shift": 350,
                "baud": 50,
                "description": "DUMMY ENTRY, DATA IS VIA radiosonde_auto_rx",
            }
        ],
        "sentences": [
            {
                "protocol": "UKHAS",
                "callsign": serial,
                "checksum": "crc16-ccitt",
                "fields": [
                    {"name": "sentence_id", "sensor": "base.ascii_int"},
                    {"name": "time", "sensor": "stdtelem.time"},
                    {
                        "name": "latitude",
                        "sensor": "stdtelem.coordinate",
                        "format": "dd.dddd",
                    },
                    {
                        "name": "longitude",
                        "sensor": "stdtelem.coordinate",
                        "format": "dd.dddd",
                    },
                    {"name": "altitude", "sensor": "base.ascii_int"},
                    {"name": "speed", "sensor": "base.ascii_float"},
                    {"name": "temperature_external", "sensor": "base.ascii_float"},
                    {"name": "humidity", "sensor": "base.ascii_float"},
                    {"name": "comment", "sensor": "base.string"},
                ],
                "filters": {
                    "post": [
                        {"filter": "common.invalid_location_zero", "type": "normal"}
                    ]
                },
                "description": "radiosonde_auto_rx to Habitat Bridge",
            }
        ],
    }

    # Perform the POST request to the Habitat DB.
    try:
        _r = requests.post(url_habitat_db, json=payload_data, timeout=timeout)

        if _r.json()["ok"] is True:
            logging.info("Habitat - Created a payload document for %s" % serial)
            return True
        else:
            logging.error(
                "Habitat - Failed to create a payload document for %s" % serial
            )
            return False

    except Exception as e:
        logging.error(
            "Habitat - Failed to create a payload document for %s - %s"
            % (serial, str(e))
        )
        return False


def postListenerData(doc, timeout=10):
    global uuids, url_habitat_db
    # do we have at least one uuid, if not go get more
    if len(uuids) < 1:
        fetchUuids()

    # Attempt to add UUID and time data to document.
    try:
        doc["_id"] = uuids.pop()
    except IndexError:
        logging.error("Habitat - Unable to post listener data - no UUIDs available.")
        return False

    doc["time_uploaded"] = ISOStringNow()

    try:
        _r = requests.post(url_habitat_db, json=doc, timeout=timeout)
        return True
    except Exception as e:
        logging.error("Habitat - Could not post listener data - %s" % str(e))
        return False


def fetchUuids(timeout=10):
    global uuids, url_habitat_uuids

    _retries = 5

    while _retries > 0:
        try:
            _r = requests.get(url_habitat_uuids % 10, timeout=timeout)
            uuids.extend(_r.json()["uuids"])
            # logging.debug("Habitat - Got UUIDs")
            return
        except Exception as e:
            logging.error(
                "Habitat - Unable to fetch UUIDs, retrying in 10 seconds - %s" % str(e)
            )
            time.sleep(10)
            _retries = _retries - 1
            continue

    logging.error("Habitat - Gave up trying to get UUIDs.")
    return


def initListenerCallsign(callsign, version="", antenna=""):
    doc = {
        "type": "listener_information",
        "time_created": ISOStringNow(),
        "data": {
            "callsign": callsign,
            "antenna": antenna,
            "radio": "radiosonde_auto_rx %s" % version,
        },
    }

    resp = postListenerData(doc)

    if resp is True:
        # logging.debug("Habitat - Listener Callsign Initialized.")
        return True
    else:
        logging.error("Habitat - Unable to initialize callsign.")
        return False


def uploadListenerPosition(callsign, lat, lon, version="", antenna=""):
    """ Initializer Listener Callsign, and upload Listener Position """

    # Attempt to initialize the listeners callsign
    resp = initListenerCallsign(callsign, version=version, antenna=antenna)
    # If this fails, it means we can't contact the Habitat server,
    # so there is no point continuing.
    if resp is False:
        return False

    doc = {
        "type": "listener_telemetry",
        "time_created": ISOStringNow(),
        "data": {
            "callsign": callsign,
            "chase": False,
            "latitude": lat,
            "longitude": lon,
            "altitude": 0,
            "speed": 0,
        },
    }

    # post position to habitat
    resp = postListenerData(doc)
    if resp is True:
        logging.info("Habitat - Station position uploaded.")
        return True
    else:
        logging.error("Habitat - Unable to upload station position.")
        return False


#
# Habitat Uploader Class
#


class HabitatUploader(object):
    """ 
    Queued Habitat Telemetry Uploader class
    This performs uploads to the Habitat servers, and also handles generation of flight documents.

    Incoming telemetry packets are fed into queue, which is checked regularly.
    If a new callsign is sighted, a payload document is created in the Habitat DB.
    The telemetry data is then converted into a UKHAS-compatible format, before being added to queue to be
    uploaded as network speed permits.

    If an upload attempt times out, the packet is discarded.
    If the queue fills up (probably indicating no network connection, and a fast packet downlink rate),
    it is immediately emptied, to avoid upload of out-of-date packets.

    Note that this uploader object is intended to handle telemetry from multiple sondes
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

    def __init__(
        self,
        user_callsign="N0CALL",
        station_position=(0.0, 0.0, 0.0),
        user_antenna="",
        synchronous_upload_time=30,
        callsign_validity_threshold=2,
        upload_queue_size=16,
        upload_timeout=10,
        upload_retries=5,
        upload_retry_interval=0.25,
        user_position_update_rate=6,
        inhibit=False,
        url="http://habitat.sondehub.org/",
    ):
        """ Initialise a Habitat Uploader object.

        Args:
            user_callsign (str): Callsign of the uploader.
            station_position (tuple): Optional - a tuple consisting of (lat, lon, alt), which if populated,
                is used to plot the listener's position on the Habitat map, both when this class is initialised, and
                when a new sonde ID is observed.

            synchronous_upload_time (int): Upload the most recent telemetry when time.time()%synchronous_upload_time == 0
                This is done in an attempt to get multiple stations uploading the same telemetry sentence simultaneously,
                and also acts as decimation on the number of sentences uploaded to Habitat.
            callsign_validity_threshold (int): Only upload telemetry data if the callsign has been observed more than N times. Default = 5

            upload_queue_size (int): Maximum umber of sentences to keep in the upload queue. If the queue is filled,
                it will be emptied (discarding the queue contents).
            upload_timeout (int): Timeout (Seconds) when performing uploads to Habitat. Default: 10 seconds.
            upload_retries (int): Retry an upload up to this many times. Default: 5
            upload_retry_interval (int): Time interval between upload retries. Default: 0.25 seconds.

            user_position_update_rate (int): Time interval between automatic station position updates, hours.
                Set to 6 hours by default, updating any more often than this is not really useful.

            inhibit (bool): Inhibit all uploads. Mainly intended for debugging.

        """

        self.user_callsign = user_callsign
        self.station_position = station_position
        self.user_antenna = user_antenna
        self.upload_timeout = upload_timeout
        self.upload_retries = upload_retries
        self.upload_retry_interval = upload_retry_interval
        self.upload_queue_size = upload_queue_size
        self.synchronous_upload_time = synchronous_upload_time
        self.callsign_validity_threshold = callsign_validity_threshold
        self.inhibit = inhibit
        self.user_position_update_rate = user_position_update_rate

        # set the habitat upload url
        global url_habitat_uuids, url_habitat_db, habitat_url
        url_habitat_uuids = url + "_uuids?count=%d"
        url_habitat_db = url + "habitat/"
        habitat_url = url

        # Our two Queues - one to hold sentences to be upload, the other to temporarily hold
        # input telemetry dictionaries before they are converted and processed.
        self.habitat_upload_queue = Queue(upload_queue_size)
        self.input_queue = Queue()

        # Dictionary where we store sorted telemetry data for upload when required.
        # Elements will be named after payload IDs, and will contain:
        #   'count' (int): Number of times this callsign has been observed. Uploads will only occur when
        #       this number rises above callsign_validity_threshold.
        #   'data' (Queue): A queue of telemetry sentences to be uploaded. When the upload timer fires,
        #       this queue will be dumped, and the most recent telemetry uploaded.
        #   'habitat_document' (bool): Indicates if a habitat document has been created for this payload ID.
        #   'listener_updated' (bool): Indicates if the listener position has been updated for the start of this ID's flight.
        self.observed_payloads = {}

        # Record of when we last uploaded a user station position to Habitat.
        self.last_user_position_upload = 0

        # Lock for dealing with telemetry uploads.
        self.upload_lock = Lock()

        # Start the uploader thread.
        self.upload_thread_running = True
        self.upload_thread = Thread(target=self.habitat_upload_thread)
        self.upload_thread.start()

        # Start the input queue processing thread.
        self.input_processing_running = True
        self.input_thread = Thread(target=self.process_queue)
        self.input_thread.start()

        self.timer_thread_running = True
        self.timer_thread = Thread(target=self.upload_timer)
        self.timer_thread.start()

    def user_position_upload(self):
        """ Upload the the station position to Habitat. """
        if self.station_position == None:
            # Upload is successful, just flag it as OK and move on.
            self.last_user_position_upload = time.time()
            return False

        if (self.station_position[0] != 0.0) or (self.station_position[1] != 0.0):
            _success = uploadListenerPosition(
                self.user_callsign,
                self.station_position[0],
                self.station_position[1],
                version=auto_rx_version,
                antenna=self.user_antenna,
            )
            self.last_user_position_upload = time.time()
            return _success
        else:
            # No position set, just flag the update as successful.
            self.last_user_position_upload = time.time()
            return False

    def habitat_upload(self, sentence):
        """ Upload a UKHAS-standard telemetry sentence to Habitat

        Args:
            sentence (str): The UKHAS-standard telemetry sentence to upload.
        """

        if self.inhibit:
            self.log_info("Upload inhibited.")
            return

        # Generate payload to be uploaded
        _sentence_b64 = b64encode(
            sentence.encode("ascii")
        )  # Encode to ASCII to be able to perform B64 encoding...
        _date = datetime.datetime.utcnow().isoformat("T") + "Z"
        _user_call = self.user_callsign

        _data = {
            "type": "payload_telemetry",
            "data": {
                "_raw": _sentence_b64.decode(
                    "ascii"
                )  # ... but decode back to a string to enable JSON serialisation.
            },
            "receivers": {
                _user_call: {"time_created": _date, "time_uploaded": _date,},
            },
        }

        # The URL to upload to.
        _url = (
            habitat_url
            + "habitat/_design/payload_telemetry/_update/add_listener/%s"
            % sha256(_sentence_b64).hexdigest()
        )

        # Delay for a random amount of time between 0 and upload_retry_interval*2 seconds.
        time.sleep(random.random() * self.upload_retry_interval * 2.0)

        _retries = 0

        # When uploading, we have three possible outcomes:
        # - Can't connect. No point immediately re-trying in this situation.
        # - The packet is uploaded successfuly (201 / 403)
        # - There is a upload conflict on the Habitat DB end (409). We can retry and it might work.
        while _retries < self.upload_retries:
            # Run the request.
            try:
                headers = {"User-Agent": "autorx-" + auto_rx_version}
                _req = requests.put(
                    _url,
                    data=json.dumps(_data),
                    timeout=(self.upload_timeout, 6.1),
                    headers=headers,
                )
            except Exception as e:
                self.log_error("Upload Failed: %s" % str(e))
                return

            if _req.status_code == 201 or _req.status_code == 403:
                # 201 = Success, 403 = Success, sentence has already seen by others.
                self.log_info(
                    "Uploaded sentence to Habitat successfully: %s" % sentence.strip()
                )
                _upload_success = True
                break
            elif _req.status_code == 409:
                # 409 = Upload conflict (server busy). Sleep for a moment, then retry.
                self.log_debug("Upload conflict.. retrying.")
                time.sleep(random.random() * self.upload_retry_interval)
                _retries += 1
            else:
                self.log_error(
                    "Error uploading to Habitat. Status Code: %d %s."
                    % (_req.status_code, _req.text)
                )
                break

        if _retries == self.upload_retries:
            self.log_error(
                "Upload conflict not resolved with %d retries." % self.upload_retries
            )

        return

    def habitat_upload_thread(self):
        """ Handle uploading of packets to Habitat """

        self.log_debug("Started Habitat Uploader Thread.")

        while self.upload_thread_running:

            if self.habitat_upload_queue.qsize() > 0:
                # If the queue is completely full, jump to the most recent telemetry sentence.
                if self.habitat_upload_queue.qsize() == self.upload_queue_size:
                    while not self.habitat_upload_queue.empty():
                        try:
                            sentence = self.habitat_upload_queue.get_nowait()
                        except:
                            pass

                    self.log_warning(
                        "Upload queue was full when reading from queue, now flushed - possible connectivity issue."
                    )
                else:
                    # Otherwise, get the first item in the queue.
                    sentence = self.habitat_upload_queue.get()

                # Attempt to upload it.
                if sentence:
                    self.habitat_upload(sentence)

            else:
                # Wait for a short time before checking the queue again.
                time.sleep(0.1)

        self.log_debug("Stopped Habitat Uploader Thread.")

    def handle_telem_dict(self, telem, immediate=False):
        # Try and convert it to a UKHAS sentence
        try:
            _sentence = sonde_telemetry_to_sentence(telem)
        except Exception as e:
            self.log_error("Error converting telemetry to sentence - %s" % str(e))
            return

        _callsign = "RS_" + telem["id"]

        # Wait for the upload_lock to be available, to ensure we don't end up with
        # race conditions resulting in multiple payload docs being created.
        self.upload_lock.acquire()

        # Habitat Payload document creation has been disabled as of 2020-03-20.
        # We now use a common payload document for all radiosonde telemetry.
        #
        # # Create a habitat document if one does not already exist:
        # if not self.observed_payloads[telem['id']]['habitat_document']:
        #     # Check if there has already been telemetry from this ID observed on Habhub
        #     _document_exists = check_callsign(_callsign)
        #     # If so, we don't need to create a new document
        #     if _document_exists:
        #         self.observed_payloads[telem['id']]['habitat_document'] = True
        #     else:
        #         # Otherwise, we attempt to create a new document.
        #         if self.inhibit:
        #             # If we have an upload inhibit, don't create a payload doc.
        #             _created = True
        #         else:
        #             _created = initPayloadDoc(_callsign, description="Meteorology Radiosonde", frequency=telem['freq_float'])

        #         if _created:
        #             self.observed_payloads[telem['id']]['habitat_document'] = True
        #         else:
        #             self.log_error("Error creating payload document!")
        #             self.upload_lock.release()
        #             return

        if immediate:
            self.log_info(
                "Performing immediate upload for first telemetry sentence of %s."
                % telem["id"]
            )
            self.habitat_upload(_sentence)

        else:
            # Attept to add it to the habitat uploader queue.
            try:
                if self.habitat_upload_queue.qsize() == self.upload_queue_size:
                    # Flush queue.
                    while not self.habitat_upload_queue.empty():
                        try:
                            self.habitat_upload_queue.get_nowait()
                        except:
                            pass

                    self.log_error(
                        "Upload queue was full when adding to queue, now flushed - possible connectivity issue."
                    )

                self.habitat_upload_queue.put_nowait(_sentence)
                self.log_debug(
                    "Upload queue size: %d" % self.habitat_upload_queue.qsize()
                )
            except Exception as e:
                self.log_error(
                    "Error adding sentence to queue, queue likely full.  %s" % str(e)
                )
                self.log_error("Queue Size: %d" % self.habitat_upload_queue.qsize())

        self.upload_lock.release()

    def upload_timer(self):
        """ Add packets to the habitat upload queue if it is time for us to upload. """

        while self.timer_thread_running:
            if int(time.time()) % self.synchronous_upload_time == 0:
                # Time to upload!
                for _id in self.observed_payloads.keys():
                    # If no data, continue...
                    if self.observed_payloads[_id]["data"].empty():
                        continue
                    else:
                        # Otherwise, dump the queue and keep the latest telemetry.
                        while not self.observed_payloads[_id]["data"].empty():
                            _telem = self.observed_payloads[_id]["data"].get()

                        self.handle_telem_dict(_telem)

                # Sleep a second so we don't hit the synchronous upload time again.
                time.sleep(1)
            else:
                # Not yet time to upload, wait for a bit.
                time.sleep(0.1)

    def process_queue(self):
        """ Process packets from the input queue.

        This thread handles packets from the input queue (provided by the decoders)
        Packets are sorted by ID, and a dictionary entry is created. 

        """

        while self.input_processing_running:
            # Process everything in the queue.
            while self.input_queue.qsize() > 0:
                # Grab latest telem dictionary.
                _telem = self.input_queue.get_nowait()

                _id = _telem["id"]

                if _id not in self.observed_payloads:
                    # We haven't seen this ID before, so create a new dictionary entry for it.
                    self.observed_payloads[_id] = {
                        "count": 1,
                        "data": Queue(),
                        "habitat_document": False,
                        "first_uploaded": False,
                    }
                    self.log_debug(
                        "New Payload %s. Not observed enough to allow upload." % _id
                    )
                    # However, we don't yet add anything to the queue for this payload...
                else:
                    # We have seen this payload before!
                    # Increment the 'seen' counter.
                    self.observed_payloads[_id]["count"] += 1

                    # If we have seen this particular ID enough times, add the data to the ID's queue.
                    if (
                        self.observed_payloads[_id]["count"]
                        >= self.callsign_validity_threshold
                    ):

                        # If this is the first time we have observed this payload, immediately upload the first position we got.
                        if self.observed_payloads[_id]["first_uploaded"] == False:
                            # Because receiving balloon telemetry appears to be a competition, immediately upload the
                            # first valid position received.
                            self.handle_telem_dict(_telem, immediate=True)

                            self.observed_payloads[_id]["first_uploaded"] = True

                        else:
                            # Otherwise, add the telemetry to the upload queue
                            self.observed_payloads[_id]["data"].put(_telem)

                    else:
                        self.log_debug(
                            "Payload ID %s not observed enough to allow upload." % _id
                        )

            # If we haven't uploaded our station position recently, re-upload it.
            if (
                time.time() - self.last_user_position_upload
            ) > self.user_position_update_rate * 3600:
                self.user_position_upload()

            time.sleep(0.1)

    def add(self, telemetry):
        """ Add a dictionary of telemetry to the input queue. 

        Args:
            telemetry (dict): Telemetry dictionary to add to the input queue.

        """

        # Discard any telemetry which is indicated to be encrypted.
        if "encrypted" in telemetry:
            if telemetry["encrypted"] == True:
                return

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

    def update_station_position(self, lat, lon, alt):
        """ Update the internal station position record. Used when determining the station position by GPSD """
        self.station_position = (lat, lon, alt)

    def close(self):
        """ Shutdown uploader and processing threads. """
        self.log_debug("Waiting for threads to close...")
        self.input_processing_running = False
        self.timer_thread_running = False
        self.upload_thread_running = False

        # Wait for all threads to close.
        if self.upload_thread is not None:
            self.upload_thread.join(60)
            if self.upload_thread.is_alive():
                self.log_error("habitat upload thread failed to join")


        if self.timer_thread is not None:
            self.timer_thread.join(60)
            if self.timer_thread.is_alive():
                self.log_error("habitat timer thread failed to join")

        if self.input_thread is not None:
            self.input_thread.join(60)
            if self.input_thread.is_alive():
                self.log_error("habitat input thread failed to join")

    def log_debug(self, line):
        """ Helper function to log a debug message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.debug("Habitat - %s" % line)

    def log_info(self, line):
        """ Helper function to log an informational message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.info("Habitat - %s" % line)

    def log_error(self, line):
        """ Helper function to log an error message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.error("Habitat - %s" % line)

    def log_warning(self, line):
        """ Helper function to log a warning message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.warning("Habitat - %s" % line)
