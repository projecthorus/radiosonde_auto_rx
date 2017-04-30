#!/usr/bin/env python
#
# Radiosonde Auto RX Tools
#
# 2017-04 Mark Jessop <vk5qi@rfhead.net>
#
# The following binaries will need to be built and copied to this directory:
# rs92/rs92gps
# scan/rs_detect
#
# The following other packages are needed:
# rtl-sdr (for the rtl_power and rtl_fm utilities)
# sox
#
# Instructions:
# Modify config parameters below as required. Take note of the APRS_USER and APRS_PASS values.
# Run with: python auto_rx.py
# A log file will be written to log/<timestamp>.log
#
#
# TODO:
# [ ] Better handling of errors from the decoder sub-process.
#       [ ] Handle no lat/long better.
#       [ ] Option to filter by DOP data
# [ ] Automatic downloading of ephemeris data, instead of almanac.
# [ ] Better peak signal detection.
# [ ] Habitat upload.
# [ ] Move configuration parameters to a separate file.
#   [ ] Allow use of custom object name instead of sonde ID.
# [ ] Build file. 
# [ ] RS41 support.



import numpy as np
import sys
import logging
import datetime
import time
import Queue
import subprocess
import traceback
from aprs_utils import *
from threading import Thread
from StringIO import StringIO
from findpeaks import *
from os import system


# Receiver Parameters
RX_PPM = 0
RX_GAIN = 0 # 0 = Auto

# Sonde Search Configuration Parameters
MIN_FREQ = 400.4e6          # Search start frequency (Hz)
MAX_FREQ = 403.5e6          # Search stop frequency (Hz)
SEARCH_STEP = 800           # Search step (Hz)
FREQ_QUANTIZATION = 5000    # Quantize search results to 5 kHz steps.
MIN_FREQ_DISTANCE = 1000    # Minimum distance between peaks.
MIN_SNR = 10                # Only takes peaks that are a minimum of 10dB above the noise floor.
SEARCH_ATTEMPTS = 5         # Number of attempts to search before giving up
SEARCH_DELAY = 120          # Delay between search attempts (seconds)

# Other Receiver Parameters
MAX_RX_TIME = 3*60*60

# APRS Output
APRS_OUTPUT_ENABLED = True
APRS_UPDATE_RATE = 30
APRS_USER = "N0CALL"    # Replace with your callsign
APRS_PASS = "000000"    # Replace with your APRS-IS passcode
aprs_queue = Queue.Queue(1)



def run_rtl_power(start, stop, step, filename="log_power.csv",  dwell = 20):
    """ Run rtl_power, with a timeout"""
    # rtl_power -f 400400000:403500000:800 -i20 -1 log_power.csv
    rtl_power_cmd = "timeout %d rtl_power -f %d:%d:%d -i %d -1 %s" % (dwell+10, start, stop, step, dwell, filename)
    logging.info("Running frequency scan.")
    ret_code = system(rtl_power_cmd)
    if ret_code == 1:
        logging.critical("rtl_power call failed!")
        sys.exit(1)
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

        freq_range = np.arange(start_freq,stop_freq,freq_step)
        samples = np.loadtxt(StringIO(",".join(fields[6:])),delimiter=',')

        # Add frequency range and samples to output buffers.
        freq = np.append(freq, freq_range)
        power = np.append(power, samples)

    f.close()
    return (freq, power, freq_step)


def quantize_freq(freq_list, quantize=5000):
    """ Quantise a list of frequencies to steps of <quantize> Hz """
    return np.round(freq_list/quantize)*quantize

def detect_sonde(frequency):
    """ Receive some FM and attempt to detect the presence of a radiosonde. """
    rx_test_command = "timeout 10s rtl_fm -p %d -M fm -s 15k -f %d 2>/dev/null |" % (RX_PPM, frequency) 
    rx_test_command += "sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -t wav - highpass 20 2>/dev/null |"
    rx_test_command += "./rs_detect -z -t 8 2>/dev/null"

    logging.info("Attempting sonde detection on %.3f MHz" % (frequency/1e6))
    ret_code = system(rx_test_command)

    ret_code = ret_code >> 8

    if ret_code == 3:
        logging.info("Detected a RS41!")
        return "RS41"
    elif ret_code == 4:
        logging.info("Detected a RS92!")
        return "RS92"
    else:
        return None


def process_rs92_line(line):
    """ Process a line of output from the rs92gps decoder, converting it to a dict """
    try:

        params = line.split(',')
        if len(params) < 9:
            logging.error("Not enough parameters: %s" % line)
            return

        # Attempt to extract parameters.
        rs92_frame = {}
        rs92_frame['frame'] = int(params[0])
        rs92_frame['id'] = str(params[1])
        rs92_frame['time'] = str(params[2])
        rs92_frame['lat'] = float(params[3])
        rs92_frame['lon'] = float(params[4])
        rs92_frame['alt'] = float(params[5])
        rs92_frame['vel_h'] = float(params[6])
        rs92_frame['heading'] = float(params[7])
        rs92_frame['vel_v'] = float(params[8])
        rs92_frame['ok'] = 'OK'

        logging.info("RS92: %s,%d,%s,%.5f,%.5f,%.1f" % (rs92_frame['id'], rs92_frame['frame'],rs92_frame['time'], rs92_frame['lat'], rs92_frame['lon'], rs92_frame['alt']))

        return rs92_frame

    except:
        logging.error("Could not parse string: %s" % line)
        return None

