#!/usr/bin/env python
#
#   radiosonde_auto_rx - Web Interface
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under MIT License
#
import base64
import copy
import datetime
import glob
import io
import json
import logging
import os
import random
import requests
import time
import traceback
import sys
import xml.etree.ElementTree as ET
import autorx
import autorx.config
import autorx.rotator
import autorx.scan
from autorx.geometry import GenericTrack
from autorx.utils import check_autorx_versions
from autorx.log_files import (
    list_log_files,
    read_log_by_serial,
    zip_log_files,
    log_files_to_kml,
    coordinates_to_kml_placemark,
    path_to_kml_placemark
)
from autorx.decode import SondeDecoder
from queue import Queue
from threading import Thread
import flask
from flask import request, abort, make_response, send_file
from flask_socketio import SocketIO
from werkzeug.middleware.proxy_fix import ProxyFix


# Inhibit Flask warning message about running a development server... (we know!)
cli = sys.modules["flask.cli"]
cli.show_server_banner = lambda *x: None

# Instantiate our Flask app.
app = flask.Flask(__name__)
app.wsgi_app = ProxyFix(app.wsgi_app, x_proto=1, x_prefix=1)
app.config["SECRET_KEY"] = "secret!"
app.config["TEMPLATES_AUTO_RELOAD"] = True
app.jinja_env.auto_reload = True
# This thread will hold the currently running flask application thread.
flask_app_thread = None
# A key that needs to be matched to allow shutdown.
flask_shutdown_key = None

# SocketIO instance
socketio = SocketIO(app, async_mode="threading")

# Global store of telemetry data, which we will add data to and manage.
# Under each key (which will be the sonde ID), we will have a dictionary containing:
#   'latest_timestamp': timestamp (unix timestamp) of when the last packet was received.
#   'latest_telem': telemetry dictionary.
#   'path': list of [lat,lon,alt] pairs
#   'track': A GenericTrack object, which is used to determine the current ascent/descent rate.
#
flask_telemetry_store = {}

#
# Globally called 'emit' function
#
def flask_emit_event(event_name="none", data={}):
    """ Emit a socketio event to any clients. """
    socketio.emit(event_name, data, namespace="/update_status")


#
#   Flask Routes
#


# --- React SPA at the root ---------------------------------------------------
# The build is at autorx/static/build/. Vite emits asset paths like
# /static/build/assets/*.js — those are served by Flask's static handler
# automatically (more-specific routes beat the catch-all below). We serve
# index.html for / and for any unmatched path so the client-side router can
# take over. API endpoints declared elsewhere in this file remain matched
# first by Werkzeug's specificity rules (literal segments beat <path:...>).
_SPA_INDEX = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "static", "build", "index.html"
)


def _serve_spa():
    try:
        with open(_SPA_INDEX, "r", encoding="utf-8") as fh:
            return fh.read()
    except OSError:
        flask.abort(404)


@app.route("/")
@app.route("/<path:_subpath>")
def flask_spa(_subpath=""):
    return _serve_spa()


@app.route("/get_version")
def flask_get_version():
    """ Return current and latest auto_rx version to client """
    _newer = check_autorx_versions()
    return flask.jsonify({"current": autorx.__version__, "latest": _newer})


@app.route("/get_task_list")
def flask_get_task_list():
    """ Return the current list of active SDRs, and their active task names """


    # Read in the task list, index by SDR ID.
    _task_list = {}
    for _task in autorx.task_list.keys():
        _task_list[str(autorx.task_list[_task]["device_idx"])] = _task

    # Now, for each configured SDR, determine what task it is currently performing
    _sdr_list = {}

    for _sdr in autorx.sdr_list.keys():
        _sdr_list[str(_sdr)] = {"task": "Not Tasked", "freq": 0}
        if str(_sdr) in _task_list:
            if _task_list[str(_sdr)] == "SCAN":
                _sdr_list[str(_sdr)] = {"task": "Scanning", "freq": 0}
            else:
                try:
                    _sdr_list[str(_sdr)] = {
                        "task": "Decoding (%.3f MHz)" % (_task_list[str(_sdr)] / 1e6),
                        "freq": _task_list[str(_sdr)],
                    }
                    
                except:
                    _sdr_list[str(_sdr)] = {"task": "Decoding (?? MHz)", "freq": 0}

                # Try and add on sonde type.
                try:
                    _sdr_list[str(_sdr)]['type'] = autorx.task_list[_task_list[str(_sdr)]]['task'].sonde_type
                except:
                    pass

    # Convert the task list to a JSON blob, and return.
    return flask.jsonify(_sdr_list)


@app.route("/rs.kml")
def flask_get_kml():
    """ Return KML with autorefresh """

    kml_root = ET.Element("kml", xmlns="http://www.opengis.net/kml/2.2")
    kml_doc = ET.SubElement(kml_root, "Document")

    network_link = ET.SubElement(kml_doc, "NetworkLink")

    name = ET.SubElement(network_link, "name")
    name.text = "Radiosonde Auto-RX Live Telemetry"

    open = ET.SubElement(network_link, "open")
    open.text = "1"

    link = ET.SubElement(network_link, "Link")

    href = ET.SubElement(link, "href")
    href.text = flask.request.url_root + "rs_feed.kml"

    refresh_mode = ET.SubElement(link, "refreshMode")
    refresh_mode.text = "onInterval"

    refresh_interval = ET.SubElement(link, "refreshInterval")
    refresh_interval.text = str(autorx.config.global_config["kml_refresh_rate"])

    kml_string = ET.tostring(kml_root, encoding="UTF-8", xml_declaration=True)
    return kml_string, 200, {"content-type": "application/vnd.google-earth.kml+xml"}


