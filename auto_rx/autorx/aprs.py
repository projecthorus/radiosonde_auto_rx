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
from threading import Thread, Lock
from . import __version__ as auto_rx_version
from .utils import strip_sonde_serial

try:
    # Python 2
    from Queue import Queue
except ImportError:
    # Python 3
    from queue import Queue


def telemetry_to_aprs_position(
    sonde_data, object_name="<id>", aprs_comment="BOM Balloon", position_report=False
):
    """ Convert a dictionary containing Sonde telemetry into an APRS packet.

    Args:
        sonde_data (dict): Sonde telemetry dictionary. (refer autorx.decoder)
        object_name (str): APRS Object Name. If <id>, the sonde's serial number will be used.
        aprs_comment (str): Comment to use in the packet.
        position_report (bool): if True, generate a position report instead of an APRS object packet.

    """

    # Generate the APRS 'callsign' for the sonde.
    if object_name == "<id>":
        _object_name = None

        # We should have been provided a APRS ID in the telemetry.
        # This is added in in decode.py, and generated in utils.py
        if 'aprsid' in sonde_data:
            if sonde_data['aprsid'] is not None:
                _object_name = sonde_data['aprsid']

        # However, it may be 'None', which indicates we don't know how to handle this sonde ID yet.
        if _object_name is None:
            # Unknown sonde type, don't know how to handle this yet.
            logging.error(
                "No APRS ID conversion available for sonde type: %s"
                % sonde_data["type"]
            )
            return (None, None)

    else:
        _object_name = object_name



    # Use the actual sonde frequency, if we have it.
    if "f_centre" in sonde_data:
        # We have an estimate of the sonde's centre frequency from the modem, use this in place of
        # the RX frequency.
        # Round to 1 kHz
        _freq = round(sonde_data["f_centre"] / 1000.0)
        # Convert to MHz.
        _freq = "%.3f MHz" % (_freq / 1e3)
    else:
        # Otherwise, use the normal frequency.
        _freq = sonde_data["freq"]

    # Generate the comment field.
    _aprs_comment = aprs_comment
    _aprs_comment = _aprs_comment.replace("<freq>", _freq)
    _aprs_comment = _aprs_comment.replace("<id>", strip_sonde_serial(sonde_data["id"]))
    _aprs_comment = _aprs_comment.replace("<temp>", "%.1fC" % sonde_data["temp"])
    _aprs_comment = _aprs_comment.replace(
        "<pressure>", "%.1fhPa" % sonde_data["pressure"]
    )
    _aprs_comment = _aprs_comment.replace(
        "<humidity>", "%.1f" % sonde_data["humidity"] + "%"
    )
    _aprs_comment = _aprs_comment.replace("<batt>", "%.1fV" % sonde_data["batt"])
    _aprs_comment = _aprs_comment.replace("<vel_v>", "%.1fm/s" % sonde_data["vel_v"])
    _aprs_comment = _aprs_comment.replace("<type>", sonde_data["type"])

    # TODO: RS41 Burst Timer

    # Add on auto_rx version
    _aprs_comment += " auto_rx v" + auto_rx_version

    # Convert float latitude to APRS format (DDMM.MM)
    lat = float(sonde_data["lat"])
    lat_degree = abs(int(lat))
    lat_minute = abs(lat - int(lat)) * 60.0
    lat_min_str = ("%02.4f" % lat_minute).zfill(7)[:5]
    lat_dir = "S"
    if lat > 0.0:
        lat_dir = "N"
    lat_str = "%02d%s" % (lat_degree, lat_min_str) + lat_dir

    # Convert float longitude to APRS format (DDDMM.MM)
    lon = float(sonde_data["lon"])
    lon_degree = abs(int(lon))
    lon_minute = abs(lon - int(lon)) * 60.0
    lon_min_str = ("%02.4f" % lon_minute).zfill(7)[:5]
    lon_dir = "E"
    if lon < 0.0:
        lon_dir = "W"
    lon_str = "%03d%s" % (lon_degree, lon_min_str) + lon_dir

    # Generate the added digits of precision, as per http://www.aprs.org/datum.txt
    # Base-91 can only encode decimal integers between 0 and 93 (otherwise we end up with non-printable characters)
    # So, we have to scale the range 00-99 down to 0-90, being careful to avoid errors due to floating point math.
    _lat_prec = int(round(float(("%02.4f" % lat_minute)[-2:]) / 1.10))
    _lon_prec = int(round(float(("%02.4f" % lon_minute)[-2:]) / 1.10))

    # Now we can add 33 to the 0-90 value to produce the Base-91 character.
    _lat_prec = chr(_lat_prec + 33)
    _lon_prec = chr(_lon_prec + 33)

    # Produce Datum + Added precision string
    # We currently assume all position data is using the WGS84 datum,
    # which I believe is true for most (if not all?) radiosondes.
    _datum = "!w%s%s!" % (_lat_prec, _lon_prec)

    # Convert Alt (in metres) to feet
    alt = int(float(sonde_data["alt"]) / 0.3048)

    # Produce the timestamp
    _aprs_timestamp = sonde_data["datetime_dt"].strftime("%H%M%S")

    # Generate course/speed data, if provided in the telemetry dictionary
    if ("heading" in sonde_data.keys()) and ("vel_h" in sonde_data.keys()):
        course_speed = "%03d/%03d" % (
            int(sonde_data["heading"]),
            int(sonde_data["vel_h"] * 1.944),
        )
    else:
        course_speed = "000/000"

    if position_report:
        # Produce an APRS position report string
        # Note, we are using the 'position with timestamp' data type, as per http://www.aprs.org/doc/APRS101.PDF
        out_str = "/%sh%s/%sO%s/A=%06d %s %s" % (
            _aprs_timestamp,
            lat_str,
            lon_str,
            course_speed,
            alt,
            _aprs_comment,
            _datum,
        )

    else:
        # Produce an APRS Object
        out_str = ";%s*%sh%s/%sO%s/A=%06d %s %s" % (
            _object_name,
            _aprs_timestamp,
            lat_str,
            lon_str,
            course_speed,
            alt,
            _aprs_comment,
            _datum,
        )

    # Return both the packet, and the 'callsign'.
    return (out_str, _object_name.strip())


