#!/usr/bin/env python
#
# Radiosonde Auto RX Tools - Habitat Upload
#
# 2018-04 Mark Jessop <vk5qi@rfhead.net>
#
import crcmod
import datetime
import logging
import Queue
import random
import requests
import time
import traceback
import json
from base64 import b64encode
from hashlib import sha256
from threading import Thread

#
# Habitat Uploader Class
#

class HabitatUploader(object):
    ''' 
    Queued Habitat Telemetry Uploader class 
    
    Packets to be uploaded to Habitat are added to a queue for uploading.
    If an upload attempt times out, the packet is discarded.
    If the queue fills up (probably indicating no network connection, and a fast packet downlink rate),
    it is immediately emptied, to avoid upload of out-of-date packets.
    '''


    def __init__(self, user_callsign='N0CALL', 
                queue_size=16,
                upload_timeout = 10,
                upload_retries = 5,
                upload_retry_interval = 0.25,
                inhibit = False,
                ):
        ''' Create a Habitat Uploader object. ''' 

        self.user_callsign = user_callsign
        self.upload_timeout = upload_timeout
        self.upload_retries = upload_retries
        self.upload_retry_interval = upload_retry_interval
        self.queue_size = queue_size
        self.habitat_upload_queue = Queue.Queue(queue_size)
        self.inhibit = inhibit

        # Start the uploader thread.
        self.habitat_uploader_running = True
        self.uploadthread = Thread(target=self.habitat_upload_thread)
        self.uploadthread.start()

    def habitat_upload(self, sentence):
        ''' Upload a UKHAS-standard telemetry sentence to Habitat '''

        # Generate payload to be uploaded
        _sentence_b64 = b64encode(sentence)
        _date = datetime.datetime.utcnow().isoformat("T") + "Z"
        _user_call = self.user_callsign

        _data = {
            "type": "payload_telemetry",
            "data": {
                "_raw": _sentence_b64
                },
            "receivers": {
                _user_call: {
                    "time_created": _date,
                    "time_uploaded": _date,
                    },
                },
        }

        # The URl to upload to.
        _url = "http://habitat.habhub.org/habitat/_design/payload_telemetry/_update/add_listener/%s" % sha256(_sentence_b64).hexdigest()

        # Delay for a random amount of time between 0 and upload_retry_interval*2 seconds.
        time.sleep(random.random()*self.upload_retry_interval*2.0)

        _retries = 0

        # When uploading, we have three possible outcomes:
        # - Can't connect. No point re-trying in this situation.
        # - The packet is uploaded successfult (201 / 403)
        # - There is a upload conflict on the Habitat DB end (409). We can retry and it might work.
        while _retries < self.upload_retries:
            # Run the request.
            try:
                _req = requests.put(_url, data=json.dumps(_data), timeout=self.upload_timeout)
            except Exception as e:
                logging.error("Habitat - Upload Failed: %s" % str(e))
                break

            if _req.status_code == 201 or _req.status_code == 403:
                # 201 = Success, 403 = Success, sentence has already seen by others.
                logging.info("Habitat - Uploaded sentence to Habitat successfully")
                _upload_success = True
                break
            elif _req.status_code == 409:
                # 409 = Upload conflict (server busy). Sleep for a moment, then retry.
                logging.debug("Habitat - Upload conflict.. retrying.")
                time.sleep(random.random()*self.upload_retry_interval)
                _retries += 1
            else:
                logging.error("Habitat - Error uploading to Habitat. Status Code: %d." % _req.status_code)
                break

        if _retries == self.upload_retries:
            logging.error("Habitat - Upload conflict not resolved with %d retries." % self.upload_retries)

        return


    def habitat_upload_thread(self):
        ''' Handle uploading of packets to Habitat '''

        logging.info("Started Habitat Uploader Thread.")

        while self.habitat_uploader_running:

            if self.habitat_upload_queue.qsize() > 0:
                # If the queue is completely full, jump to the most recent telemetry sentence.
                if self.habitat_upload_queue.qsize() == self.queue_size:
                    while not self.habitat_upload_queue.empty():
                        sentence = self.habitat_upload_queue.get()

                    logging.warning("Habitat uploader queue was full - possible connectivity issue.")
                else:
                    # Otherwise, get the first item in the queue.
                    sentence = self.habitat_upload_queue.get()

                # Attempt to upload it.
                self.habitat_upload(sentence)

            else:
                # Wait for a short time before checking the queue again.
                time.sleep(0.1)

        logging.info("Stopped Habitat Uploader Thread.")


    def add(self, sentence):
        ''' Add a sentence to the upload queue '''

        if self.inhibit:
            # We have upload inhibited. Return.
            return

        # Handling of arbitrary numbers of $$'s at the start of a sentence:
        # Extract the data part of the sentence (i.e. everything after the $$'s')
        sentence = sentence.split('$')[-1]
        # Now add the *correct* number of $$s back on.
        sentence = '$$' +sentence

        if not (sentence[-1] == '\n'):
            sentence += '\n'

        try:
            self.habitat_upload_queue.put_nowait(sentence)
        except Queue.Full:
            logging.error("Upload Queue is full, sentence discarded.")
        except Exception as e:
            logging.error("Error adding sentence to queue: %s" % str(e))


    def close(self):
        ''' Shutdown uploader thread. '''
        self.habitat_uploader_running = False


