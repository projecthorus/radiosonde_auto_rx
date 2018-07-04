#!/usr/bin/env python
#
#   radiosonde_auto_rx - Email Notification
#
#   Copyright (C) 2018 Philip Heron <phil@sanslogic.co.uk>
#   Released under GNU GPL v3 or later

import time
import smtplib
from email.mime.text import MIMEText
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
    REQUIRED_FIELDS = [ 'id', 'lat', 'lon', 'alt', 'type', 'freq', ]

    def __init__(self, smtp_server = 'localhost', mail_from = None, mail_to = None):

        self.smtp_server = smtp_server
        self.mail_from = mail_from
        self.mail_to = mail_to

        # Dictionary to track sonde IDs
        self.sondes = {}

        # Input Queue.
        self.input_queue = Queue()

        # Start queue processing thread.
        self.input_processing_running = True
        self.log_process_thread = Thread(target = self.process_queue)
        self.log_process_thread.start()

    def add(self, telemetry):

        # Check the telemetry dictionary contains the required fields.
        for _field in self.REQUIRED_FIELDS:
            if _field not in telemetry:
                print("JSON object missing required field %s" % _field)
                return

        # Add it to the queue if we are running.
        if self.input_processing_running:
            self.input_queue.put(telemetry)
        else:
            print("Processing not running, discarding.")

    def process_queue(self):

        while self.input_processing_running:

            # Process everything in the queue.
            while self.input_queue.qsize() > 0:
                try:
                    _telem = self.input_queue.get_nowait()
                    self.process_telemetry(_telem)

                except Exception as e:
                    raise
                    print("Error processing telemetry dict - %s" % str(e))

            # Sleep while waiting for some new data.
            time.sleep(0.5)

    def process_telemetry(self, telemetry):

        _id = telemetry['id']

        if _id not in self.sondes:

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

            s = smtplib.SMTP(self.smtp_server)
            s.sendmail(msg['From'], msg['To'], msg.as_string())
            s.quit()

        self.sondes[_id] = { 'last_time': time.time() }

    def close(self):
        """ Close input processing thread. """
        self.input_processing_running = False

    def running(self):
        """ Check if the logging thread is running.

        Returns:
            bool: True if the logging thread is running.
        """
        return self.input_processing_running

if __name__ == "__main__":
    # Test Script
    pass