@app.route("/rs_feed.kml")
def flask_get_kml_feed():
    """ Return KML with RS telemetry """
    kml_root = ET.Element("kml", xmlns="http://www.opengis.net/kml/2.2")
    kml_doc = ET.SubElement(kml_root, "Document")

    name = ET.SubElement(kml_doc, "name")
    name.text = "Track"
    open = ET.SubElement(kml_doc, "open")
    open.text = "1"

    # Station Placemark
    kml_doc.append(coordinates_to_kml_placemark(
        autorx.config.global_config["station_lat"],
        autorx.config.global_config["station_lon"],
        autorx.config.global_config["station_alt"],
        name=autorx.config.global_config["habitat_uploader_callsign"],
        description="AutoRX Ground Station",
        absolute=True,
        icon=flask.request.url_root + "static/img/antenna-green.png"
    ))

    for rs_id in flask_telemetry_store:
        try:
            coordinates = []

            for tp in flask_telemetry_store[rs_id]["track"].track_history:
                coordinates.append((tp[1], tp[2], tp[3]))

            rs_data = """\
            {type}/{subtype}
            Frequency: {freq}
            Altitude: {alt:.1f} m
            Heading: {heading:.1f} degrees
            Ground Speed: {vel_h:.2f} m/s
            Ascent Rate: {vel_v:.2f} m/s
            Temperature: {temp:.1f} C
            Humidity: {humidity:.1f} %
            Pressure: {pressure:.1f} hPa
            """
            if flask_telemetry_store[rs_id]["latest_telem"]["vel_v"] > -5:
                icon = flask.request.url_root + "static/img/balloon-green.png"
            else:
                icon = flask.request.url_root + "static/img/parachute-green.png"

            # Add folder
            folder = ET.SubElement(kml_doc, "Folder", id=f"folder_{rs_id}")
            name = ET.SubElement(folder, "name")
            name.text = rs_id
            open = ET.SubElement(folder, "open")
            open.text = "1"

            # HAB Placemark
            folder.append(coordinates_to_kml_placemark(
                flask_telemetry_store[rs_id]["latest_telem"]["lat"],
                flask_telemetry_store[rs_id]["latest_telem"]["lon"],
                flask_telemetry_store[rs_id]["latest_telem"]["alt"],
                name=rs_id,
                description=rs_data.format(**flask_telemetry_store[rs_id]["latest_telem"]),
                absolute=True,
                icon=icon
            ))

            # Track
            folder.append(path_to_kml_placemark(
                coordinates,
                name="Track",
                absolute=True,
                extrude=True
            ))

            # LOS line
            coordinates = [
                (
                    autorx.config.global_config["station_lat"],
                    autorx.config.global_config["station_lon"],
                    autorx.config.global_config["station_alt"],
                ),
                (
                    flask_telemetry_store[rs_id]["latest_telem"]["lat"],
                    flask_telemetry_store[rs_id]["latest_telem"]["lon"],
                    flask_telemetry_store[rs_id]["latest_telem"]["alt"],
                ),
            ]
            folder.append(path_to_kml_placemark(
                coordinates,
                name="LOS",
                track_color="ffffffff",
                absolute=True,
                extrude=False
            ))

        except Exception as e:
            logging.error(
                "KML - Could not parse data from RS %s - %s" % (rs_id, str(e))
            )

    kml_string = ET.tostring(kml_root, encoding="UTF-8", xml_declaration=True)
    return kml_string, 200, {"content-type": "application/vnd.google-earth.kml+xml"}


@app.route("/get_config")
def flask_get_config():
    """ Return a copy of the current auto_rx configuration """
    return flask.jsonify(autorx.config.global_config)


@app.route("/sdr_status")
def flask_sdr_status():
    """ Return per-SDR runtime status (device_idx, gain, ppm, bias, in_use, task, freq).
    Matches the shape consumed by the React Status page (SDRStatusItem).

    Source of truth: autorx.sdr_list (populated by auto_rx.py at startup) holds
    gain/ppm/bias/in_use/task directly per entry. autorx.task_list is keyed by
    the active frequency (or the string "SCAN") with value {device_idx, task},
    so we reverse it to look up by SDR.
    """
    out = []
    by_dev = {}
    for k, v in autorx.task_list.items():
        by_dev[str(v.get("device_idx"))] = (k, v)

    for sdr_id, sdr in autorx.sdr_list.items():
        task_label = "Not Tasked"
        freq = 0
        entry = by_dev.get(str(sdr_id))
        if entry is not None:
            k, _v = entry
            if k == "SCAN":
                task_label = "Scanning"
            else:
                try:
                    task_label = "Decoding (%.3f MHz)" % (float(k) / 1e6)
                    freq = int(k)
                except Exception:
                    task_label = "Decoding"
        out.append({
            "device_idx": str(sdr_id),
            "id": sdr_id if isinstance(sdr_id, int) else None,
            # These come straight from sdr_list, which is mutated as decoders
            # start/stop. Defaults match the original station.cfg defaults.
            "gain": float(sdr.get("gain", -1)),
            "ppm": float(sdr.get("ppm", 0)),
            "bias": bool(sdr.get("bias", False)),
            "in_use": bool(sdr.get("in_use", False)),
            "task": task_label,
            "freq": freq,
        })
    return flask.jsonify(out)


