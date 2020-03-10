#!/usr/bin/env python
#
#   radiosonde_auto_rx - Sonde Telemetry Logger
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import datetime
import glob
import logging
import os
import time
from threading import Thread
try:
    # Python 2
    from Queue import Queue
except ImportError:
    # Python 3
    from queue import Queue



class TelemetryLogger(object):
    """ Radiosonde Telemetry Logger Class.

    Accepts telemetry dictionaries from a decoder, and writes out to log files, one per sonde ID.
    Incoming telemetry is processed via a queue, so this object should be thread safe.

    Log files are written to filenames of the form YYYYMMDD-HHMMSS_<id>_<type>_<freq_mhz>_sonde.log

    """

    # Close any open file handles after X seconds of no activity.
    # This will help avoid having lots of file handles open for a long period of time if we are handling telemetry
    # from multiple sondes.
    FILE_ACTIVITY_TIMEOUT = 30

    # We require the following fields to be present in the input telemetry dict.
    REQUIRED_FIELDS = ['frame', 'id', 'datetime', 'lat', 'lon', 'alt', 'temp', 'humidity', 'type', 'freq', 'datetime_dt']

    def __init__(self,
        log_directory = "./log"):
        """ Initialise and start a sonde logger.
        
        Args:
            log_directory (str): Directory in which to save log files.

        """

        self.log_directory = log_directory

        # Dictionary to contain file handles.
        # Each sonde id is added as a unique key. Under each key are the contents:
        # 'log' (file): Open file object.
        # 'last_time' (float): Timestamp of the last time telemetry was added to the open log.
        self.open_logs = {}

        # Input Queue.
        self.input_queue = Queue()


        # Start queue processing thread.
        self.input_processing_running = True
        self.log_process_thread = Thread(target=self.process_queue)
        self.log_process_thread.start()


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


    def process_queue(self):
        """ Process data from the input queue, and write telemetry to log files.
        """
        self.log_info("Started Telemetry Logger Thread.")

        while self.input_processing_running:

            # Process everything in the queue.
            while self.input_queue.qsize() > 0:
                try:
                    _telem = self.input_queue.get_nowait()
                    self.write_telemetry(_telem)
                except Exception as e:
                    self.log_error("Error processing telemetry dict - %s" % str(e))

            # Close any un-needed log handlers.
            self.cleanup_logs()

            # Sleep while waiting for some new data.
            time.sleep(0.5)

        self.log_info("Stopped Telemetry Logger Thread.")


    def telemetry_to_string(self, telemetry):
        """ Convert a telemetry dictionary to a CSV string.

        Args:
            telemetry (dict): Telemetry dictionary to process.
        """
        _log_line = "%s,%s,%d,%.5f,%.5f,%.1f,%.1f,%.1f,%s,%.3f" % (
            telemetry['datetime'],
            telemetry['id'],
            telemetry['frame'],
            telemetry['lat'],
            telemetry['lon'],
            telemetry['alt'],
            telemetry['temp'],
            telemetry['humidity'],
            telemetry['type'],
            telemetry['freq_float'])

        # Other fields that may not always be present.
        if 'snr' in telemetry:
            _log_line += ",SNR %.1f" % telemetry['snr']

        if 'f_error' in telemetry:
            _log_line += ",FERROR %d" % int(telemetry['f_error'])

        if 'sats' in telemetry:
            _log_line += ",SATS %d" % telemetry['sats']

        if 'batt' in telemetry:
            _log_line += ",BATT %.1f" % telemetry['batt']

        # Check for Burst/Kill timer data, and add in.
        if 'bt' in telemetry:
            if (telemetry['bt'] != -1) and (telemetry['bt'] != 65535):
                _log_line += ",BT %s" % time.strftime("%H:%M:%S", time.gmtime(telemetry['bt']))

        # Add Aux data, if it exists.
        if 'aux' in telemetry:
            _log_line += ",AUX %s" % telemetry['aux'].strip()

        if 'vel_v' in telemetry:
            _log_line += ",vel_v %.1f" % telemetry['vel_v']

        if 'vel_h' in telemetry:
            _log_line += ",vel_h %.1f" % telemetry['vel_h']

        # Terminate the log line.
        _log_line += "\n"

        return _log_line

    def write_telemetry(self, telemetry):
        """ Write a packet of telemetry to a log file.

        Args:
            telemetry (dict): Telemetry dictionary to process.
        """

        _id = telemetry['id']
        _type = telemetry['type']


        # If there is no log open for the current ID check to see if there is an existing (closed) log file, and open it.
        if _id not in self.open_logs:
            _search_string = os.path.join(self.log_directory, "*%s_*_sonde.log" % (_id))
            _existing_files = glob.glob(_search_string)
            if len(_existing_files) != 0:
                # Open the existing log file.
                _log_file_name = _existing_files[0]
                self.log_info("Using existing log file: %s" % _log_file_name)
                # Create entry in open logs dictionary
                self.open_logs[_id] = {'log':open(_log_file_name,'a'), 'last_time':time.time()}
            else:
                # Create a new log file.
                _log_suffix = "%s_%s_%s_%d_sonde.log" % (
                    datetime.datetime.utcnow().strftime("%Y%m%d-%H%M%S"),
                    _id,
                    _type,
                    int(telemetry['freq_float']*1e3) # Convert frequency to kHz
                    )
                _log_file_name = os.path.join(self.log_directory, _log_suffix)
                self.log_info("Opening new log file: %s" % _log_file_name)
                # Create entry in open logs dictionary
                self.open_logs[_id] = {'log':open(_log_file_name,'a'), 'last_time':time.time()}               


        # Produce log file sentence.
        _log_line = self.telemetry_to_string(telemetry)

        # Write out to log.
        self.open_logs[_id]['log'].write(_log_line)
        self.open_logs[_id]['log'].flush()
        # Update the last_time field.
        self.open_logs[_id]['last_time'] = time.time()
        self.log_debug("Wrote line: %s" % _log_line.strip())


    def cleanup_logs(self):
        """ Close any open logs that have not had telemetry added in X seconds. """

        _now = time.time()

        for _id in self.open_logs.copy().keys():
            try:
                if _now > (self.open_logs[_id]['last_time'] + self.FILE_ACTIVITY_TIMEOUT):
                    # Flush and close the log file, and pop this element from the dictionary.
                    self.open_logs[_id]['log'].flush()
                    self.open_logs[_id]['log'].close()
                    self.open_logs.pop(_id, None)
                    self.log_info("Closed log file for %s" % _id)
            except Exception as e:
                self.log_error("Error closing log for %s - %s" % (_id, str(e)))



    def close(self):
        """ Close input processing thread. """
        self.input_processing_running = False


    def running(self):
        """ Check if the logging thread is running. 

        Returns:
            bool: True if the logging thread is running.
        """
        return self.input_processing_running


    def log_debug(self, line):
        """ Helper function to log a debug message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.debug("Telemetry Logger - %s" % line)


    def log_info(self, line):
        """ Helper function to log an informational message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.info("Telemetry Logger - %s" % line)


    def log_error(self, line):
        """ Helper function to log an error message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.error("Telemetry Logger - %s" % line)


if __name__ == "__main__":
    # Test Script
    pass
