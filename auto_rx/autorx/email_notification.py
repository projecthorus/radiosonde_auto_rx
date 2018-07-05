#!/usr/bin/env python
#
#   radiosonde_auto_rx - Email Notification
#
#   Copyright (C) 2018 Philip Heron <phil@sanslogic.co.uk>
#   Released under GNU GPL v3 or later

import logging
import time
import smtplib
from email.mime.text import MIMEText
from email.utils import formatdate
from threading import Thread

try:
    # Python 2
    from Queue import Queue

except ImportError:
    # Python 3
    from queue import Queue


class EmailNotification(object):
    """ Radiosonde Email Notification Class.

    Accepts telemetry dictionaries from a decoder, and sends an email on newly detected sondes.
    Incoming telemetry is processed via a queue, so this object should be thread safe.

    """

    # We require the following fields to be present in the input telemetry dict.
    REQUIRED_FIELDS = [ 'id', 'lat', 'lon', 'alt', 'type', 'freq']

    def __init__(self, smtp_server = 'localhost', mail_from = None, mail_to = None):
        """ Init a new E-Mail Notification Thread """
        self.smtp_server = smtp_server
        self.mail_from = mail_from
        self.mail_to = mail_to

        # Dictionary to track sonde IDs
        self.sondes = {}

        # Input Queue.
        self.input_queue = Queue()

        # Start queue processing thread.
        self.input_processing_running = True
        self.input_thread = Thread(target = self.process_queue)
        self.input_thread.start()

        self.log_info("Started E-Mail Notifier Thread")


    def add(self, telemetry):
        """ Add a telemetery dictionary to the input queue. """
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
        """ Process packets from the input queue. """
        while self.input_processing_running:

            # Process everything in the queue.
            while self.input_queue.qsize() > 0:
                try:
                    _telem = self.input_queue.get_nowait()
                    self.process_telemetry(_telem)

                except Exception as e:
                    self.log_error("Error processing telemetry dict - %s" % str(e))

            # Sleep while waiting for some new data.
            time.sleep(0.5)


    def process_telemetry(self, telemetry):
        """ Process a new telemmetry dict, and send an e-mail if it is a new sonde. """
        _id = telemetry['id']

        if _id not in self.sondes:
            try:
                # This is a new sonde. Send the email.
                msg  = 'Sonde launch detected:\n'
                msg += '\n'
                msg += 'Callsign:  %s\n' % _id
                msg += 'Type:      %s\n' % telemetry['type']
                msg += 'Frequency: %s MHz\n' % telemetry['freq']
                msg += 'Position:  %.5f,%.5f\n' % (telemetry['lat'], telemetry['lon'])
                msg += 'Altitude:  %dm\n' % round(telemetry['alt'])
                msg += '\n'
                msg += 'https://tracker.habhub.org/#!qm=All&q=RS_%s\n' % _id

                msg = MIMEText(msg, 'plain', 'UTF-8')
                msg['Subject'] = 'Sonde launch detected: ' + _id
                msg['From'] = self.mail_from
                msg['To'] = self.mail_to
                msg["Date"] = formatdate()

                s = smtplib.SMTP(self.smtp_server)
                s.sendmail(msg['From'], msg['To'], msg.as_string())
                s.quit()

                self.log_info("E-mail sent.")
            except Exception as e:
                self.log_error("Error sending E-mail - %s" % str(e))

        self.sondes[_id] = { 'last_time': time.time() }


    def close(self):
        """ Close input processing thread. """
        self.log_debug("Waiting for processing thread to close...")
        self.input_processing_running = False

        if self.input_thread is not None:
            self.input_thread.join()


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
        logging.debug("E-Mail - %s" % line)


    def log_info(self, line):
        """ Helper function to log an informational message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.info("E-Mail - %s" % line)


    def log_error(self, line):
        """ Helper function to log an error message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.error("E-Mail - %s" % line)


if __name__ == "__main__":
    # Test Script
    pass

