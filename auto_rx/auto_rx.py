#!/usr/bin/env python
#
#   Radiosonde Auto RX Service - V2.0
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
#   Refer github page for instructions on setup and usage.
#   https://github.com/projecthorus/radiosonde_auto_rx/
#
import argparse
import datetime
import logging
import re
import sys
import time
import traceback

from autorx.scan import SondeScanner
from autorx.decode import SondeDecoder
from autorx.logger import TelemetryLogger
from autorx.habitat import HabitatUploader
from autorx.utils import rtlsdr_test, position_info
from autorx.config import read_auto_rx_config

try:
    # Python 2
    from Queue import Queue
except ImportError:
    # Python 3
    from queue import Queue


# Logging level
# INFO = Basic status messages
# DEBUG = Adds detailed information on submodule operations.
logging_level = logging.INFO


#
#   Global Variables
#

RS_PATH = "./"

# Optional override for RS92 ephemeris data.
rs92_ephemeris = None

# Global configuration dictionary
config = None

# Exporter Lists
exporter_objects = []   # This list will hold references to each exporter instance that is created.
exporter_functions = [] # This list will hold references to the exporter add functions, which will be passed onto the decoders.

# RTLSDR Usage Register - This dictionary holds information about each SDR and its currently running Decoder / Scanner
#   Key = SDR device index / ID
#   'device_idx': {
#       'in_use' (bool) : True if the SDR is currently in-use by a decoder or scanner.
#       'task' (class)  : If this SDR is in use, a reference to the task.
#       'bias' (bool)   : True if the bias-tee should be enabled on this SDR, False otherwise.
#       'ppm' (int)     : The PPM offset for this SDR.
#       'gain' (float)  : The gain setting to use with this SDR. A setting of -1 turns on hardware AGC.    
#   }
#
#
sdr_list = {}

# Currently running task register.
#   Keys will either be 'SCAN' (only one scanner shall be running at a time), or a sonde frequency in MHz.
#   Each element contains:
#       'task' : (class) Reference to the currently running task.
#       'device_idx' (str): The allocated SDR.
#
task_list = {}


# Scan Result Queue
# Scan results are processed asynchronously from the main scanner object.
scan_results = Queue()


def allocate_sdr(check_only = False, task_description = ""):
    """ Allocate an un-used SDR for a task.

    Args:
        check_only (bool) : If True, don't set the free SDR as in-use. Used to check if there are any free SDRs.

    Returns:
        (str): The device index/serial number of the free/allocated SDR, if one is free, else None.
    """
    global sdr_list

    for _idx in sdr_list.keys():
        if sdr_list[_idx]['in_use'] == False:
            # Found a free SDR!
            if check_only:
                # If we are just checking to see if there are any SDRs free, we don't allocate it.
                pass
            else:
                # Otherwise, set the SDR as in-use.
                sdr_list[_idx]['in_use'] = True
                logging.info("SDR #%s has been allocated for %s." % (str(_idx), task_description))
            
            return _idx

    # Otherwise, no SDRs are free.
    return None


def start_scanner():
    """ Start a scanner thread on the first available SDR """
    global task_list, sdr_list, config, scan_results, RS_PATH

    if 'SCAN' in task_list:
        # Already a scanner running! Return.
        logging.debug("Task Manager - Attempted to start a scanner, but one already running.")
        return

    # Attempt to allocate a SDR.
    _device_idx = allocate_sdr(task_description="Scanner")
    if _device_idx is None:
        logging.debug("Task Manager - No SDRs free to run Scanner.")
        return
    else:
        # Create entry in task list.
        task_list['SCAN'] = {'device_idx': _device_idx, 'task': None}

        # Init Scanner using settings from the global config.

        task_list['SCAN']['task'] = SondeScanner(
            callback = scan_results.put,
            auto_start = True,
            min_freq = config['min_freq'],
            max_freq = config['max_freq'],
            search_step = config['search_step'],
            whitelist = config['whitelist'],
            greylist = config['greylist'],
            blacklist = config['blacklist'],
            snr_threshold = config['snr_threshold'],
            min_distance = config['min_distance'],
            quantization = config['quantization'],
            scan_dwell_time = config['scan_dwell_time'],
            detect_dwell_time = config['detect_dwell_time'],
            max_peaks = config['max_peaks'],
            rs_path = RS_PATH,
            sdr_power = config['sdr_power'],
            sdr_fm = config['sdr_fm'],
            device_idx = _device_idx,
            gain = sdr_list[_device_idx]['gain'],
            ppm = sdr_list[_device_idx]['ppm'],
            bias = sdr_list[_device_idx]['bias']
            )

        # Add a reference into the sdr_list entry
        sdr_list[_device_idx]['task'] = task_list['SCAN']['task']