def generate_station_object(
    callsign,
    lat,
    lon,
    comment="radiosonde_auto_rx SondeGate v<version>",
    icon="/r",
    position_report=False,
):
    """ Generate a station object """

    # Pad or limit the station callsign to 9 characters, if it is to long or short.
    if len(callsign) > 9:
        callsign = callsign[:9]
    elif len(callsign) < 9:
        callsign = callsign + " " * (9 - len(callsign))

    # Convert float latitude to APRS format (DDMM.MM)
    lat = float(lat)
    lat_degree = abs(int(lat))
    lat_minute = abs(lat - int(lat)) * 60.0
    lat_min_str = ("%02.4f" % lat_minute).zfill(7)[:5]
    lat_dir = "S"
    if lat > 0.0:
        lat_dir = "N"
    lat_str = "%02d%s" % (lat_degree, lat_min_str) + lat_dir

    # Convert float longitude to APRS format (DDDMM.MM)
    lon = float(lon)
    lon_degree = abs(int(lon))
    lon_minute = abs(lon - int(lon)) * 60.0
    lon_min_str = ("%02.4f" % lon_minute).zfill(7)[:5]
    lon_dir = "E"
    if lon < 0.0:
        lon_dir = "W"
    lon_str = "%03d%s" % (lon_degree, lon_min_str) + lon_dir

    # Generate the added digits of precision, as per http://www.aprs.org/datum.txt
    # Base-91 can only encode decimal integers between 0 and 93 (otherwise we end up with non-printable characters)
    # So, we have to scale the range 00-99 down to 0-90, being careful to avoid errors due to floating point math.
    _lat_prec = int(round(float(("%02.4f" % lat_minute)[-2:]) / 1.10))
    _lon_prec = int(round(float(("%02.4f" % lon_minute)[-2:]) / 1.10))

    # Now we can add 33 to the 0-90 value to produce the Base-91 character.
    _lat_prec = chr(_lat_prec + 33)
    _lon_prec = chr(_lon_prec + 33)

    # Produce Datum + Added precision string
    # We currently assume all position data is using the WGS84 datum.
    _datum = "!w%s%s!" % (_lat_prec, _lon_prec)

    # Generate timestamp using current UTC time
    _aprs_timestamp = datetime.datetime.utcnow().strftime("%H%M%S")

    # Add version string to position comment, if requested.
    _aprs_comment = comment
    _aprs_comment = _aprs_comment.replace("<version>", auto_rx_version)

    # Generate output string
    if position_report:
        # Produce a position report with no timestamp, as per page 32 of http://www.aprs.org/doc/APRS101.PDF
        out_str = "!%s%s%s%s%s %s" % (
            lat_str,
            icon[0],
            lon_str,
            icon[1],
            _aprs_comment,
            _datum,
        )

    else:
        # Produce an object string
        out_str = ";%s*%sh%s%s%s%s%s %s" % (
            callsign,
            _aprs_timestamp,
            lat_str,
            icon[0],
            lon_str,
            icon[1],
            _aprs_comment,
            _datum,
        )

    return out_str