def decode_rs92(frequency, ppm=RX_PPM, rx_queue=None):
    """ Decode a RS92 sonde """
    decode_cmd = "rtl_fm -p %d -M fm -s 12k -f %d 2>/dev/null |" % (ppm, frequency)
    decode_cmd += "sox -t raw -r 12k -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - lowpass 2500 highpass 20 2>/dev/null |"
    decode_cmd += "./rs92gps --vel2 --crc -a almanac.txt"


    rx_start_time = time.time()

    rx = subprocess.Popen(decode_cmd, shell=True, stdin=None, stdout=subprocess.PIPE)

    while True:
        try:
            line = rx.stdout.readline()
            if (line != None) and (line != ""):
                data = process_rs92_line(line)

                if data != None:
                    data['freq'] = "%.3f MHz" % (frequency/1e6)

                    if rx_queue != None:
                        try:
                            rx_queue.put_nowait(data)
                        except:
                            pass
        except:
            traceback.print_exc()
            logging.error("Could not read from rxer stdout?")
            rx.kill()
            return


def internet_push_thread():
    """ Push a frame of sonde data into various internet services (APRS-IS, Habitat) """
    global aprs_queue, APRS_USER, APRS_PASS, APRS_UPDATE_RATE, APRS_OUTPUT_ENABLED
    print("Started thread.")
    while APRS_OUTPUT_ENABLED:                    
        try:
            data = aprs_queue.get_nowait()
        except:
            continue

        aprs_comment = "Sonde Auto-RX Test %s" % data['freq']
        if APRS_OUTPUT_ENABLED:
            aprs_data = push_balloon_to_aprs(data,aprs_comment=aprs_comment,aprsUser=APRS_USER, aprsPass=APRS_PASS)
            logging.debug("Data pushed to APRS-IS: %s" % aprs_data)

        time.sleep(APRS_UPDATE_RATE)

    print("Closing thread.")


if __name__ == "__main__":

    # Setup logging.
    logging.basicConfig(format='%(asctime)s %(levelname)s:%(message)s', filename=datetime.datetime.utcnow().strftime("log/%Y%m%d-%H%M%S.log"), level=logging.DEBUG)
    logging.getLogger().addHandler(logging.StreamHandler())

    sonde_freq = 0.0
    sonde_type = None

    while SEARCH_ATTEMPTS>0:
        # Scan Band
        run_rtl_power(MIN_FREQ, MAX_FREQ, SEARCH_STEP)

        # Read in result
        (freq, power, step) = read_rtl_power('log_power.csv')

        # Rough approximation of the noise floor of the received power spectrum.
        power_nf = np.mean(power)

        # Detect peaks.
        peak_indices = detect_peaks(power, mph=(power_nf+MIN_SNR), mpd=(MIN_FREQ_DISTANCE/step), show = False)

        if len(peak_indices) == 0:
            logging.info("No peaks found!")
            continue

        # Sort peaks by power.
        peak_powers = power[peak_indices]
        peak_freqs = freq[peak_indices]
        peak_frequencies = peak_freqs[np.argsort(peak_powers)][::-1]

        # Quantize to nearest 5 kHz
        peak_frequencies = quantize_freq(peak_frequencies, FREQ_QUANTIZATION)
        logging.info("Peaks found at (MHz): %s" % str(peak_frequencies/1e6))

        # Run rs_detect on each peak frequency, to determine if there is a sonde there.
        for freq in peak_frequencies:
            detected = detect_sonde(freq)
            if detected != None:
                sonde_freq = freq
                sonde_type = detected
                break

        if sonde_type != None:
            # Found a sonde! Break out of the while loop and attempt to decode it.
            break
        else:
            # No sondes found :-( Wait and try again.
            SEARCH_ATTEMPTS -= 1
            logging.warning("Search attempt failed, %d attempts remaining. Waiting %d seconds." % (SEARCH_ATTEMPTS, SEARCH_DELAY))
            time.sleep(SEARCH_DELAY)

    if SEARCH_ATTEMPTS == 0:
        logging.error("No sondes detcted, exiting.")
        sys.exit(0)

    logging.info("Starting decoding of %s on %.3f MHz" % (sonde_type, sonde_freq/1e6))

    # Start a thread to push data to the web.
    t = Thread(target=internet_push_thread)
    t.start()

    # Start decoding the sonde!
    if sonde_type == "RS92":
        decode_rs92(sonde_freq, rx_queue=aprs_queue)
    elif sonde_type == "RS41":
        logging.error("Not implemented.")
    else:
        pass

    # Stop the APRS output thread.
    APRS_OUTPUT_ENABLED = False


