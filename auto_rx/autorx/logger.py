#!/usr/bin/env python
#
#   radiosonde_auto_rx - Sonde Telemetry Logger
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
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
    REQUIRED_FIELDS = ['frame', 'id', 'datetime', 'lat', 'lon', 'alt', 'temp', 'type', 'freq', 'datetime_dt']

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


    def write_telemetry(self, telemetry):
        """ Write a packet of telemetry to a log file.

        Args:
            telemetry (dict): Telemetry dictionary to process.
        """

        # TODO
        print(telemetry)

        # Check to see if we already have an open log file for this sonde ID.

        # If not, check to see if there is an existing log file for this sonde ID, and open it.

        # Otherside, create a new log file, and open it.


        # Produce log file sentence.

        # Write out to log.



    def cleanup_logs(self):
        """ Close any open logs that have not had telemetry added in X seconds. """

        _now = time.time()

        for _id in self.open_logs.keys():
            try:
                if _now > (self.open_logs[_id]['last_time'] + self.FILE_ACTIVITY_TIMEOUT):
                    # Flush and close the log file, and pop this element from the dictionary.
                    self.open_logs[_id]['log'].flush()
                    self.open_logs[_id]['log'].close()
                    self.open_logs.pop(_id, None)
                    self.log_debug("Closed log file for %s" % _id)
            except Exception as e:
                self.log_error("Error closing log for %s - %s" % (_id, str(e)))



    def close(self):
        """ Close input processing thread. """
        self.input_processing_running = False


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