def stop_scanner():
    """ Stop a currently running scan thread, and release the SDR it was using. """
    global task_list, sdr_list

    if 'SCAN' not in task_list:
        # No scanner thread running!
        # This means we likely have a SDR free already.
        return
    else:
        logging.info("Halting Scanner to decode detected radiosonde.")
        _scan_sdr = task_list['SCAN']['device_idx']
        # Stop the scanner.
        task_list['SCAN']['task'].stop()
        # Relase the SDR.
        sdr_list[_scan_sdr]['in_use'] = False
        sdr_list[_scan_sdr]['task'] = None
        # Remove the scanner task from the task list
        task_list.pop('SCAN')


def start_decoder(freq, sonde_type):
    """ Attempt to start a decoder thread """
    global config, task_list, sdr_list, RS_PATH, exporter_functions, rs92_ephemeris

    # Allocate a SDR.
    _device_idx = allocate_sdr(task_description="Decoder (%s, %.3f MHz)" % (sonde_type, freq/1e6))

    if _device_idx is None:
        logging.error("Could not allocate SDR for decoder!")
        return
    else:
        # Add an entry to the task list
        task_list[freq] = {'device_idx': _device_idx, 'task': None}

        # Set the SDR to in-use
        sdr_list[_device_idx]['in_use'] = True

        # Initialise a decoder.
        task_list[freq]['task'] = SondeDecoder(
            sonde_type = sonde_type,
            sonde_freq = freq,
            rs_path = RS_PATH,
            sdr_fm = config['sdr_fm'],
            device_idx = _device_idx,
            gain = sdr_list[_device_idx]['gain'],
            ppm = sdr_list[_device_idx]['ppm'],
            bias = sdr_list[_device_idx]['bias'],
            exporter = exporter_functions,
            timeout = config['rx_timeout'],
            telem_filter = telemetry_filter,
            rs92_ephemeris = rs92_ephemeris
            )
        sdr_list[_device_idx]['task'] = task_list[freq]['task']



def handle_scan_results():
    """ Read in Scan results via the scan results Queue.

    Depending on how many SDRs are available, two things can happen:
    - If there is a free SDR, allocate it to a decoder.
    - If there is no free SDR, but a scanner is running, stop the scanner and start decoding.
    """
    global scan_results, task_list, sdr_list
    if scan_results.qsize() > 0:
        _scan_data = scan_results.get()
        for _sonde in _scan_data:
            _freq = _sonde[0]
            _type = _sonde[1]

            if _freq in task_list:
                # Already decoding this sonde, continue.
                continue
            else:
                logging.info("Scanner - Detected new %s sonde on %.3f MHz!" % (_type, _freq/1e6))
                if allocate_sdr(check_only=True) is not None :
                    # There is a SDR free! Start the decoder on that SDR
                    start_decoder(_freq, _type)

                elif (allocate_sdr(check_only=True) is None) and ('SCAN' in task_list):
                    # We have run out of SDRs, but a scan thread is running.
                    # Stop the scan thread and take that receiver!
                    stop_scanner()
                    start_decoder(_freq, _type)
                else:
                    # We have no SDRs free 
                    pass


def clean_task_list():
    """ Check the task list to see if any tasks have stopped running. If so, release the associated SDR """
    global task_list, sdr_list

    for _key in task_list.keys():
        # Attempt to get the state of the task
        try:
            _running = task_list[_key]['task'].running()
            _task_sdr = task_list[_key]['device_idx']
        except Exception as e:
            logging.error("Task Manager - Error getting task %s state - %s" % (str(_key),str(e)))
            continue

        if _running == False:
            # This task has stopped. Release it's associated SDR.
            sdr_list[_task_sdr]['in_use'] = False
            sdr_list[_task_sdr]['task'] = None
            # Pop the task from the task list.
            task_list.pop(_key)

    # Check if there is a scanner thread still running. If not, and if there is a SDR free, start one up again.
    if ('SCAN' not in task_list) and (allocate_sdr(check_only=True) is not None):
        # We have a SDR free, and we are not running a scan thread. Start one.
        start_scanner()


def stop_all():
    """ Shut-down all decoders, scanners, and exporters. """
    global task_list, exporter_objects
    logging.info("Starting shutdown of all threads.")
    for _task in task_list.keys():
        try:
            task_list[_task]['task'].stop()
        except Exception as e:
            logging.error("Error stopping task - %s" % str(e))

    for _exporter in exporter_objects:
        try:
            _exporter.close()
        except Exception as e:
            logging.error("Error stopping exporter - %s" % str(e))


