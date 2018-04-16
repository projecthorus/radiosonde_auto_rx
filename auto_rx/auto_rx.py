#!/usr/bin/env python
#
# Radiosonde Auto RX Tools
#
# 2017-04 Mark Jessop <vk5qi@rfhead.net>
#
# Refer github page for instructions on setup and usage.
# https://github.com/projecthorus/radiosonde_auto_rx/
#

import numpy as np
import sys
import argparse
import logging
import datetime
import time
import os
import glob
import shutil
import platform
import signal
import Queue
import subprocess
import traceback
import json
import re
from aprs_utils import *
from habitat_utils import *
from ozi_utils import *
from rotator_utils import *
from threading import Thread
from StringIO import StringIO
from findpeaks import *
from config_reader import *
from gps_grabber import *
from async_file_reader import AsynchronousFileReader

# TODO: Break this out to somewhere else, that is set automatically based on releases...
AUTO_RX_VERSION = '20180417'

# Logging level
# INFO = Basic status messages
# DEBUG = Adds information on each command run by subprocess.
logging_level = logging.INFO

# Set this to true to enable dumping of all the rtl_power output to files in ./log/
# Note that this can result in a LOT of log files being generated depending on your scanning settings.
uber_debug = False

# Internet Push Globals
APRS_OUTPUT_ENABLED = False
HABITAT_OUTPUT_ENABLED = False

INTERNET_PUSH_RUNNING = True
internet_push_queue = Queue.Queue()

# Second Queue for OziPlotter outputs, since we want this to run at a faster rate.
OZI_PUSH_RUNNING = True
ozi_push_queue = Queue.Queue()


# Flight Statistics data
# stores copies of the telemetry dictionary returned by process_rs_line.
flight_stats = {
    'first': None,
    'apogee': None,
    'last': None
}

# Station config, we need to populate this with data from station.cfg
config = {}

def run_rtl_power(start, stop, step, filename="log_power.csv", dwell = 20, sdr_power='rtl_power', ppm = 0, gain = -1, bias = False):
    """ Run rtl_power, with a timeout"""
    # Example: rtl_power -T -f 400400000:403500000:800 -i20 -1 -c 20% -p 0 -g 26.0 log_power.csv

    # Add a -T option if bias is enabled
    bias_option = "-T " if bias else ""

    # Add a gain parameter if we have been provided one.
    if gain != -1:
        gain_param = '-g %.1f ' % gain
    else:
        gain_param = ''

    # Add -k 30 option, to SIGKILL rtl_power 30 seconds after the regular timeout expires.
    # Note that this only works with the GNU Coreutils version of Timeout, not the IBM version,
    # which is provided with OSX (Darwin).
    if 'Darwin' in platform.platform():
        timeout_kill = ''
    else:
        timeout_kill = '-k 30 '

    rtl_power_cmd = "timeout %s%d %s %s-f %d:%d:%d -i %d -1 -c 20%% -p %d %s%s" % (timeout_kill, dwell+10, sdr_power, bias_option, start, stop, step, dwell, int(ppm), gain_param, filename)
    logging.info("Running frequency scan.")
    logging.debug("Running command: %s" % rtl_power_cmd)
    ret_code = os.system(rtl_power_cmd)
    if ret_code == 1:
        logging.critical("rtl_power call failed!")
        return False
    else:
        return True

def read_rtl_power(filename):
    """ Read in frequency samples from a single-shot log file produced by rtl_power """

    # Output buffers.
    freq = np.array([])
    power = np.array([])

    freq_step = 0


    # Open file.
    f = open(filename,'r')

    # rtl_power log files are csv's, with the first 6 fields in each line describing the time and frequency scan parameters
    # for the remaining fields, which contain the power samples. 

    for line in f:
        # Split line into fields.
        fields = line.split(',')

        if len(fields) < 6:
            logging.error("Invalid number of samples in input file - corrupt?")
            raise Exception("Invalid number of samples in input file - corrupt?")

        start_date = fields[0]
        start_time = fields[1]
        start_freq = float(fields[2])
        stop_freq = float(fields[3])
        freq_step = float(fields[4])
        n_samples = int(fields[5])

        #freq_range = np.arange(start_freq,stop_freq,freq_step)
        samples = np.loadtxt(StringIO(",".join(fields[6:])),delimiter=',')
        freq_range = np.linspace(start_freq,stop_freq,len(samples))

        # Add frequency range and samples to output buffers.
        freq = np.append(freq, freq_range)
        power = np.append(power, samples)

    f.close()

    # Sanitize power values, to remove the nan's that rtl_power puts in there occasionally.
    power = np.nan_to_num(power)

    return (freq, power, freq_step)


def quantize_freq(freq_list, quantize=5000):
    """ Quantise a list of frequencies to steps of <quantize> Hz """
    return np.round(freq_list/quantize)*quantize