@app.route("/rotator_status")
def flask_rotator_status():
    """ Live rotator state for the Status page.

    The Rotator class doesn't cache "current" position — it queries rotctld
    over TCP for every move decision. So we do the same here: a short-timeout
    socket read against the configured rotctld endpoint. On failure we return
    the last known target (derived from the latest sonde telemetry) so the UI
    has something to show.
    """
    from autorx.rotator import read_rotator
    from autorx.utils import position_info

    enabled = bool(autorx.config.global_config.get("rotator_enabled", False))
    rot = autorx.rotator_object
    az = el = 0.0
    target_az = target_el = None
    target_id = None
    mode = "disabled" if not enabled else "idle"

    if rot is not None:
        # 1. Current position from rotctld — short timeout so /rotator_status
        # remains snappy even when the rotator is offline.
        try:
            pos = read_rotator(rot.rotctld_host, rot.rotctld_port, timeout=2)
            if pos:
                az = float(pos[0]) % 360.0
                el = float(pos[1])
        except Exception as e:
            logging.debug("rotator_status: read_rotator failed: %s", e)

        # 2. Derive target az/el from the latest sonde telemetry the rotator
        # would track. The rotator thread does the same calculation (see
        # rotator.py: position_info(station_position, [lat,lon,alt])).
        try:
            with rot.telem_lock:
                latest = rot.latest_telemetry.copy() if rot.latest_telemetry else None
            if latest:
                info = position_info(
                    rot.station_position,
                    [latest["lat"], latest["lon"], latest["alt"]],
                )
                target_az = float(info["bearing"]) % 360.0
                target_el = 0.0 if rot.azimuth_only else float(info["elevation"])
                target_id = latest.get("id")
                mode = "tracking"
        except Exception as e:
            logging.debug("rotator_status: target derivation failed: %s", e)

        # 3. "Homed" state.
        if getattr(rot, "rotator_homed", False) and target_id is None:
            mode = "homed"

    return flask.jsonify({
        "enabled": enabled,
        "az": az,
        "el": el,
        "target_az": target_az,
        "target_el": target_el,
        "mode": mode,
        "target_id": target_id,
        "last_move": None,  # not currently tracked on the Rotator object
    })


@app.route("/blocklist")
def flask_blocklist():
    """ Temporary + permanent block-list entries. """
    out = []
    # Permanent: never_scan list from config. Supports both formats users
    # write into station.cfg's [search_params] never_scan: a flat list of
    # MHz floats, or a list of {freq_start, freq_end} range dicts.
    for entry in (autorx.config.global_config.get("never_scan") or []):
        try:
            if isinstance(entry, (int, float)):
                out.append({
                    "freq": int(float(entry) * 1e6),
                    "reason": "Permanent",
                })
            elif isinstance(entry, dict):
                start = float(entry.get("freq_start"))
                end = float(entry.get("freq_end"))
                span = end - start
                out.append({
                    "freq": int(start * 1e6),
                    "reason": (
                        "Permanent" if span <= 0
                        else f"Permanent ({span:.3f} MHz range)"
                    ),
                })
        except Exception:
            pass
    # Temporary: from the live scanner instance, if one exists.
    try:
        scan_task = autorx.task_list.get("SCAN")
        scanner = scan_task["task"] if scan_task else None
        if scanner is not None and hasattr(scanner, "temporary_block_list"):
            with scanner.temporary_block_list_lock:
                tb = dict(scanner.temporary_block_list)
            now = time.time()
            block_secs = float(getattr(scanner, "temporary_block_time", 60)) * 60.0
            for freq_hz, blocked_at in tb.items():
                try:
                    out.append({
                        "freq": int(freq_hz),
                        "reason": "Temporary",
                        "until": float(blocked_at) + block_secs,
                    })
                except Exception:
                    pass
    except Exception as e:
        logging.debug("blocklist endpoint: %s", e)
    return flask.jsonify(out)


@app.route("/save_config", methods=["POST"])
def flask_save_config():
    """ Persist UI-edited settings back to station.cfg in place, preserving
    comments and structure. Requires web_control + correct web_password.
    The change does NOT take effect until auto_rx is restarted.
    """
    from autorx.config_writer import save_config_file

    if not autorx.config.global_config.get("web_control"):
        abort(403)

    try:
        body = request.get_json(force=True) or {}
    except Exception as e:
        return flask.jsonify({"ok": False, "errors": [f"Invalid JSON: {e}"]}), 400

    password = body.pop("__password", None)
    if autorx.config.web_password == "none" or password != autorx.config.web_password:
        abort(403)

    cfg_path = autorx.config.cfg_filename or "station.cfg"
    try:
        result = save_config_file(cfg_path, body)
    except Exception as e:
        return flask.jsonify({"ok": False, "errors": [str(e)]}), 500

    if not result.get("ok"):
        return flask.jsonify(result), 400
    # Reflect into in-memory config so subsequent /get_config calls match the file
    autorx.config.global_config.update({k: v for k, v in body.items() if k != "sdrs"})
    return flask.jsonify(result)


@app.route("/get_scan_data")
def flask_get_scan_data():
    """ Return a copy of the latest scan results """
    return flask.jsonify(autorx.scan.scan_result)


@app.route("/get_telemetry_archive")
def flask_get_telemetry_archive():
    """ Return a copy of the telemetry archive """
    # Make a copy of the store, and remove the non-serialisable GenericTrack object
    _temp_store = copy.deepcopy(flask_telemetry_store)
    for _element in _temp_store:
        _temp_store[_element].pop("track")

    return flask.jsonify(_temp_store)


