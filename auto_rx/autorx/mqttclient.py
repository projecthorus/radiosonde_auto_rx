#!/usr/bin/env python
#
#   radiosonde_auto_rx - MQTT Client
#
#   Copyright (C) 2026 Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#

import json
import logging
from queue import Empty, Queue
from threading import Thread
from .sondehub import telem_to_sondehub_format


class MQTTClient(object):
    """ 
    Publish radiosonde telemetry to a MQTT server.
    """

    def __init__(
        self,
        mqtt_hostname = "localhost",
        mqtt_port = 1883,
        mqtt_use_ssl = False,
        mqtt_username = "none",
        mqtt_password = "none",
        mqtt_topic_prefix = "sonde/",
        user_callsign="N0CALL",
        user_position=None,
        user_antenna="",
    ):
        """
        Initialise an MQTT Client
        """

        self.mqtt_hostname = mqtt_hostname
        self.mqtt_port = mqtt_port
        self.mqtt_use_ssl = mqtt_use_ssl
        self.mqtt_username = mqtt_username
        self.mqtt_password = mqtt_password
        self.mqtt_topic_prefix = mqtt_topic_prefix
        self.user_callsign = user_callsign
        self.user_position = user_position
        self.user_antenna = user_antenna

        # MQTT is optional, so only load Paho when the exporter is enabled.
        import paho.mqtt.client as mqtt

        self.mqtt_connection = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        if self.mqtt_username and self.mqtt_username.lower() != "none":
            self.mqtt_connection.username_pw_set(
                self.mqtt_username, self.mqtt_password
            )
        if self.mqtt_use_ssl:
            self.mqtt_connection.tls_set()

        self.mqtt_connection.on_connect = self._on_connect
        self.mqtt_connection.connect_async(self.mqtt_hostname, self.mqtt_port)
        self.mqtt_connection.loop_start()

        # Input Queue.
        self.input_queue = Queue()

        # Start the input queue processing thread.
        self.input_processing_running = True
        self.input_thread = Thread(target=self.process_queue)
        self.input_thread.start()

        self.log_info("Started MQTT Client")

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        """Log the broker's response to a connection attempt."""
        if reason_code.is_failure:
            self.log_error(f"Broker rejected connection: {reason_code}")
        else:
            self.log_info(f"Connected to {self.mqtt_hostname}:{self.mqtt_port}")

    def process_queue(self):
        """ Process packets from the input queue.

        This thread handles packets from the input queue (provided by the decoders)
        """

        from paho.mqtt.client import MQTT_ERR_NO_CONN, MQTT_ERR_SUCCESS, error_string

        while self.input_processing_running:
            try:
                telemetry = self.input_queue.get(timeout=0.5)
            except Empty:
                continue

            try:
                topic = self.mqtt_topic_prefix + telemetry["serial"]
                result = self.mqtt_connection.publish(topic, json.dumps(telemetry))
            except (KeyError, TypeError, ValueError) as exc:
                self.log_error(f"Invalid MQTT telemetry: {exc}")
                continue

            if result.rc == MQTT_ERR_NO_CONN:
                self.log_debug(f"Broker disconnected, discarding telemetry for {topic}")
            elif result.rc != MQTT_ERR_SUCCESS:
                self.log_error(f"Publish to {topic} failed: {error_string(result.rc)}")

    def add(self, telemetry):
        """ Add a dictionary of telemetry to the input queue. 

        Args:
            telemetry (dict): Telemetry dictionary to add to the input queue.

        """

        # Attempt to reformat the data.
        _telem = telem_to_sondehub_format(
            telemetry,
            uploader_callsign = self.user_callsign,
            uploader_position = self.user_position,
            uploader_antenna = self.user_antenna
            )

        # Add it to the queue if we are running.
        if self.input_processing_running:
            if _telem:
                self.input_queue.put(_telem)
        else:
            self.log_error("Processing not running, discarding.")

    def close(self):
        """ Shutdown processing thread. """
        self.log_debug("Waiting for processing thread to close...")
        self.input_processing_running = False

        if self.input_thread is not None:
            self.input_thread.join(60)
            if self.input_thread.is_alive():
                self.log_error("MQTT Client input thread failed to join")
        
        self.mqtt_connection.disconnect()
        self.mqtt_connection.loop_stop()

    def log_debug(self, line):
        """ Helper function to log a debug message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.debug("MQTT Client - %s" % line)

    def log_info(self, line):
        """ Helper function to log an informational message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.info("MQTT Client - %s" % line)

    def log_error(self, line):
        """ Helper function to log an error message with a descriptive heading. 
        Args:
            line (str): Message to be logged.
        """
        logging.error("MQTT Client - %s" % line)
