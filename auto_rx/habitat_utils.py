#!/usr/bin/env python
#
# Radiosonde Auto RX Tools - Habitat Upload
#
# 2018-04 Mark Jessop <vk5qi@rfhead.net>
#
import crcmod
import requests
import datetime
import logging
import time
import traceback
import json
from base64 import b64encode
from hashlib import sha256

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

def habitat_upload_payload_telemetry(telemetry, payload_callsign = "RADIOSONDE", callsign="N0CALL", comment=None, timeout=10):

    sentence = telemetry_to_sentence(telemetry, payload_callsign = payload_callsign, comment=comment)

    sentence_b64 = b64encode(sentence)

    date = datetime.datetime.utcnow().isoformat("T") + "Z"

    data = {
        "type": "payload_telemetry",
        "data": {
            "_raw": sentence_b64
            },
        "receivers": {
            callsign: {
                "time_created": date,
                "time_uploaded": date,
                },
            },
    }


    # The URl to upload to.
    _url = "http://habitat.habhub.org/habitat/_design/payload_telemetry/_update/add_listener/%s" % sha256(sentence_b64).hexdigest()

    try:
        # Run the request.
        _req = requests.put(_url, data=json.dumps(data), timeout=timeout)

        if _req.status_code == 201:
            logging.info("Uploaded sentence to Habitat successfully: %s" % sentence)
        elif _req.status_code == 403:
            logging.info("Sentence uploaded to Habitat, but already present in database.")
        else:
            logging.error("Error uploading to Habitat. Status Code: %d" % _req.status_code)

    except Exception as e:
        logging.error("Error Uploading to Habitat: %s" % str(e))

    return

#
# Functions for uploading a listener position to Habitat.
# from https://raw.githubusercontent.com/rossengeorgiev/hab-tools/master/spot2habitat_chase.py
#
callsign_init = False
url_habitat_uuids = "http://habitat.habhub2.org/_uuids?count=%d"
url_habitat_db = "http://habitat.habhub2.org/habitat/"
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
    _callsign_present = False# check_callsign(serial)

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

