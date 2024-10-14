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

# exit status codes:
#
# 0 - normal termination (ctrl-c)
# 1 - critical error, needs human attention to fix
# 2 - exit because continous running timeout reached
# 3 - exception occurred, can rerun after resetting SDR
# 4 - some of the threads failed to join, SDR reset and restart required
#     this is mostly caused by hung external utilities

import argparse
import datetime
import logging
import re
import sys
import time
import traceback
import os
from dateutil.parser import parse
from queue import Queue

if sys.version_info < (3, 6):
    print("CRITICAL - radiosonde_auto_rx requires Python 3.6 or newer!")
    sys.exit(1)

import autorx
from autorx.scan import SondeScanner
from autorx.decode import SondeDecoder, VALID_SONDE_TYPES, DRIFTY_SONDE_TYPES
from autorx.logger import TelemetryLogger
from autorx.email_notification import EmailNotification
from autorx.aprs import APRSUploader
from autorx.ozimux import OziUploader
from autorx.sondehub import SondehubUploader
from autorx.rotator import Rotator
from autorx.utils import (
    rtlsdr_test,
    position_info,
    check_rs_utils,
    version_startup_check,
)
from autorx.config import read_auto_rx_config
from autorx.web import (
    start_flask,
    stop_flask,
    flask_emit_event,
    flask_running,
    WebHandler,
    WebExporter,
)
from autorx.gpsd import GPSDAdaptor
from autorx.sdr_wrappers import shutdown_sdr


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
exporter_objects = (
    []
)  # This list will hold references to each exporter instance that is created.
exporter_functions = (
    []
)  # This list will hold references to the exporter add functions, which will be passed onto the decoders.

# Separate reference to the e-mail exporter, as we may want to use this for error notifications.
email_exporter = None

# GPSDAdaptor Instance, if used.
gpsd_adaptor = None

# Temporary frequency block list
# This contains frequncies that should be blocked for a short amount of time.
temporary_block_list = {}


def allocate_sdr(check_only=False, task_description=""):
    """Allocate an un-used SDR for a task.

    Args:
        check_only (bool) : If True, don't set the free SDR as in-use. Used to check if there are any free SDRs.

    Returns:
        (str): The device index/serial number of the free/allocated SDR, if one is free, else None.
    """

    for _idx in sorted(autorx.sdr_list.keys()):
        if autorx.sdr_list[_idx]["in_use"] == False:
            # Found a free SDR!
            if check_only:
                # If we are just checking to see if there are any SDRs free, we don't allocate it.
                pass
            else:
                # Otherwise, set the SDR as in-use.
                autorx.sdr_list[_idx]["in_use"] = True
                logging.info(
                    "Task Manager - SDR #%s has been allocated to %s."
                    % (str(_idx), task_description)
                )

            return _idx

    # Otherwise, no SDRs are free.
    return None


def start_scanner():
    """Start a scanner thread on the first available SDR"""
    global config, RS_PATH, temporary_block_list

    if "SCAN" in autorx.task_list:
        # Already a scanner running! Return.
        logging.debug(
            "Task Manager - Attempted to start a scanner, but one already running."
        )
        return

    # Attempt to allocate a SDR.
    _device_idx = allocate_sdr(task_description="Scanner")
    if _device_idx is None:
        logging.debug("Task Manager - No SDRs free to run Scanner.")
        return
    else:
        # Create entry in task list.
        autorx.task_list["SCAN"] = {"device_idx": _device_idx, "task": None}

        # Init Scanner using settings from the global config.
        # TODO: Nicer way of passing in the huge list of args.
        autorx.task_list["SCAN"]["task"] = SondeScanner(
            callback=autorx.scan_results.put,
            auto_start=True,
            min_freq=config["min_freq"],
            max_freq=config["max_freq"],
            search_step=config["search_step"],
            only_scan=config["only_scan"],
            always_scan=config["always_scan"],
            never_scan=config["never_scan"],
            snr_threshold=config["snr_threshold"],
            min_distance=config["min_distance"],
            quantization=config["quantization"],
            scan_dwell_time=config["scan_dwell_time"],
            scan_delay=config["scan_delay"],
            detect_dwell_time=config["detect_dwell_time"],
            max_peaks=config["max_peaks"],
            rs_path=RS_PATH,
            sdr_type=config["sdr_type"],
            # Network SDR Options
            sdr_hostname=config["sdr_hostname"],
            sdr_port=config["sdr_port"],
            ss_iq_path=config["ss_iq_path"],
            ss_power_path=config["ss_power_path"],
            rtl_power_path=config["sdr_power"],
            rtl_fm_path=config["sdr_fm"],
            rtl_device_idx=_device_idx,
            gain=autorx.sdr_list[_device_idx]["gain"],
            ppm=autorx.sdr_list[_device_idx]["ppm"],
            bias=autorx.sdr_list[_device_idx]["bias"],
            save_detection_audio=config["save_detection_audio"],
            wideband_sondes=config["wideband_sondes"],
            temporary_block_list=temporary_block_list,
            temporary_block_time=config["temporary_block_time"],
        )

        # Add a reference into the sdr_list entry
        autorx.sdr_list[_device_idx]["task"] = autorx.task_list["SCAN"]["task"]

    # Indicate to the web client that the task list has been updated.
    flask_emit_event("task_event")