def detect_sonde(frequency, sdr_fm='rtl_fm', ppm=0, gain=-1, bias=False, dwell_time=10):
    """ Receive some FM and attempt to detect the presence of a radiosonde. """

    # Example command (for command-line testing):
    # rtl_fm -T -p 0 -M fm -g 26.0 -s 15k -f 401500000 | sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -t wav - highpass 20 | ./rs_detect -z -t 8

    # Add a -T option if bias is enabled
    bias_option = "-T " if bias else ""

    # Add a gain parameter if we have been provided one.
    if gain != -1:
        gain_param = '-g %.1f ' % gain
    else:
        gain_param = ''

    rx_test_command = "timeout %ds %s %s-p %d %s-M fm -F9 -s 15k -f %d 2>/dev/null |" % (dwell_time, sdr_fm, bias_option, int(ppm), gain_param, frequency) 
    rx_test_command += "sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -t wav - highpass 20 2>/dev/null |"
    rx_test_command += "./rs_detect -z -t 8 2>/dev/null"

    logging.info("Attempting sonde detection on %.3f MHz" % (frequency/1e6))
    logging.debug("Running command: %s" % rx_test_command)

    ret_code = os.system(rx_test_command)

    ret_code = ret_code >> 8

    if ret_code == 3:
        logging.info("Detected a RS41!")
        return "RS41"
    elif ret_code == 4:
        logging.info("Detected a RS92!")
        return "RS92"
    elif ret_code == 2:
        logging.info("Detected a DFM Sonde! (Unsupported)")
        return "DFM"
    elif ret_code == 5:
        logging.info("Detected a M10 Sonde! (Unsupported)")
        return "M10"
    elif ret_code == 6:
        logging.info("Detected a iMet Sonde! (Unsupported)")
        return "iMet"
    else:
        return None

def reset_rtlsdr():
    """ Attempt to perform a USB Reset on all attached RTLSDRs. This uses the usb_reset binary from ../scan"""
    lsusb_output = subprocess.check_output(['lsusb'])
    try:
        devices = lsusb_output.split('\n')
        for device in devices:
            if 'RTL2838' in device:
                # Found an rtlsdr! Attempt to extract bus and device number.
                # Expecting something like: 'Bus 001 Device 005: ID 0bda:2838 Realtek Semiconductor Corp. RTL2838 DVB-T'
                device_fields = device.split(' ')
                # Attempt to cast fields to integers, to give some surety that we have the correct data.
                device_bus = int(device_fields[1])
                device_number = int(device_fields[3][:-1])
                # Construct device address
                reset_argument = '/dev/bus/usb/%03d/%03d' % (device_bus, device_number)
                # Attempt to reset the device.
                logging.info("Resetting device: %s" % reset_argument)
                ret_code = subprocess.call(['./reset_usb', reset_argument])
                logging.debug("Got return code: %s" % ret_code)
            else:
                continue
    except:
        logging.error("Errors occured while attempting to reset USB device.")


def sonde_search(config, attempts = 5):
    """ Perform a frequency scan across the defined range, and test each frequency for a radiosonde's presence. """
    search_attempts = attempts

    sonde_freq = None
    sonde_type = None

    while search_attempts > 0:

        if len(config['whitelist']) == 0 :
            # No whitelist frequencies provided - perform a scan.
            run_rtl_power(config['min_freq']*1e6, config['max_freq']*1e6, config['search_step'], sdr_power=config['sdr_power_path'], ppm=config['sdr_ppm'], gain=config['sdr_gain'], bias=config['sdr_bias'])

            # Read in result
            try:
                (freq, power, step) = read_rtl_power('log_power.csv')
                # Sanity check results.
                if step == 0 or len(freq)==0 or len(power)==0:
                    raise Exception("Invalid file.")

                if uber_debug:
                    # Copy log_power.csv to log directory, for later debugging.
                    shutil.copy('log_power.csv', './log/log_power_%s.csv'%datetime.datetime.utcnow().strftime('%Y-%m-%d_%H%M%S'))


            except Exception as e:
                traceback.print_exc()
                logging.error("Failed to read log_power.csv. Resetting RTLSDRs and attempting to run rtl_power again.")
                # no log_power.csv usually means that rtl_power has locked up and had to be SIGKILL'd. 
                # This occurs when it can't get samples from the RTLSDR, because it's locked up for some reason.
                # Issuing a USB Reset to the rtlsdr can sometimes solve this. 
                reset_rtlsdr()
                search_attempts -= 1
                time.sleep(10)
                continue


            # Rough approximation of the noise floor of the received power spectrum.
            power_nf = np.mean(power)

            # Detect peaks.
            peak_indices = detect_peaks(power, mph=(power_nf+config['min_snr']), mpd=(config['min_distance']/step), show = False)

            # If we have found no peaks, and no greylist has been provided, re-scan.
            if (len(peak_indices) == 0) and (len(config['greylist'])==0):
                logging.info("No peaks found on this pass.")
                search_attempts -= 1
                time.sleep(10)
                continue

            # Sort peaks by power.
            peak_powers = power[peak_indices]
            peak_freqs = freq[peak_indices]
            peak_frequencies = peak_freqs[np.argsort(peak_powers)][::-1]

            # Quantize to nearest x kHz
            peak_frequencies = quantize_freq(peak_frequencies, config['quantization'])

            # Append on any frequencies in the supplied greylist
            peak_frequencies = np.append(np.array(config['greylist'])*1e6, peak_frequencies)

            # Remove any duplicate entries after quantization, but preserve order.
            _, peak_idx = np.unique(peak_frequencies, return_index=True)
            peak_frequencies = peak_frequencies[np.sort(peak_idx)]

            # Remove any frequencies in the blacklist.
            for _frequency in np.array(config['blacklist'])*1e6:
                _index = np.argwhere(peak_frequencies==_frequency)
                peak_frequencies = np.delete(peak_frequencies, _index)

            if len(peak_frequencies) == 0:
                logging.info("No peaks found after blacklist frequencies removed.")
            else:
                logging.info("Performing scan on %d frequencies (MHz): %s" % (len(peak_frequencies),str(peak_frequencies/1e6)))

        else:
            # We have been provided a whitelist - scan through the supplied frequencies.
            peak_frequencies = np.array(config['whitelist'])*1e6
            logging.info("Scanning on whitelist frequencies (MHz): %s" % str(peak_frequencies/1e6))

        # Run rs_detect on each peak frequency, to determine if there is a sonde there.
        for freq in peak_frequencies:
            detected = detect_sonde(freq,
                sdr_fm=config['sdr_fm_path'], 
                ppm=config['sdr_ppm'], 
                gain=config['sdr_gain'], 
                bias=config['sdr_bias'], 
                dwell_time=config['dwell_time'])
            if detected != None:
                sonde_freq = freq
                sonde_type = detected
                break

        if sonde_type != None:
            # Found a sonde! Break out of the while loop and attempt to decode it.
            return (sonde_freq, sonde_type)
        else:
            # No sondes found :-( Wait and try again.
            search_attempts -= 1
            logging.warning("Search attempt failed, %d attempts remaining. Waiting %d seconds." % (search_attempts, config['search_delay']))
            time.sleep(config['search_delay'])

    # If we get here, we have exhausted our search attempts.
    logging.error("No sondes detected.")
    return (None, None)


