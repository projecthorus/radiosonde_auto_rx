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

import autorx
from autorx.scan import SondeScanner
from autorx.decode import SondeDecoder
from autorx.logger import TelemetryLogger
from autorx.habitat import HabitatUploader
from autorx.aprs import APRSUploader
from autorx.ozimux import OziUploader
from autorx.utils import rtlsdr_test, position_info
from autorx.config import read_auto_rx_config
from autorx.web import start_flask, stop_flask, WebHandler, WebExporter

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

# Global configuration dictionary. Populated on startup.
config = None

# Exporter Lists
exporter_objects = []   # This list will hold references to each exporter instance that is created.
exporter_functions = [] # This list will hold references to the exporter add functions, which will be passed onto the decoders.



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

    for _idx in autorx.sdr_list.keys():
        if autorx.sdr_list[_idx]['in_use'] == False:
            # Found a free SDR!
            if check_only:
                # If we are just checking to see if there are any SDRs free, we don't allocate it.
                pass
            else:
                # Otherwise, set the SDR as in-use.
                autorx.sdr_list[_idx]['in_use'] = True
                logging.info("SDR #%s has been allocated to %s." % (str(_idx), task_description))
            
            return _idx

    # Otherwise, no SDRs are free.
    return None


def start_scanner():
    """ Start a scanner thread on the first available SDR """
    global config, scan_results, RS_PATH

    if 'SCAN' in autorx.task_list:
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
        autorx.task_list['SCAN'] = {'device_idx': _device_idx, 'task': None}

        # Init Scanner using settings from the global config.
        # TODO: Nicer way of passing in the huge list of args.
        autorx.task_list['SCAN']['task'] = SondeScanner(
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
            gain = autorx.sdr_list[_device_idx]['gain'],
            ppm = autorx.sdr_list[_device_idx]['ppm'],
            bias = autorx.sdr_list[_device_idx]['bias']
            )

        # Add a reference into the sdr_list entry
        autorx.sdr_list[_device_idx]['task'] = autorx.task_list['SCAN']['task']


def stop_scanner():
    """ Stop a currently running scan thread, and release the SDR it was using. """

    if 'SCAN' not in autorx.task_list:
        # No scanner thread running!
        # This means we likely have a SDR free already.
        return
    else:
        logging.info("Halting Scanner to decode detected radiosonde.")
        _scan_sdr = autorx.task_list['SCAN']['device_idx']
        # Stop the scanner.
        autorx.task_list['SCAN']['task'].stop()
        # Relase the SDR.
        autorx.sdr_list[_scan_sdr]['in_use'] = False
        autorx.sdr_list[_scan_sdr]['task'] = None
        # Remove the scanner task from the task list
        autorx.task_list.pop('SCAN')



def start_decoder(freq, sonde_type):
    """ Attempt to start a decoder thread for a given sonde.

    Args:
        freq (float): Radiosonde frequency in Hz.
        sonde_type (str): The radiosonde type ('RS41', 'RS92', 'DFM')

    """
    global config, RS_PATH, exporter_functions, rs92_ephemeris

    # Allocate a SDR.
    _device_idx = allocate_sdr(task_description="Decoder (%s, %.3f MHz)" % (sonde_type, freq/1e6))

    if _device_idx is None:
        logging.error("Could not allocate SDR for decoder!")
        return
    else:
        # Add an entry to the task list
        autorx.task_list[freq] = {'device_idx': _device_idx, 'task': None}

        # Set the SDR to in-use
        autorx.sdr_list[_device_idx]['in_use'] = True

        # Initialise a decoder.
        autorx.task_list[freq]['task'] = SondeDecoder(
            sonde_type = sonde_type,
            sonde_freq = freq,
            rs_path = RS_PATH,
            sdr_fm = config['sdr_fm'],
            device_idx = _device_idx,
            gain = autorx.sdr_list[_device_idx]['gain'],
            ppm = autorx.sdr_list[_device_idx]['ppm'],
            bias = autorx.sdr_list[_device_idx]['bias'],
            exporter = exporter_functions,
            timeout = config['rx_timeout'],
            telem_filter = telemetry_filter,
            rs92_ephemeris = rs92_ephemeris
            )
        autorx.sdr_list[_device_idx]['task'] = autorx.task_list[freq]['task']



