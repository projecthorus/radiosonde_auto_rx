#!/usr/bin/env python
#
#   radiosonde_auto_rx - Web Interface
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under MIT License
#
import datetime
import json
import logging
import random
import requests
import traceback
import autorx
import autorx.config
import autorx.scan
from threading import Thread
import flask
from flask_socketio import SocketIO


# Instantiate our Flask app.
app = flask.Flask(__name__)
app.config['SECRET_KEY'] = 'secret!'
app.config['TEMPLATES_AUTO_RELOAD'] = True
app.jinja_env.auto_reload = True
# This thread will hold the currently running flask application thread.
flask_app_thread = None
# A key that needs to be matched to allow shutdown.
flask_shutdown_key = "temp"

# SocketIO instance
socketio = SocketIO(app)

# Global store of telemetry data, which we will add data do and manage.
flask_telemetry_store = {}

#
# Globally called 'emit' function
#
def flask_emit_event(event_name="none", data={}):
    """ Emit a socketio event to any clients. """
    socketio.emit(event_name, data, namespace='/update_status')

#
#   Flask Routes
#

@app.route("/")
def flask_index():
    """ Render main index page """
    return flask.render_template('index.html')


@app.route("/get_version")
def flask_get_version():
    """ Return auto_rx version to client """
    return autorx.__version__


@app.route("/get_sdr_list")
def flask_get_sdr_list():
    """ Return the current list of active SDRs, and their active task names """

    # Read in the task list, index by SDR ID. 
    _task_list = {}
    for _task in autorx.task_list.keys():
        _task_list[str(autorx.task_list[_task]['device_idx'])] = _task

    # Now, for each configured SDR, determine what task it is currently performing
    _sdr_list = {}

    for _sdr in autorx.sdr_list.keys():
        _sdr_list[str(_sdr)] = 'Not Tasked'
        if str(_sdr) in _task_list:
            _sdr_list[str(_sdr)] = _task_list[str(_sdr)]
            if _sdr_list[str(_sdr)] == 'SCAN':
                _sdr_list[str(_sdr)] = 'Scanning'

    # Convert the task list to a JSON blob, and return.
    return json.dumps(_sdr_list)


@app.route("/get_config")
def flask_get_config():
    """ Return a copy of the current auto_rx configuration """
    # Grab a copy of the config
    _config = autorx.config.global_config

    # TODO: Sanitise config output a bit?
    return json.dumps(_config)


@app.route("/get_scan_data")
def flask_get_scan_data():
    """ Return a copy of the latest scan results """
    return json.dumps(autorx.scan.scan_result)


@app.route("/shutdown/<shutdown_key>")
def shutdown_flask(shutdown_key):
    """ Shutdown the Flask Server """
    global flask_shutdown_key
    # Only shutdown if the supplied key matches our shutdown key
    if shutdown_key == flask_shutdown_key:
        flask.request.environ.get('werkzeug.server.shutdown')()

    return ""


#
# SocketIO Events
#

@socketio.on('client_connected', namespace='/update_status')
def refresh_client(arg1):
    """ A client has connected, let them know to grab data."""
    logging.info("Flask - New Web Client connected!")
    # Tell them to get a copy of the latest scan results.
    flask_emit_event('scan_event')
    # TODO: Send last few log entries


#
#   Flask Startup & Shutdown Helper Scripts
#

def flask_thread(host='0.0.0.0', port=5000):
    """ Flask Server Thread"""
    socketio.run(app, host=host, port=port)


def start_flask(host='0.0.0.0', port=5000):
    """ Start up the Flask Server """
    global flask_app_thread, flask_shutdown_key
    # Generate the shutdown key
    flask_shutdown_key = str(random.randint(10000,100000000))

    # Start up Flask
    flask_app_thread = Thread(target=flask_thread, kwargs={'host':host, 'port':port})
    flask_app_thread.start()
    logging.info("Started Flask server on http://%s:%d" % (host,port))


def stop_flask(host='0.0.0.0', port=5000):
    """ Shutdown the Flask Server by submmitting a shutdown request """
    global flask_shutdown_key
    try:
        r = requests.get('http://%s:%d/shutdown/%s' % (host,port, flask_shutdown_key))
        logging.info("Web - Flask Server Shutdown.")
    except:
        # TODO: Cleanup errors
        traceback.print_exc()



class WebHandler(logging.Handler):
    """ Logging Handler for sending log messages via Socket.IO to a Web Client """

    def emit(self, record):
        """ Emit a log message via SocketIO """
        if 'socket.io' not in record.msg:
            # Convert log record into a dictionary
            log_data = {
                'level': record.levelname,
                'timestamp': datetime.datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%S.%fZ'),
                'msg': record.msg
            }
            # Emit to all socket.io clients
            socketio.emit('log_event', log_data, namespace='/update_status')


class WebExporter(object):
    """ Push Radiosonde Telemetry Data to a web client """

    # We require the following fields to be present in the incoming telemetry dictionary data
    REQUIRED_FIELDS = ['frame', 'id', 'datetime', 'lat', 'lon', 'alt', 'temp', 'type', 'freq', 'freq_float', 'datetime_dt']

    def __init__(self):
        """ """
        pass

    def add(self, telemetry):
        for _field in self.REQUIRED_FIELDS:
            if _field not in telemetry:
                self.log_error("JSON object missing required field %s" % _field)
                return
        
        _telem = telemetry.copy()
        _telem.pop('datetime_dt')
        socketio.emit('telemetry_event', _telem, namespace='/update_status')


    def close(self):
        """ Dummy close function """
        pass


if __name__ == "__main__":
    # Test script to start up the flask server and show some dummy log data
    import time
    logging.basicConfig(format='%(asctime)s %(levelname)s:%(message)s', level=logging.DEBUG)
    web_handler = WebHandler()
    logging.getLogger().addHandler(web_handler)
    start_flask()

    try:
        while flask_app_thread.isAlive():
            time.sleep(1)
            logging.info("This is a test message.")
    except:
        stop_flask()