def stop_scanner():
    """Stop a currently running scan thread, and release the SDR it was using."""

    if "SCAN" not in autorx.task_list:
        # No scanner thread running!
        # This means we likely have a SDR free already.
        return
    else:
        logging.info("Halting Scanner to decode detected radiosonde.")
        _scan_sdr = autorx.task_list["SCAN"]["device_idx"]
        # Stop the scanner.
        autorx.task_list["SCAN"]["task"].stop()
        # Relase the SDR.
        autorx.sdr_list[_scan_sdr]["in_use"] = False
        autorx.sdr_list[_scan_sdr]["task"] = None
        # Remove the scanner task from the task list
        autorx.task_list.pop("SCAN")


def start_decoder(freq, sonde_type, continuous=False):
    """Attempt to start a decoder thread for a given sonde.

    Args:
        freq (float): Radiosonde frequency in Hz.
        sonde_type (str): The radiosonde type ('RS41', 'RS92', 'DFM', 'M10, 'iMet')
        continuous (bool): If true, don't use a decode timeout.

    """
    global config, RS_PATH, exporter_functions, rs92_ephemeris, temporary_block_list

    # Allocate a SDR.
    _device_idx = allocate_sdr(
        task_description="Decoder (%s, %.3f MHz)" % (sonde_type, freq / 1e6)
    )

    if _device_idx is None:
        logging.error("Could not allocate SDR for decoder!")
        return
    else:
        # Add an entry to the task list
        autorx.task_list[freq] = {"device_idx": _device_idx, "task": None}

        # Set the SDR to in-use
        autorx.sdr_list[_device_idx]["in_use"] = True

        if sonde_type.startswith("-"):
            _exp_sonde_type = sonde_type[1:]
        else:
            _exp_sonde_type = sonde_type

        if continuous:
            _timeout = 3600*6 # 6 hours before a 'continuous' decoder gets restarted automatically.
        else:
            _timeout = config["rx_timeout"]

        # Initialise a decoder.
        autorx.task_list[freq]["task"] = SondeDecoder(
            sonde_type=sonde_type,
            sonde_freq=freq,
            rs_path=RS_PATH,
            sdr_type=config["sdr_type"],
            # Network SDR Options
            sdr_hostname=config["sdr_hostname"],
            sdr_port=config["sdr_port"],
            ss_iq_path=config["ss_iq_path"],
            # RTLSDR Options
            rtl_fm_path=config["sdr_fm"],
            rtl_device_idx=_device_idx,
            gain=autorx.sdr_list[_device_idx]["gain"],
            ppm=autorx.sdr_list[_device_idx]["ppm"],
            bias=autorx.sdr_list[_device_idx]["bias"],
            # Other options
            save_decode_audio=config["save_decode_audio"],
            save_decode_iq=config["save_decode_iq"],
            exporter=exporter_functions,
            timeout=_timeout,
            telem_filter=telemetry_filter,
            rs92_ephemeris=rs92_ephemeris,
            rs41_drift_tweak=config["rs41_drift_tweak"],
            experimental_decoder=config["experimental_decoders"][_exp_sonde_type],
            save_raw_hex=config["save_raw_hex"],
            wideband_sondes=config["wideband_sondes"]
        )
        autorx.sdr_list[_device_idx]["task"] = autorx.task_list[freq]["task"]

    # Indicate to the web client that the task list has been updated.
    flask_emit_event("task_event")