#
# APRS Uploader Class
#


class APRSUploader(object):
    """ 
    Queued APRS Telemetry Uploader class
    This performs uploads to an APRS-IS server.

    Incoming telemetry packets are fed into queue, which is checked regularly.
    At a regular interval, the most recent telemetry packet is extracted, and converted to an
    APRS object format, and then uploaded into APRS-IS.

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
        aprs_callsign="N0CALL",
        aprs_passcode="00000",
        object_name_override=None,
        object_comment="RadioSonde",
        position_report=False,
        aprsis_host="rotate.aprs2.net",
        aprsis_port=14580,
        aprsis_reconnect=300,
        station_beacon=False,
        station_beacon_rate=30,
        station_beacon_position=(0.0, 0.0, 0.0),
        station_beacon_comment="radiosonde_auto_rx SondeGate v<version>",
        station_beacon_icon="/r",
        synchronous_upload_time=30,
        callsign_validity_threshold=5,
        upload_queue_size=16,
        upload_timeout=5,
        inhibit=False,
    ):
        """ Initialise an APRS Uploader object.

        Args:
            aprs_callsign (str): Callsign of the uploader, used when logging into APRS-IS.
            aprs_passcode (tuple): Optional - a tuple consisting of (lat, lon, alt), which if populated,
                is used to plot the listener's position on the Habitat map, both when this class is initialised, and
                when a new sonde ID is observed.

            object_name_override (str): Override the object name in the uploaded sentence with this value.
                WARNING: This will horribly break the aprs.fi map if multiple sondes are uploaded simultaneously under the same callsign.
                USE WITH CAUTION!!!
            object_comment (str): A comment to go with the object. Various fields will be replaced with telmetry data.

            position_report (bool): If True, upload positions as APRS position reports, otherwise, upload as an Object.

            aprsis_host (str): APRS-IS Server to upload packets to.
            aprsis_port (int): APRS-IS TCP port number.
            aprsis_reconnect (int): Reconnect to the APRS-IS server at least every X minutes. Reconnections will occur when telemetry needs to be sent.

            station_beacon (bool): Enable beaconing of station position.
            station_beacon_rate (int): Time delay between beacon uploads (minutes)
            station_beacon_position (tuple): (lat, lon, alt), in decimal degrees, of the station position.
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
        self.position_report = position_report
        self.aprsis_host = aprsis_host
        self.aprsis_port = aprsis_port
        self.aprsis_reconnect = aprsis_reconnect
        self.upload_timeout = upload_timeout
        self.upload_queue_size = upload_queue_size
        self.synchronous_upload_time = synchronous_upload_time
        self.callsign_validity_threshold = callsign_validity_threshold
        self.inhibit = inhibit

        self.station_beacon = {
            "enabled": station_beacon,
            "position": station_beacon_position,
            "rate": station_beacon_rate,
            "comment": station_beacon_comment,
            "icon": station_beacon_icon,
        }

        if object_name_override is None:
            self.object_name_override = "<id>"
        else:
            self.object_name_override = object_name_override
            self.log_info(
                "Using APRS Object Name Override: %s" % self.object_name_override
            )

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

        # APRS-IS Socket Object
        self.aprsis_socket = None
        self.aprsis_lastconnect = 0
        self.aprsis_upload_lock = Lock()
        # Attempt to connect to the APRS-IS server.
        # If this fails, we will attempt to re-connect when a packet needs to be uploaded.
        self.connect()

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

    def connect(self):
        """ Connect to an APRS-IS Server """
        # create socket & connect to server
        self.aprsis_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.aprsis_socket.settimeout(self.upload_timeout)
        try:
            self.aprsis_socket.connect((self.aprsis_host, self.aprsis_port))
            # Send logon string
            # _logon = 'user %s pass %s vers VK5QI-AutoRX filter b/%s \r\n' % (self.aprs_callsign, self.aprs_passcode, self.aprs_callsign)
            _logon = "user %s pass %s vers VK5QI-AutoRX\r\n" % (
                self.aprs_callsign,
                self.aprs_passcode,
            )
            self.log_debug("Logging in: %s" % _logon)
            self.aprsis_socket.sendall(_logon.encode("ascii"))

            # Set packet filters to limit inbound bandwidth.
            _filter = "#filter p/ZZ\r\n"
            self.log_debug("Setting Filter: %s" % _filter)
            self.aprsis_socket.sendall(_filter.encode("ascii"))
            _filter = "#filter -t/po\r\n"
            self.log_debug("Setting Filter: %s" % _filter)
            self.aprsis_socket.sendall(_filter.encode("ascii"))

            # Wait for login to complete.
            time.sleep(1)

            # Check response
            _resp = self.aprsis_socket.recv(1024)

            try:
                _resp = _resp.decode("ascii").strip()
            except:
                print(_resp)

            if _resp[0] != "#":
                raise IOError("Invalid response from APRS-IS Server: %s" % _resp)
            else:
                self.log_debug("Server Logon Response: %s" % str(_resp))

            self.log_info(
                "Connected to APRS-IS server %s:%d"
                % (self.aprsis_host, self.aprsis_port)
            )
            self.aprsis_lastconnect = time.time()
            return True

        except Exception as e:
            self.log_error("Connection to APRS-IS Failed - %s" % str(e))
            self.aprsis_socket = None
            return False

    def flush_rx(self):
        """ Flush the APRS-IS RX buffer """
        try:
            _start = time.time()
            _data = self.aprsis_socket.recv(32768)
            _dur = time.time() - _start
            self.log_debug("Incoming data from APRS-IS: %s" % (_data.decode()))
        except:
            # Ignore any exceptions from attempting to read the buffer.
            pass

    def aprsis_upload(self, source, packet, igate=False, retries=5):
        """ Upload a packet to APRS-IS

        Args:
            source (str): Callsign of the packet source.
            packet (str): APRS packet to upload.
            igate (boolean): If True, iGate the packet into APRS-IS
                (i.e. use the original source call, but add SONDEGATE and our callsign to the path.)
            retries (int): Number of times to retry uploading.

        """
        # If we are inhibited, just return immediately.
        if self.inhibit:
            self.log_info("Upload Inhibited: %s" % packet)
            return True

        self.aprsis_upload_lock.acquire()

        # If we have not connected in a long time, reset the APRS-IS connection.
        if (time.time() - self.aprsis_lastconnect) > (self.aprsis_reconnect * 60):
            self.disconnect()
            time.sleep(1)
            self.connect()

        # Generate APRS packet
        if igate:
            # If we are emulating an IGATE, then we need to add in a path, a q-construct, and our own callsign.
            # We have the TOCALL field 'APRARX' allocated by Bob WB4APR, so we can now use this to indicate
            # that these packets have arrived via radiosonde_auto_rx!
            _packet = "%s>APRARX,SONDEGATE,TCPIP,qAR,%s:%s\r\n" % (
                source,
                self.aprs_callsign,
                packet,
            )
        else:
            # Otherwise, we are probably just placing an object, usually sourced by our own callsign
            _packet = "%s>APRS:%s\r\n" % (source, packet)

        _attempts = 1
        while _attempts < retries:
            try:
                # Immediately throw exception if we're not connected.
                # This will trigger a reconnect.
                if self.aprsis_socket is None:
                    raise IOError("Socket not connected.")

                # Attempt to send the packet.
                # This will timeout if the socket is locked up.
                self.aprsis_socket.sendall(_packet.encode("ascii"))

                # If OK, return.
                self.log_info("Uploaded to APRS-IS: %s" % str(_packet).strip())
                self.aprsis_upload_lock.release()
                return True

            except Exception as e:
                # If something broke, forcibly shutdown the socket, then reconnect.
                self.log_error("Upload Error: %s" % str(e))

                self.log_info("Attempting to reconnect...")
                self.disconnect()
                time.sleep(1)
                self.connect()

                _attempts += 1

        # If we end up here, something has really broken.
        self.aprsis_upload_lock.release()
        return False

    def disconnect(self):
        """ Close APRS-IS connection """
        try:
            self.aprsis_socket.shutdown(0)
        except Exception as e:
            self.log_debug("Socket shutdown failed - %s" % str(e))

        try:
            self.aprsis_socket.close()
        except Exception as e:
            self.log_debug("Socket close failed - %s" % str(e))

    def beacon_station_position(self):
        """ Send a station position beacon into APRS-IS """
        if self.station_beacon["enabled"]:
            if (self.station_beacon["position"][0] == 0.0) and (
                self.station_beacon["position"][1] == 0.0
            ):
                self.log_error(
                    "Station position is 0,0, not uploading position beacon."
                )
                self.last_user_position_upload = time.time()
                return

            # Generate the station position packet
            # Note - this is now generated as an APRS position report, for radiosondy.info compatability.
            _packet = generate_station_object(
                self.aprs_callsign,
                self.station_beacon["position"][0],
                self.station_beacon["position"][1],
                self.station_beacon["comment"],
                self.station_beacon["icon"],
                position_report=True,
            )

            # Send the packet as an iGated packet.
            self.aprsis_upload(self.aprs_callsign, _packet, igate=True)
            self.last_user_position_upload = time.time()

    def update_station_position(self, lat, lon, alt):
        """ Update the internal station position record. Used when determining the station position by GPSD """
        self.station_beacon["position"] = (lat, lon, alt)

    def aprs_upload_thread(self):
        """ Handle uploading of packets to APRS """

        self.log_debug("Started APRS Uploader Thread.")

        while self.upload_thread_running:

            if self.aprs_upload_queue.qsize() > 0:
                # If the queue is completely full, jump to the most recent telemetry sentence.
                if self.aprs_upload_queue.qsize() == self.upload_queue_size:
                    while not self.aprs_upload_queue.empty():
                        _telem = self.aprs_upload_queue.get()

                    self.log_warning(
                        "Uploader queue was full - possible connectivity issue."
                    )
                else:
                    # Otherwise, get the first item in the queue.
                    _telem = self.aprs_upload_queue.get()

                # Convert to a packet.
                try:
                    (_packet, _call) = telemetry_to_aprs_position(
                        _telem,
                        object_name=self.object_name_override,
                        aprs_comment=self.object_comment,
                        position_report=self.position_report,
                    )
                except Exception as e:
                    self.log_error(
                        "Error converting telemetry to APRS packet - %s" % str(e)
                    )
                    _packet = None

                # Attempt to upload it.
                if _packet is not None:

                    # If we are uploading position reports, the source call is the generated callsign
                    # usually based on the sonde serial number, and we iGate the position report.
                    # Otherwise, we upload APRS Objects, sourced by our own callsign, but still iGated via us.
                    if self.position_report:
                        self.aprsis_upload(_call, _packet, igate=True)
                    else:
                        self.aprsis_upload(self.aprs_callsign, _packet, igate=True)

            else:
                # Wait for a short time before checking the queue again.
                time.sleep(0.1)

        self.log_debug("Stopped APRS Uploader Thread.")

    def upload_timer(self):
        """ Add packets to the aprs upload queue if it is time for us to upload. """

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

                        # Attept to add it to the habitat uploader queue.
                        try:
                            self.aprs_upload_queue.put_nowait(_telem)
                        except Exception as e:
                            self.log_error(
                                "Error adding sentence to queue: %s" % str(e)
                            )

                # Sleep a second so we don't hit the synchronous upload time again.
                time.sleep(1)

                # Flush APRS-IS RX buffer
                self.flush_rx()
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
                    self.observed_payloads[_id] = {"count": 1, "data": Queue()}
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
                        # Add the telemetry to the queue
                        self.observed_payloads[_id]["data"].put(_telem)

                    else:
                        self.log_debug(
                            "Payload ID %s not observed enough to allow upload." % _id
                        )

            if (time.time() - self.last_user_position_upload) > self.station_beacon[
                "rate"
            ] * 60:
                if self.aprsis_socket != None:
                    self.beacon_station_position()

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

    def close(self):
        """ Shutdown uploader and processing threads. """
        self.log_debug("Waiting for threads to close...")
        self.input_processing_running = False
        self.timer_thread_running = False
        self.upload_thread_running = False

        self.disconnect()

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