def handle_scan_results():
    """ Read in Scan results via the scan results Queue.

    Depending on how many SDRs are available, two things can happen:
    - If there is a free SDR, allocate it to a decoder.
    - If there is no free SDR, but a scanner is running, stop the scanner and start decoding.
    """
    global scan_results

    if scan_results.qsize() > 0:
        # Grab the latest detections from the scan result queue.
        _scan_data = scan_results.get()
        for _sonde in _scan_data:
            # Extract frequency & type info
            _freq = _sonde[0]
            _type = _sonde[1]

            if _freq in autorx.task_list:
                # Already decoding this sonde, continue.
                continue
            else:
                logging.info("Detected new %s sonde on %.3f MHz!" % (_type, _freq/1e6))
                if allocate_sdr(check_only=True) is not None :
                    # There is a SDR free! Start the decoder on that SDR
                    start_decoder(_freq, _type)

                elif (allocate_sdr(check_only=True) is None) and ('SCAN' in autorx.task_list):
                    # We have run out of SDRs, but a scan thread is running.
                    # Stop the scan thread and take that receiver!
                    stop_scanner()
                    start_decoder(_freq, _type)
                else:
                    # We have no SDRs free.
                    # TODO: Alert the user that a sonde was detected, but no SDR was available,
                    # but don't do this EVERY time we detect the sonde...
                    pass



def clean_task_list():
    """ Check the task list to see if any tasks have stopped running. If so, release the associated SDR """

    for _key in autorx.task_list.keys():
        # Attempt to get the state of the task
        try:
            _running = autorx.task_list[_key]['task'].running()
            _task_sdr = autorx.task_list[_key]['device_idx']
        except Exception as e:
            logging.error("Task Manager - Error getting task %s state - %s" % (str(_key),str(e)))
            continue

        if _running == False:
            # This task has stopped. Release it's associated SDR.
            autorx.sdr_list[_task_sdr]['in_use'] = False
            autorx.sdr_list[_task_sdr]['task'] = None
            # Pop the task from the task list.
            autorx.task_list.pop(_key)

    # Check if there is a scanner thread still running. If not, and if there is a SDR free, start one up again.
    if ('SCAN' not in autorx.task_list) and (allocate_sdr(check_only=True) is not None):
        # We have a SDR free, and we are not running a scan thread. Start one.
        start_scanner()