@app.route("/shutdown/<shutdown_key>")
def shutdown_flask(shutdown_key):
    """ Shutdown the Flask Server """
    global flask_shutdown_key
    # Only shutdown if the supplied key matches our shutdown key
    if shutdown_key == flask_shutdown_key:
        shutdown_function = flask.request.environ.get("werkzeug.server.shutdown")
        if shutdown_function:
            shutdown_function()
        else:
            logging.debug("Unable to stop this version of Werkzeug, continuing...")

    return ""


@app.route("/get_log_list")
def flask_get_log_list():
    """ Return a list of log files, as a list of objects """
    return flask.jsonify(list_log_files(quicklook=True))

def flask_running():
    global flask_shutdown_key
    return flask_shutdown_key is not None

# ---- log-file response cache ------------------------------------------------
# read_log_by_serial parses the on-disk log every call (~500 ms per file on
# a slow ARM/Atom box). A finished flight's log file is immutable, so we
# cache parsed results in two layers:
#
#   1. In-memory LRU (this process) — ~instant repeat hits.
#   2. On-disk JSON cache keyed by (serial, decim, path_only, source-log-mtime)
#      — survives process restarts. Once a sonde has been viewed once, ever,
#      it's fast every time after.
#
# Cache validity check: the source log file's mtime is encoded in the cache
# filename. If the source log changes (still-live flight), mtime advances and
# the old cache entry is bypassed. Stale entries are pruned on write.
from collections import OrderedDict
from threading import Lock as _CacheLock
import json as _json
import glob as _glob

_LOG_CACHE: "OrderedDict[tuple, dict]" = OrderedDict()
_LOG_CACHE_LOCK = _CacheLock()
_LOG_CACHE_MAX = 64

_LOG_CACHE_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "log_cache"
)


def _ensure_cache_dir():
    try:
        os.makedirs(_LOG_CACHE_DIR, exist_ok=True)
    except OSError as e:
        logging.warning("log_cache: cannot create %s: %s", _LOG_CACHE_DIR, e)


def _source_log_path(serial: str):
    """Locate the on-disk log for `serial`, plus its mtime; (None, 0) if missing."""
    mask = os.path.join(autorx.logging_path, f"*_*{serial}_*_sonde.log")
    files = _glob.glob(mask)
    if not files:
        return None, 0.0
    f = files[0]
    try:
        return f, os.path.getmtime(f)
    except OSError:
        return f, 0.0


def _disk_cache_path(serial: str, decim: int, path_only: bool, mtime: float):
    # mtime as integer microseconds keeps the filename short and stable.
    return os.path.join(
        _LOG_CACHE_DIR,
        f"{serial}__d{decim}__p{int(path_only)}__{int(mtime * 1e6)}.json",
    )


def _disk_cache_load(serial: str, decim: int, path_only: bool, mtime: float):
    if mtime <= 0:
        return None
    path = _disk_cache_path(serial, decim, path_only, mtime)
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return _json.load(fh)
    except (OSError, ValueError):
        return None


def _disk_cache_save(serial: str, decim: int, path_only: bool, mtime: float, data: dict):
    if mtime <= 0:
        return
    _ensure_cache_dir()
    out = _disk_cache_path(serial, decim, path_only, mtime)
    tmp = out + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8") as fh:
            _json.dump(data, fh)
        os.replace(tmp, out)
    except OSError as e:
        logging.warning("log_cache: write failed for %s: %s", out, e)
        return
    # Prune any older entries for the same (serial, decim, path_only). The log
    # file's mtime should never go backwards, so anything older than what we
    # just wrote is stale.
    try:
        prefix = os.path.join(_LOG_CACHE_DIR, f"{serial}__d{decim}__p{int(path_only)}__")
        for old in _glob.glob(prefix + "*.json"):
            if old != out:
                try: os.remove(old)
                except OSError: pass
    except OSError:
        pass


def _cached_log_by_serial(serial: str, decim: int, path_only: bool):
    # Layer 1: in-memory LRU. Keyed on (serial, decim, path_only); the source
    # log's mtime is checked below so the in-mem entry is still valid only
    # while the file hasn't been rewritten.
    _src, mtime = _source_log_path(serial)
    mem_key = (serial, decim, path_only, mtime)
    with _LOG_CACHE_LOCK:
        if mem_key in _LOG_CACHE:
            _LOG_CACHE.move_to_end(mem_key)
            return _LOG_CACHE[mem_key]

    # Layer 2: disk cache. Survives restarts.
    data = _disk_cache_load(serial, decim, path_only, mtime)
    if data is None:
        # True miss — parse the log.
        data = read_log_by_serial(serial, path_only=path_only)
        if decim and decim > 1 and isinstance(data, dict) and isinstance(data.get("path"), list):
            p = data["path"]
            if len(p) > decim * 2:
                keep = [p[0]] + p[decim::decim]
                if keep[-1] is not p[-1]:
                    keep.append(p[-1])
                data = dict(data, path=keep)
        _disk_cache_save(serial, decim, path_only, mtime, data)

    # Promote into in-memory LRU.
    with _LOG_CACHE_LOCK:
        _LOG_CACHE[mem_key] = data
        _LOG_CACHE.move_to_end(mem_key)
        while len(_LOG_CACHE) > _LOG_CACHE_MAX:
            _LOG_CACHE.popitem(last=False)
    return data