if __name__ == "__main__":
    # Some unit tests for the APRS packet generation code.
    # ['frame', 'id', 'datetime', 'lat', 'lon', 'alt', 'temp', 'type', 'freq', 'freq_float', 'datetime_dt']
    test_telem = [
        # These types of DFM serial IDs are deprecated
        # {'id':'DFM06-123456', 'frame':10, 'lat':-10.0, 'lon':10.0, 'alt':10000, 'temp':1.0, 'type':'DFM', 'freq':'401.520 MHz', 'freq_float':401.52, 'heading':0.0, 'vel_h':5.1, 'vel_v':-5.0, 'datetime_dt':datetime.datetime.utcnow()},
        # {'id':'DFM09-123456', 'frame':10, 'lat':-10.0, 'lon':10.0, 'alt':10000, 'temp':1.0, 'type':'DFM', 'freq':'401.520 MHz', 'freq_float':401.52, 'heading':0.0, 'vel_h':5.1, 'vel_v':-5.0, 'datetime_dt':datetime.datetime.utcnow()},
        # {'id':'DFM15-123456', 'frame':10, 'lat':-10.0, 'lon':10.0, 'alt':10000, 'temp':1.0, 'type':'DFM', 'freq':'401.520 MHz', 'freq_float':401.52, 'heading':0.0, 'vel_h':5.1, 'vel_v':-5.0, 'datetime_dt':datetime.datetime.utcnow()},
        # {'id':'DFM17-12345678', 'frame':10, 'lat':-10.0, 'lon':10.0, 'alt':10000, 'temp':1.0, 'type':'DFM', 'freq':'401.520 MHz', 'freq_float':401.52, 'heading':0.0, 'vel_h':5.1, 'vel_v':-5.0, 'datetime_dt':datetime.datetime.utcnow()},
        {
            "id": "DFM-19123456",
            "frame": 10,
            "lat": -10.0,
            "lon": 10.0,
            "alt": 10000,
            "temp": 1.0,
            "humidity": 1.0,
            "pressure": 1000.0,
            "batt": 3.0,
            "type": "DFM17",
            "freq": "401.520 MHz",
            "freq_float": 401.52,
            "heading": 0.0,
            "vel_h": 5.1,
            "vel_v": -5.0,
            "datetime_dt": datetime.datetime.utcnow(),
        },
        {
            "id": "DFM-123456",
            "frame": 10,
            "lat": -10.0,
            "lon": 10.0,
            "alt": 10000,
            "temp": 1.0,
            "humidity": 1.0,
            "pressure": 1000.0,
            "batt": 3.0,
            "type": "DFM06",
            "freq": "401.520 MHz",
            "freq_float": 401.52,
            "heading": 0.0,
            "vel_h": 5.1,
            "vel_v": -5.0,
            "datetime_dt": datetime.datetime.utcnow(),
        },
        {
            "id": "N1234567",
            "frame": 10,
            "lat": -10.00001,
            "lon": 9.99999999,
            "alt": 10000,
            "temp": 1.0,
            "humidity": 1.0,
            "pressure": 1000.0,
            "batt": 3.0,
            "type": "RS41",
            "freq": "401.520 MHz",
            "freq_float": 401.52,
            "heading": 0.0,
            "vel_h": 5.1,
            "vel_v": -5.0,
            "datetime_dt": datetime.datetime.utcnow(),
        },
        {
            "id": "M1234567",
            "frame": 10,
            "lat": -10.0,
            "lon": 10.0,
            "alt": 10000,
            "temp": 1.0,
            "humidity": 1.0,
            "pressure": 1000.0,
            "batt": 3.0,
            "type": "RS92",
            "freq": "401.520 MHz",
            "freq_float": 401.52,
            "heading": 0.0,
            "vel_h": 5.1,
            "vel_v": -5.0,
            "datetime_dt": datetime.datetime.utcnow(),
        },
    ]

    comment_field = (
        "Clb=<vel_v> t=<temp> <freq> Type=<type> Radiosonde http://bit.ly/2Bj4Sfk"
    )

    for _telem in test_telem:
        out_str = telemetry_to_aprs_position(
            _telem,
            object_name="<id>",
            aprs_comment=comment_field,
            position_report=False,
        )
        print(out_str)

    # APRS Testing
    logging.basicConfig(
        format="%(asctime)s %(levelname)s:%(message)s", level=logging.DEBUG
    )
    test = APRSUploader(
        aprs_callsign="VK5QI", aprs_passcode="23032", aprsis_host="radiosondy.info"
    )
    test.connect()

    time.sleep(5)
    test.disconnect()

    test.close()