def check_position_valid(data):
    """
    Check to see if a payload position frame breaches one of our filters.
    In this function we also check that the payload callsign is not invalid.
    """
    # Access the global copy of the station config. Bit of a hack, but the alternative is
    # passing the config through multiple layers of functions.
    global config

    # First check: Altitude cap.
    if data['alt'] > config['max_altitude']:
        _altitude_breach = data['alt'] - config['max_altitude']
        logging.warning("Position breached altitude cap by %d m." % _altitude_breach)
        return False

    # Second check - is the payload more than x km from our listening station.
    # Only run this check if a station location has been provided.
    if (config['station_lat'] != 0.0) and (config['station_lon'] != 0.0):
        # Calculate the distance from the station to the payload.
        _listener = (config['station_lat'], config['station_lon'], config['station_alt'])
        _payload = (data['lat'], data['lon'], data['alt'])
        # Calculate using positon_info function from rotator_utils.py
        _info = position_info(_listener, _payload)

        if _info['straight_distance'] > config['max_radius_km']*1000:
            _radius_breach = _info['straight_distance']/1000.0 - config['max_radius_km']
            logging.warning("Position breached radius cap by %.1f km." % (_radius_breach))
            return False

    # Payload Serial Number Checks
    _serial = data['id']
    # Run a Regex to match known Vaisala RS92/RS41 serial numbers (YWWDxxxx)
    # RS92: https://www.vaisala.com/sites/default/files/documents/Vaisala%20Radiosonde%20RS92%20Serial%20Number.pdf
    # RS41: https://www.vaisala.com/sites/default/files/documents/Vaisala%20Radiosonde%20RS41%20Serial%20Number.pdf
    # This will need to be re-evaluated if we're still using this code in 2021!
    callsign_valid = re.match(r'[J-T][0-5][\d][1-7]\d{4}', _serial)

    if callsign_valid:
        return True
    else:
        logging.warning("Payload ID does not match regex. Discarding.")
        return False


# Dictionary of observed payload IDs.
seen_payload_ids = {}

def payload_id_valid_for_upload(payload_id, update=False):
    ''' Update our list of seen payload IDs '''
    global config, seen_payload_ids

    if payload_id in seen_payload_ids:
        if seen_payload_ids[payload_id] >= config['payload_id_valid']:
            # We have seen this payload ID often enough to consider it to be valid.
            return True
        else:
            if update:
                seen_payload_ids[payload_id] += 1
    else:
        if update:
            seen_payload_ids[payload_id] = 1

    # Otherwise, we still haven't seen this payload enough to be sure it's ID is valid.
    return False



def process_rs_line(line):
    """ Process a line of output from the rs92gps decoder, converting it to a dict """
    try:

        if line[0] != "{":
            return None

        rs_frame = json.loads(line)
        # Note: We expect the following fields available within the JSON blob:
        # id, frame, datetime, lat, lon, alt, crc
        rs_frame['crc'] = True # the rs92ecc only reports frames that match crc so we can lie here

        if 'temp' not in rs_frame.keys():
            rs_frame['temp'] = -273.0 # We currently don't get temperature data out of the RS92s.

        rs_frame['humidity'] = -1.0 # Currently no Humidity data available.
        rs_frame['datetime_str'] = rs_frame['datetime'].replace("Z","") #python datetime sucks
        rs_frame['short_time'] = rs_frame['datetime'].split(".")[0].split("T")[1]

        _telem_string = "%s,%d,%s,%.5f,%.5f,%.1f,%.1f,%s" % (rs_frame['id'], rs_frame['frame'],rs_frame['datetime'], rs_frame['lat'], rs_frame['lon'], rs_frame['alt'], rs_frame['temp'], rs_frame['crc'])

        if check_position_valid(rs_frame):
            logging.info("TELEMETRY: %s" % _telem_string)
            # Update the seen-payload-id list
            # This will then be queried within the internet upload threads.
            payload_id_valid_for_upload(rs_frame['id'],update=True)

            return rs_frame
        else:
            logging.warning("Invalid Position, discarding: %s" % _telem_string)
            return None

    except:
        logging.error("Could not parse string: %s" % line)
        traceback.print_exc()
        return None

