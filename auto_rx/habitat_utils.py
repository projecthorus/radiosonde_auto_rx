#!/usr/bin/env python
#
# Radiosonde Auto RX Tools - Habitat Upload
#
# 2017-04 Mark Jessop <vk5qi@rfhead.net>
#
import crcmod
import httplib
import urllib2
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

def habitat_upload_payload_telemetry(telemetry, payload_callsign = "RADIOSONDE", callsign="N0CALL", comment=None):

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
    try:
        c = httplib.HTTPConnection("habitat.habhub.org",timeout=4)
        c.request(
            "PUT",
            "/habitat/_design/payload_telemetry/_update/add_listener/%s" % sha256(sentence_b64).hexdigest(),
            json.dumps(data),  # BODY
            {"Content-Type": "application/json"}  # HEADERS
            )

        response = c.getresponse()
        logging.info("Telemetry uploaded to Habitat: %s" % sentence)
        return
    except Exception as e:
        logging.error("Failed to upload to Habitat: %s" % (str(e)))
        return

#
# Functions for uploading a listener position to Habitat.
# from https://raw.githubusercontent.com/rossengeorgiev/hab-tools/master/spot2habitat_chase.py
#
callsign_init = False
url_habitat_uuids = "http://habitat.habhub.org/_uuids?count=%d"
url_habitat_db = "http://habitat.habhub.org/habitat/"
uuids = []

def ISOStringNow():
    return "%sZ" % datetime.datetime.utcnow().isoformat()


def postListenerData(doc):
    global uuids, url_habitat_db
    # do we have at least one uuid, if not go get more
    if len(uuids) < 1:
        fetchUuids()

    # add uuid and uploade time
    doc['_id'] = uuids.pop()
    doc['time_uploaded'] = ISOStringNow()

    data = json.dumps(doc)
    headers = {
            'Content-Type': 'application/json; charset=utf-8',
            'Referer': url_habitat_db,
            }

    req = urllib2.Request(url_habitat_db, data, headers)
    return urllib2.urlopen(req).read()

def fetchUuids():
    global uuids, url_habitat_uuids
    while True:
        try:
            resp = urllib2.urlopen(url_habitat_uuids % 10).read()
            data = json.loads(resp)
        except urllib2.HTTPError, e:
            logging.error("Habitat Listener: Unable to fetch UUIDs, retrying in 10 seconds.")
            time.sleep(10)
            continue

        uuids.extend(data['uuids'])
        break;


def initListenerCallsign(callsign):
    doc = {
            'type': 'listener_information',
            'time_created' : ISOStringNow(),
            'data': { 'callsign': callsign }
            }

    while True:
        try:
            resp = postListenerData(doc)
            logging.debug("Habitat Listener: Listener callsign Initialized.")
            break;
        except urllib2.HTTPError, e:
            logging.error("Habitat Listener: Unable to initialize callsign. Retrying...")
            time.sleep(10)
            continue

def uploadListenerPosition(callsign, lat, lon):
    # initialize call sign (one time only)
    global callsign_init
    if not callsign_init:
        initListenerCallsign(callsign)
        callsign_init = True

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
    try:
        postListenerData(doc)
    except urllib2.HTTPError, e:
        traceback.print_exc()
        logging.error("Habitat Listener: Unable to upload listener information.")
        return

    logging.info("Habitat Listener: Listener information uploaded.")
    return