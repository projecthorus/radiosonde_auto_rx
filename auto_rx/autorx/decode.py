#!/usr/bin/env python
#
#   radiosonde_auto_rx - Sonde Decoder Class.
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import logging
import json
import os
import signal
import subprocess
import time
from dateutil.parser import parse
from threading import Thread
from types import FunctionType, MethodType
from .utils import AsynchronousFileReader



class SondeDecoder(object):
    '''
    Generic Sonde Decoder class. Run a radiosonde decoder program as a subprocess, and pass the output onto exporters.

    Notes:
    The sonde decoder binary needs to output telemetry data as a valid JSON, with one frame of telemetry per line.
    Example:
    { "frame": 1909, "id": "N3710309", "datetime": "2018-05-12T11:32:20.000Z", "lat": -34.90842, "lon": 138.49243, "alt": 3896.43871, "vel_h": 8.60708, "heading": 342.43237, "vel_v": 6.83107 }
    Required Fields are:
        "frame" (int):  Frame counter. Usually provided by the radiosonde, and increments per telemetry frame.
        "id" (str):     Unique identifier for the radiosonde, usually some kind of serial number.
        "datetime" (str): UTC Date/Time string, which indicates the time applicable to the telemetry sentence.
            Must be parseable with dateutil. 
        "lat" (float):  Radiosonde Latitude (decmial degrees)
        "lon" (float):  Radiosonde Longitude (decimal degrees)
        "alt" (float):  Radiosonde Altitude (metres)
    Optional Fields:
        These fields will be set to dummy values if they are not provided within the JSON blob.
        "temp" (float): Atmospheric temperature reported by the Radiosonde (degrees Celsius)
        "humidity" (float): Humidity value, reported by the radiosonde (%)
        "vel_h" (float): Horizontal Velocity (metres/s)
        "vel_v" (float): Vertical Velocity (metres/s)
        "heading" (float): Heading of the movement of the payload (degrees true)
    The following fields are added to the dictionary:
        "type" (str): Radiosonde type
        "freq_float" (float): Radiosonde frequency in MHz, as a float.
        "freq" (str): Radiosonde frequency as a string (XXX.XXX MHz).
        "datetime_dt" (datetime): Telemetry sentence time, as a datetime object.
    '''

    DECODER_REQUIRED_FIELDS = ['frame', 'id', 'datetime', 'lat', 'lon', 'alt']
    DECODER_OPTIONAL_FIELDS = {
        'temp'      : -273.0,
        'humidity'  : -1,
        'vel_h'     : 0.0,
        'vel_v'     : 0.0,
        'heading'   : 0.0
    }

    def __init__(self,
        decoder_command,
        sonde_frequency=400.0,
        sonde_type='',
        exporter = None,
        timeout = 180,
        telem_filter = None):
        """ Initialise and start a Sonde Decoder.

        Args:
            decoder_command (str): The command to be run as a decoder. This will be run as a subprocess.Popen within a thread.
            sonde_frequency (float): The radio freqency of the current sonde (as a float, in MHz). Defaults to 400 MHz.
            sonde_type (str): The general type of the radiosonde to be decoder (as a string, i.e. 'RS41', to be passed onto the exporters.
                Defaults to an empty string.
            exporter (function, list): Either a function, or a list of functions, which accept a single dictionary. Fields described above.
            timeout (int): Timeout after X seconds of no valid data received from the decoder.
            telem_filter (function): An optional filter function, which determines if a telemetry frame is valid. 
                This can be used to allow the decoder to timeout based on telemetry contents (i.e. no lock, too far away, etc), 
                not just lack-of-telemetry. This function is passed the telemetry dict, and must return a boolean based on the telemetry validity
        """
        # Local copy of init arguments
        self.decoder_command = decoder_command
        self.sonde_frequency = sonde_frequency
        self.sonde_type = sonde_type
        self.telem_filter = telem_filter
        self.timeout = timeout

        # We can accept a few different types in the exporter argument..
        # Nothing...
        if exporter == None:
            self.exporters = None

        # A single function...
        elif type(exporter) == FunctionType:
            self.exporters = [exporter]

        # A list of functions...
        elif type(exporter) == list:
            # Check everything in the list is a function
            for _func in exporter:
                if (type(_func) is not FunctionType) and (type(_func) is not MethodType):
                    raise TypeError("Supplied exporter list does not contain functions.")
            
            # If it all checks out, use the supplied list.
            self.exporters = exporter
            
        else:
            # Otherwise, bomb out. 
            raise TypeError("Supplied exporter has incorrect type.")

        # Start up the decoder thread.
        self.decode_process = None
        self.async_reader = None

        self.decoder_running = True
        self.decoder = Thread(target=self.decoder_thread)
        self.decoder.start()


    def decoder_thread(self):
        """ Runs the supplied decoder command as a subprocess, and passes returned lines to handle_decoder_line. """
        
        # Timeout Counter. 
        _last_packet = time.time()

        # Start the thread.
        self.decode_process = subprocess.Popen(self.decoder_command, shell=True, stdin=None, stdout=subprocess.PIPE, preexec_fn=os.setsid) 
        self.async_reader = AsynchronousFileReader(self.decode_process.stdout, autostart=True)

        self.log_info("Starting decoder subprocess.")

        while (not self.async_reader.eof()) and self.decoder_running:
            # Read in any lines available in the async reader queue.
            for _line in self.async_reader.readlines():
                if (_line != None) and (_line != ""):
                    # Pass the line into the handler, and see if it is OK.
                    _ok = self.handle_decoder_line(_line)

                    # If we decoded a valid JSON blob, update our last-packet time.
                    if _ok:
                        _last_packet = time.time()



            # Check timeout counter.
            if time.time() > (_last_packet + self.timeout):
                # If we have not seen data for a while, break.
                self.log_error("RX Timed out.")
                break
            else:
                # Otherwise, sleep for a short time.
                time.sleep(0.1)

        # Either our subprocess has exited, or the user has asked to close the process. 
        #Try many things to kill off the subprocess.
        try:
            # Stop the async reader
            self.async_reader.stop()
            # Send a SIGKILL via subprocess
            self.decode_process.kill()
            # Send a SIGKILL to the subprocess PID via OS.
            os.killpg(os.getpgid(self.decode_process.pid), signal.SIGKILL)
            # Finally, join the async reader.
            self.async_reader.join()
            

        except Exception as e:
            self.log_error("Error while killing subprocess - %s" % str(e))

        self.log_info("Closed decoder subprocess.")
        self.decoder_running = False


    def handle_decoder_line(self, data):
        """ Handle a line of output from the decoder subprocess.

        Args:
            data (str, bytearray): One line of text output from the decoder subprocess.

        Returns:
            bool:   True if the line was decoded to a JSON object correctly, False otherwise.
        """

        # Don't even try and decode lines which don't start with a '{'
        # These may be other output from the decoder, which we shouldn't try to parse.

        # Catch 'bad' first characters.
        try:
            _first_char = data.decode('ascii')[0]
        except UnicodeDecodeError:
            return

        # Catch non-JSON object lines.
        if data.decode('ascii')[0] != '{':
            return

        else:
            try:
                _telemetry = json.loads(data)
            except Exception as e:
                self.log_debug("Line could not be parsed as JSON - %s" % str(e))
                return False

            # Check the JSON blob has been parsed as a dictionary
            if type(_telemetry) is not dict:
                self.log_error("Parsed JSON object is not a dictionary!")
                return False

            # Check that the required fields are in the telemetry blob
            for _field in self.DECODER_REQUIRED_FIELDS:
                if _field not in _telemetry:
                    self.log_error("JSON object missing required field %s" % _field)
                    return False

            # Check for optional fields, and add them if necessary.
            for _field in self.DECODER_OPTIONAL_FIELDS.keys():
                if _field not in _telemetry:
                    _telemetry[_field] = self.DECODER_OPTIONAL_FIELDS[_field]

            # Check the datetime field is parseable.
            try:
                _telemetry['datetime_dt'] = parse(_telemetry['datetime'])
            except Exception as e:
                self.log_error("Invalid date/time in telemetry dict - %s" % str(e))
                return False

            # Add in the sonde frequency and type fields.
            _telemetry['type'] = self.sonde_type
            _telemetry['freq_float'] = self.sonde_frequency
            _telemetry['freq'] = "%.3f MHz" % (self.sonde_frequency)

            # Check for an 'aux' field, this indicates that the sonde has an auxilliary payload,
            # which is most likely an Ozone sensor. We append -Ozone to the sonde type field to indicate this.
            if 'aux' in _telemetry:
                _telemetry['type'] += "-Ozone"


            # Send to the exporter functions (if we have any).
            if self.exporters is None:
                return
            else:
                for _exporter in self.exporters:
                    try:
                        _exporter(_telemetry)
                    except Exception as e:
                        self.log_error("Exporter Error %s" % str(e))

            # If we have been provided a telemetry filter function, pass the telemetry data
            # through the filter, and return the response
            if self.telem_filter is not None:
                try:
                    _ok = self.telem_filter(_telemetry)
                    return _ok
                except Exception as e:
                    self.log_error("Failed to run telemetry filter - %s" % str(e))

            # Otherwise, just assume the telemetry is good.
            else:
                return True




    def log_debug(self, line):
        """ Helper function to log a debug message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.debug("Decoder %s %s - %s" % (self.sonde_type, self.sonde_frequency, line))


    def log_info(self, line):
        """ Helper function to log an informational message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.info("Decoder %s %s - %s" % (self.sonde_type, self.sonde_frequency, line))


    def log_error(self, line):
        """ Helper function to log an error message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.error("Decoder %s %s - %s" % (self.sonde_type, self.sonde_frequency, line))


    def close(self):
        """ Kill the currently running decoder subprocess """
        self.decoder_running = False


    def running(self):
        """ Check if the decoder subprocess is running. 

        Returns:
            bool: True if the decoder subprocess is running.
        """
        return self.decoder_running


if __name__ == "__main__":
    # Test script.
    from .logger import TelemetryLogger

    logging.basicConfig(format='%(asctime)s %(levelname)s:%(message)s', level=logging.DEBUG)

    def print_json(data):
        """ Test Exporter function """
        print("JSON: " + str(data))

    def print_id(data):
        """ Another test exporter function """
        print("ID: " + data['id'])

    _log = TelemetryLogger(log_directory="./testlog/")


    _cmd = "rtl_fm -p 0 -g 40 -M fm -F9 -s 15k -f 405500000 | sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - lowpass 2600 2>/dev/null | ./rs41ecc --crc --ecc --ptu"

    _decoder = SondeDecoder(_cmd,
        sonde_frequency = 402.5,
        sonde_type = "TEST",
        timeout = 50,
        exporter=[print_id,_log.add])


    try:
        while True:
            time.sleep(1)
            if not _decoder.running():
                break
    except KeyboardInterrupt:
        _decoder.close()
    
    _log.close()