def update_flight_stats(data):
    """ Maintain a record of flight statistics. """
    global flight_stats

    # Save the current frame into the 'last' frame storage
    flight_stats['last'] = data

    # Is this our first telemetry frame?
    # If so, populate all fields in the flight stats dict with the current telemetry frame.
    if flight_stats['first'] == None:
        flight_stats['first'] = data
        flight_stats['apogee'] = data

    # Is the current altitude higher than the current peak altitude?
    if data['alt'] > flight_stats['apogee']['alt']:
        flight_stats['apogee'] = data



def calculate_flight_statistics():
    """ Produce a flight summary, for inclusion in the log file. """
    global flight_stats

    # Grab peak altitude.
    peak_altitude = flight_stats['apogee']['alt']

    # Grab last known descent rate
    descent_rate = flight_stats['last']['vel_v']

    # Calculate average ascent rate, based on data we have.
    # Wrap this in a try, in case we have time string parsing issues.
    try:
        if flight_stats['first'] == flight_stats['apogee']:
            # We have only caught a flight during descent. Don't calculate ascent rate.
            ascent_rate = -1.0
        else:
            ascent_height = flight_stats['apogee']['alt'] - flight_stats['first']['alt']
            start_time = datetime.datetime.strptime(flight_stats['first']['datetime_str'],"%Y-%m-%dT%H:%M:%S.%f")
            apogee_time = datetime.datetime.strptime(flight_stats['apogee']['datetime_str'],"%Y-%m-%dT%H:%M:%S.%f")
            ascent_time = (apogee_time - start_time).seconds
            ascent_rate = ascent_height/float(ascent_time)
    except:
        ascent_rate = -1.0

    stats_str = "Acquired %s at %s on %s, at %d m altitude.\n" % (flight_stats['first']['type'], flight_stats['first']['datetime_str'], flight_stats['first']['freq'], int(flight_stats['first']['alt']))
    stats_str += "Ascent Rate: %.1f m/s, Peak Altitude: %d, Descent Rate: %.1f m/s\n" % (ascent_rate, int(peak_altitude), descent_rate)
    stats_str += "Last Position: %.5f, %.5f, %d m alt, at %s\n" % (flight_stats['last']['lat'], flight_stats['last']['lon'], int(flight_stats['last']['alt']), flight_stats['last']['datetime_str'])
    stats_str += "Flight Path: https://aprs.fi/#!call=%s&timerange=10800&tail=10800\n" % flight_stats['last']['id']

    return stats_str