def handle_scan_results():
    """Read in Scan results via the scan results Queue.

    Depending on how many SDRs are available, two things can happen:
    - If there is a free SDR, allocate it to a decoder.
    - If there is no free SDR, but a scanner is running, stop the scanner and start decoding.
    """
    global config, temporary_block_list

    if autorx.scan_results.qsize() > 0:
        # Grab the latest detections from the scan result queue.
        _scan_data = autorx.scan_results.get()
        for _sonde in _scan_data:
            # Extract frequency & type info
            _freq = _sonde[0]
            _type = _sonde[1]

            if _freq in autorx.task_list:
                # Already decoding this sonde, continue.
                continue
            else:

                # Handle an inverted sonde detection.
                if _type.startswith("-"):
                    _inverted = " (Inverted)"
                    _check_type = _type[1:]
                else:
                    _check_type = _type
                    _inverted = ""

                # Note: We don't indicate if it's been detected as inverted here.
                logging.info(
                    "Task Manager - Detected new %s sonde on %.3f MHz!"
                    % (_check_type, _freq / 1e6)
                )

                # Break if we don't support this sonde type.
                if _check_type not in VALID_SONDE_TYPES:
                    logging.warning(
                        "Task Manager - Unsupported sonde type: %s" % _check_type
                    )
                    # TODO - Potentially add the frequency of the unsupported sonde to the temporary block list?
                    continue

                # Check that we are not attempting to start a decoder too close to an existing decoder for known 'drifty' radiosonde types.
                # 'Too close' is defined by the 'decoder_spacing_limit' advanced coniguration option.
                _too_close = False
                for _key in autorx.task_list.keys():
                    # Iterate through the task list, and only attempt to compare with those that are a decoder task.
                    # This is indicated by the task key being an integer (the sonde frequency).
                    if (type(_key) == int) or (type(_key) == float):
                        # Extract the currently decoded sonde type from the currently running decoder.
                        _decoding_sonde_type = autorx.task_list[_key]["task"].sonde_type

                        # Remove any inverted decoder information for the comparison.
                        if _decoding_sonde_type.startswith("-"):
                            _decoding_sonde_type = _decoding_sonde_type[1:]

                        # Only check the frequency spacing if we have a known 'drifty' sonde type, *and* the new sonde type is of the same type.
                        if (_decoding_sonde_type in DRIFTY_SONDE_TYPES) and (
                            _decoding_sonde_type == _check_type
                        ):
                            if abs(_key - _freq) < config["decoder_spacing_limit"]:
                                # At this point, we can be pretty sure that there is another decoder already decoding this particular sonde ID.
                                # Without actually starting another decoder and matching IDs, we can't be 100% sure, but it's a good chance.
                                logging.info(
                                    "Task Manager - Detected %s sonde on %.3f MHz, but this is within %d kHz of an already running decoder. (This limit can be set using the 'decoder_spacing_limit' advanced config option.)"
                                    % (
                                        _type,
                                        _freq / 1e6,
                                        config["decoder_spacing_limit"] / 1e3,
                                    )
                                )
                                _too_close = True
                                continue

                # Continue to the next scan result if this one is too close to a currently running decoder.
                if _too_close:
                    continue

                # Check the frequency is not in our temporary block list
                # (This may happen from time-to-time depending on the timing of the scan thread)
                if _freq in temporary_block_list.keys():
                    if temporary_block_list[_freq] > (
                        time.time() - config["temporary_block_time"] * 60
                    ):
                        logging.warning(
                            "Task Manager - Attempted to start a decoder on a temporarily blocked frequency (%.3f MHz)"
                            % (_freq / 1e6)
                        )
                        continue
                    else:
                        # This frequency should not be blocked any more, remove it from the block list.
                        logging.info(
                            "Task Manager - Removed %.3f MHz from temporary block list."
                            % (_freq / 1e6)
                        )
                        temporary_block_list.pop(_freq)


                if allocate_sdr(check_only=True) is not None:
                    # There is a SDR free! Start the decoder on that SDR
                    start_decoder(_freq, _type)

                elif (allocate_sdr(check_only=True) is None) and (
                    "SCAN" in autorx.task_list
                ):
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
    """Check the task list to see if any tasks have stopped running. If so, release the associated SDR"""

    for _key in autorx.task_list.copy().keys():
        # Attempt to get the state of the task
        try:
            _running = autorx.task_list[_key]["task"].running()
            _task_sdr = autorx.task_list[_key]["device_idx"]
            _exit_state = autorx.task_list[_key]["task"].exit_state
        except Exception as e:
            logging.error(
                "Task Manager - Error getting task %s state - %s" % (str(_key), str(e))
            )
            continue

        if _running == False:
            # This task has stopped.
            # Check the exit state of the task for any abnormalities:
            if (_exit_state == "Encrypted") or (_exit_state == "TempBlock"):
                # This task was a decoder, and it has encountered an encrypted sonde, or one too far away.
                logging.info(
                    "Task Manager - Adding temporary block for frequency %.3f MHz"
                    % (_key / 1e6)
                )
                # Add the sonde's frequency to the global temporary block-list
                temporary_block_list[_key] = time.time()
                # If there is a scanner currently running, add it to the scanners internal block list.
                if "SCAN" in autorx.task_list:
                    autorx.task_list["SCAN"]["task"].add_temporary_block(_key)

            if _exit_state == "FAILED SDR":
                # The SDR was not able to be recovered after many attempts.
                # Remove it from the SDR list and flag an error.
                autorx.sdr_list.pop(_task_sdr)
                _error_msg = (
                    "Task Manager - Removed SDR %s from SDR list due to repeated failures."
                    % (str(_task_sdr))
                )
                logging.error(_error_msg)

                # Send email if configured.
                email_error(_error_msg)

            else:
                # Shutdown the SDR, if required for the particular SDR type.
                if _key != 'SCAN':
                    shutdown_sdr(config["sdr_type"], _task_sdr, sdr_hostname=config["sdr_hostname"], frequency=_key)
                # Release its associated SDR.
                autorx.sdr_list[_task_sdr]["in_use"] = False
                autorx.sdr_list[_task_sdr]["task"] = None

            # Pop the task from the task list.
            autorx.task_list.pop(_key)
            # Indicate to the web client that the task list has been updated.
            flask_emit_event("task_event")

    # Clean out the temporary block list of old entries.
    for _freq in temporary_block_list.copy().keys():
        if temporary_block_list[_freq] < (
            time.time() - config["temporary_block_time"] * 60
        ):
            temporary_block_list.pop(_freq)
            logging.info(
                "Task Manager - Removed %.3f MHz from temporary block list."
                % (_freq / 1e6)
            )

    # Check if there is a scanner thread still running.
    # If not, and if there is a SDR free, start one up again.
    # Also check for a global scan inhibit flag.
    if (
        ("SCAN" not in autorx.task_list)
        and (not autorx.scan_inhibit)
        and (allocate_sdr(check_only=True) is not None)
    ):
        # We have a SDR free, and we are not running a scan thread. Start one.
        start_scanner()

    # Always-on decoders.
    if len(config["always_decode"]) > 0:
        for _entry in config["always_decode"]:
            try:
                _freq_hz = float(_entry[0])*1e6
                _type = str(_entry[1])
            except:
                logging.warning(f"Task Manager - Invalid entry found in always_decode list, skipping.")
                continue

            if _freq_hz in autorx.task_list:
                # Already running a decoder here.
                continue
            else:
                # Try and start up a decoder.
                if (allocate_sdr(check_only=True) is not None):
                    logging.info(f"Task Manager - Starting Always-On Decoder: {_type}, {_freq_hz/1e6:.3f} MHz")
                    start_decoder(_freq_hz, _type, continuous=True)



