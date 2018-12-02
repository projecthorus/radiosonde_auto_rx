#!/usr/bin/env python
#
#   radiosonde_auto_rx - APRS Exporter
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import datetime
import logging
import random
import time
import traceback
import socket
from threading import Thread
from . import __version__ as auto_rx_version
try:
    # Python 2
    from Queue import Queue
except ImportError:
    # Python 3
    from queue import Queue



def telemetry_to_aprs_position(sonde_data, object_name="<id>", aprs_comment="BOM Balloon"):
    """ Convert a dictionary containing Sonde telemetry into an APRS packet.

    Args:
        sonde_data (dict): Sonde telemetry dictionary. (refer autorx.decoder)
        object_name (str): APRS Object Name. If <id>, the sonde's serial number will be used.
        aprs_comment (str): Comment to use in the packet.

    """

    # Generate the APRS 'callsign' for the sonde. 
    if object_name == "<id>":
        # Use the radiosonde ID as the object ID
        if ('RS92' in sonde_data['type']) or ('RS41' in sonde_data['type']):
            # We can use the Vaisala sonde ID directly.
            _object_name = sonde_data["id"].strip()
        elif sonde_data['type'] == 'DFM':
            # The DFM sonde IDs are too long to use directly.
            # Grab the last six digits of the sonde ID (which is the serial number)
            _id_suffix = sonde_data['id'][-6:]
            if "DFM09" in sonde_data['id']:
                _object_name = "DF9" + _id_suffix
            else:
                _object_name = "DF6" + _id_suffix
        else:
            # Unknown sonde type, don't know how to handle this yet.
            return None
    else:
        _object_name = object_name
    
    
    # Generate the comment field.
    _aprs_comment = aprs_comment
    _aprs_comment = _aprs_comment.replace("<freq>", sonde_data['freq'])
    _aprs_comment = _aprs_comment.replace("<id>", sonde_data['id'])
    _aprs_comment = _aprs_comment.replace("<temp>", "%.1f degC" % sonde_data['temp'])
    _aprs_comment = _aprs_comment.replace("<vel_v>", "%.1fm/s" % sonde_data['vel_v'])
    _aprs_comment = _aprs_comment.replace("<type>", sonde_data['type'])

    # Convert float latitude to APRS format (DDMM.MM)
    lat = float(sonde_data["lat"])
    lat_degree = abs(int(lat))
    lat_minute = abs(lat - int(lat)) * 60.0
    lat_min_str = ("%02.2f" % lat_minute).zfill(5)
    lat_dir = "S"
    if lat>0.0:
        lat_dir = "N"
    lat_str = "%02d%s" % (lat_degree,lat_min_str) + lat_dir
    
    # Convert float longitude to APRS format (DDDMM.MM)
    lon = float(sonde_data["lon"])
    lon_degree = abs(int(lon))
    lon_minute = abs(lon - int(lon)) * 60.0
    lon_min_str = ("%02.2f" % lon_minute).zfill(5)
    lon_dir = "E"
    if lon<0.0:
        lon_dir = "W"
    lon_str = "%03d%s" % (lon_degree,lon_min_str) + lon_dir

    # Generate the added digits of precision, as per http://www.aprs.org/datum.txt
    # Note: This is a bit hacky.
    _lat_prec = chr(int(("%02.4f" % lat_minute)[-2:]) + 33)
    _lon_prec = chr(int(("%02.4f" % lon_minute)[-2:]) + 33)

    # Produce Datum + Added precision string
    _datum = "!w%s%s!" % (_lat_prec, _lon_prec)

    # Convert Alt (in metres) to feet
    alt = int(float(sonde_data["alt"])/0.3048)

    # Produce the timestamp
    _aprs_timestamp = sonde_data['datetime_dt'].strftime("%H%M%S")
    
    # Generate course/speed data, if provided in the telemetry dictionary
    if ('heading' in sonde_data.keys()) and ('vel_h' in sonde_data.keys()):
        course_speed = "%03d/%03d" % (int(sonde_data['heading']), int(sonde_data['vel_h']*1.944))
    else:
        course_speed = "000/000"

    # Produce the APRS position report string
    # Note, we are using the 'position with timestamp' data type, as per http://www.aprs.org/doc/APRS101.PDF
    out_str = "/%sh%s/%sO%s/A=%06d %s %s" % (_aprs_timestamp,lat_str,lon_str,course_speed,alt,_aprs_comment,_datum)

    # Return both the packet, and the 'callsign'.
    return (out_str, _object_name)



