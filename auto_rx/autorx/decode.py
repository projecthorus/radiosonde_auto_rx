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
import traceback
from dateutil.parser import parse
from threading import Thread
from types import FunctionType, MethodType
from .utils import AsynchronousFileReader, rtlsdr_test
from .gps import get_ephemeris, get_almanac

# Global valid sonde types list.
VALID_SONDE_TYPES = ['RS92', 'RS41', 'DFM', 'M10']


class SondeDecoder(object):
    '''
    Radiosonde Sonde Decoder class. Run a radiosonde decoder program as a subprocess, and pass the output onto exporters.

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

    VALID_SONDE_TYPES = ['RS92', 'RS41', 'DFM', 'M10']

    def __init__(self,
        sonde_type="None",
        sonde_freq=400000000.0,
        rs_path = "./",
        sdr_fm = "rtl_fm",
        device_idx = 0,
        ppm = 0,
        gain = -1,
        bias = False,

        exporter = None,
        timeout = 180,
        telem_filter = None,

        rs92_ephemeris = None):
        """ Initialise and start a Sonde Decoder.

        Args:
            sonde_type (str): The radiosonde type, as returned by SondeScanner. Valid types listed in VALID_SONDE_TYPES
            sonde_freq (int/float): The radiosonde frequency, in Hz.
            
            rs_path (str): Path to the RS binaries (i.e rs_detect). Defaults to ./
            sdr_fm (str): Path to rtl_fm, or drop-in equivalent. Defaults to 'rtl_fm'
            device_idx (int or str): Device index or serial number of the RTLSDR. Defaults to 0 (the first SDR found).
            ppm (int): SDR Frequency accuracy correction, in ppm.
            gain (int): SDR Gain setting, in dB. A gain setting of -1 enables the RTLSDR AGC.
            bias (bool): If True, enable the bias tee on the SDR.

            exporter (function, list): Either a function, or a list of functions, which accept a single dictionary. Fields described above.
            timeout (int): Timeout after X seconds of no valid data received from the decoder. Defaults to 180.
            telem_filter (function): An optional filter function, which determines if a telemetry frame is valid. 
                This can be used to allow the decoder to timeout based on telemetry contents (i.e. no lock, too far away, etc), 
                not just lack-of-telemetry. This function is passed the telemetry dict, and must return a boolean based on the telemetry validity.

            rs92_ephemeris (str): OPTIONAL - A fixed ephemeris file to use if decoding a RS92. If not supplied, one will be downloaded.
        """
        # Thread running flag
        self.decoder_running = True

        # Local copy of init arguments
        self.sonde_type = sonde_type
        self.sonde_freq = sonde_freq

        self.rs_path = rs_path
        self.sdr_fm = sdr_fm
        self.device_idx = device_idx
        self.ppm = ppm
        self.gain = gain
        self.bias = bias

        self.telem_filter = telem_filter
        self.timeout = timeout
        self.rs92_ephemeris = rs92_ephemeris

        # This will become our decoder thread.
        self.decoder = None

        # Detect if we have an 'inverted' sonde.
        if self.sonde_type.startswith('-'):
            self.inverted = True
            # Strip off the leading '-' character'
            self.sonde_type = self.sonde_type[1:]
        else:
            self.inverted = False

        # Check if the sonde type is valid.
        if self.sonde_type not in self.VALID_SONDE_TYPES:
            self.log_error("Unsupported sonde type: %s" % self.sonde_type)
            self.decoder_running = False
            return 

        # Test if the supplied RTLSDR is working.
        _rtlsdr_ok = rtlsdr_test(device_idx)

        # TODO: How should this error be handled?
        if not _rtlsdr_ok:
            self.log_error("RTLSDR #%s non-functional - exiting." % device_idx)
            self.decoder_running = False
            return

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

        # Generate the decoder command.
        self.decoder_command = self.generate_decoder_command()

        if self.decoder_command is None:
            self.log_error("Could not generate decoder command. Not starting decoder.")
            self.decoder_running = False
        else:
            # Start up the decoder thread.
            self.decode_process = None
            self.async_reader = None

            self.decoder_running = True
            self.decoder = Thread(target=self.decoder_thread)
            self.decoder.start()


    def generate_decoder_command(self):
        """ Generate the shell command which runs the relevant radiosonde decoder.

        This is where support for new sonde types can be added.s

        Returns:
            str/None: The shell command which will be run in the decoder thread, or none if a valid decoder could not be found.

        """
        # Common options to rtl_fm

        # Add a -T option if bias is enabled
        bias_option = "-T " if self.bias else ""

        # Add a gain parameter if we have been provided one.
        if self.gain != -1:
            gain_param = '-g %.1f ' % self.gain
        else:
            gain_param = ''


        # Emit demodulator statistics every X modem frames.
        _stats_rate = 10


        if self.sonde_type == "RS41":
            # RS41 Decoder command.
            _sdr_rate = 48000 # IQ rate. Lower rate = lower CPU usage, but less frequency tracking ability.
            _baud_rate = 4800
            _offset = 0.25 # Place the sonde frequency in the centre of the passband.
            _lower = int(0.025 * _sdr_rate) # Limit the frequency estimation window to not include the passband edges.
            _upper = int(0.475 * _sdr_rate)
            _freq = int(self.sonde_freq - _sdr_rate*_offset)

            decode_cmd = "%s %s-p %d -d %s %s-M raw -s %d -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, _sdr_rate, _freq)
            decode_cmd += "./fsk_demod --cs16 -b %d -u %d --stats=%d 2 %d %d - - 2>stats.txt " % (_lower, _upper, _stats_rate, _sdr_rate, _baud_rate)
            decode_cmd += "| python ./test/bit_to_samples.py %d %d | sox -t raw -r %d -e unsigned-integer -b 8 -c 1 - -r %d -b 8 -t wav - 2>/dev/null|" % (_sdr_rate, _baud_rate, _sdr_rate, _sdr_rate)
            decode_cmd += "./rs41ecc --crc --ecc --ptu --json 2>/dev/null"


        elif self.sonde_type == "RS92":
            # Decoding a RS92 requires either an ephemeris or an almanac file.
            # If we have been supplied an ephemeris file, we will attempt to use it, otherwise
            # we will try and download one.
            if self.rs92_ephemeris == None:
                # If no ephemeris data defined, attempt to download it.
                # get_ephemeris will either return the saved file name, or None.
                self.rs92_ephemeris = get_ephemeris(destination="ephemeris.dat")

                # If ephemeris is still None, then we failed to download the ephemeris data.
                # Try and grab the almanac data instead
                if self.rs92_ephemeris == None:
                    self.log_error("Could not obtain ephemeris data, trying to download an almanac.")
                    almanac = get_almanac(destination="almanac.txt")
                    if almanac == None:
                        # We probably don't have an internet connection. Bomb out, since we can't do much with the sonde telemetry without an almanac!
                        self.log_error("Could not obtain GPS ephemeris or almanac data.")
                        return None
                    else:
                        _rs92_gps_data = "-a almanac.txt"
                else:
                    _rs92_gps_data = "-e ephemeris.dat"
            else:
                _rs92_gps_data = "-e %s" % self.rs92_ephemeris


            if self.sonde_freq > 1000e6:
                # Use a higher IQ rate for 1680 MHz sondes, at the expensive of some CPU usage.
                _sdr_rate = 96000
            else:
                # On 400 MHz, use 48 khz - RS92s dont drift far enough to need any more than this.
                _sdr_rate = 48000

            _baud_rate = 4800
            _offset = 0.25 # Place the sonde frequency in the centre of the passband.
            _lower = int(0.025 * _sdr_rate) # Limit the frequency estimation window to not include the passband edges.
            _upper = int(0.475 * _sdr_rate)
            _freq = int(self.sonde_freq - _sdr_rate*_offset)

            decode_cmd = "%s %s-p %d -d %s %s-M raw -s %d -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, _sdr_rate, _freq)
            decode_cmd += "./fsk_demod --cs16 -b %d -u %d --stats=%d 2 %d %d - - 2>stats.txt " % (_lower, _upper, _stats_rate, _sdr_rate, _baud_rate)
            decode_cmd += "| python ./test/bit_to_samples.py %d %d | sox -t raw -r %d -e unsigned-integer -b 8 -c 1 - -r %d -b 8 -t wav - 2>/dev/null|" % (_sdr_rate, _baud_rate, _sdr_rate, _sdr_rate)
            decode_cmd += "./rs92ecc -vx -v --crc --ecc --vel --json %s 2>/dev/null" % _rs92_gps_data

        elif self.sonde_type == "DFM":
            # DFM06/DFM09 Sondes.
            # As of 2019-02-10, dfm09ecc auto-detects if the signal is inverted,
            # so we don't need to specify an invert flag.
            # 2019-02-27: Added the --dist flag, which should reduce bad positions a bit.
            _sdr_rate = 50000
            _baud_rate = 2500
            _offset = 0.25 # Place the sonde frequency in the centre of the passband.
            _lower = int(0.025 * _sdr_rate) # Limit the frequency estimation window to not include the passband edges.
            _upper = int(0.475 * _sdr_rate)
            _freq = int(self.sonde_freq - _sdr_rate*_offset)

            decode_cmd = "%s %s-p %d -d %s %s-M raw -s %d -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, _sdr_rate, _freq)
            decode_cmd += "./fsk_demod --cs16 -b %d -u %d --stats=%d 2 %d %d - - 2>stats.txt " % (_lower, _upper, _stats_rate, _sdr_rate, _baud_rate)
            decode_cmd += "| python ./test/bit_to_samples.py %d %d | sox -t raw -r %d -e unsigned-integer -b 8 -c 1 - -r %d -b 8 -t wav - 2>/dev/null|" % (_sdr_rate, _baud_rate, _sdr_rate, _sdr_rate)
            decode_cmd += "./dfm09ecc -vv --ecc --json --dist --auto 2>/dev/null"
			
        elif self.sonde_type == "M10":
            # M10 Sondes
            _sdr_rate = 48080
            _baud_rate = 9616
            _offset = 0.25 # Place the sonde frequency in the centre of the passband.
            _lower = int(0.025 * _sdr_rate) # Limit the frequency estimation window to not include the passband edges.
            _upper = int(0.475 * _sdr_rate)
            _freq = int(self.sonde_freq - _sdr_rate*_offset)

            decode_cmd = "%s %s-p %d -d %s %s-M raw -s %d -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, _sdr_rate, _freq)
            decode_cmd += "./fsk_demod --cs16 -b %d -u %d --stats=%d 2 %d %d - - 2>stats.txt " % (_lower, _upper, _stats_rate, _sdr_rate, _baud_rate)
            decode_cmd += "| python ./test/bit_to_samples.py %d %d | sox -t raw -r %d -e unsigned-integer -b 8 -c 1 - -r %d -b 8 -t wav - 2>/dev/null| " % (_sdr_rate, _baud_rate, _sdr_rate, _sdr_rate)
            # M10 decoder
            decode_cmd += "./m10 -b -b2 2>/dev/null"

        else:
            # Should never get here.
            return None

        return decode_cmd


    def decoder_thread(self):
        """ Runs the supplied decoder command as a subprocess, and passes returned lines to handle_decoder_line. """
        
        # Timeout Counter. 
        _last_packet = time.time()

        self.log_debug("Decoder Command: %s" % self.decoder_command )

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
            # Send a SIGKILL to the subprocess PID via OS.
            try:
                os.killpg(os.getpgid(self.decode_process.pid), signal.SIGKILL)
            except Exception as e:
                self.log_debug("SIGKILL via os.killpg failed. - %s" % str(e))
            time.sleep(1)
            try:
                # Send a SIGKILL via subprocess
                self.decode_process.kill()
            except Exception as e:
                self.log_debug("SIGKILL via subprocess.kill failed - %s" % str(e))
            # Finally, join the async reader.
            self.async_reader.join()
            

        except Exception as e:
            traceback.print_exc()
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
                _telemetry = json.loads(data.decode('ascii'))
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
                self.log_error("Invalid date/time in telemetry dict - %s (Sonde may not have GPS lock" % str(e))
                return False

            # Add in the sonde frequency and type fields.
            _telemetry['type'] = self.sonde_type
            _telemetry['freq_float'] = self.sonde_freq/1e6
            _telemetry['freq'] = "%.3f MHz" % (self.sonde_freq/1e6)

            # Add in information about the SDR used.
            _telemetry['sdr_device_idx'] = self.device_idx

            # Check for an 'aux' field, this indicates that the sonde has an auxilliary payload,
            # which is most likely an Ozone sensor. We append -Ozone to the sonde type field to indicate this.
            if 'aux' in _telemetry:
                _telemetry['type'] += "-Ozone"

            # If we have been provided a telemetry filter function, pass the telemetry data
            # through the filter, and return the response
            # By default, we will assume the telemetry is OK.
            _telem_ok = True
            if self.telem_filter is not None:
                try:
                    _telem_ok = self.telem_filter(_telemetry)
                except Exception as e:
                    self.log_error("Failed to run telemetry filter - %s" % str(e))
                    _telem_ok = True


            # If the telemetry is OK, send to the exporter functions (if we have any).
            if self.exporters is None:
                return
            else:
                if _telem_ok:
                    for _exporter in self.exporters:
                        try:
                            _exporter(_telemetry)
                        except Exception as e:
                            self.log_error("Exporter Error %s" % str(e))

            return _telem_ok




    def log_debug(self, line):
        """ Helper function to log a debug message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.debug("Decoder #%s %s %.3f - %s" % (str(self.device_idx), self.sonde_type, self.sonde_freq/1e6, line))


    def log_info(self, line):
        """ Helper function to log an informational message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.info("Decoder #%s %s %.3f - %s" % (str(self.device_idx), self.sonde_type, self.sonde_freq/1e6, line))


    def log_error(self, line):
        """ Helper function to log an error message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.error("Decoder #%s %s %.3f - %s" % (str(self.device_idx), self.sonde_type, self.sonde_freq/1e6, line))


    def stop(self):
        """ Kill the currently running decoder subprocess """
        self.decoder_running = False

        if self.decoder is not None:
            self.decoder.join()


    def running(self):
        """ Check if the decoder subprocess is running. 

        Returns:
            bool: True if the decoder subprocess is running.
        """
        return self.decoder_running


if __name__ == "__main__":
    # Test script.
    from .logger import TelemetryLogger
    from .habitat import HabitatUploader

    logging.basicConfig(format='%(asctime)s %(levelname)s:%(message)s', level=logging.DEBUG)
    # Make requests & urllib3 STFU
    requests_log = logging.getLogger("requests")
    requests_log.setLevel(logging.CRITICAL)
    urllib3_log = logging.getLogger("urllib3")
    urllib3_log.setLevel(logging.CRITICAL)


    _log = TelemetryLogger(log_directory="./testlog/")
    _habitat = HabitatUploader(user_callsign="VK5QI_AUTO_RX_DEV", inhibit=False)

    try:
        _decoder = SondeDecoder(sonde_freq = 401.5*1e6,
            sonde_type = "RS41",
            timeout = 50,
            device_idx="00000002",
            exporter=[_habitat.add, _log.add])

        # _decoder2 = SondeDecoder(sonde_freq = 405.5*1e6,
        #     sonde_type = "RS41",
        #     timeout = 50,
        #     device_idx="00000001",
        #     exporter=[_habitat.add, _log.add])

        while True:
            time.sleep(5)
            if not _decoder.running():
                break
    except KeyboardInterrupt:
        _decoder.stop()
        #_decoder2.stop()
    except:
        traceback.print_exc()
        pass
    
    _habitat.close()
    _log.close()