def decode_rs92(frequency, sdr_fm='rtl_fm', ppm=0, gain=-1, bias=False, rx_queue=None, almanac=None, ephemeris=None, timeout=120, save_log=False):
    """ Decode a RS92 sonde """
    global latest_sonde_data, internet_push_queue, ozi_push_queue

    # Before we get started, do we need to download GPS data?
    if ephemeris == None:
        # If no ephemeris data defined, attempt to download it.
        # get_ephemeris will either return the saved file name, or None.
        ephemeris = get_ephemeris(destination="ephemeris.dat")

    # If ephemeris is still None, then we failed to download the ephemeris data.
    # Try and grab the almanac data instead
    if ephemeris == None:
        logging.error("Could not obtain ephemeris data, trying to download an almanac.")
        almanac = get_almanac(destination="almanac.txt")
        if almanac == None:
            # We probably don't have an internet connection. Bomb out, since we can't do much with the sonde telemetry without an almanac!
            logging.critical("Could not obtain GPS ephemeris or almanac data.")
            return False

    # Add a -T option if bias is enabled
    bias_option = "-T " if bias else ""

    # Add a gain parameter if we have been provided one.
    if gain != -1:
        gain_param = '-g %.1f ' % gain
    else:
        gain_param = ''

    # Example command:
    # rtl_fm -p 0 -g 26.0 -M fm -F9 -s 12k -f 400500000 | sox -t raw -r 12k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - highpass 20 lowpass 2500 2>/dev/null | ./rs92ecc
    decode_cmd = "%s %s-p %d %s-M fm -F9 -s 12k -f %d 2>/dev/null |" % (sdr_fm,bias_option, int(ppm), gain_param, frequency)
    decode_cmd += "sox -t raw -r 12k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - lowpass 2500 highpass 20 2>/dev/null |"

    # Note: I've got the check-CRC option hardcoded in here as always on. 
    # I figure this is prudent if we're going to proceed to push this telemetry data onto a map.

    if ephemeris != None:
        decode_cmd += "./rs92ecc -vx -v --crc --ecc --vel -e %s" % ephemeris
    elif almanac != None:
        decode_cmd += "./rs92ecc -vx -v --crc --ecc --vel -a %s" % almanac

    logging.debug("Running command: %s" % decode_cmd)

    rx_last_line = time.time()

    # Receiver subprocess. Discard stderr, and feed stdout into an asynchronous read class.
    rx = subprocess.Popen(decode_cmd, shell=True, stdin=None, stdout=subprocess.PIPE, preexec_fn=os.setsid) 
    rx_stdout = AsynchronousFileReader(rx.stdout, autostart=True)

    _log_file = None

    while not rx_stdout.eof():
        for line in rx_stdout.readlines():
            if (line != None) and (line != ""):
                try:
                    data = process_rs_line(line)

                    if data != None:
                        # Reset timeout counter
                        rx_last_line = time.time()
                        # Add in a few fields that don't come from the sonde telemetry.
                        data['freq'] = "%.3f MHz" % (frequency/1e6)
                        data['type'] = "RS92"

                        # If we are seeing any aux data (i.e. there is something strapped to this RS92), append '-Ozone' to the type.
                        if 'aux' in data.keys():
                            _ozone = "-Ozone"
                        else:
                            _ozone = ""

                        # post to MQTT
                        if mqtt_client:
                            data['seen_by'] = config['uploader_callsign']
                            mqtt_client.publish("sonde/%s" % data['id'], payload=json.dumps(data), retain=True)

                        # Per-Sonde Logging
                        if save_log:
                            if _log_file is None:
                                _existing_files = glob.glob("./log/*%s_%s*_sonde.log" % (data['id'], data['type']))
                                if len(_existing_files) != 0:
                                    _log_file_name = _existing_files[0]
                                    logging.debug("Using existing log file: %s" % _log_file_name)
                                else:
                                    _log_file_name = "./log/%s_%s_%s_%d_sonde.log" % (
                                        datetime.datetime.utcnow().strftime("%Y%m%d-%H%M%S"),
                                        data['id'],
                                        (data['type'] + _ozone),
                                        int(frequency/1e3))
                                    logging.debug("Opening new log file: %s" % _log_file_name)

                                _log_file = open(_log_file_name,'ab')

                            # Write a log line
                            # datetime,id,frame_no,lat,lon,alt,type,frequency
                            _log_line = "%s,%s,%d,%.5f,%.5f,%.1f,%.1f,%s,%.3f\n" % (
                                data['datetime_str'],
                                data['id'],
                                data['frame'],
                                data['lat'],
                                data['lon'],
                                data['alt'],
                                data['temp'],
                                (data['type'] + _ozone),
                                frequency/1e6)

                            _log_file.write(_log_line)
                            _log_file.flush()


                        update_flight_stats(data)

                        if rx_queue != None:
                            try:
                                internet_push_queue.put_nowait(data)
                                ozi_push_queue.put_nowait(data)
                            except:
                                pass
                except:
                    traceback.print_exc()
                    logging.error("Error parsing line: %s" % line)

        # Check timeout counter.
        if time.time() > (rx_last_line+timeout):
            logging.error("RX Timed out.")
            break
        # Sleep for a short time.
        time.sleep(0.1)

    # If we were writing a log, close the file.
    if _log_file != None:
        _log_file.flush()
        _log_file.close()

    logging.error("Closing RX Thread.")
    os.killpg(os.getpgid(rx.pid), signal.SIGTERM)
    rx_stdout.stop()
    rx_stdout.join()
    return