def stop_all():
    """Shut-down all decoders, scanners, and exporters."""
    global exporter_objects
    logging.info("Starting shutdown of all threads.")
    for _task in autorx.task_list.keys():
        try:
            autorx.task_list[_task]["task"].stop()

            # Release the SDR channel if necessary
            _task_sdr = autorx.task_list[_task]["device_idx"]
            if _task != 'SCAN':
                shutdown_sdr(config["sdr_type"], _task_sdr, sdr_hostname=config["sdr_hostname"], frequency=_task)

        except Exception as e:
            logging.error("Error stopping task - %s" % str(e))

    for _exporter in exporter_objects:
        try:
            _exporter.close()
        except Exception as e:
            logging.error("Error stopping exporter - %s" % str(e))

    if gpsd_adaptor != None:
        gpsd_adaptor.close()


def telemetry_filter(telemetry):
    """Filter incoming radiosonde telemetry based on various factors,
        - Invalid Position
        - Invalid Altitude
        - Abnormal range from receiver.
        - Invalid serial number.
        - Abnormal date (more than 6 hours from utcnow)

    This function is defined within this script to avoid passing around large amounts of configuration data.

    """
    global config

    # First Check: zero lat/lon
    if (telemetry["lat"] == 0.0) and (telemetry["lon"] == 0.0):
        logging.warning(
            "Zero Lat/Lon. Sonde %s does not have GPS lock." % telemetry["id"]
        )
        return False

    # Second check: Altitude cap.
    if telemetry["alt"] > config["max_altitude"]:
        _altitude_breach = telemetry["alt"] - config["max_altitude"]
        logging.warning(
            "Sonde %s position breached altitude cap by %d m."
            % (telemetry["id"], _altitude_breach)
        )
        return False

    # Third check: Number of satellites visible.
    if "sats" in telemetry:
        if telemetry["sats"] < 4:
            logging.warning(
                "Sonde %s can only see %d GNSS sats - discarding position as bad."
                % (telemetry["id"], telemetry["sats"])
            )
            return False

    # Fourth check - is the payload more than x km from our listening station.
    # Only run this check if a station location has been provided.
    if (config["station_lat"] != 0.0) and (config["station_lon"] != 0.0):
        # Calculate the distance from the station to the payload.
        _listener = (
            config["station_lat"],
            config["station_lon"],
            config["station_alt"],
        )
        _payload = (telemetry["lat"], telemetry["lon"], telemetry["alt"])
        # Calculate using positon_info function from rotator_utils.py
        _info = position_info(_listener, _payload)

        if _info["straight_distance"] > config["max_radius_km"] * 1000:
            _radius_breach = (
                _info["straight_distance"] / 1000.0 - config["max_radius_km"]
            )
            logging.warning(
                "Sonde %s position breached radius cap by %.1f km."
                % (telemetry["id"], _radius_breach)
            )

            if config["radius_temporary_block"]:
                logging.warning(
                    "Blocking for %d minutes." % config["temporary_block_time"]
                )
                return "TempBlock"
            else:
                return False

        if (_info["straight_distance"] < config["min_radius_km"] * 1000) and config[
            "radius_temporary_block"
        ]:
            logging.warning(
                "Sonde %s within minimum radius limit (%.1f km). Blocking for %d minutes."
                % (
                    telemetry["id"],
                    config["min_radius_km"],
                    config["temporary_block_time"],
                )
            )
            return "TempBlock"

    # DateTime Check
    _delta_time = (
        datetime.datetime.now(datetime.timezone.utc) - parse(telemetry["datetime"])
    ).total_seconds()
    logging.debug("Delta time: %d" % _delta_time)

    if abs(_delta_time) > (3600 * config["sonde_time_threshold"]):
        logging.warning(
            "Sonde reported time too far from current UTC time. Either sonde time or system time is invalid. (Threshold: %d hours)"
            % config["sonde_time_threshold"]
        )
        return False

    # Payload Serial Number Checks
    _serial = telemetry["id"]
    # Run a Regex to match known Vaisala RS92/RS41 serial numbers (YWWDxxxx)
    # RS92: https://www.vaisala.com/sites/default/files/documents/Vaisala%20Radiosonde%20RS92%20Serial%20Number.pdf
    # RS41: https://www.vaisala.com/sites/default/files/documents/Vaisala%20Radiosonde%20RS41%20Serial%20Number.pdf
    # This will need to be re-evaluated if we're still using this code in 2021!
    # UPDATE: Had some confirmation that Vaisala will continue to use the alphanumeric numbering up until
    # ~2025-2030, so have expanded the regex to match (and also support some older RS92s)
    # Modified 2021-06 to be more flexible and match older sondes, and reprogrammed sondes.
    # Still needs a letter at the start, but the numbers don't need to match the format exactly.
    vaisala_callsign_valid = re.match(r"[C-Z][\d][\d][\d]\d{4}", _serial)

    # Just make sure we're not getting the 'xxxxxxxx' unknown serial from the DFM decoder.
    # Also applies to PS15 sondes.
    if "DFM" in telemetry["type"] or "PS15" in telemetry["type"]:
        dfm_callsign_valid = "x" not in _serial.split("-")[1]
    else:
        dfm_callsign_valid = False

    # Check Meisei sonde callsigns for validity.
    # meisei_ims returns a callsign of IMS100-xxxxxx until it receives the serial number, so we filter based on the x's being present or not.
    if "MEISEI" in telemetry["type"] or "IMS100" in telemetry["type"] or "RS11G" in telemetry["type"]:
        meisei_callsign_valid = "x" not in _serial.split("-")[1]
    else:
        meisei_callsign_valid = False

    if "MRZ" in telemetry["type"]:
        mrz_callsign_valid = "x" not in _serial.split("-")[1]
    else:
        mrz_callsign_valid = False

    # If Vaisala or DFMs, check the callsigns are valid. If M10/M20, iMet, MTS01 or LMS6, just pass it through - we get callsigns immediately and reliably from these.
    if (
        vaisala_callsign_valid
        or dfm_callsign_valid
        or meisei_callsign_valid
        or mrz_callsign_valid
        or ("M10" in telemetry["type"])
        or ("M20" in telemetry["type"])
        or ("LMS" in telemetry["type"])
        or ("IMET" in telemetry["type"])
        or ("MTS01" in telemetry["type"])
        or ("WXR" in telemetry["type"])
    ):
        return "OK"
    else:
        _id_msg = "Payload ID %s is invalid." % telemetry["id"]
        # Add in a note about DFM sondes and their oddness...
        if "DFM" in telemetry["id"]:
            _id_msg += " Note: DFM sondes may take a while to get an ID."

        if "MRZ" in telemetry["id"]:
            _id_msg += " Note: MRZ sondes may take a while to get an ID."

        logging.warning(_id_msg)
        return False