@app.route("/get_log_by_serial/<serial>")
def flask_get_log_by_serial(serial):
    """ Request a log file be read, by serial number.

    Query params:
      ?decim=N      server-side stride decimation of the path (default 1)
      ?path_only=1  skip the Skew-T computation — ~5-10x faster on slow CPUs
                    when the caller only needs path/first/last/burst
    """
    try:
        decim = int(request.args.get("decim", "1"))
    except (TypeError, ValueError):
        decim = 1
    path_only = request.args.get("path_only", "").lower() in ("1", "true", "yes")
    return flask.jsonify(_cached_log_by_serial(serial, decim, path_only))


@app.route("/get_log_detail", methods=["POST"])
def flask_get_log_by_serial_detail():
    """ 
    A more customizable version of the above, with the ability
    to set a decimation for the skewt data. 
    """

    if request.method == "POST":
        if "serial" not in request.form:
            abort(403)

        _serial = request.form["serial"]

        if "decimation" in request.form:
            _decim = int(float(request.form["decimation"]))
        else:
            _decim = 25

        return flask.jsonify(read_log_by_serial(_serial, skewt_decimation=_decim))


@app.route("/export_all_log_files")
@app.route("/export_log_files/<serialb64>")
def flask_export_log_files(serialb64=None):
    """ 
    Zip and download a set of log files.
    The list of log files is provided in the URL as a base64-encoded JSON list.
    """

    try:
        _serial_list = json.loads(base64.b64decode(serialb64)) if serialb64 else None

        _zip = zip_log_files(_serial_list)

        _ts = datetime.datetime.strftime(datetime.datetime.now(datetime.timezone.utc), "%Y%m%d-%H%M%SZ")

        response = make_response(
            flask.send_file(
                _zip,
                mimetype="application/zip",
                as_attachment=True,
                download_name=f"autorx_logfiles_{autorx.config.global_config['habitat_uploader_callsign']}_{_ts}.zip",
            )
        )

        # Add header asking client not to cache the download
        response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
        response.headers["Pragma"] = "no-cache"

        return response

    except Exception as e:
        logging.error("Web - Error handling Zip request:" + str(e))
        abort(400)


@app.route("/generate_kml")
@app.route("/generate_kml/<serialb64>")
def flask_generate_kml(serialb64=None):
    """ 
    Generate a KML file from a set of log files.
    The list of log files is provided in the URL as a base64-encoded JSON list.
    """

    try:
        if serialb64:
            _serial_list = json.loads(base64.b64decode(serialb64))
            _log_files = []
            for _serial in _serial_list:
                _log_mask = os.path.join(autorx.logging_path, f"*_*{_serial}_*_sonde.log")
                _matching_files = glob.glob(_log_mask)

                if len(_matching_files) >= 1:
                    _log_files.append(_matching_files[0])
        else:
            _log_mask = os.path.join(autorx.logging_path, "*_sonde.log")
            _log_files = glob.glob(_log_mask)

        _kml_file = io.BytesIO()
        _log_files.sort(reverse=True)
        log_files_to_kml(_log_files, _kml_file)
        _kml_file.seek(0)

        _ts = datetime.datetime.strftime(datetime.datetime.now(datetime.timezone.utc), "%Y%m%d-%H%M%SZ")

        response = make_response(
            flask.send_file(
                _kml_file,
                mimetype="application/vnd.google-earth.kml+xml",
                as_attachment=True,
                download_name=f"autorx_logfiles_{autorx.config.global_config['habitat_uploader_callsign']}_{_ts}.kml",
            )
        )

        # Add header asking client not to cache the download
        response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
        response.headers["Pragma"] = "no-cache"

        return response

    except Exception as e:
        logging.error("Web - Error handling KML request:" + str(e))
        abort(400)

#
#   Control Endpoints.
#


@app.route("/check_password", methods=["POST"])
def flask_check_password():
    """ Check a supplied password 
    Example:
    curl -d "password=foobar" -X POST http://localhost:5000/check_password
    """
    if request.method == "POST" and autorx.config.global_config["web_control"]:
        if "password" not in request.form:
            abort(403)

        if (request.form["password"] == autorx.config.web_password) and (
            autorx.config.web_password != "none"
        ):
            return "OK"
        else:
            abort(403)

    else:
        abort(403)


@app.route("/start_decoder", methods=["POST"])
def flask_start_decoder():
    """ Inject a scan result, which will cause a decoder to be started if there
    are enough resources (SDRs) to do so. 
    Example:
    curl -d "type=DFM&freq=403240000&password=foobar" -X POST http://localhost:5000/start_decoder
    """

    if request.method == "POST" and autorx.config.global_config["web_control"]:
        if "password" not in request.form:
            abort(403)

        if (request.form["password"] == autorx.config.web_password) and (
            autorx.config.web_password != "none"
        ):

            try:
                _type = str(request.form["type"])
                _freq = float(request.form["freq"])
            except Exception as e:
                logging.error("Web - Error in decoder start request: %s", str(e))
                abort(500)

            logging.info("Web - Got decoder start request: %s, %f" % (_type, _freq))

            autorx.scan_results.put([[_freq, _type]])

            return "OK"
        else:
            abort(403)

    else:
        abort(403)


@app.route("/stop_decoder", methods=["POST"])
def flask_stop_decoder():
    """ Request that a decoder process be halted. 
    Example:

    curl -d "freq=403250000&password=foobar" -X POST http://localhost:5000/stop_decoder

    Stop decoder and lockout for temporary_block_time
    curl -d "freq=403250000&password=foobar&lockout=1" -X POST http://localhost:5000/stop_decoder
    """

    if request.method == "POST" and autorx.config.global_config["web_control"]:
        if "password" not in request.form:
            abort(403)

        if (request.form["password"] == autorx.config.web_password) and (
            autorx.config.web_password != "none"
        ):
            _freq = float(request.form["freq"])

            _lockout = False
            if "lockout" in request.form:
                if int(request.form["lockout"]) == 1:
                    _lockout = True

            logging.info("Web - Got decoder stop request: %f" % (_freq))

            if _freq in autorx.task_list:
                autorx.task_list[_freq]["task"].stop(nowait=True, temporary_lockout=_lockout)
                return "OK"
            else:
                # If we aren't running a decoder, 404.
                abort(404)
        else:
            abort(403)
    else:
        abort(403)