def stop_all():
    """ Shut-down all decoders, scanners, and exporters. """
    global exporter_objects
    logging.info("Starting shutdown of all threads.")
    for _task in autorx.task_list.keys():
        try:
            autorx.task_list[_task]['task'].stop()
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

    This function is defined within this script to avoid passing around large amounts of configuration data.

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
    global config, exporter_objects, exporter_functions, logging_level, rs92_ephemeris

    # Command line arguments. 
    parser = argparse.ArgumentParser()
    parser.add_argument("-c" ,"--config", default="station.cfg", help="Receive Station Configuration File. Default: station.cfg")
    parser.add_argument("-f", "--frequency", type=float, default=0.0, help="Sonde Frequency Override (MHz). This overrides the scan whitelist with the supplied frequency.")
    parser.add_argument("-t", "--timeout", type=int, default=0, help="Close auto_rx system after N minutes. Use 0 to run continuously.")
    parser.add_argument("-v", "--verbose", help="Enable debug output.", action="store_true")
    parser.add_argument("-e", "--ephemeris", type=str, default="None", help="Use a manually obtained ephemeris file when decoding RS92 Sondes.")
    args = parser.parse_args()

    # Copy out timeout value, and convert to seconds,
    _timeout = args.timeout*60

    # Copy out RS92 ephemeris value, if provided.
    if args.ephemeris != "None":
        rs92_ephemeris = args.ephemeris

    # Set log-level to DEBUG if requested
    if args.verbose:
        logging_level = logging.DEBUG


    # Configure logging
    logging.basicConfig(format='%(asctime)s %(levelname)s:%(message)s', filename=datetime.datetime.utcnow().strftime("log/%Y%m%d-%H%M%S_system.log"), level=logging_level)
    stdout_format = logging.Formatter('%(asctime)s %(levelname)s:%(message)s')
    stdout_handler = logging.StreamHandler(sys.stdout)
    stdout_handler.setFormatter(stdout_format)
    logging.getLogger().addHandler(stdout_handler)

    web_handler = WebHandler()
    logging.getLogger().addHandler(web_handler)


    # Set the requests/socketio logger to only display critical log messages.
    logging.getLogger("requests").setLevel(logging.CRITICAL)
    logging.getLogger("urllib3").setLevel(logging.CRITICAL)
    logging.getLogger('werkzeug').setLevel(logging.ERROR)
    logging.getLogger('socketio').setLevel(logging.ERROR)
    logging.getLogger('engineio').setLevel(logging.ERROR)


    # Attempt to read in config file
    logging.info("Reading configuration file...")
    _temp_cfg = read_auto_rx_config(args.config)
    if _temp_cfg is None:
        logging.critical("Error in configuration file! Exiting...")
        sys.exit(1)
    else:
        config = _temp_cfg
        autorx.sdr_list = config['sdr_settings']

    # Start up the flask server.
    # This needs to occur AFTER logging is setup, else logging breaks horribly for some reason.
    start_flask(port=config['web_port'])

    # If we have been supplied a frequency via the command line, override the whitelist settings
    # to only include the supplied frequency.
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
            user_antenna = config['habitat_uploader_antenna'],
            user_position = _habitat_user_position,
            payload_callsign_override = _habitat_payload_call,
            synchronous_upload_time = config['habitat_upload_rate'],
            callsign_validity_threshold = config['payload_id_valid']
            )

        exporter_objects.append(_habitat)
        exporter_functions.append(_habitat.add)


    # APRS Uploader
    if config['aprs_enabled']:
        if config['aprs_object_id'] == "<id>":
            _aprs_object = None
        else:
            _aprs_object = config['aprs_object_id']

        _aprs = APRSUploader(
            aprs_callsign = config['aprs_user'],
            aprs_passcode = config['aprs_pass'],
            object_name_override = _aprs_object,
            object_comment = config['aprs_custom_comment'],
            aprsis_host = config['aprs_server'],
            synchronous_upload_time = config['aprs_upload_rate'],
            callsign_validity_threshold = config['payload_id_valid']
            )

        exporter_objects.append(_aprs)
        exporter_functions.append(_aprs.add)

    # OziExplorer 
    if config['ozi_enabled'] or config['payload_summary_enabled']:
        if config['ozi_enabled']:
            _ozi_port = config['ozi_port']
        else:
            _ozi_port = None

        if config['payload_summary_enabled']:
            _summary_port = config['payload_summary_port']
        else:
            _summary_port = None

        _ozimux = OziUploader(
            ozimux_port = _ozi_port,
            payload_summary_port = _summary_port,
            update_rate = config['ozi_update_rate'])

        exporter_objects.append(_ozimux)
        exporter_functions.append(_ozimux.add)

    _web_exporter = WebExporter(max_age=config['web_archive_age'])
    exporter_objects.append(_web_exporter)
    exporter_functions.append(_web_exporter.add)

    # MQTT (?) - TODO

    # Note the start time.
    _start_time = time.time()

    # Loop. 
    while True:
        # Check for finished tasks.
        clean_task_list()
        # Handle any new scan results.
        handle_scan_results()
        # Sleep a little bit.
        time.sleep(2)

        # Allow a timeout after a set time, for users who wish to run auto_rx
        # within a cronjob.
        if (_timeout > 0) and ((time.time()-_start_time) > _timeout):
            logging.info("Shutdown time reached. Closing.")
            stop_flask(port=config['web_port'])
            stop_all()
            break




if __name__ == "__main__":

    try:
        main()
    except KeyboardInterrupt:
        # Upon CTRL+C, shutdown all threads and exit.
        stop_flask(port=config['web_port'])
        stop_all()
    except Exception as e:
        # Upon exceptions, attempt to shutdown threads and exit.
        traceback.print_exc()
        print("Main Loop Error - %s" % str(e))
        stop_flask(port=config['web_port'])
        stop_all()