def decode_rs41(frequency, sdr_fm='rtl_fm', ppm=0, gain=-1, bias=False, rx_queue=None, timeout=120, save_log=False):
    """ Decode a RS41 sonde """
    global latest_sonde_data, internet_push_queue, ozi_push_queue
    # Add a -T option if bias is enabled
    bias_option = "-T " if bias else ""

    # Add a gain parameter if we have been provided one.
    if gain != -1:
        gain_param = '-g %.1f ' % gain
    else:
        gain_param = ''

    # rtl_fm -p 0 -g -1 -M fm -F9 -s 15k -f 405500000 | sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - lowpass 2600 2>/dev/null | ./rs41ecc
    # Note: Have removed a 'highpass 20' filter from the sox line, will need to re-evaluate if adding that is useful in the future.
    decode_cmd = "%s %s-p %d %s-M fm -F9 -s 15k -f %d 2>/dev/null |" % (sdr_fm, bias_option, int(ppm), gain_param, frequency)
    decode_cmd += "sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - lowpass 2600 2>/dev/null |"

    # Note: I've got the check-CRC option hardcoded in here as always on. 
    # I figure this is prudent if we're going to proceed to push this telemetry data onto a map.

    decode_cmd += "./rs41ecc --crc --ecc --ptu" # if this doesn't work try -i at the end

    logging.debug("Running command: %s" % decode_cmd)

    rx_last_line = time.time()

    # Receiver subprocess. Discard stderr, and feed stdout into an asynchronous read class.
    rx = subprocess.Popen(decode_cmd, shell=True, stdin=None, stdout=subprocess.PIPE, preexec_fn=os.setsid) 
    rx_stdout = AsynchronousFileReader(rx.stdout, autostart=True)

    _log_file = None

    while not rx_stdout.eof():
        for line in rx_stdout.readlines():
            if (line != None) and (line != ""):
                try:
                    data = process_rs_line(line)

                    if data != None:
                        # Reset timeout counter.
                        rx_last_line = time.time()
                        # Add in a few fields that don't come from the sonde telemetry.
                        data['freq'] = "%.3f MHz" % (frequency/1e6)
                        data['type'] = "RS41"

                        # post to MQTT
                        if mqtt_client:
                            data['seen_by'] = config['uploader_callsign']
                            mqtt_client.publish("sonde/%s" % data['id'], payload=json.dumps(data), retain=True)

                        # Per-Sonde Logging
                        if save_log:
                            if _log_file is None:
                                _existing_files = glob.glob("./log/*%s_%s*_sonde.log" % (data['id'], data['type']))
                                if len(_existing_files) != 0:
                                    _log_file_name = _existing_files[0]
                                    logging.debug("Using existing log file: %s" % _log_file_name)
                                else:
                                    _log_file_name = "./log/%s_%s_%s_%d_sonde.log" % (
                                        datetime.datetime.utcnow().strftime("%Y%m%d-%H%M%S"),
                                        data['id'],
                                        data['type'],
                                        int(frequency/1e3))
                                    logging.debug("Opening new log file: %s" % _log_file_name)

                                _log_file = open(_log_file_name,'ab')

                            # Write a log line
                            # datetime,id,frame_no,lat,lon,alt,type,frequency
                            _log_line = "%s,%s,%d,%.5f,%.5f,%.1f,%.1f,%s,%.3f\n" % (
                                data['datetime_str'],
                                data['id'],
                                data['frame'],
                                data['lat'],
                                data['lon'],
                                data['alt'],
                                data['temp'],
                                data['type'],
                                frequency/1e6)

                            _log_file.write(_log_line)
                            _log_file.flush()

                        update_flight_stats(data)

                        latest_sonde_data = data

                        if rx_queue != None:
                            try:
                                internet_push_queue.put_nowait(data)
                                ozi_push_queue.put_nowait(data)
                            except:
                                pass
                except:
                    _err_str = traceback.format_exc()
                    logging.error("Error parsing line: %s - %s" % (line, _err_str))

        # Check timeout counter.
        if time.time() > (rx_last_line+timeout):
            logging.error("RX Timed out.")
            break
        # Sleep for a short time.
        time.sleep(0.1)

    # If we were writing a log, close the file.
    if _log_file != None:
        _log_file.flush()
        _log_file.close()

    logging.error("Closing RX Thread.")
    os.killpg(os.getpgid(rx.pid), signal.SIGTERM)
    rx_stdout.stop()
    rx_stdout.join()
    return

def internet_push_thread(station_config):
    """ Push a frame of sonde data into various internet services (APRS-IS, Habitat), and also to a rotator (if configured) """
    global internet_push_queue, INTERNET_PUSH_RUNNING
    logging.info("Started Internet Push thread.")
    while INTERNET_PUSH_RUNNING:
        data = None
        try:
            # Wait until there is somethign in the queue before trying to process.
            if internet_push_queue.empty():
                time.sleep(1)
                continue
            else:
                # Read in entire contents of queue, and keep the most recent entry.
                while not internet_push_queue.empty():
                    data = internet_push_queue.get()
        except:
            traceback.print_exc()
            continue

        try:
            # Wrap this entire section in a try/except, to catch any data parsing errors.

            # Test to see if this payload ID has been seen often enough to permit uploading.
            if not payload_id_valid_for_upload(data['id'],update=False):
                logging.warning("Payload ID has not been observed enough to permit uploading.")
            else:
                # Data from this payload is considered 'valid'
                
                # APRS Upload
                if station_config['enable_aprs'] and (data['lat'] != 0.0) and (data['lon'] != 0.0):
                    # Produce aprs comment, based on user config.
                    aprs_comment = station_config['aprs_custom_comment']
                    aprs_comment = aprs_comment.replace("<freq>", data['freq'])
                    aprs_comment = aprs_comment.replace("<id>", data['id'])
                    aprs_comment = aprs_comment.replace("<temp>", "%.1f degC" % data['temp'])
                    aprs_comment = aprs_comment.replace("<vel_v>", "%.1fm/s" % data['vel_v'])
                    # Add 'Ozone' to the sonde type field if we are seeing aux data.
                    _sonde_type = data['type']
                    if 'aux' in data.keys():
                        _sonde_type += "-Ozone"
                    aprs_comment = aprs_comment.replace("<type>", _sonde_type)

                    # Push data to APRS.
                    aprs_data = push_balloon_to_aprs(data,
                                                    object_name=station_config['aprs_object_id'],
                                                    aprs_comment=aprs_comment,
                                                    aprsUser=station_config['aprs_user'],
                                                    aprsPass=station_config['aprs_pass'],
                                                    serverHost=station_config['aprs_server'])
                    logging.info("Data pushed to APRS-IS: %s" % aprs_data)

                # Habitat Upload
                if station_config['enable_habitat']:
                    # We make the habitat comment field fixed, as we only need to add the payload type/serial/frequency.
                    # If we are seeing aux data, it likely means we have an Ozone sonde!
                    if 'aux' in data.keys():
                        _ozone = "-Ozone"
                    else:
                        _ozone = ""
                    
                    payload_callsign = config['payload_callsign']
                    if config['payload_callsign'] == "<id>":
                        payload_callsign = 'RS_' + data['id']
                        initPayloadDoc(payload_callsign, config['payload_description']) # it's fine for us to call this multiple times as initPayloadDoc keeps a cache for serial numbers it's created payloads for.

                    # Create comment field.
                    habitat_comment = "%s%s %s %s" % (data['type'], _ozone, data['id'], data['freq'])

                    habitat_upload_payload_telemetry(data, 
                                                    payload_callsign=payload_callsign, 
                                                    callsign=config['uploader_callsign'], 
                                                    comment=habitat_comment)
                    logging.debug("Data pushed to Habitat.")

                # Update Rotator positon, if configured.
                if config['enable_rotator'] and (config['station_lat'] != 0.0) and (config['station_lon'] != 0.0):
                    # Calculate Azimuth & Elevation to Radiosonde.
                    rel_position = position_info((config['station_lat'], config['station_lon'], config['station_alt']),
                        (data['lat'], data['lon'], data['alt']))

                    # Update the rotator with the current sonde position.
                    update_rotctld(hostname=config['rotator_hostname'], 
                                port=config['rotator_port'],
                                azimuth=rel_position['bearing'],
                                elevation=rel_position['elevation'])

        except:
            logging.error("Error while uploading data: %s" % traceback.format_exc())

        if station_config['synchronous_upload']:
            # Sleep for a second to ensure we don't double upload in the same slot (shouldn't' happen, but anyway...)
            time.sleep(1)

            # Wait until the next valid uplink timeslot.
            # This is determined by waiting until the time since epoch modulus the upload rate is equal to zero.
            # Note that this will result in some odd upload times, due to leap seconds and otherwise, but should
            # result in multiple stations (assuming local timezones are the same, and the stations are synced to NTP)
            # uploading at roughly the same time.
            while int(time.time())%station_config['upload_rate'] != 0:
                time.sleep(0.1)
        else:
            # Otherwise, just sleep.
            time.sleep(station_config['upload_rate'])

    logging.debug("Closing internet push thread.")