@app.route("/disable_scanner", methods=["POST"])
def flask_disable_scanner():
    """ Disable and Halt a Scanner, if one is running. """

    # This probably needs to use a lock to avoid this being run simultaneously through multiple requests

    if request.method == "POST" and autorx.config.global_config["web_control"]:
        if "password" not in request.form:
            abort(403)

        if (request.form["password"] == autorx.config.web_password) and (
            autorx.config.web_password != "none"
        ):
            if "SCAN" not in autorx.task_list:
                # No scanner thread running!
                abort(404)
            else:
                logging.info("Web - Got scanner stop request.")
                # Set the scanner inhibit flag so it doesn't automatically start again.
                autorx.scan_inhibit = True
                _scan_sdr = autorx.task_list["SCAN"]["device_idx"]
                # Stop the scanner.
                try:
                    autorx.task_list["SCAN"]["task"].stop(nowait=True)
                except:
                    abort(500)

                # The following actions not required.
                # Relase the SDR.
                # autorx.sdr_list[_scan_sdr]["in_use"] = False
                # autorx.sdr_list[_scan_sdr]["task"] = None
                # # Remove the scanner task from the task list
                # autorx.task_list.pop("SCAN")
                return "OK"
        else:
            abort(403)
    else:
        abort(403)


@app.route("/enable_scanner", methods=["POST"])
def flask_enable_scanner():
    """ Re-enable the Scanner """

    if request.method == "POST" and autorx.config.global_config["web_control"]:
        if "password" not in request.form:
            abort(403)

        if (request.form["password"] == autorx.config.web_password) and (
            autorx.config.web_password != "none"
        ):
            # We re-enable the scanner by clearing the scan_inhibit flag.
            # This makes it start up on the next run of clean_task_list (approx every 2 seconds)
            # unless one is already running.
            autorx.scan_inhibit = False
            return "OK"
        else:
            abort(403)
    else:
        abort(403)


@app.route("/rescan_now", methods=["POST"])
def flask_rescan_now():
    """ Force an immediate rescan. If a SCAN task is currently running, stop it
    so clean_task_list spawns a fresh scanner on its next tick (~2 s).
    Also clears any scan_inhibit flag that was set.
    """
    if not autorx.config.global_config.get("web_control"):
        abort(403)
    if "password" not in request.form:
        abort(403)
    if request.form["password"] != autorx.config.web_password or autorx.config.web_password == "none":
        abort(403)

    autorx.scan_inhibit = False
    try:
        scan_task = autorx.task_list.get("SCAN")
        scanner = scan_task["task"] if scan_task else None
        if scanner is not None and hasattr(scanner, "stop"):
            logging.info("Web - Operator requested forced rescan; stopping current scanner.")
            scanner.stop(nowait=True)
    except Exception as e:
        logging.warning("Web - rescan_now: %s", e)
    return "OK"


@app.route("/move_rotator", methods=["POST"])
def flask_move_rotator():
    """ Move rotator to target az/el position by injecting telem packet in rotator queue
    Example:
    curl -d "az=180&el=15&password=foobar" -X POST http://localhost:5000/move_rotator
    """

    if request.method == "POST" and autorx.config.global_config["web_control"]:
        if "password" not in request.form:
            abort(403)

        if (request.form["password"] == autorx.config.web_password) and (
            autorx.config.web_password != "none"
        ):

            try:
                _az = float(request.form["az"])
                _el = float(request.form["el"])
            except Exception as e:
                logging.error("Web - Error in rotator move request: %s", str(e))
                abort(500)

            logging.info("Web - Got rotator move request: %f, %f" % (_az, _el))

            if autorx.rotator_object:
                autorx.rotator_object.move_rotator(_az, _el)

            return "OK"
        else:
            abort(403)

    else:
        abort(403)

@app.route("/home_rotator", methods=["POST"])
def flask_home_rotator():
    """ Move rotator to target az/el position by injecting telem packet in rotator queue
    Example:
    curl -d "password=foobar" -X POST http://localhost:5000/home_rotator
    """

    if request.method == "POST" and autorx.config.global_config["web_control"]:
        if "password" not in request.form:
            abort(403)

        if (request.form["password"] == autorx.config.web_password) and (
            autorx.config.web_password != "none"
        ):

            logging.info("Web - Got rotator home request")

            if autorx.rotator_object:
                autorx.rotator_object.home_rotator()

            return "OK"
        else:
            abort(403)

    else:
        abort(403)

#
# SocketIO Events
#


@socketio.on("client_connected", namespace="/update_status")
def refresh_client(*_args):
    """ A client has connected, let them know to grab data.
    Accepts and ignores any payload the client sends with the event
    (some clients emit it bare, others pass a sid/ack)."""
    logging.info("Flask - New Web Client connected!")
    # Tell them to get a copy of the latest scan results.
    flask_emit_event("scan_event")
    flask_emit_event("task_event")
    # TODO: Send last few log entries?


#
#   Flask Startup & Shutdown Helper Scripts
#