def generate_station_object(callsign, lat, lon, comment="radiosonde_auto_rx SondeGate v<version>", icon='/r'):
    ''' Generate a station object '''

    # Pad or limit the station callsign to 9 characters, if it is to long or short.
    if len(callsign) > 9:
        callsign = callsign[:9]
    elif len(callsign) < 9:
        callsign = callsign + " "*(9-len(callsign))


    # Convert float latitude to APRS format (DDMM.MM)
    lat = float(lat)
    lat_degree = abs(int(lat))
    lat_minute = abs(lat - int(lat)) * 60.0
    lat_min_str = ("%02.2f" % lat_minute).zfill(5)
    lat_dir = "S"
    if lat>0.0:
        lat_dir = "N"
    lat_str = "%02d%s" % (lat_degree,lat_min_str) + lat_dir
    
    # Convert float longitude to APRS format (DDDMM.MM)
    lon = float(lon)
    lon_degree = abs(int(lon))
    lon_minute = abs(lon - int(lon)) * 60.0
    lon_min_str = ("%02.2f" % lon_minute).zfill(5)
    lon_dir = "E"
    if lon<0.0:
        lon_dir = "W"
    lon_str = "%03d%s" % (lon_degree,lon_min_str) + lon_dir

    # Generate timestamp using current UTC time
    _aprs_timestamp = datetime.datetime.utcnow().strftime("%H%M%S")


    # Add version string to position comment, if requested.
    _aprs_comment = comment
    _aprs_comment = _aprs_comment.replace('<version>', auto_rx_version)

    # Generate output string
    out_str = ";%s*%sh%s%s%s%s%s" % (callsign, _aprs_timestamp, lat_str, icon[0], lon_str, icon[1], _aprs_comment)

    return out_str


#
# APRS Uploader Class
#