def telemetry_filter(telemetry):
    """ Filter incoming radiosonde telemetry based on various factors, 
        - Invalid Position
        - Invalid Altitude
        - Abnormal range from receiver.
        - Invalid serial number.

    """
    global config

    # First Check: zero lat/lon
    if (telemetry['lat'] == 0.0) and (telemetry['lon'] == 0.0):
        logging.warning("Zero Lat/Lon. Sonde %s does not have GPS lock." % telemetry['id'])
        return False

    # Second check: Altitude cap.
    if telemetry['alt'] > config['max_altitude']:
        _altitude_breach = telemetry['alt'] - config['max_altitude']
        logging.warning("Sonde %s position breached altitude cap by %d m." % (telemetry['id'], _altitude_breach))
        return False

    # Third check - is the payload more than x km from our listening station.
    # Only run this check if a station location has been provided.
    if (config['station_lat'] != 0.0) and (config['station_lon'] != 0.0):
        # Calculate the distance from the station to the payload.
        _listener = (config['station_lat'], config['station_lon'], config['station_alt'])
        _payload = (telemetry['lat'], telemetry['lon'], telemetry['alt'])
        # Calculate using positon_info function from rotator_utils.py
        _info = position_info(_listener, _payload)

        if _info['straight_distance'] > config['max_radius_km']*1000:
            _radius_breach = _info['straight_distance']/1000.0 - config['max_radius_km']
            logging.warning("Sonde %s position breached radius cap by %.1f km." % (telemetry['id'], _radius_breach))
            return False

    # Payload Serial Number Checks
    _serial = telemetry['id']
    # Run a Regex to match known Vaisala RS92/RS41 serial numbers (YWWDxxxx)
    # RS92: https://www.vaisala.com/sites/default/files/documents/Vaisala%20Radiosonde%20RS92%20Serial%20Number.pdf
    # RS41: https://www.vaisala.com/sites/default/files/documents/Vaisala%20Radiosonde%20RS41%20Serial%20Number.pdf
    # This will need to be re-evaluated if we're still using this code in 2021!
    vaisala_callsign_valid = re.match(r'[J-T][0-5][\d][1-7]\d{4}', _serial)

    # Regex to check DFM06/09 callsigns.
    # TODO: Check if this valid for DFM06s, and find out what's up with the 8-digit DFM09 callsigns.
    dfm_callsign_valid = re.match(r'DFM0[69]-\d{6}', _serial)

    if vaisala_callsign_valid or dfm_callsign_valid:
        return True
    else:
        logging.warning("Payload ID %s does not match regex. Discarding." % telemetry['id'])
        return False


def main():
    """ Main Loop """
    global config, sdr_list, exporter_objects, exporter_functions, logging_level

    # Command line arguments. 
    parser = argparse.ArgumentParser()
    parser.add_argument("-c" ,"--config", default="station.cfg", help="Receive Station Configuration File")
    parser.add_argument("-f", "--frequency", type=float, default=0.0, help="Sonde Frequency (MHz) (bypass scan step, and quit if no sonde found).")
    parser.add_argument("-e", "--ephemeris", type=str, default="None", help="Use a manually obtained ephemeris file.")
    parser.add_argument("-v", "--verbose", help="Enable debug output.", action="store_true")
    args = parser.parse_args()

    # Set log-level to DEBUG if requested
    if args.verbose:
        logging_level = logging.DEBUG


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


    # Attempt to read in config file
    logging.info("Reading configuration file...")
    _temp_cfg = read_auto_rx_config(args.config)
    if _temp_cfg is None:
        logging.critical("Error in configuration file! Exiting...")
        sys.exit(1)

    else:
        config = _temp_cfg
        sdr_list = config['sdr_settings']

    # If we have been supplied a frequency via the command line, override the whitelist settings.
    if args.frequency != 0.0:
        config['whitelist'] = [args.frequency]


    # Start our exporter options
    # Telemetry Logger
    if config['per_sonde_log']:
        _logger = TelemetryLogger(log_directory="./log/")
        exporter_objects.append(_logger)
        exporter_functions.append(_logger.add)

    # Habitat Uploader
    if config['habitat_enabled']:
        if config['habitat_payload_callsign'] == "<id>":
            _habitat_payload_call = None
        else:
            _habitat_payload_call = config['habitat_payload_callsign']

        if config['habitat_upload_listener_position'] is False:
            _habitat_user_position = None
        else:
            _habitat_user_position = (config['station_lat'], config['station_lon'], config['station_alt'])
 
        _habitat = HabitatUploader(
            user_callsign = config['habitat_uploader_callsign'],
            user_position = _habitat_user_position,
            payload_callsign_override = _habitat_payload_call,
            synchronous_upload_time = config['habitat_upload_rate'],
            callsign_validity_threshold = config['payload_id_valid']
            )

        exporter_objects.append(_habitat)
        exporter_functions.append(_habitat.add)


    # APRS - TODO

    # OziExplorer - TODO

    # MQTT (?) - TODO


    while True:
        clean_task_list()
        handle_scan_results()
        time.sleep(2)




if __name__ == "__main__":

    try:
        main()
    except KeyboardInterrupt:
        stop_all()
    except Exception as e:
        traceback.print_exc()
        print("Main Loop Error - %s" % str(e))
        stop_all()