#
# Functions for uploading telemetry to Habitat
#


# CRC16 function
def crc16_ccitt(data):
    """
    Calculate the CRC16 CCITT checksum of *data*.
    (CRC16 CCITT: start 0xFFFF, poly 0x1021)
    """
    crc16 = crcmod.predefined.mkCrcFun('crc-ccitt-false')
    return hex(crc16(data))[2:].upper().zfill(4)


def telemetry_to_sentence(sonde_data, payload_callsign="RADIOSONDE", comment=None):
    ''' Convert a telemetry data dictionary into a UKHAS-compliant telemetry sentence '''
    # RS produces timestamps with microseconds on the end, we only want HH:MM:SS for uploading to habitat.
    data_datetime = datetime.datetime.strptime(sonde_data['datetime_str'],"%Y-%m-%dT%H:%M:%S.%f")
    short_time = data_datetime.strftime("%H:%M:%S")

    sentence = "$$%s,%d,%s,%.5f,%.5f,%d,%.1f,%.1f,%.1f" % (payload_callsign,sonde_data['frame'],short_time,sonde_data['lat'],
        sonde_data['lon'],int(sonde_data['alt']),sonde_data['vel_h'], sonde_data['temp'], sonde_data['humidity'])

    # Add on a comment field if provided - note that this will result in a different habitat payload doc being required.
    if comment != None:
        comment = comment.replace(',','_')
        sentence += "," + comment

    checksum = crc16_ccitt(sentence[2:])
    output = sentence + "*" + checksum + "\n"
    return output


def habitat_upload_payload_telemetry(uploader, telemetry, payload_callsign = "RADIOSONDE", callsign="N0CALL", comment=None):
    ''' Add a packet of radiosonde telemetry to the Habitat uploader queue. '''

    sentence = telemetry_to_sentence(telemetry, payload_callsign = payload_callsign, comment=comment)

    try:
        uploader.add(sentence)
    except Exception as e:
        logging.error("Could not add telemetry to Habitat Uploader - %s" % str(e))

#
# Functions for uploading a listener position to Habitat.
# from https://raw.githubusercontent.com/rossengeorgiev/hab-tools/master/spot2habitat_chase.py
#
callsign_init = False
url_habitat_uuids = "http://habitat.habhub.org/_uuids?count=%d"
url_habitat_db = "http://habitat.habhub.org/habitat/"
url_check_callsign = "http://spacenear.us/tracker/datanew.php?mode=6hours&type=positions&format=json&max_positions=10&position_id=0&vehicle=%s"
uuids = []


def check_callsign(callsign, timeout=10):
    ''' 
    Check if a payload document exists for a given callsign. 

    This is done in a bit of a hack-ish way at the moment. We just check to see if there have
    been any reported packets for the payload callsign on the tracker.
    This should really be replaced with the correct call into the habitat tracker.
    '''
    global url_check_callsign

    # Perform the request
    _r = requests.get(url_check_callsign % callsign, timeout=timeout)

    try:
        # Read the response in as JSON
        _r_json = _r.json()

        # Read out the list of positions for the requested callsign
        _positions = _r_json['positions']['position']

        # If there is at least one position returned, we assume there is a valid payload document.
        if len(_positions) > 0:
            logging.info("Callsign %s already present in Habitat DB, not creating new payload doc." % callsign)
            return True
        else:
            # Otherwise, we don't, and go create one.
            return False

    except Exception as e:
        # Handle errors with JSON parsing.
        logging.error("Unable to request payload positions from spacenear.us - %s" % str(e))
        return False



# Keep an internal cache for which payload docs we've created so we don't spam couchdb with updates
payload_config_cache = {}


def ISOStringNow():
    return "%sZ" % datetime.datetime.utcnow().isoformat()