def ozi_push_thread(station_config):
    """ Push a frame of sonde data into various internet services (APRS-IS, Habitat) """
    global ozi_push_queue, OZI_PUSH_RUNNING
    logging.info("Started OziPlotter Push thread.")
    while OZI_PUSH_RUNNING:
        data = None
        try:
            # Wait until there is somethign in the queue before trying to process.
            if ozi_push_queue.empty():
                time.sleep(1)
                continue
            else:
                # Read in entire contents of queue, and keep the most recent entry.
                while not ozi_push_queue.empty():
                    data = ozi_push_queue.get()
        except:
            traceback.print_exc()
            continue

        try:
            if station_config['ozi_enabled']:
                push_telemetry_to_ozi(data,hostname=station_config['ozi_hostname'], udp_port=station_config['ozi_port'])

            if station_config['payload_summary_enabled']:
                push_payload_summary(data, udp_port=station_config['payload_summary_port'])
        except:
            traceback.print_exc()

        time.sleep(station_config['ozi_update_rate'])

    logging.debug("Closing thread.")


if __name__ == "__main__":

    # Setup logging.
    logging.basicConfig(format='%(asctime)s %(levelname)s:%(message)s', filename=datetime.datetime.utcnow().strftime("log/%Y%m%d-%H%M%S_system.log"), level=logging_level)
    stdout_format = logging.Formatter('%(asctime)s %(levelname)s:%(message)s')
    stdout_handler = logging.StreamHandler(sys.stdout)
    stdout_handler.setFormatter(stdout_format)
    logging.getLogger().addHandler(stdout_handler)

    # Set the requests logger to only display WARNING messages or higher.
    requests_log = logging.getLogger("requests")
    requests_log.setLevel(logging.CRITICAL)
    urllib3_log = logging.getLogger("urllib3")
    urllib3_log.setLevel(logging.CRITICAL)

    # Command line arguments. 
    parser = argparse.ArgumentParser()
    parser.add_argument("-c" ,"--config", default="station.cfg", help="Receive Station Configuration File")
    parser.add_argument("-f", "--frequency", type=float, default=0.0, help="Sonde Frequency (MHz) (bypass scan step, and quit if no sonde found).")
    parser.add_argument("-t", "--timeout", type=int, default=180, help="Stop receiving after X minutes. Set to 0 to run continuously with no timeout.")
    parser.add_argument("-e", "--ephemeris", type=str, default="None", help="Use a manually obtained ephemeris file.")
    args = parser.parse_args()

    # If we haven't been given an ephemeris file, set the ephemeris variable to None, so that we download one.
    ephemeris = args.ephemeris
    if ephemeris == "None":
        ephemeris = None
    else:
        logging.info("Using provided ephemeris file: %s" % ephemeris)

    # Attempt to read in configuration file. Use default config if reading fails.
    config = read_auto_rx_config(args.config)

    logging.debug("Using Configuration: %s" % str(config))

    # Set the timeout
    timeout_time = time.time() + int(args.timeout)*60

    # Internet push thread object.
    push_thread_1 = None
    push_thread_2 = None

    # Sonde Frequency & Type variables.
    sonde_freq = None
    sonde_type = None

    # MQTT Client
    mqtt_client = None

    try:
        # If Habitat upload is enabled and we have been provided with listener coords, push our position to habitat
        if config['enable_habitat'] and (config['station_lat'] != 0.0) and (config['station_lon'] != 0.0) and config['upload_listener_position']:
            uploadListenerPosition(config['uploader_callsign'], config['station_lat'], config['station_lon'], version=AUTO_RX_VERSION)

        if config['mqtt_enabled']:
            import paho.mqtt.client
            mqtt_client = paho.mqtt.client.Client()
            print "Connecting to MQTT Server %s:%s" % (config['mqtt_hostname'], config['mqtt_port'])
            mqtt_client.connect(config['mqtt_hostname'], config['mqtt_port'])
            mqtt_client.loop_start()

        # Main scan & track loop. We keep on doing this until we timeout (i.e. after we expect the sonde to have landed)

        while time.time() < timeout_time or args.timeout == 0:
            # Attempt to detect a sonde on a supplied frequency.
            if args.frequency != 0.0:
                sonde_type = detect_sonde(int(float(args.frequency)*1e6), sdr_fm=config['sdr_fm_path'], ppm=config['sdr_ppm'], gain=config['sdr_gain'], bias=config['sdr_bias'])
                if sonde_type != None:
                    sonde_freq = int(float(args.frequency)*1e6)
                else:
                    logging.info("No sonde found. Exiting.")
                    sys.exit(1)

            # If we have a rotator configured, attempt to point the rotator to the home location
            if config['enable_rotator'] and (config['station_lat'] != 0.0) and (config['station_lon'] != 0.0) and config['rotator_homing_enabled']:
                update_rotctld(hostname=config['rotator_hostname'], 
                            port=config['rotator_port'], 
                            azimuth=config['rotator_home_azimuth'], 
                            elevation=config['rotator_home_elevation'])

            # If nothing is detected, or we haven't been supplied a frequency, perform a scan.
            if sonde_type == None:
                (sonde_freq, sonde_type) = sonde_search(config, config['search_attempts'])

            # If we *still* haven't detected a sonde... just keep on trying, until we hit our timeout.
            if sonde_type == None:
                continue

            logging.info("Starting decoding of %s on %.3f MHz" % (sonde_type, sonde_freq/1e6))

            # Re-push our listener position to habitat, as if we have been running continuously we may have dropped off the map.
            if config['enable_habitat'] and (config['station_lat'] != 0.0) and (config['station_lon'] != 0.0) and config['upload_listener_position']:
                uploadListenerPosition(config['uploader_callsign'], config['station_lat'], config['station_lon'], version=AUTO_RX_VERSION)


            # Start both of our internet/ozi push threads, even if we're not going to use them.
            if push_thread_1 == None:
                push_thread_1 = Thread(target=internet_push_thread, kwargs={'station_config':config})
                push_thread_1.start()

            if push_thread_2 == None:
                push_thread_2 = Thread(target=ozi_push_thread, kwargs={'station_config':config})
                push_thread_2.start()

            # Start decoding the sonde!
            if sonde_type == "RS92":
                decode_rs92(sonde_freq, 
                            sdr_fm=config['sdr_fm_path'],
                            ppm=config['sdr_ppm'], 
                            gain=config['sdr_gain'], 
                            bias=config['sdr_bias'], 
                            rx_queue=internet_push_queue, 
                            timeout=config['rx_timeout'], 
                            save_log=config['per_sonde_log'], 
                            ephemeris=ephemeris)
            elif sonde_type == "RS41":
                decode_rs41(sonde_freq, 
                            sdr_fm=config['sdr_fm_path'],
                            ppm=config['sdr_ppm'], 
                            gain=config['sdr_gain'], 
                            bias=config['sdr_bias'], 
                            rx_queue=internet_push_queue, 
                            timeout=config['rx_timeout'], 
                            save_log=config['per_sonde_log'])
            else:
                pass

            # Receiver has timed out. Reset sonde type and frequency variables and loop.
            logging.error("Receiver timed out. Re-starting scan.")
            time.sleep(config['search_delay'])
            sonde_type = None
            sonde_freq = None

    except KeyboardInterrupt:
        logging.info("Caught CTRL-C, exiting.")
        # Shut down the Internet Push Threads.
        INTERNET_PUSH_RUNNING = False
        OZI_PUSH_RUNNING = False
        # Kill all rtl_fm processes.
        os.system('killall rtl_power')
        os.system('killall rtl_fm')
        #.. and the rx_tools equivalents, just in case.
        os.system('killall rx_power')
        os.system('killall rx_fm')
        sys.exit(0)
    # Note that if we are running as a service, we won't ever get here.

    logging.info("Exceeded maximum receive time. Exiting.")

    # Write flight statistics to file.
    if flight_stats['last'] != None:
        stats_str = calculate_flight_statistics()
        logging.info(stats_str)

        f = open("last_positions.txt", 'a')
        f.write(stats_str + "\n")
        f.close()

    # Stop the Output threads.
    INTERNET_PUSH_RUNNING = False
    OZI_PUSH_RUNNING = False


