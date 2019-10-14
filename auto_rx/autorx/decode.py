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
from .sonde_specific import *
from .fsk_demod import FSKDemodStats

# Global valid sonde types list.
VALID_SONDE_TYPES = ['RS92', 'RS41', 'DFM', 'M10', 'iMet', 'MK2LMS', 'LMS6', 'MEISEI', 'UDP']

# Known 'Drifty' Radiosonde types
# NOTE: Due to observed adjacent channel detections of RS41s, the adjacent channel decoder restriction
# is now applied to all radiosonde types. This may need to be re-evaluated in the future.
DRIFTY_SONDE_TYPES = VALID_SONDE_TYPES # ['RS92', 'DFM', 'LMS6']


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
        'batt'      : -1,
        'vel_h'     : 0.0,
        'vel_v'     : 0.0,
        'heading'   : 0.0
    }

    # TODO: Use the global valid sonde type list.
    VALID_SONDE_TYPES = ['RS92', 'RS41', 'DFM', 'M10', 'iMet', 'MK2LMS', 'LMS6', 'MEISEI', 'UDP']

    def __init__(self,
        sonde_type="None",
        sonde_freq=400000000.0,
        rs_path = "./",
        sdr_fm = "rtl_fm",
        device_idx = 0,
        ppm = 0,
        gain = -1,
        bias = False,
        save_decode_audio = False,
        save_decode_iq = False,

        exporter = None,
        timeout = 180,
        telem_filter = None,

        rs92_ephemeris = None,
        rs41_drift_tweak = False,
        experimental_decoder = False,
        imet_location = "SONDE"):
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

            save_decode_audio (bool): If True, save the FM-demodulated audio to disk to decode_<device_idx>.wav.
                                      Note: This may use up a lot of disk space!
            save_decode_iq (bool): If True, save the decimated IQ stream (48 or 96k complex s16 samples) to disk to decode_IQ_<device_idx>.bin
                                      Note: This will use up a lot of disk space!

            exporter (function, list): Either a function, or a list of functions, which accept a single dictionary. Fields described above.
            timeout (int): Timeout after X seconds of no valid data received from the decoder. Defaults to 180.
            telem_filter (function): An optional filter function, which determines if a telemetry frame is valid. 
                This can be used to allow the decoder to timeout based on telemetry contents (i.e. no lock, too far away, etc), 
                not just lack-of-telemetry. This function is passed the telemetry dict, and must return a boolean based on the telemetry validity.

            rs92_ephemeris (str): OPTIONAL - A fixed ephemeris file to use if decoding a RS92. If not supplied, one will be downloaded.

            rs41_drift_tweak (bool): If True, add a high-pass filter in the decode chain, which can improve decode performance on drifty SDRs.
            experimental_decoder (bool): If True, use the experimental fsk_demod-based decode chain.

            imet_location (str): OPTIONAL - A location field which is use in the generation of iMet unique ID.
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
        self.save_decode_audio = save_decode_audio
        self.save_decode_iq = save_decode_iq

        self.telem_filter = telem_filter
        self.timeout = timeout
        self.rs92_ephemeris = rs92_ephemeris
        self.rs41_drift_tweak = rs41_drift_tweak
        self.experimental_decoder = experimental_decoder
        self.imet_location = imet_location

        # iMet ID store. We latch in the first iMet ID we calculate, to avoid issues with iMet-1-RS units
        # which don't necessarily have a consistent packet count to time increment ratio.
        # This is a tradeoff between being able to handle multiple iMet sondes on a single frequency, and
        # not flooding the various databases with sonde IDs in the case of a bad sonde.
        self.imet_id = None

        # This will become our decoder thread.
        self.decoder = None

        self.exit_state = "OK"

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

        self.decoder_command = None # Decoder command for 'regular' decoders.
        self.decoder_command_2 = None # Second part of split demod/decode command for experimental decoders.
        self.demod_stats = None # FSKDemodStats object, used to parse demodulator statistics.

        # Generate the decoder command.
        if self.experimental_decoder:
            # Create a copy of the RX frequency, which will be updated when generating the decoder command.
            self.rx_frequency = self.sonde_freq
            # Generate the demodulator / decoder commands, and get the fsk_demod stats parser, tuned for the particular
            # sonde.
            (self.decoder_command, self.decoder_command_2, self.demod_stats) = self.generate_decoder_command_experimental()
        else:
            # 'Regular' decoder - just a single command.
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
        """ Generate the shell command which runs the relevant radiosonde decoder - Standard decoders.

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


        if self.sonde_type == "RS41":
            # RS41 Decoder command.
            # rtl_fm -p 0 -g -1 -M fm -F9 -s 15k -f 405500000 | sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - lowpass 2600 2>/dev/null | ./rs41ecc --crc --ecc --ptu
            # Note: Have removed a 'highpass 20' filter from the sox line, will need to re-evaluate if adding that is useful in the future.
            decode_cmd = "%s %s-p %d -d %s %s-M fm -F9 -s 15k -f %d 2>/dev/null | " % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, self.sonde_freq)
            
            # If selected by the user, we can add a highpass filter into the sox command. This helps handle up to about 5ppm of receiver drift
            # before performance becomes significantly degraded. By default this is off, as it is not required with TCXO RTLSDRs, and actually
            # slightly degrades performance.
            if self.rs41_drift_tweak:
                _highpass = "highpass 20 "
            else:
                _highpass = ""

            decode_cmd += "sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - %slowpass 2600 2>/dev/null | " % _highpass

            # Add in tee command to save audio to disk if debugging is enabled.
            if self.save_decode_audio:
                decode_cmd += " tee decode_%s.wav |" % str(self.device_idx)

            decode_cmd += "./rs41mod --ptu --json 2>/dev/null"

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
                        _rs92_gps_data = "-a almanac.txt --gpsepoch 2" # Note - This will need to be updated in... 19 years.
                else:
                    _rs92_gps_data = "-e ephemeris.dat"
            else:
                _rs92_gps_data = "-e %s" % self.rs92_ephemeris

            # Adjust the receive bandwidth based on the band the scanning is occuring in.
            if self.sonde_freq < 1000e6:
                # 400-406 MHz sondes - use a 12 kHz FM demod bandwidth.
                _rx_bw = 12000
            else:
                # 1680 MHz sondes - use a 28 kHz FM demod bandwidth.
                # NOTE: This is a first-pass of this bandwidth, and may need to be optimized.
                _rx_bw = 28000

            # Now construct the decoder command.
            # rtl_fm -p 0 -g 26.0 -M fm -F9 -s 12k -f 400500000 | sox -t raw -r 12k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - highpass 20 lowpass 2500 2>/dev/null | ./rs92ecc -vx -v --crc --ecc --vel -e ephemeris.dat
            decode_cmd = "%s %s-p %d -d %s %s-M fm -F9 -s %d -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, _rx_bw, self.sonde_freq)
            decode_cmd += "sox -t raw -r %d -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - lowpass 2500 highpass 20 2>/dev/null |" % _rx_bw

            # Add in tee command to save audio to disk if debugging is enabled.
            if self.save_decode_audio:
                decode_cmd += " tee decode_%s.wav |" % str(self.device_idx)

            decode_cmd += "./rs92mod -vx -v --crc --ecc --vel --json %s 2>/dev/null" % _rs92_gps_data

        elif self.sonde_type == "DFM":
            # DFM06/DFM09 Sondes.
            # As of 2019-02-10, dfm09ecc auto-detects if the signal is inverted,
            # so we don't need to specify an invert flag.
            # 2019-02-27: Added the --dist flag, which should reduce bad positions a bit.

            # Note: Have removed a 'highpass 20' filter from the sox line, will need to re-evaluate if adding that is useful in the future.
            decode_cmd = "%s %s-p %d -d %s %s-M fm -F9 -s 15k -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, self.sonde_freq)
            decode_cmd += "sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - highpass 20 lowpass 2000 2>/dev/null |"

            # Add in tee command to save audio to disk if debugging is enabled.
            if self.save_decode_audio:
                decode_cmd += " tee decode_%s.wav |" % str(self.device_idx)

            # DFM decoder
            decode_cmd += "./dfm09mod -vv --ecc --json --dist --auto 2>/dev/null"
            
        elif self.sonde_type == "M10":
            # M10 Sondes

            decode_cmd = "%s %s-p %d -d %s %s-M fm -F9 -s 22k -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, self.sonde_freq)
            decode_cmd += "sox -t raw -r 22k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - highpass 20 2>/dev/null |"

            # Add in tee command to save audio to disk if debugging is enabled.
            if self.save_decode_audio:
                decode_cmd += " tee decode_%s.wav |" % str(self.device_idx)

            # M10 decoder
            decode_cmd += "./m10mod --json --ptu -vvv 2>/dev/null"

        elif self.sonde_type == "iMet":
            # iMet-4 Sondes

            decode_cmd = "%s %s-p %d -d %s %s-M fm -F9 -s 15k -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, self.sonde_freq)
            decode_cmd += "sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - highpass 20 2>/dev/null |"

            # Add in tee command to save audio to disk if debugging is enabled.
            if self.save_decode_audio:
                decode_cmd += " tee decode_%s.wav |" % str(self.device_idx)

            # iMet-4 (IMET1RS) decoder
            decode_cmd += "./imet1rs_dft --json 2>/dev/null"

        elif self.sonde_type == "MK2LMS":
            # 1680 MHz LMS6 sondes, using 9600 baud MK2A-format telemetry.
            # TODO: see if we need to use a high-pass filter, and how much it degrades telemetry reception.

            decode_cmd = "%s %s-p %d -d %s %s-M fm -F9 -s 200k -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, self.sonde_freq)
            decode_cmd += "sox -t raw -r 200k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - highpass 20 2>/dev/null |"

            # Add in tee command to save audio to disk if debugging is enabled.
            if self.save_decode_audio:
                decode_cmd += " tee decode_%s.wav |" % str(self.device_idx)

            # iMet-4 (IMET1RS) decoder
            if self.inverted:
                self.log_debug("Using inverted MK2A decoder.")
                decode_cmd += "./mk2a_lms1680 -i --json 2>/dev/null"
            else:
                decode_cmd += "./mk2a_lms1680 --json 2>/dev/null"

        elif self.sonde_type == "LMS6":
            # LMS6 Decoder command.
            # rtl_fm -p 0 -g -1 -M fm -F9 -s 15k -f 405500000 | sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - lowpass 2600 2>/dev/null | ./rs41ecc --crc --ecc --ptu
            # Note: Have removed a 'highpass 20' filter from the sox line, will need to re-evaluate if adding that is useful in the future.
            decode_cmd = "%s %s-p %d -d %s %s-M fm -F9 -s 15k -f %d 2>/dev/null | " % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, self.sonde_freq)
            
            # If selected by the user, we can add a highpass filter into the sox command. This helps handle up to about 5ppm of receiver drift
            # before performance becomes significantly degraded. By default this is off, as it is not required with TCXO RTLSDRs, and actually
            # slightly degrades performance.
            if self.rs41_drift_tweak:
                _highpass = "highpass 20 "
            else:
                _highpass = ""

            decode_cmd += "sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - %slowpass 2600 2>/dev/null | " % _highpass

            # Add in tee command to save audio to disk if debugging is enabled.
            if self.save_decode_audio:
                decode_cmd += " tee decode_%s.wav |" % str(self.device_idx)

            decode_cmd += "./lms6Xmod --json 2>/dev/null"

        elif self.sonde_type == "MEISEI":
            # Meisei IMS-100 Sondes
            # Starting out with a 15 kHz bandwidth filter.

            decode_cmd = "%s %s-p %d -d %s %s-M fm -F9 -s 15k -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, self.sonde_freq)
            decode_cmd += "sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - highpass 20 2>/dev/null |"

            # Add in tee command to save audio to disk if debugging is enabled.
            if self.save_decode_audio:
                decode_cmd += " tee decode_%s.wav |" % str(self.device_idx)

            # Meisei IMS-100 decoder
            decode_cmd += "./meisei100mod --json 2>/dev/null"

        elif self.sonde_type == "UDP":
            # UDP Input Mode.
            # Used only for testing of new decoders, prior to them being integrated into auto_rx.
            decode_cmd = "python -m autorx.udplistener"

        else:
            return None

        return decode_cmd



    def generate_decoder_command_experimental(self):
        """ Generate the shell command which runs the relevant radiosonde decoder - Experimental Decoders

        Returns:
            Tuple(str, str, FSKDemodState) / None: The demod & decoder commands, and a FSKDemodStats object to process the demodulator statistics.

        """

        self.log_info("Using experimental decoder chain.")
        # Common options to rtl_fm

        # Add a -T option if bias is enabled
        bias_option = "-T " if self.bias else ""

        # Add a gain parameter if we have been provided one.
        if self.gain != -1:
            gain_param = '-g %.1f ' % self.gain
        else:
            gain_param = ''

        # Emit demodulator statistics every X modem frames.
        _stats_rate = 5

        if self.sonde_type == "RS41":
            # RS41 Decoder command.
            _sdr_rate = 48000 # IQ rate. Lower rate = lower CPU usage, but less frequency tracking ability.
            _baud_rate = 4800
            _offset = 0.25 # Place the sonde frequency in the centre of the passband.
            _lower = int(0.025 * _sdr_rate) # Limit the frequency estimation window to not include the passband edges.
            _upper = int(0.475 * _sdr_rate)
            _freq = int(self.sonde_freq - _sdr_rate*_offset)

            demod_cmd = "%s %s-p %d -d %s %s-M raw -F9 -s %d -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, _sdr_rate, _freq)
            # Add in tee command to save IQ to disk if debugging is enabled.
            if self.save_decode_iq:
                demod_cmd += " tee decode_IQ_%s.bin |" % str(self.device_idx)

            demod_cmd += "./fsk_demod --cs16 -b %d -u %d --stats=%d 2 %d %d - -" % (_lower, _upper, _stats_rate, _sdr_rate, _baud_rate)
            
            decode_cmd = "./rs41mod --ptu --json --bin 2>/dev/null"

            # RS41s transmit pulsed beacons - average over the last 2 frames, and use a peak-hold 
            demod_stats = FSKDemodStats(averaging_time=2.0, peak_hold=True)
            self.rx_frequency = _freq


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
                        _rs92_gps_data = "-a almanac.txt --gpsepoch 2" # Note - This will need to be updated in... 19 years.
                else:
                    _rs92_gps_data = "-e ephemeris.dat"
            else:
                _rs92_gps_data = "-e %s" % self.rs92_ephemeris


            if self.sonde_freq > 1000e6:
                # Use a higher IQ rate for 1680 MHz sondes, at the expense of some CPU usage.
                _sdr_rate = 96000
            else:
                # On 400 MHz, use 48 khz - RS92s dont drift far enough to need any more than this.
                _sdr_rate = 48000

            _output_rate = 48000
            _baud_rate = 4800
            _offset = 0.25 # Place the sonde frequency in the centre of the passband.
            _lower = int(0.025 * _sdr_rate) # Limit the frequency estimation window to not include the passband edges.
            _upper = int(0.475 * _sdr_rate)
            _freq = int(self.sonde_freq - _sdr_rate*_offset)

            demod_cmd = "%s %s-p %d -d %s %s-M raw -F9 -s %d -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, _sdr_rate, _freq)

            # Add in tee command to save IQ to disk if debugging is enabled.
            if self.save_decode_iq:
                demod_cmd += " tee decode_IQ_%s.bin |" % str(self.device_idx)

            demod_cmd += "./fsk_demod --cs16 -b %d -u %d --stats=%d 2 %d %d - -" % (_lower, _upper, _stats_rate, _sdr_rate, _baud_rate)
            
            decode_cmd = " python ./test/bit_to_samples.py %d %d | sox -t raw -r %d -e unsigned-integer -b 8 -c 1 - -r %d -b 8 -t wav - 2>/dev/null|" % (_output_rate, _baud_rate, _output_rate, _output_rate)

            # Add in tee command to save audio to disk if debugging is enabled.
            if self.save_decode_audio:
                decode_cmd += " tee decode_%s.wav |" % str(self.device_idx)

            decode_cmd += "./rs92mod -vx -v --crc --ecc --vel --json %s 2>/dev/null" % _rs92_gps_data

            # RS92s transmit continuously - average over the last 2 frames, and use a mean
            demod_stats = FSKDemodStats(averaging_time=2.0, peak_hold=False)
            self.rx_frequency = _freq

        elif self.sonde_type == "DFM":
            # DFM06/DFM09 Sondes.

            _sdr_rate = 50000
            _baud_rate = 2500
            _offset = 0.25 # Place the sonde frequency in the centre of the passband.
            _lower = int(0.025 * _sdr_rate) # Limit the frequency estimation window to not include the passband edges.
            _upper = int(0.475 * _sdr_rate)
            _freq = int(self.sonde_freq - _sdr_rate*_offset)

            demod_cmd = "%s %s-p %d -d %s %s-M raw -F9 -s %d -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, _sdr_rate, _freq)

            # Add in tee command to save IQ to disk if debugging is enabled.
            if self.save_decode_iq:
                demod_cmd += " tee decode_IQ_%s.bin |" % str(self.device_idx)

            demod_cmd += "./fsk_demod --cs16 -b %d -u %d --stats=%d 2 %d %d - -" % (_lower, _upper, _stats_rate, _sdr_rate, _baud_rate)

            decode_cmd = ""
            # Add in tee command to save audio to disk if debugging is enabled.
            if self.save_decode_audio:
                decode_cmd += " tee decode_%s.wav |" % str(self.device_idx)

            # DFM decoder
            decode_cmd += "./dfm09mod -vv --ecc --json --dist --auto --bin 2>/dev/null"

            # DFM sondes transmit continuously - average over the last 2 frames, and use a mean
            demod_stats = FSKDemodStats(averaging_time=1.0, peak_hold=False)
            self.rx_frequency = _freq

        elif self.sonde_type == "M10":
            # M10 Sondes
            # These have a 'weird' baud rate, and as fsk_demod requires the input sample rate to be an integer multiple of the baud rate,
            # our required sample rate is correspondingly weird!
            _sdr_rate = 48080
            _baud_rate = 9616
            _offset = 0.25 # Place the sonde frequency in the centre of the passband.
            _lower = int(0.025 * _sdr_rate) # Limit the frequency estimation window to not include the passband edges.
            _upper = int(0.475 * _sdr_rate)
            _freq = int(self.sonde_freq - _sdr_rate*_offset)

            demod_cmd = "%s %s-p %d -d %s %s-M raw -F9 -s %d -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, _sdr_rate, _freq)

            # Add in tee command to save IQ to disk if debugging is enabled.
            if self.save_decode_iq:
                demod_cmd += " tee decode_IQ_%s.bin |" % str(self.device_idx)

            demod_cmd += "./fsk_demod --cs16 -b %d -u %d --stats=%d 2 %d %d - -" % (_lower, _upper, _stats_rate, _sdr_rate, _baud_rate)
            
            decode_cmd = " python ./test/bit_to_samples.py %d %d | sox -t raw -r %d -e unsigned-integer -b 8 -c 1 - -r %d -b 8 -t wav - 2>/dev/null| " % (_sdr_rate, _baud_rate, _sdr_rate, _sdr_rate)

            # Add in tee command to save audio to disk if debugging is enabled.
            if self.save_decode_audio:
                decode_cmd += " tee decode_%s.wav |" % str(self.device_idx)

            # M10 decoder
            decode_cmd += "./m10mod --json --ptu -vvv 2>/dev/null"

            # M10 sondes transmit in short, irregular pulses - average over the last 2 frames, and use a peak hold
            demod_stats = FSKDemodStats(averaging_time=2.0, peak_hold=True)
            self.rx_frequency = _freq

        elif self.sonde_type == "LMS6":
            # LMS6 (400 MHz variant) Decoder command.
            _sdr_rate = 48000 # IQ rate. Lower rate = lower CPU usage, but less frequency tracking ability.
            _output_rate = 48000
            _baud_rate = 4800
            _offset = 0.25 # Place the sonde frequency in the centre of the passband.
            _lower = int(0.025 * _sdr_rate) # Limit the frequency estimation window to not include the passband edges.
            _upper = int(0.475 * _sdr_rate)
            _freq = int(self.sonde_freq - _sdr_rate*_offset)

            demod_cmd = "%s %s-p %d -d %s %s-M raw -F9 -s %d -f %d 2>/dev/null |" % (self.sdr_fm, bias_option, int(self.ppm), str(self.device_idx), gain_param, _sdr_rate, _freq)
            # Add in tee command to save IQ to disk if debugging is enabled.
            if self.save_decode_iq:
                demod_cmd += " tee decode_IQ_%s.bin |" % str(self.device_idx)

            demod_cmd += "./fsk_demod --cs16 -b %d -u %d --stats=%d 2 %d %d - -" % (_lower, _upper, _stats_rate, _sdr_rate, _baud_rate)
            
            decode_cmd = " python ./test/bit_to_samples.py %d %d | sox -t raw -r %d -e unsigned-integer -b 8 -c 1 - -r %d -b 8 -t wav - 2>/dev/null|" % (_output_rate, _baud_rate, _output_rate, _output_rate)
            
            # Add in tee command to save audio to disk if debugging is enabled.
            if self.save_decode_audio:
                decode_cmd += " tee decode_%s.wav |" % str(self.device_idx)

            decode_cmd += "./lms6Xmod --json 2>/dev/null"

            # LMS sondes transmit continuously - average over the last 2 frames, and use a mean
            demod_stats = FSKDemodStats(averaging_time=2.0, peak_hold=False)
            self.rx_frequency = _freq

        else:
            return None

        return (demod_cmd, decode_cmd, demod_stats)


    def stats_thread(self, asyncreader):
        """ Process demodulator statistics from a supplied AsynchronousFileReader object (which will be hooked into stderr from fsk_demod) """
        while (not asyncreader.eof()) and self.decoder_running:
            for _line in asyncreader.readlines():
                self.demod_stats.update(_line)
            # Avoid spinlocking..
            # Probably about time we looked at using async for this stuff...
            time.sleep(0.2)
        
        asyncreader.stop()


    def decoder_thread(self):
        """ Runs the supplied decoder command(s) as a subprocess, and passes returned lines to handle_decoder_line. """
        
        # Timeout Counter. 
        _last_packet = time.time()

        if self.decoder_command_2 is None:
            # No second decoder command, so we only need to process stdout from the one process.
            self.log_debug("Decoder Command: %s" % self.decoder_command )

            # Start the thread.
            self.decode_process = subprocess.Popen(self.decoder_command, shell=True, stdin=None, stdout=subprocess.PIPE, preexec_fn=os.setsid) 

        else:
            # Two decoder commands! This means one is a demod command, from which we need to handle stderr,
            # and one is a decoder, which we pipe in stdout from the demodulator.
            self.log_debug("Demodulator Command: %s" % self.decoder_command)
            self.log_debug("Decoder Command: %s" % self.decoder_command_2)

            # Startup the subprocesses
            self.demod_process = subprocess.Popen(self.decoder_command, shell=True, stdin=None, stdout=subprocess.PIPE, stderr=subprocess.PIPE, preexec_fn=os.setsid) 
            self.decode_process = subprocess.Popen(self.decoder_command_2, shell=True, stdin=self.demod_process.stdout, stdout=subprocess.PIPE, preexec_fn=os.setsid)
            
            self.demod_reader = AsynchronousFileReader(self.demod_process.stderr, autostart=True)

            # Start thread to process demodulator stats.
            self.demod_stats_thread = Thread(target=self.stats_thread, args=(self.demod_reader,))
            self.demod_stats_thread.start()

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
                self.exit_state = "Timeout"
                break
            else:
                # Otherwise, sleep for a short time.
                time.sleep(0.1)

        # Either our subprocess has exited, or the user has asked to close the process. 
        # Try many things to kill off the subprocess.
        try:
            # Stop the async reader
            self.async_reader.stop()
            # Send a SIGKILL to the subprocess PID via OS.
            try:
                os.killpg(os.getpgid(self.decode_process.pid), signal.SIGKILL)
                if self.experimental_decoder:
                    os.killpg(os.getpgid(self.demod_process.pid), signal.SIGKILL)
            except Exception as e:
                self.log_debug("SIGKILL via os.killpg failed. - %s" % str(e))
            time.sleep(1)
            try:
                # Send a SIGKILL via subprocess
                self.decode_process.kill()
                if self.experimental_decoder:
                    self.demod_process.kill()
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
        """ Handle a line of output from the decoder subprocess, and pass it onto all of the telemetry
            exporters.

        Args:
            data (str, bytearray): One line of text output from the decoder subprocess.

        Returns:
            bool:   True if the line was decoded to a JSON object correctly, False otherwise.
        """
        
        # Catch 'bad' first characters.
        try:
            _first_char = data.decode('ascii')[0]
        except UnicodeDecodeError:
            return

        # Don't even try and decode lines which don't start with a '{'
        # These may be other output from the decoder, which we shouldn't try to parse.
        # TODO: Perhaps we should add the option to log the raw data output from the decoders? 
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


            # Check for an encrypted flag, and check if it is set.
            # Currently encrypted == true indicates an encrypted RS41-SGM. There's no point
            # trying to decode this, so we close the decoder at this point.
            if 'encrypted' in _telemetry:
                if _telemetry['encrypted']:
                    self.log_error("Radiosonde %s has encrypted telemetry (Possible encrypted RS41-SGM)! We cannot decode this, closing decoder." % _telemetry['id'])
                    self.exit_state = "Encrypted"
                    self.decoder_running = False
                    return False

            # Check the datetime field is parseable.
            try:
                _telemetry['datetime_dt'] = parse(_telemetry['datetime'])
            except Exception as e:
                self.log_error("Invalid date/time in telemetry dict - %s (Sonde may not have GPS lock)" % str(e))
                return False

            # Add in the sonde type field.
            # If we are provided with a subtype field from the decoder, use this,
            # otherwise use the detected sonde type.
            if 'subtype' in _telemetry:
                _telemetry['type'] = _telemetry['subtype']
            else:
                _telemetry['type'] = self.sonde_type

            # TODO: Use frequency data provided by the decoder, if available.
            _telemetry['freq_float'] = self.sonde_freq/1e6
            _telemetry['freq'] = "%.3f MHz" % (self.sonde_freq/1e6)

            # Add in information about the SDR used.
            _telemetry['sdr_device_idx'] = self.device_idx

            # Check for an 'aux' field, this indicates that the sonde has an auxilliary payload,
            # which is most likely an Ozone sensor. We append -Ozone to the sonde type field to indicate this.
            if 'aux' in _telemetry:
                _telemetry['type'] += "-Ozone"


            # iMet Specific actions
            if self.sonde_type == 'iMet':
                # Check we have GPS lock.
                if _telemetry['sats'] < 4:
                    # No GPS lock means an invalid time, which means we can't accurately calculate a unique ID.
                    # We need to quit at this point before the telemetry processing gos any further.
                    self.log_error("iMet sonde has no GPS lock - discarding frame.")
                    return False

                # Fix up the time.
                _telemetry['datetime_dt'] = fix_datetime(_telemetry['datetime'])
                # Generate a unique ID based on the power-on time and frequency, as iMet sondes don't send one.
                # Latch this ID and re-use it for the entire decode run.
                if self.imet_id == None:
                    self.imet_id = imet_unique_id(_telemetry, custom=self.imet_location)
                
                _telemetry['id'] = self.imet_id
                _telemetry['station_code'] = self.imet_location


            # LMS6 Specific Actions
            if self.sonde_type == 'MK2LMS' or self.sonde_type == 'LMS6':
                # We are only provided with HH:MM:SS, so the timestamp needs to be fixed, just like with the iMet sondes
                _telemetry['datetime_dt'] = fix_datetime(_telemetry['datetime'])

            # Grab a snapshot of modem statistics, if we are using an experimental decoder.
            if self.demod_stats is not None:
                if self.demod_stats.snr != -999.0:
                    _telemetry['snr'] = self.demod_stats.snr
                    _telemetry['fest'] = self.demod_stats.fest
                    _telemetry['ppm'] = self.demod_stats.ppm

                    # Calculate an estimate of the radiosonde's centre frequency, based on the SDR frequency
                    # and the modem's tone estimates.
                    _telemetry['f_centre'] = self.rx_frequency + (_telemetry['fest'][0] + _telemetry['fest'][1])/2.0
                    # Calculate estimated frequency error from where we expected the sonde to be.
                    _telemetry['f_error'] = _telemetry['f_centre'] - self.sonde_freq

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