def station_position_update(position):
    """Handle a callback from GPSDAdaptor object, and update each exporter object."""
    global exporter_objects
    # Quick sanity check of the incoming data
    if "valid" not in position:
        return

    for _exporter in exporter_objects:
        try:
            _exporter.update_station_position(
                position["latitude"], position["longitude"], position["altitude"]
            )
        except AttributeError:
            # This exporter does not require station position data.
            pass
        except Exception as e:
            traceback.print_exc()
            logging.error("Error updating exporter station position.")


def email_error(message="foo"):
    """Helper function to email an error message, if the email exporter is available"""
    global email_exporter

    if email_exporter and config["email_error_notifications"]:
        try:
            email_exporter.send_notification_email(message=message)
        except Exception as e:
            logging.error("Error attempting to send notification email: %s" % str(e))
    else:
        logging.debug("Not sending Email notification, as Email not configured.")

def main():
    """Main Loop"""
    global config, exporter_objects, exporter_functions, logging_level, rs92_ephemeris, gpsd_adaptor, email_exporter

    # Command line arguments.
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-c",
        "--config",
        default="station.cfg",
        help="Receive Station Configuration File. Default: station.cfg",
    )
    parser.add_argument(
        "-l",
        "--log",
        default="./log/",
        help="Receive Station Log Path. Default: ./log/",
    )
    parser.add_argument(
        "-f",
        "--frequency",
        type=float,
        default=0.0,
        help="Sonde Frequency Override (MHz). This overrides the only_scan list with the supplied frequency.",
    )
    parser.add_argument(
        "-m",
        "--type",
        type=str,
        default=None,
        help="Immediately start a decoder for a provided sonde type (Valid Types: RS41, RS92, DFM, M10, M20, IMET, IMET5, LMS6, MK2LMS, MEISEI, MRZ)",
    )
    parser.add_argument(
        "-t",
        "--timeout",
        type=int,
        default=0,
        help="Close auto_rx system after N minutes. Use 0 to run continuously.",
    )
    parser.add_argument(
        "-v", "--verbose", help="Enable debug output.", action="store_true"
    )
    parser.add_argument(
        "-e",
        "--ephemeris",
        type=str,
        default="None",
        help="Use a manually obtained ephemeris file when decoding RS92 Sondes.",
    )
    parser.add_argument(
        "--systemlog",
        action="store_true",
        default=False,
        help="Write a auto_rx system log-file to ./log/ (default=False)",
    )
    args = parser.parse_args()

    # Copy out RS92 ephemeris value, if provided.
    if args.ephemeris != "None":
        rs92_ephemeris = args.ephemeris

    # Set log-level to DEBUG if requested
    if args.verbose:
        logging_level = logging.DEBUG

    # Define the default logging path
    logging_path = "./log/"

    # Validate the user supplied log path
    if os.path.isdir(args.log):
        logging_path = os.path.abspath(args.log)
    else:
        # Using print because logging may not be established yet
        print("Invalid logging path, using default. Does the folder exist?")

    # Update Global logging path, used by other modules.
    autorx.logging_path = logging_path

    # Configure logging
    _log_suffix = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S_system.log")
    _log_path = os.path.join(logging_path, _log_suffix)

    system_log_enabled = False

    if args.systemlog:
        # Only write out a logs to a system log file if we have been asked to.
        # Systemd will capture and logrotate our logs anyway, so writing to our own log file is less useful.
        logging.basicConfig(
            format="%(asctime)s %(levelname)s:%(message)s",
            filename=_log_path,
            level=logging_level,
        )
        logging.info("Opened new system log file: %s" % _log_path)
        # Also add a separate stdout logger.
        stdout_format = logging.Formatter("%(asctime)s %(levelname)s:%(message)s")
        stdout_handler = logging.StreamHandler(sys.stdout)
        stdout_handler.setFormatter(stdout_format)
        logging.getLogger().addHandler(stdout_handler)
        system_log_enabled = True
    else:
        # Otherwise, we only need the stdout logger, which if we don't specify a filename to logging.basicConfig,
        # is the default...
        logging.basicConfig(
            format="%(asctime)s %(levelname)s:%(message)s", level=logging_level
        )


    # Set the requests/socketio loggers (and related) to only display critical log messages.
    logging.getLogger("requests").setLevel(logging.CRITICAL)
    logging.getLogger("urllib3").setLevel(logging.CRITICAL)
    logging.getLogger("werkzeug").setLevel(logging.ERROR)
    logging.getLogger("socketio").setLevel(logging.ERROR)
    logging.getLogger("engineio").setLevel(logging.ERROR)
    logging.getLogger("geventwebsocket").setLevel(logging.ERROR)

    # Copy out timeout value, and convert to seconds.
    if args.timeout > 0:
        logging.info(f"Will shut down automatically after {args.timeout} minutes.")
    _timeout = args.timeout * 60

    # Check all the RS utilities exist.
    logging.debug("Checking if required binaries exist")
    if not check_rs_utils(config):
        sys.exit(1)

    # Attempt to read in config file
    logging.info("Reading configuration file...")
    _temp_cfg = read_auto_rx_config(args.config)
    if _temp_cfg is None:
        logging.critical("Error in configuration file! Exiting...")
        sys.exit(1)
    else:
        config = _temp_cfg
        autorx.sdr_list = config["sdr_settings"]


    # Apply any logging changes based on configuration file settings.
    if config["save_system_log"]:
        # Enable system logging.
        if system_log_enabled == False:
            # Clear all existing handlers, and add new ones.
            logging.basicConfig(
                format="%(asctime)s %(levelname)s:%(message)s",
                filename=_log_path,
                level=logging_level,
                force=True # This removes all existing handlers before adding new ones.
            )
            # Also add a separate stdout logger.
            stdout_format = logging.Formatter("%(asctime)s %(levelname)s:%(message)s")
            stdout_handler = logging.StreamHandler(sys.stdout)
            stdout_handler.setFormatter(stdout_format)
            logging.getLogger().addHandler(stdout_handler)
            system_log_enabled = True
            logging.info("Opened new system log file: %s" % _log_path)

    if config["enable_debug_logging"]:
        # Set log level to logging.DEBUG
        logging.getLogger().setLevel(logging.DEBUG)
        logging.debug("Log level set to DEBUG based on configuration file setting.")

    # Add the web interface logging handler.
    web_handler = WebHandler()
    logging.getLogger().addHandler(web_handler)


    # If a sonde type has been provided, insert an entry into the scan results,
    # and immediately start a decoder. This also sets the decoder time to 0, which
    # allows it to run indefinitely.
    if args.type != None:
        if args.type in VALID_SONDE_TYPES:
            logging.warning(
                "Overriding RX timeout for manually specified radiosonde type. Decoders will not automatically stop!"
            )
            config["rx_timeout"] = 0
            autorx.scan_results.put([[args.frequency * 1e6, args.type]])
        else:
            logging.error("Unknown Radiosonde Type: %s. Exiting." % args.type)
            sys.exit(1)

    # Start up the flask server.
    # This needs to occur AFTER logging is setup, else logging breaks horribly for some reason.
    start_flask(host=config["web_host"], port=config["web_port"])

    # If we have been supplied a frequency via the command line, override the only_scan list settings
    # to only include the supplied frequency.
    if args.frequency != 0.0:
        config["only_scan"] = [args.frequency]

    # Start our exporter options
    # Telemetry Logger
    if config["per_sonde_log"]:
        _logger = TelemetryLogger(
            log_directory=logging_path,
            save_cal_data=config["save_cal_data"]
        )
        exporter_objects.append(_logger)
        exporter_functions.append(_logger.add)

    if config["email_enabled"]:

        _email_notification = EmailNotification(
            smtp_server=config["email_smtp_server"],
            smtp_port=config["email_smtp_port"],
            smtp_authentication=config["email_smtp_authentication"],
            smtp_login=config["email_smtp_login"],
            smtp_password=config["email_smtp_password"],
            mail_from=config["email_from"],
            mail_to=config["email_to"],
            mail_subject=config["email_subject"],
            mail_nearby_landing_subject=config["email_nearby_landing_subject"],
            station_position=(
                config["station_lat"],
                config["station_lon"],
                config["station_alt"],
            ),
            launch_notifications=config["email_launch_notifications"],
            landing_notifications=config["email_landing_notifications"],
            encrypted_sonde_notifications=config["email_encrypted_sonde_notifications"],
            landing_range_threshold=config["email_landing_range_threshold"],
            landing_altitude_threshold=config["email_landing_altitude_threshold"],
        )
        email_exporter = _email_notification

        exporter_objects.append(_email_notification)
        exporter_functions.append(_email_notification.add)

    # APRS Uploader
    if config["aprs_enabled"]:

        if (config["aprs_object_id"] == "<id>") or (
            config["aprs_use_custom_object_id"] == False
        ):
            _aprs_object = None
        else:
            _aprs_object = config["aprs_object_id"]

        _aprs = APRSUploader(
            aprs_callsign=config["aprs_user"],
            aprs_passcode=config["aprs_pass"],
            object_name_override=_aprs_object,
            object_comment=config["aprs_custom_comment"],
            position_report=config["aprs_position_report"],
            aprsis_host=config["aprs_server"],
            aprsis_port=config["aprs_port"],
            upload_time=config["aprs_upload_rate"],
            callsign_validity_threshold=config["payload_id_valid"],
            station_beacon=config["station_beacon_enabled"],
            station_beacon_rate=config["station_beacon_rate"],
            station_beacon_position=(
                config["station_lat"],
                config["station_lon"],
                config["station_alt"],
            ),
            station_beacon_comment=config["station_beacon_comment"],
            station_beacon_icon=config["station_beacon_icon"],
        )

        exporter_objects.append(_aprs)
        exporter_functions.append(_aprs.add)

    # OziExplorer
    if config["ozi_enabled"] or config["payload_summary_enabled"]:
        if config["ozi_host"]:
            _ozi_host = config["ozi_host"]
        else:
            _ozi_host = None

        if config["ozi_enabled"]: # Causes port to be set to None which disables the export.
            _ozi_port = config["ozi_port"]
        else:
            _ozi_port = None

        if config["payload_summary_host"]:
            _summary_host = config["payload_summary_host"]
        else:
            _summary_host = None

        if config["payload_summary_enabled"]: # Causes port to be set to None which disables the export.
            _summary_port = config["payload_summary_port"]
        else:
            _summary_port = None

        _ozimux = OziUploader(
            ozimux_host=_ozi_host,
            ozimux_port=_ozi_port,
            payload_summary_host=_summary_host,
            payload_summary_port=_summary_port,
            update_rate=config["ozi_update_rate"],
            station=config["habitat_uploader_callsign"],
        )

        exporter_objects.append(_ozimux)
        exporter_functions.append(_ozimux.add)

    # Rotator
    if config["rotator_enabled"]:
        _rotator = Rotator(
            station_position=(
                config["station_lat"],
                config["station_lon"],
                config["station_alt"],
            ),
            rotctld_host=config["rotator_hostname"],
            rotctld_port=config["rotator_port"],
            rotator_update_rate=config["rotator_update_rate"],
            rotator_update_threshold=config["rotation_threshold"],
            rotator_homing_enabled=config["rotator_homing_enabled"],
            rotator_homing_delay=config["rotator_homing_delay"],
            rotator_home_position=[
                config["rotator_home_azimuth"],
                config["rotator_home_elevation"],
            ],
            azimuth_only=config["rotator_azimuth_only"]
        )

        exporter_objects.append(_rotator)
        exporter_functions.append(_rotator.add)

        autorx.rotator_object = _rotator

    # Sondehub v2 Database
    if config["sondehub_enabled"]:
        if config["habitat_upload_listener_position"] is False:
            _sondehub_station_position = None
        else:
            _sondehub_station_position = (
                config["station_lat"],
                config["station_lon"],
                config["station_alt"],
            )

        _sondehub = SondehubUploader(
            user_callsign=config["habitat_uploader_callsign"],
            user_position=_sondehub_station_position,
            user_antenna=config["habitat_uploader_antenna"],
            contact_email=config["sondehub_contact_email"],
            upload_rate=config["sondehub_upload_rate"],
        )

        exporter_objects.append(_sondehub)
        exporter_functions.append(_sondehub.add)

    _web_exporter = WebExporter(max_age=config["web_archive_age"])
    exporter_objects.append(_web_exporter)
    exporter_functions.append(_web_exporter.add)

    # GPSD Startup
    if config["gpsd_enabled"]:
        gpsd_adaptor = GPSDAdaptor(
            hostname=config["gpsd_host"],
            port=config["gpsd_port"],
            callback=station_position_update,
        )

    version_startup_check()

    # Note the start time.
    _start_time = time.time()

    # If we have been asked to start decoding a specific radiosonde type, we need to start up
    # the decoder immediately, before a scanner thread is started.
    if args.type != None:
        handle_scan_results()

    # Loop.
    while True:
        # Check for finished tasks.
        clean_task_list()
        # Handle any new scan results.
        handle_scan_results()
        # Sleep a little bit.
        time.sleep(2)

        if len(autorx.sdr_list) == 0:
            # No Functioning SDRs!
            logging.critical("Task Manager - No SDRs available! Cannot continue...")
            email_error("auto_rx exited due to all SDRs being marked as failed.")
            raise IOError("No SDRs available!")

        # Allow a timeout after a set time, for users who wish to run auto_rx
        # within a cronjob.
        if (_timeout > 0) and ((time.time() - _start_time) > _timeout):
            logging.info("Shutdown time reached. Closing.")
            stop_flask(host=config["web_host"], port=config["web_port"])
            stop_all()
            sys.exit(2)


if __name__ == "__main__":

    try:
        main()
    except KeyboardInterrupt:
        # Upon CTRL+C, shutdown all threads and exit.
        stop_flask(host=config["web_host"], port=config["web_port"])
        stop_all()
        sys.exit(0)
    except Exception as e:
        # Upon exceptions, attempt to shutdown threads and exit.
        traceback.print_exc()
        print("Main Loop Error - %s" % str(e))
        if flask_running():
            stop_flask(host=config["web_host"], port=config["web_port"])
        stop_all()
        sys.exit(3)