def flask_thread(host="0.0.0.0", port=5000):
    """ Flask Server Thread"""
    try:
        socketio.run(app, host=host, port=port, allow_unsafe_werkzeug=True)
    except TypeError:
        # Catch old flask-socketio version.
        logging.debug("Web - Not using allow_unsafe_werkzeug argument.")
        socketio.run(app, host=host, port=port)


def start_flask(host="0.0.0.0", port=5000):
    """ Start up the Flask Server """
    global flask_app_thread, flask_shutdown_key
    # Generate the shutdown key
    flask_shutdown_key = str(random.randint(10000, 100000000))

    # Start up Flask
    flask_app_thread = Thread(target=flask_thread, kwargs={"host": host, "port": port})
    # Set thread to be a daemon, so python will quit nicely.
    flask_app_thread.daemon = True
    flask_app_thread.start()
    logging.info("Started Flask server on http://%s:%d" % (host, port))


def stop_flask(host="0.0.0.0", port=5000):
    """ Shutdown the Flask Server by submmitting a shutdown request """
    global flask_shutdown_key
    try:
        r = requests.get("http://%s:%d/shutdown/%s" % (host, port, flask_shutdown_key))
        logging.info("Web - Flask Server Shutdown.")
    except:
        # TODO: Cleanup errors
        traceback.print_exc()


class WebHandler(logging.Handler):
    """ Logging Handler for sending log messages via Socket.IO to a Web Client.
    Also retains the most recent N entries in a ring buffer so newly connecting
    clients can backfill via /recent_logs instead of starting from a blank tray. """

    # Ring buffer for newly-connecting clients to backfill. Lives on the class
    # so /recent_logs can read it without holding a handler reference.
    from collections import deque as _deque
    from threading import Lock as _Lock
    _recent: "_deque" = _deque(maxlen=200)
    _recent_lock = _Lock()

    def emit(self, record):
        """ Emit a log message via SocketIO """
        if "socket.io" not in record.msg:

            # Inhibit flask session disconnected errors
            if "Error on request" in record.msg:
                return

            # Convert log record into a dictionary
            log_data = {
                "level": record.levelname,
                "timestamp": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
                "msg": record.msg,
            }
            # Stash for /recent_logs before broadcasting.
            with WebHandler._recent_lock:
                WebHandler._recent.append(log_data)
            # Emit to all socket.io clients
            socketio.emit("log_event", log_data, namespace="/update_status")


@app.route("/recent_logs")
def flask_recent_logs():
    """ Return the most recent log entries from the in-memory ring buffer.
    Used by the dashboard on mount to backfill the Event Log tray so the
    user sees recent history instead of an empty tray. Caps at the buffer's
    maxlen; client passes ?n=N to request fewer. """
    try:
        n = int(request.args.get("n", "50"))
    except (TypeError, ValueError):
        n = 50
    n = max(1, min(n, 200))
    with WebHandler._recent_lock:
        buf = list(WebHandler._recent)
    return flask.jsonify(buf[-n:])


class WebExporter(object):
    """ Push Radiosonde Telemetry Data to a web client """

    # We require the following fields to be present in the incoming telemetry dictionary data
    REQUIRED_FIELDS = [
        "frame",
        "id",
        "datetime",
        "lat",
        "lon",
        "alt",
        "temp",
        "type",
        "freq",
        "freq_float",
        "datetime_dt",
    ]

    def __init__(self, max_age=120):
        """ Initialise a WebExporter object.

        Args:
            max_age: Store telemetry data up to X minutes old
        """

        self.max_age = max_age * 60
        self.input_queue = Queue()

        # Start the input queue processing thread.
        self.input_processing_running = True
        self.input_thread = Thread(target=self.process_queue)
        self.input_thread.start()

    def process_queue(self):
        """ Process data from the input queue.
        """
        while self.input_processing_running:
            # Read in all queue items and handle them.
            while not self.input_queue.empty():
                self.handle_telemetry(self.input_queue.get())

            # Check the telemetry store for old data.
            self.clean_telemetry_store()

            # Wait a short time before processing new data
            time.sleep(0.1)

        logging.debug("WebExporter - Closed Processing thread.")

    def handle_telemetry(self, telemetry):
        """ Send incoming telemetry to clients, and add it to the telemetry store. """
        global flask_telemetry_store

        if telemetry == None:
            logging.error("WebExporter - Passed NoneType instead of Telemetry.")
            return

        for _field in self.REQUIRED_FIELDS:
            if _field not in telemetry:
                logging.error(
                    "WebExporter - JSON object missing required field %s" % _field
                )
                return

        _telem = telemetry.copy()

        if "f_centre" in _telem:
            # We have an estimate of the sonde's centre frequency from the modem, use this in place of
            # the RX frequency.
            # Round to 1 kHz
            _freq = round(telemetry["f_centre"] / 1000.0)
            # Convert to MHz.
            _telem["freq"] = "%.3f MHz" % (_freq / 1e3)

        # Add the telemetry information to the global telemetry store
        if _telem["id"] not in flask_telemetry_store:
            flask_telemetry_store[_telem["id"]] = {
                "timestamp": time.time(),
                "latest_telem": _telem,
                "path": [],
                "track": GenericTrack(),
            }

        flask_telemetry_store[_telem["id"]]["path"].append(
            [_telem["lat"], _telem["lon"], _telem["alt"]]
        )
        flask_telemetry_store[_telem["id"]]["latest_telem"] = _telem
        flask_telemetry_store[_telem["id"]]["timestamp"] = time.time()

        # Update the sonde's track and extract the current state.
        flask_telemetry_store[_telem["id"]]["track"].add_telemetry(
            {
                "time": _telem["datetime_dt"],
                "lat": _telem["lat"],
                "lon": _telem["lon"],
                "alt": _telem["alt"],
            }
        )
        _telem_state = flask_telemetry_store[_telem["id"]]["track"].get_latest_state()

        # Add the calculated vertical and horizontal velocity, and heading to the telemetry dict.
        _telem["vel_v"] = _telem_state["ascent_rate"]
        _telem["vel_h"] = _telem_state["speed"]
        _telem["heading"] = _telem_state["heading"]

        # Remove the datetime object that is part of the telemetry, if it exists.
        # (it might not be present in test data)
        if "datetime_dt" in _telem:
            _telem.pop("datetime_dt")

        # Pass it on to the client.
        socketio.emit("telemetry_event", _telem, namespace="/update_status")

    def clean_telemetry_store(self):
        """ Remove any old data from the telemetry store """
        global flask_telemetry_store

        _now = time.time()
        _telem_ids = list(flask_telemetry_store.keys())
        for _id in _telem_ids:
            # If the most recently telemetry is older than self.max_age, remove all data for
            # that sonde from the archive.
            if (_now - flask_telemetry_store[_id]["timestamp"]) > self.max_age:
                flask_telemetry_store.pop(_id)
                logging.debug("WebExporter - Removed Sonde #%s from archive." % _id)

    def add(self, telemetry):
        # Add it to the queue if we are running.
        if self.input_processing_running:
            self.input_queue.put(telemetry)
        else:
            logging.error("WebExporter - Processing not running, discarding.")

    def update_station_position(self, lat, lon, alt):
        """ Update the internal station position record. Used when determining the station position by GPSD """
        self.station_position = (lat, lon, alt)
        _position = {"lat": lat, "lon": lon, "alt": alt}
        socketio.emit("station_update", _position, namespace="/update_status")

    def close(self):
        """ Shutdown """
        self.input_processing_running = False