def initPayloadDoc(serial, description="Meteorology Radiosonde", frequency=401500000, timeout=20):
    """Creates a payload in Habitat for the radiosonde before uploading"""
    global url_habitat_db
    global payload_config_cache 
    
    # First, check if the payload's serial number is already in our local cache.
    if serial in payload_config_cache:
        return payload_config_cache[serial]

    # Next, check to see if the payload has been observed on the online tracker already.
    _callsign_present = check_callsign(serial)

    if _callsign_present:
        # Add the callsign to the local cache.
        payload_config_cache[serial] = serial
        return

    # Otherwise, proceed to creating a new payload document.

    payload_data = {
        "type": "payload_configuration",
        "name": serial,
        "time_created": ISOStringNow(),
        "metadata": { 
             "description": description
        },
        "transmissions": [
            {
                "frequency": frequency, # Currently a dummy value.
                "modulation": "RTTY",
                "mode": "USB",
                "encoding": "ASCII-8",
                "parity": "none",
                "stop": 2,
                "shift": 350,
                "baud": 50,
                "description": "DUMMY ENTRY, DATA IS VIA radiosonde_auto_rx"
            }
        ],
        "sentences": [
            {
                "protocol": "UKHAS",
                "callsign": serial,
                "checksum":"crc16-ccitt",
                "fields":[
                    {
                        "name": "sentence_id",
                        "sensor": "base.ascii_int"
                    },
                    {
                        "name": "time",
                        "sensor": "stdtelem.time"
                    }, 
                    {
                        "name": "latitude",
                        "sensor": "stdtelem.coordinate",
                        "format": "dd.dddd"
                    },
                    {
                        "name": "longitude",
                        "sensor": "stdtelem.coordinate",
                        "format": "dd.dddd"
                    },
                    {
                        "name": "altitude",
                        "sensor": "base.ascii_int"
                    },
                    {
                        "name": "speed",
                        "sensor": "base.ascii_float"
                    },
                    {
                        "name": "temperature_external",
                        "sensor": "base.ascii_float"
                    },
                    {
                        "name": "humidity",
                        "sensor": "base.ascii_float"
                    },
                    {
                        "name": "comment",
                        "sensor": "base.string"
                    }
                ],
            "filters": 
                {
                    "post": [
                        {
                            "filter": "common.invalid_location_zero",
                            "type": "normal"
                        }
                    ]
                },
             "description": "radiosonde_auto_rx to Habitat Bridge"
            }
        ]
    }

    # Perform the POST request to the Habitat DB.
    try:
        _r = requests.post(url_habitat_db, json=payload_data, timeout=timeout)

        if _r.json()['ok'] is True:
            logging.info("Habitat Listener: Created a payload document for %s" % serial)
            payload_config_cache[serial] = _r.json()
        else:
            logging.error("Habitat Listener: Failed to create a payload document for %s" % serial)

    except Exception as e:
        logging.error("Habitat Listener: Failed to create a payload document for %s - %s" % (serial, str(e)))



def postListenerData(doc, timeout=10):
    global uuids, url_habitat_db
    # do we have at least one uuid, if not go get more
    if len(uuids) < 1:
        fetchUuids()

    # Attempt to add UUID and time data to document.
    try:
        doc['_id'] = uuids.pop()
    except IndexError:
        logging.error("Habitat Listener: Unable to post listener data - no UUIDs available.")
        return False

    doc['time_uploaded'] = ISOStringNow()

    try:
        _r = requests.post(url_habitat_db, json=doc, timeout=timeout)
        return True
    except Exception as e:
        logging.error("Habitat Listener: Could not post listener data - %s" % str(e))
        return False


def fetchUuids(timeout=10):
    global uuids, url_habitat_uuids

    _retries = 5

    while _retries > 0:
        try:
            _r = requests.get(url_habitat_uuids % 10, timeout=timeout)
            uuids.extend(_r.json()['uuids'])
            logging.debug("Habitat Listener: Got UUIDs")
            return
        except Exception as e:
            logging.error("Habitat Listener: Unable to fetch UUIDs, retrying in 10 seconds - %s" % str(e))
            time.sleep(10)
            _retries = _retries - 1
            continue

    logging.error("Habitat Listener: Gave up trying to get UUIDs.")
    return


def initListenerCallsign(callsign, version=''):
    doc = {
            'type': 'listener_information',
            'time_created' : ISOStringNow(),
            'data': {
                'callsign': callsign,
                'antenna': '',
                'radio': 'radiosonde_auto_rx %s' % version,
                }
            }

    resp = postListenerData(doc)

    if resp is True:
        logging.debug("Habitat Listener: Listener Callsign Initialized.")
        return True
    else:
        logging.error("Habitat Listener: Unable to initialize callsign.")
        return False


def uploadListenerPosition(callsign, lat, lon, version=''):
    """ Initializer Listener Callsign, and upload Listener Position """

    # Attempt to initialize the listeners callsign
    resp = initListenerCallsign(callsign, version=version)
    # If this fails, it means we can't contact the Habitat server,
    # so there is no point continuing.
    if resp is False:
        return

    doc = {
        'type': 'listener_telemetry',
        'time_created': ISOStringNow(),
        'data': {
            'callsign': callsign,
            'chase': False,
            'latitude': lat,
            'longitude': lon,
            'altitude': 0,
            'speed': 0,
        }
    }

    # post position to habitat
    resp = postListenerData(doc)
    if resp is True:
        logging.info("Habitat Listener: Listener information uploaded.")
    else:
        logging.error("Habitat Listener: Unable to upload listener information.")