class APRSUploader(object):
    ''' 
    Queued APRS Telemetry Uploader class
    This performs uploads to the Habitat servers, and also handles generation of flight documents.

    Incoming telemetry packets are fed into queue, which is checked regularly.
    If a new callsign is sighted, a payload document is created in the Habitat DB.
    The telemetry data is then converted into a UKHAS-compatible format, before being added to queue to be
    uploaded as network speed permits.

    If an upload attempt times out, the packet is discarded.
    If the queue fills up (probably indicating no network connection, and a fast packet downlink rate),
    it is immediately emptied, to avoid upload of out-of-date packets.

    Note that this uploader object is intended to handle telemetry from multiple sondes
    '''

    # We require the following fields to be present in the incoming telemetry dictionary data
    REQUIRED_FIELDS = ['frame', 'id', 'datetime', 'lat', 'lon', 'alt', 'temp', 'type', 'freq', 'freq_float', 'datetime_dt']


    def __init__(self, 
                aprs_callsign = 'N0CALL', 
                aprs_passcode = "00000",
                object_name_override = None,
                object_comment = "RadioSonde",
                aprsis_host = 'rotate.aprs2.net',
                aprsis_port = 14580,
                station_beacon = False,
                station_beacon_rate = 30,
                station_beacon_position = [0.0,0.0],
                station_beacon_comment = "radiosonde_auto_rx SondeGate v<version>",
                station_beacon_icon = "/r",
                synchronous_upload_time = 30,
                callsign_validity_threshold = 5,
                upload_queue_size = 16,
                upload_timeout = 10,
                inhibit = False
                ):
        """ Initialise an APRS Uploader object.

        Args:
            aprs_callsign (str): Callsign of the uploader, used when logging into APRS-IS.
            aprs_passcode (tuple): Optional - a tuple consisting of (lat, lon, alt), which if populated,
                is used to plot the listener's position on the Habitat map, both when this class is initialised, and
                when a new sonde ID is observed.

            object_name_override (str): Override the object name in the uploaded sentence with this value.
                WARNING: This will horribly break the aprs.fi map if multiple sondes are uploaded under the same callsign.
                USE WITH CAUTION!!!
            object_comment (str): A comment to go with the object. Various fields will be replaced with telmetry data.

            aprsis_host (str): APRS-IS Server to upload packets to.
            aprsis_port (int): APRS-IS TCP port number.

            station_beacon (bool): Enable beaconing of station position.
            station_beacon_rate (int): Time delay between beacon uploads (minutes)
            station_beacon_position (list): [lat, lon], in decimal degrees, of the station position.
            station_beacon_comment (str): Comment field for the station beacon. <version> will be replaced with the current auto_rx version.
            station_beacon_icon (str): The APRS icon to be used, as the two characters (symbol table, symbol index), as per http://www.aprs.org/symbols.html

            synchronous_upload_time (int): Upload the most recent telemetry when time.time()%synchronous_upload_time == 0
                This is done in an attempt to get multiple stations uploading the same telemetry sentence simultaneously,
                and also acts as decimation on the number of sentences uploaded to APRS-IS.

            callsign_validity_threshold (int): Only upload telemetry data if the callsign has been observed more than N times. Default = 5

            upload_queue_size (int): Maximum number of sentences to keep in the upload queue. If the queue is filled,
                it will be emptied (discarding the queue contents).
            upload_timeout (int): Timeout (Seconds) when performing uploads to APRS-IS. Default: 10 seconds.

            inhibit (bool): Inhibit all uploads. Mainly intended for debugging.

        """

        self.aprs_callsign = aprs_callsign
        self.aprs_passcode = aprs_passcode
        self.object_comment = object_comment
        self.aprsis_host = aprsis_host
        self.aprsis_port = aprsis_port
        self.upload_timeout = upload_timeout
        self.upload_queue_size = upload_queue_size
        self.synchronous_upload_time = synchronous_upload_time
        self.callsign_validity_threshold = callsign_validity_threshold
        self.inhibit = inhibit

        self.station_beacon = {
            'enabled': station_beacon,
            'position': station_beacon_position,
            'rate': station_beacon_rate,
            'comment': station_beacon_comment,
            'icon': station_beacon_icon
        }

        if object_name_override is None:
            self.object_name_override = "<id>"
        else:
            self.object_name_override = object_name_override

        # Our two Queues - one to hold sentences to be upload, the other to temporarily hold
        # input telemetry dictionaries before they are converted and processed.
        self.aprs_upload_queue = Queue(upload_queue_size)
        self.input_queue = Queue()

        # Dictionary where we store sorted telemetry data for upload when required.
        # Elements will be named after payload IDs, and will contain:
        #   'count' (int): Number of times this callsign has been observed. Uploads will only occur when
        #       this number rises above callsign_validity_threshold.
        #   'data' (Queue): A queue of telemetry sentences to be uploaded. When the upload timer fires,
        #       this queue will be dumped, and the most recent telemetry uploaded.
        self.observed_payloads = {}

        # Record of when we last uploaded a user station position to Habitat.
        self.last_user_position_upload = 0

        # Start the uploader thread.
        self.upload_thread_running = True
        self.upload_thread = Thread(target=self.aprs_upload_thread)
        self.upload_thread.start()

        # Start the input queue processing thread.
        self.input_processing_running = True
        self.input_thread = Thread(target=self.process_queue)
        self.input_thread.start()

        self.timer_thread_running = True
        self.timer_thread = Thread(target=self.upload_timer)
        self.timer_thread.start()

        self.log_info("APRS Uploader Started.")


    def aprsis_upload(self, source, packet, igate=False):
        """ Upload a packet to APRS-IS

        Args:
            packet (str): APRS packet to upload.

        """

        # If we are inhibited, just return immediately.
        if self.inhibit:
            self.log_info("Upload Inhibited: %s" % packet)
            return True


        # Generate APRS packet
        if igate:
            # If we are emulating an IGATE, then we need to add in a path, a q-construct, and our own callsign.
            _packet = '%s>APRS,SONDEGATE,TCPIP,qAR,%s:%s\n' % (source, self.aprs_callsign, packet)
        else:
            # Otherwise, we are probably just placing an object, usually sourced by our own callsign
            _packet = '%s>APRS:%s\n' % (source, packet)

        # create socket & connect to server
        _s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        _s.settimeout(self.upload_timeout)
        try:
            _s.connect((self.aprsis_host, self.aprsis_port))
            # Send logon string
            _logon = 'user %s pass %s vers VK5QI-AutoRX \n' % (self.aprs_callsign, self.aprs_passcode)
            _s.send(_logon.encode('ascii'))
            # send packet
            _s.send(_packet.encode('ascii'))
            # close socket
            _s.shutdown(0)
            _s.close()
            self.log_info("Uploaded to APRS-IS: %s" % _packet)
            return True
        except Exception as e:
            self.log_error("Upload to APRS-IS Failed - %s" % str(e))
            return False


    def beacon_station_position(self):
        ''' Send a station position beacon into APRS-IS '''
        if self.station_beacon['enabled']:
            # Generate the station position packet
            # Note - this is generated as an APRS object.
            _packet = generate_station_object(self.aprs_callsign,
                self.station_beacon['position'][0], 
                self.station_beacon['position'][1],
                self.station_beacon['comment'], 
                self.station_beacon['icon'])

            # Send the packet
            self.aprsis_upload(self.aprs_callsign, _packet, igate=False)
            self.last_user_position_upload = time.time()




    def aprs_upload_thread(self):
        ''' Handle uploading of packets to Habitat '''

        self.log_debug("Started APRS Uploader Thread.")

        while self.upload_thread_running:

            if self.aprs_upload_queue.qsize() > 0:
                # If the queue is completely full, jump to the most recent telemetry sentence.
                if self.aprs_upload_queue.qsize() == self.upload_queue_size:
                    while not self.aprs_upload_queue.empty():
                        _sentence = self.aprs_upload_queue.get()

                    self.log_warning("Uploader queue was full - possible connectivity issue.")
                else:
                    # Otherwise, get the first item in the queue.
                    _sentence = self.aprs_upload_queue.get()

                # Attempt to upload it.
                self.aprsis_upload(_sentence)

            else:
                # Wait for a short time before checking the queue again.
                time.sleep(0.1)

        self.log_debug("Stopped APRS Uploader Thread.")



    def upload_timer(self):
        """ Add packets to the habitat upload queue if it is time for us to upload. """
        
        while self.timer_thread_running:
            if int(time.time()) % self.synchronous_upload_time == 0:
                # Time to upload! 
                for _id in self.observed_payloads.keys():
                    # If no data, continue...
                    if self.observed_payloads[_id]['data'].empty():
                        continue
                    else:
                        # Otherwise, dump the queue and keep the latest telemetry.
                        while not self.observed_payloads[_id]['data'].empty():
                            _telem = self.observed_payloads[_id]['data'].get()

                        # Try and convert it to a UKHAS sentence
                        try:
                            _sentence = telemetry_to_aprs_sentence(_telem, 
                                object_name=self.object_name_override, 
                                aprs_comment=self.object_comment)
                        except Exception as e:
                            self.log_error("Error converting telemetry to sentence - %s" % str(e))
                            continue

                        # Attept to add it to the habitat uploader queue.
                        try:
                            self.aprs_upload_queue.put_nowait(_sentence)
                        except Exception as e:
                            self.log_error("Error adding sentence to queue: %s" % str(e))

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

                _id = _telem['id']

                if _id not in self.observed_payloads:
                    # We haven't seen this ID before, so create a new dictionary entry for it.
                    self.observed_payloads[_id] = {'count':1, 'data':Queue()}
                    self.log_debug("New Payload %s. Not observed enough to allow upload." % _id)
                    # However, we don't yet add anything to the queue for this payload...
                else:
                    # We have seen this payload before!
                    # Increment the 'seen' counter.
                    self.observed_payloads[_id]['count'] += 1

                    # If we have seen this particular ID enough times, add the data to the ID's queue.
                    if self.observed_payloads[_id]['count'] >= self.callsign_validity_threshold:
                        # Add the telemetry to the queue
                        self.observed_payloads[_id]['data'].put(_telem)

                    else:
                        self.log_debug("Payload ID %s not observed enough to allow upload." % _id)

            if (time.time() - self.last_user_position_upload) > self.station_beacon['rate']*60:
                self.beacon_station_position()


            time.sleep(0.1)


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
        ''' Shutdown uploader and processing threads. '''
        self.log_debug("Waiting for threads to close...")
        self.input_processing_running = False
        self.timer_thread_running = False
        self.upload_thread_running = False

        # Wait for all threads to close.
        if self.upload_thread is not None:
            self.upload_thread.join()

        if self.timer_thread is not None:
            self.timer_thread.join()

        if self.input_thread is not None:
            self.input_thread.join()


    def log_debug(self, line):
        """ Helper function to log a debug message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.debug("APRS-IS - %s" % line)


    def log_info(self, line):
        """ Helper function to log an informational message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.info("APRS-IS - %s" % line)


    def log_error(self, line):
        """ Helper function to log an error message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.error("APRS-IS - %s" % line)


    def log_warning(self, line):
        """ Helper function to log a warning message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.warning("APRS-IS - %s" % line)