#
# Testing Functions, for easier web development.
#


def test_web_log_to_dict(log_line):
    """ Convert a line read from a sonde log to a 'fake' telemetery dictionary """

    # ['frame', 'id', 'datetime', 'lat', 'lon', 'alt', 'temp', 'type', 'freq', 'freq_float', 'datetime_dt']
    # ('2017-12-29T23:20:47.420', 'M2913212', 1563, -34.94541, 138.52819, 761.7, -273., 'RS92', 401.52)
    try:
        _telem = {
            "frame": log_line[2],
            "id": log_line[1],
            "datetime": log_line[0],
            "lat": log_line[3],
            "lon": log_line[4],
            "alt": log_line[5],
            "temp": log_line[6],
            "type": log_line[7],
            "freq": str(log_line[8]) + " MHz",
            "freq_float": log_line[8],
            "vel_v": 0.0,
            "datetime_dt": None,
            "sdr_device_idx": "00000001",
        }
        return _telem
    except:
        return None


def test_web_interface(file_list, delay=1.0):
    """ Test the web interface map functions by injecting a large amount of sonde telemetry data from sonde log files. """
    import numpy as np

    global _web

    print(file_list)

    _sondes = []
    # Minimum number of data points in a file
    _min_data = 10000

    # Read in files and add data to _sondes.
    for _file_name in file_list:
        try:
            _data = np.genfromtxt(_file_name, delimiter=",", dtype=None)
            _sondes.append(_data)
            print("Read %d records from %s" % (len(_data), _file_name))
            if len(_data) < _min_data:
                _min_data = len(_data)
        except:
            print("Could not read %s" % _file_name)

    # Number of data points to feed in initially. (10%)
    _i = _min_data // 10

    # Start up a WebExporter instance
    _web = WebExporter()

    # Feed in the first 10% of data points from each sonde.
    print("Injecting %d initial data points." % _i)
    for _sonde in _sondes:
        for _j in range(0, _i):
            _web.add(test_web_log_to_dict(_sonde[_j]))

    # Now add in new data every second until CTRL-C
    for _k in range(_i, _min_data):
        for _sonde in _sondes:
            _web.add(test_web_log_to_dict(_sonde[_k]))

        logging.info("Added new telemetry data: %d/%d" % (_k, _min_data))
        time.sleep(delay)


if __name__ == "__main__":
    # Test script to start up the flask server and show some dummy log data
    # This script should be called from the auto_rx directory with:
    # python -m autorx.web filename1_sonde.log filename2_sonde.log ..etc
    #
    import time, sys
    from autorx.config import read_auto_rx_config

    logging.basicConfig(
        format="%(asctime)s %(levelname)s:%(message)s", level=logging.DEBUG
    )
    logging.getLogger("werkzeug").setLevel(logging.ERROR)
    logging.getLogger("socketio").setLevel(logging.ERROR)
    logging.getLogger("engineio").setLevel(logging.ERROR)

    # Read in config, as the web interface now uses a lot of config data during startup.
    # TODO: Make this actually work... it doesnt seem to be writing into the global_config store
    # _temp_cfg = read_auto_rx_config('station.cfg')

    web_handler = WebHandler()
    logging.getLogger().addHandler(web_handler)
    start_flask()

    try:
        # If we have been provided some sonde logs as an argument, read them in.
        if len(sys.argv) > 1:
            test_web_interface(sys.argv[1:], delay=1.0)
        else:
            while flask_app_thread.isAlive():
                time.sleep(1)
                logging.info("This is a test message.")
    except:
        stop_flask()
