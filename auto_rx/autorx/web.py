#!/usr/bin/env python
#
#   radiosonde_auto_rx - Web Interface
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under MIT License
#
import datetime
import logging
import requests
import traceback
import flask
from threading import Thread
from flask_socketio import SocketIO

# Instantiate our Flask app.
app = flask.Flask(__name__)
app.config['SECRET_KEY'] = 'secret!'
# This thread will hold the currently running flask application thread.
flask_app_thread = None

# SocketIO instance
socketio = SocketIO(app)

#
#   Flask Routes
#


@app.route("/")
def flask_index():
    """ Render main index page """
    return flask.render_template('index.html')


@app.route("/server_shutdown")
def shutdown_flask():
    """ Shutdown the Flask Server """
    flask.request.environ.get('werkzeug.server.shutdown')()
    return ""

#
#   Flask Startup & Shutdown Helper Scripts
#

def flask_thread(host='0.0.0.0', port=5000):
    """ Flask Server Thread"""
    socketio.run(app, host=host, port=port)


def start_flask(host='0.0.0.0', port=5000):
    """ Start up the Flask Server """
    global flask_app_thread
    flask_app_thread = Thread(target=flask_thread, kwargs={'host':host, 'port':port})
    flask_app_thread.start()
    logging.info("Started Flask server on http://%s:%d" % (host,port))


def stop_flask(host='0.0.0.0', port=5000):
    """ Shutdown the Flask Server by submmitting a server_shutdown request """
    try:
        r = requests.get('http://%s:%d/server_shutdown' % (host,port))
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
