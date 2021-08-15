#!/usr/bin/env python
#
#   radiosonde_auto_rx - Utility Classes & Functions
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under MIT License
#

from __future__ import division, print_function
import codecs
import fcntl
import logging
import os
import platform
import re
import requests
import subprocess
import threading
import time
import numpy as np
import semver
from dateutil.parser import parse
from datetime import datetime, timedelta
from math import radians, degrees, sin, cos, atan2, sqrt, pi
from . import __version__ as auto_rx_version

try:
    # Python 2
    from Queue import Queue
except ImportError:
    # Python 3
    from queue import Queue


# List of binaries we check for on startup
REQUIRED_RS_UTILS = [
    "dft_detect",
    "dfm09mod",
    "m10mod",
    "imet1rs_dft",
    "rs41mod",
    "rs92mod",
    "fsk_demod",
    "mk2mod",
    "lms6Xmod",
    "meisei100mod",
    "imet54mod",
    "mp3h1mod",
]


def check_rs_utils():
    """ Check the required RS decoder binaries exist
        Currently we just check there is a file present - we don't check functionality.
    """
    for _file in REQUIRED_RS_UTILS:
        if not os.path.isfile(_file):
            logging.critical("Binary %s does not exist - did you run build.sh?" % _file)
            return False

    return True


AUTORX_MAIN_VERSION_URL = "https://raw.githubusercontent.com/projecthorus/radiosonde_auto_rx/master/auto_rx/autorx/__init__.py"
AUTORX_TESTING_VERSION_URL = "https://raw.githubusercontent.com/projecthorus/radiosonde_auto_rx/testing/auto_rx/autorx/__init__.py"


def get_autorx_version(version_url=AUTORX_MAIN_VERSION_URL):
    """ Parse an auto_rx __init__ file and return the version """
    try:
        _r = requests.get(version_url, timeout=5)
    except Exception as e:
        logging.exception(
            f"Version - Error determining version from URL {version_url}", e
        )
        return None

    try:
        for _line in _r.text.split("\n"):
            if _line.startswith("__version__"):
                _main_version = _line.split("=")[1]
                _main_version = _main_version.replace('"', "").strip()
                return _main_version

    except Exception as e:
        logging.exception(
            f"Version - Error extracting version from url {version_url}.", e
        )
        return None


def check_autorx_versions(current_version=auto_rx_version):
    """
        Check the current auto_rx version against the latest main and testing branches.
        Returns a string 'Latest' if this is the latest version, or the newer version if
        there is an update available. Returns 'Unknown' if the version could not be determined.
    """

    # Grab the current versions
    _main_branch_version = get_autorx_version(AUTORX_MAIN_VERSION_URL)
    _testing_branch_version = get_autorx_version(AUTORX_TESTING_VERSION_URL)

    if (_main_branch_version is None) or (_testing_branch_version is None):
        logging.error("Version - Could not determine latest versions.")
        return "Unknown"

    # First, determine if the user is on a main or beta (testing) version
    # We use the presence of a '-' in the version name to figure this out.
    if "-" in current_version:
        # User is on a testing branch version.
        # Compare against the testing branch version - when a release is made, the testing
        # branch will have the same version as the main branch, then will advance.
        if semver.compare(_testing_branch_version, current_version):
            # Newer testing version available.
            return _testing_branch_version
        else:
            # User is on latest testing branch version.
            return "Latest"
    else:
        # User is running the main branch
        if semver.compare(_main_branch_version, current_version):
            return _main_branch_version
        else:
            return "Latest"

    # Should never get here.
    return "Unknown"


def version_startup_check():
    """ Helper function to check version on startup """
    _newer_version = check_autorx_versions()
    if _newer_version == "Latest":
        logging.info(f"Version - Local Version: {auto_rx_version} - Up to date!")
    elif _newer_version == "Unknown":
        # An error will have already been printed out for this case.
        pass
    else:
        logging.info(
            f"Version - Local Version: {auto_rx_version} - Newer Version Available! ({_newer_version})"
        )


def strip_sonde_serial(serial):
    """ Strip off any leading sonde type that may be present in a serial number """

    # Look for serials with prefixes matching the following known sonde types.
    _re = re.compile("^(DFM|M10|M20|IMET|IMET54|MRZ|LMS6)-")

    # If we have a match, return the trailing part of the serial, re-adding
    # any - separators if they exist.
    if _re.match(serial):
        return "-".join(serial.split("-")[1:])
    else:
        # Otherwise, it's probably a RS41 or RS92
        return serial


def short_type_lookup(type_name):
    """ Lookup a short type name to a more descriptive name """

    if type_name.startswith("RS41"):
        if type_name == "RS41":
            return "Vaisala RS41"
        else:
            return "Vaisala " + type_name
    elif type_name.startswith("RS92"):
        if type_name == "RS92":
            return "Vaisala RS92"
        else:
            return "Vaisala " + type_name
    elif type_name.startswith("DFM"):
        return "Graw " + type_name
    elif type_name.startswith("M10"):
        return "Meteomodem M10"
    elif type_name.startswith("M20"):
        return "Meteomodem M20"
    elif type_name == "LMS6":
        return "Lockheed Martin LMS6-400"
    elif type_name == "MK2LMS":
        return "Lockheed Martin LMS6-1680"
    elif type_name == "IMET":
        return "Intermet Systems iMet-1/4"
    elif type_name == "IMET5":
        return "Intermet Systems iMet-54"
    elif type_name == "MEISEI":
        return "Meisei iMS-100/RS-11"
    elif type_name == "MRZ":
        return "Meteo-Radiy MRZ"
    else:
        return "Unknown"

def short_short_type_lookup(type_name):
    """ Lookup a short type name to a more descriptive, but short name """

    if type_name.startswith("RS41"):
        if type_name == "RS41":
            return "RS41"
        else:
            return type_name
    elif type_name.startswith("RS92"):
        if type_name == "RS92":
            return "RS92"
        else:
            return type_name
    elif type_name.startswith("DFM"):
        return type_name
    elif type_name.startswith("M10"):
        return "M10"
    elif type_name.startswith("M20"):
        return "M20"
    elif type_name == "LMS6":
        return "LMS6-400"
    elif type_name == "MK2LMS":
        return "LMS6-1680"
    elif type_name == "IMET":
        return "iMet-1/4"
    elif type_name == "IMET5":
        return "iMet-54"
    elif type_name == "MEISEI":
        return "iMS-100"
    elif type_name == "MRZ":
        return "MRZ"
    else:
        return "Unknown"


def generate_aprs_id(sonde_data):
        """ Generate an APRS-compatible object name based on the radiosonde type and ID. """

        _object_name = None

        # Use the radiosonde ID as the object ID
        if ("RS92" in sonde_data["type"]) or ("RS41" in sonde_data["type"]):
            # We can use the Vaisala sonde ID directly.
            _object_name = sonde_data["id"].strip()
        elif "DFM" in sonde_data["type"]:
            # As per agreement with other radiosonde decoding software developers, we will now
            # use the DFM serial number verbatim in the APRS ID, prefixed with 'D'.
            # For recent DFM sondes, this will result in a object ID of: Dyynnnnnn
            # Where yy is the manufacture year, and nnnnnn is a sequential serial.
            # Older DFMs may have only a 6-digit ID of Dnnnnnn.
            # Mark J - 2019-12-29

            # Split out just the serial number part of the ID, and cast it to an int
            # This acts as another check that we have been provided with a numeric serial.
            _dfm_id = int(sonde_data["id"].split("-")[-1])

            # Create the object name
            _object_name = "D%d" % _dfm_id

            # Convert to upper-case hex, and take the last 5 nibbles.
            _id_suffix = hex(_dfm_id).upper()[-5:]

        elif "M10" in sonde_data["type"]:
            # Use the generated id same as dxlAPRS
            _object_name = sonde_data["aprsid"]

        elif "M20" in sonde_data["type"]:
            # Generate the M20 ID based on the first two hex digits of the
            # raw hexadecimal id, followed by the last decimal section.
            # Why we do this and not just use the three hex bytes, nobody knows...
            if 'rawid' in sonde_data:
                _object_name = "ME" + sonde_data['rawid'].split('_')[1][:2] + sonde_data["id"].split("-")[-1]
            else:
                _object_name = None

        elif "IMET" in sonde_data["type"]:
            # Use the last 5 characters of the unique ID we have generated.
            _object_name = "IMET" + sonde_data["id"][-5:]

        elif "LMS" in sonde_data["type"]:
            # Use the last 5 hex digits of the sonde ID.
            _id_suffix = int(sonde_data["id"].split("-")[1])
            _id_hex = hex(_id_suffix).upper()
            _object_name = "LMS6" + _id_hex[-5:]

        elif "MEISEI" in sonde_data["type"]:
            # Convert the serial number to an int
            _meisei_id = int(sonde_data["id"].split("-")[-1])
            _id_suffix = hex(_meisei_id).upper().split("0X")[1]
            # Clip to 6 hex digits, in case we end up with more for some reason.
            if len(_id_suffix) > 6:
                _id_suffix = _id_suffix[-6:]
            _object_name = "IMS" + _id_suffix

        elif "MRZ" in sonde_data["type"]:
            # Concatenate the two portions of the serial number, convert to an int,
            # then take the 6 least-significant hex digits as our ID, prefixed with 'MRZ'.
            # e.g. MRZ-5667-39155 -> 566739155 -> 21C7C0D3 -> MRZC7C0D3
            _mrz_id_parts = sonde_data["id"].split("-")
            _mrz_id = int(_mrz_id_parts[1] + _mrz_id_parts[2])
            _id_hex = "%06x" % _mrz_id
            if len(_id_hex) > 6:
                _id_hex = _id_hex[-6:]
            _object_name = "MRZ" + _id_hex.upper()

        # New Sonde types will be added in here.
        else:
            # Unknown sonde type, don't know how to handle this yet.
            _object_name = None
        
        # Pad or clip to 9 characters
        if len(_object_name) > 9:
            _object_name = _object_name[:9]
        elif len(_object_name) < 9:
            _object_name = _object_name + " " * (9 - len(_object_name))
        
        return _object_name


def readable_timedelta(duration: timedelta):
    """ 
    Convert a timedelta into a readable string.
    From: https://codereview.stackexchange.com/a/245215
    """
    data = {}
    data["months"], remaining = divmod(duration.total_seconds(), 2_592_000)
    data["days"], remaining = divmod(remaining, 86_400)
    data["hours"], remaining = divmod(remaining, 3_600)
    data["minutes"], _foo = divmod(remaining, 60)

    time_parts = [f"{round(value)} {name}" for name, value in data.items() if value > 0]
    if time_parts:
        return " ".join(time_parts)
    else:
        return "below 1 second"


class AsynchronousFileReader(threading.Thread):
    """ Asynchronous File Reader
    Helper class to implement asynchronous reading of a file
    in a separate thread. Pushes read lines on a queue to
    be consumed in another thread.
    see https://github.com/soxofaan/asynchronousfilereader
    MIT License
    Copyright (c) 2014 Stefaan Lippens
    """

    def __init__(self, fd, queue=None, autostart=True):
        self._fd = fd
        if queue is None:
            queue = Queue()
        self.queue = queue
        self.running = True

        threading.Thread.__init__(self)

        if autostart:
            self.start()

    def run(self):
        """
        The body of the thread: read lines and put them on the queue.
        """
        while self.running:
            line = self._fd.readline()
            if not line:
                break
            self.queue.put(line)

    def eof(self):
        """
        Check whether there is no more content to expect.
        """
        return not self.is_alive() and self.queue.empty()

    def stop(self):
        """
        Stop the running thread.
        """
        self.running = False

    def readlines(self):
        """
        Get currently available lines.
        """
        while not self.queue.empty():
            yield self.queue.get()


#
#   Peak Search Utilities, used by the sonde scanning functions.
#


def detect_peaks(
    x,
    mph=None,
    mpd=1,
    threshold=0,
    edge="rising",
    kpsh=False,
    valley=False,
    show=False,
    ax=None,
):

    """Detect peaks in data based on their amplitude and other features.

    Author: Marcos Duarte, https://github.com/demotu/BMC

    Parameters
    ----------
    x : 1D array_like
        data.
    mph : {None, number}, optional (default = None)
        detect peaks that are greater than minimum peak height.
    mpd : positive integer, optional (default = 1)
        detect peaks that are at least separated by minimum peak distance (in
        number of data).
    threshold : positive number, optional (default = 0)
        detect peaks (valleys) that are greater (smaller) than `threshold`
        in relation to their immediate neighbors.
    edge : {None, 'rising', 'falling', 'both'}, optional (default = 'rising')
        for a flat peak, keep only the rising edge ('rising'), only the
        falling edge ('falling'), both edges ('both'), or don't detect a
        flat peak (None).
    kpsh : bool, optional (default = False)
        keep peaks with same height even if they are closer than `mpd`.
    valley : bool, optional (default = False)
        if True (1), detect valleys (local minima) instead of peaks.
    show : bool, optional (default = False)
        if True (1), plot data in matplotlib figure.
    ax : a matplotlib.axes.Axes instance, optional (default = None).

    Returns
    -------
    ind : 1D array_like
        indeces of the peaks in `x`.

    Notes
    -----
    The detection of valleys instead of peaks is performed internally by simply
    negating the data: `ind_valleys = detect_peaks(-x)`

    The function can handle NaN's

    See this IPython Notebook [1]_.

    References
    ----------
    .. [1] http://nbviewer.ipython.org/github/demotu/BMC/blob/master/notebooks/DetectPeaks.ipynb

    Examples
    --------
    >>> from detect_peaks import detect_peaks
    >>> x = np.random.randn(100)
    >>> x[60:81] = np.nan
    >>> # detect all peaks and plot data
    >>> ind = detect_peaks(x, show=True)
    >>> print(ind)

    >>> x = np.sin(2*np.pi*5*np.linspace(0, 1, 200)) + np.random.randn(200)/5
    >>> # set minimum peak height = 0 and minimum peak distance = 20
    >>> detect_peaks(x, mph=0, mpd=20, show=True)

    >>> x = [0, 1, 0, 2, 0, 3, 0, 2, 0, 1, 0]
    >>> # set minimum peak distance = 2
    >>> detect_peaks(x, mpd=2, show=True)

    >>> x = np.sin(2*np.pi*5*np.linspace(0, 1, 200)) + np.random.randn(200)/5
    >>> # detection of valleys instead of peaks
    >>> detect_peaks(x, mph=0, mpd=20, valley=True, show=True)

    >>> x = [0, 1, 1, 0, 1, 1, 0]
    >>> # detect both edges
    >>> detect_peaks(x, edge='both', show=True)

    >>> x = [-2, 1, -2, 2, 1, 1, 3, 0]
    >>> # set threshold = 2
    >>> detect_peaks(x, threshold = 2, show=True)
    """

    x = np.atleast_1d(x).astype("float64")
    if x.size < 3:
        return np.array([], dtype=int)
    if valley:
        x = -x
    # find indices of all peaks
    dx = x[1:] - x[:-1]
    # handle NaN's
    indnan = np.where(np.isnan(x))[0]
    if indnan.size:
        x[indnan] = np.inf
        dx[np.where(np.isnan(dx))[0]] = np.inf
    ine, ire, ife = np.array([[], [], []], dtype=int)
    if not edge:
        ine = np.where((np.hstack((dx, 0)) < 0) & (np.hstack((0, dx)) > 0))[0]
    else:
        if edge.lower() in ["rising", "both"]:
            ire = np.where((np.hstack((dx, 0)) <= 0) & (np.hstack((0, dx)) > 0))[0]
        if edge.lower() in ["falling", "both"]:
            ife = np.where((np.hstack((dx, 0)) < 0) & (np.hstack((0, dx)) >= 0))[0]
    ind = np.unique(np.hstack((ine, ire, ife)))
    # handle NaN's
    if ind.size and indnan.size:
        # NaN's and values close to NaN's cannot be peaks
        ind = ind[
            np.in1d(
                ind, np.unique(np.hstack((indnan, indnan - 1, indnan + 1))), invert=True
            )
        ]
    # first and last values of x cannot be peaks
    if ind.size and ind[0] == 0:
        ind = ind[1:]
    if ind.size and ind[-1] == x.size - 1:
        ind = ind[:-1]
    # remove peaks < minimum peak height
    if ind.size and mph is not None:
        ind = ind[x[ind] >= mph]
    # remove peaks - neighbors < threshold
    if ind.size and threshold > 0:
        dx = np.min(np.vstack([x[ind] - x[ind - 1], x[ind] - x[ind + 1]]), axis=0)
        ind = np.delete(ind, np.where(dx < threshold)[0])
    # detect small peaks closer than minimum peak distance
    if ind.size and mpd > 1:
        ind = ind[np.argsort(x[ind])][::-1]  # sort ind by peak height
        idel = np.zeros(ind.size, dtype=bool)
        for i in range(ind.size):
            if not idel[i]:
                # keep peaks with the same height if kpsh is True
                idel = idel | (ind >= ind[i] - mpd) & (ind <= ind[i] + mpd) & (
                    x[ind[i]] > x[ind] if kpsh else True
                )
                idel[i] = 0  # Keep current peak
        # remove the small peaks and sort back the indices by their occurrence
        ind = np.sort(ind[~idel])

    if show:
        if indnan.size:
            x[indnan] = np.nan
        if valley:
            x = -x
        peak_plot(x, mph, mpd, threshold, edge, valley, ax, ind)

    return ind


def peak_plot(x, mph, mpd, threshold, edge, valley, ax, ind):
    """Plot results of the detect_peaks function, see its help."""
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib is not available.")
    else:
        if ax is None:
            _, ax = plt.subplots(1, 1, figsize=(8, 4))

        ax.plot(x, "b", lw=1)
        if ind.size:
            label = "valley" if valley else "peak"
            label = label + "s" if ind.size > 1 else label
            ax.plot(
                ind,
                x[ind],
                "+",
                mfc=None,
                mec="r",
                mew=2,
                ms=8,
                label="%d %s" % (ind.size, label),
            )
            ax.legend(loc="best", framealpha=0.5, numpoints=1)
        ax.set_xlim(-0.02 * x.size, x.size * 1.02 - 1)
        ymin, ymax = x[np.isfinite(x)].min(), x[np.isfinite(x)].max()
        yrange = ymax - ymin if ymax > ymin else 1
        ax.set_ylim(ymin - 0.1 * yrange, ymax + 0.1 * yrange)
        ax.set_xlabel("Data #", fontsize=14)
        ax.set_ylabel("Amplitude", fontsize=14)
        mode = "Valley detection" if valley else "Peak detection"
        ax.set_title(
            "%s (mph=%s, mpd=%d, threshold=%s, edge='%s')"
            % (mode, str(mph), mpd, str(threshold), edge)
        )
        # plt.grid()
        plt.show()


#
#   RTLSDR Utility Functions
#

# Regexes to help parse lsusb's output
_INDENTATION_RE = re.compile(r"^( *)")
_LSUSB_BUS_DEVICE_RE = re.compile(r"^Bus (\d{3}) Device (\d{3}):")
_LSUSB_ENTRY_RE = re.compile(r"^ *([^ ]+) +([^ ]+) *([^ ].*)?$")
_LSUSB_GROUP_RE = re.compile(r"^ *([^ ]+.*):$")

# USB Reset ioctl argument
_USBDEVFS_RESET = ord("U") << 8 | 20

# List of known RTLSDR-Compatible devices, taken from
# https://github.com/steve-m/librtlsdr/blob/master/src/librtlsdr.c#L313
KNOWN_RTLSDR_DEVICES = [
    ["0x0bda", "0x2832", "Generic RTL2832U"],
    ["0x0bda", "0x2838", "Generic RTL2832U OEM"],
    ["0x0413", "0x6680", "DigitalNow Quad DVB-T PCI-E card"],
    ["0x0413", "0x6f0f", "Leadtek WinFast DTV Dongle mini D"],
    ["0x0458", "0x707f", "Genius TVGo DVB-T03 USB dongle (Ver. B)"],
    ["0x0ccd", "0x00a9", "Terratec Cinergy T Stick Black (rev 1)"],
    ["0x0ccd", "0x00b3", "Terratec NOXON DAB/DAB+ USB dongle (rev 1)"],
    ["0x0ccd", "0x00b4", "Terratec Deutschlandradio DAB Stick"],
    ["0x0ccd", "0x00b5", "Terratec NOXON DAB Stick - Radio Energy"],
    ["0x0ccd", "0x00b7", "Terratec Media Broadcast DAB Stick"],
    ["0x0ccd", "0x00b8", "Terratec BR DAB Stick"],
    ["0x0ccd", "0x00b9", "Terratec WDR DAB Stick"],
    ["0x0ccd", "0x00c0", "Terratec MuellerVerlag DAB Stick"],
    ["0x0ccd", "0x00c6", "Terratec Fraunhofer DAB Stick"],
    ["0x0ccd", "0x00d3", "Terratec Cinergy T Stick RC (Rev.3)"],
    ["0x0ccd", "0x00d7", "Terratec T Stick PLUS"],
    ["0x0ccd", "0x00e0", "Terratec NOXON DAB/DAB+ USB dongle (rev 2)"],
    ["0x1554", "0x5020", "PixelView PV-DT235U(RN)"],
    ["0x15f4", "0x0131", "Astrometa DVB-T/DVB-T2"],
    ["0x15f4", "0x0133", "HanfTek DAB+FM+DVB-T"],
    ["0x185b", "0x0620", "Compro Videomate U620F"],
    ["0x185b", "0x0650", "Compro Videomate U650F"],
    ["0x185b", "0x0680", "Compro Videomate U680F"],
    ["0x1b80", "0xd393", "GIGABYTE GT-U7300"],
    ["0x1b80", "0xd394", "DIKOM USB-DVBT HD"],
    ["0x1b80", "0xd395", "Peak 102569AGPK"],
    ["0x1b80", "0xd397", "KWorld KW-UB450-T USB DVB-T Pico TV"],
    ["0x1b80", "0xd398", "Zaapa ZT-MINDVBZP"],
    ["0x1b80", "0xd39d", "SVEON STV20 DVB-T USB & FM"],
    ["0x1b80", "0xd3a4", "Twintech UT-40"],
    ["0x1b80", "0xd3a8", "ASUS U3100MINI_PLUS_V2"],
    ["0x1b80", "0xd3af", "SVEON STV27 DVB-T USB & FM"],
    ["0x1b80", "0xd3b0", "SVEON STV21 DVB-T USB & FM"],
    ["0x1d19", "0x1101", "Dexatek DK DVB-T Dongle (Logilink VG0002A)"],
    ["0x1d19", "0x1102", "Dexatek DK DVB-T Dongle (MSI DigiVox mini II V3.0)"],
    ["0x1d19", "0x1103", "Dexatek Technology Ltd. DK 5217 DVB-T Dongle"],
    ["0x1d19", "0x1104", "MSI DigiVox Micro HD"],
    ["0x1f4d", "0xa803", "Sweex DVB-T USB"],
    ["0x1f4d", "0xb803", "GTek T803"],
    ["0x1f4d", "0xc803", "Lifeview LV5TDeluxe"],
    ["0x1f4d", "0xd286", "MyGica TD312"],
    ["0x1f4d", "0xd803", "PROlectrix DV107669"],
]


def lsusb():
    """Call lsusb and return the parsed output.

    Returns:
        (list): List of dictionaries containing the device information for each USB device.
    """
    try:
        FNULL = open(os.devnull, "w")
        lsusb_raw_output = subprocess.check_output(["lsusb", "-v"], stderr=FNULL)
        FNULL.close()
        # Convert from bytes.
        lsusb_raw_output = lsusb_raw_output.decode("utf8")
    except Exception as e:
        logging.error("lsusb parse error - %s" % str(e))
        return

    device = None
    devices = []
    depth_stack = []
    for line in lsusb_raw_output.splitlines():
        if not line:
            if device:
                devices.append(device)
            device = None
            continue

        if not device:
            m = _LSUSB_BUS_DEVICE_RE.match(line)
            if m:
                device = {"bus": m.group(1), "device": m.group(2)}
                depth_stack = [device]
            continue

        indent_match = _INDENTATION_RE.match(line)
        if not indent_match:
            continue

        depth = 1 + len(indent_match.group(1)) / 2
        if depth > len(depth_stack):
            logging.debug('lsusb parsing error: unexpected indentation: "%s"', line)
            continue

        while depth < len(depth_stack):
            depth_stack.pop()

        cur = depth_stack[-1]
        m = _LSUSB_GROUP_RE.match(line)

        if m:
            new_group = {}
            cur[m.group(1)] = new_group
            depth_stack.append(new_group)
            continue

        m = _LSUSB_ENTRY_RE.match(line)
        if m:
            new_entry = {
                "_value": m.group(2),
                "_desc": m.group(3),
            }
            cur[m.group(1)] = new_entry
            depth_stack.append(new_entry)
            continue

        logging.debug('lsusb parsing error: unrecognized line: "%s"', line)

    if device:
        devices.append(device)

    return devices


def is_not_linux():
    """
    Attempt to detect a non-native-Linux system (e.g. OSX or WSL),
    where lsusb isn't going to work.
    """
    # Basic check for non-Linux platforms (e.g. Darwin or Windows)
    if platform.system() != "Linux":
        return True

    # Second check for the existence of '-Microsoft' in the uname release field.
    # This is a good check that we are running in WSL.
    # Note the use of indexing instead of the named field, for Python 2 & 3 compatability.
    if "Microsoft" in platform.uname()[2]:
        return True

    # Else, we're probably in native Linux!
    return False


def reset_usb(bus, device):
    """Reset the USB device with the given bus and device."""
    usb_file_path = "/dev/bus/usb/%03d/%03d" % (bus, device)
    with open(usb_file_path, "w") as usb_file:
        # logging.debug('fcntl.ioctl(%s, %d)', usb_file_path, _USBDEVFS_RESET)
        try:
            fcntl.ioctl(usb_file, _USBDEVFS_RESET)

        except IOError:
            logging.error("RTLSDR - USB Reset Failed.")


def is_rtlsdr(vid, pid):
    """ Check if a device with given VID/PID is a known RTLSDR """
    for _dev in KNOWN_RTLSDR_DEVICES:
        _vid = _dev[0]
        _pid = _dev[1]
        if (vid == _vid) and (pid == _pid):
            return True

    return False


def reset_rtlsdr_by_serial(serial):
    """ Attempt to reset a RTLSDR with a provided serial number """

    # If not Linux, return immediately.
    if is_not_linux():
        logging.debug("RTLSDR - Not a native Linux system, skipping reset attempt.")
        return

    lsusb_info = lsusb()
    bus_num = None
    device_num = None

    for device in lsusb_info:
        try:
            device_serial = device["Device Descriptor"]["iSerial"]["_desc"]
            device_product = device["Device Descriptor"]["iProduct"]["_desc"]
            device_pid = device["Device Descriptor"]["idProduct"]["_value"]
            device_vid = device["Device Descriptor"]["idVendor"]["_value"]
        except:
            # If we hit an exception, the device likely doesn't have one of the required fields.
            continue

        if (device_serial == serial) and is_rtlsdr(device_vid, device_pid):
            bus_num = int(device["bus"])
            device_num = int(device["device"])

    if bus_num and device_num:
        logging.info(
            "RTLSDR - Attempting to reset: /dev/bus/usb/%03d/%03d"
            % (bus_num, device_num)
        )
        reset_usb(bus_num, device_num)
    else:
        logging.error("RTLSDR - Could not find RTLSDR with serial %s!" % serial)
        return False


def find_rtlsdr(serial=None):
    """ Search through lsusb and see if an RTLSDR exists """

    # If not Linux, return immediately, and assume the RTLSDR exists..
    if is_not_linux():
        logging.debug("RTLSDR - Not a native Linux system, skipping RTLSDR search.")
        return True

    lsusb_info = lsusb()
    bus_num = None
    device_num = None

    for device in lsusb_info:
        try:
            device_serial = device["Device Descriptor"]["iSerial"]["_desc"]
            device_product = device["Device Descriptor"]["iProduct"]["_desc"]
            device_pid = device["Device Descriptor"]["idProduct"]["_value"]
            device_vid = device["Device Descriptor"]["idVendor"]["_value"]
        except:
            # If we hit an exception, the device likely doesn't have one of the required fields.
            continue

        if is_rtlsdr(device_vid, device_pid):
            # We have found a RTLSDR! If we're not looking for a particular serial number, we can just quit now.
            if serial == None:
                return True
            else:
                if device_serial == serial:
                    bus_num = int(device["bus"])
                    device_num = int(device["device"])

    if bus_num and device_num:
        # We have found an RTLSDR with this serial number!
        return True

    else:
        # Otherwise, nope.
        return False


def reset_all_rtlsdrs():
    """ Reset all RTLSDR devices found in the lsusb tree """

    # If not Linux, return immediately.
    if is_not_linux():
        logging.debug("RTLSDR - Not a native Linux system, skipping reset attempt.")
        return

    lsusb_info = lsusb()
    bus_num = None
    device_num = None

    for device in lsusb_info:
        try:
            device_product = device["Device Descriptor"]["iProduct"]["_desc"]
            device_pid = device["Device Descriptor"]["idProduct"]["_value"]
            device_vid = device["Device Descriptor"]["idVendor"]["_value"]
        except:
            # If we hit an exception, the device likely doesn't have one of the required fields.
            continue

        if is_rtlsdr(device_vid, device_pid):
            bus_num = int(device["bus"])
            device_num = int(device["device"])

            logging.info(
                "RTLSDR - Attempting to reset: Bus: %d  Device: %d"
                % (bus_num, device_num)
            )
            reset_usb(bus_num, device_num)

    if device_num is None:
        logging.error("RTLSDR - Could not find any RTLSDR devices to reset!")


def rtlsdr_test(device_idx="0", rtl_sdr_path="rtl_sdr", retries=5):
    """ Test that a RTLSDR with supplied device ID is accessible.

    This function attempts to read a small set of samples from a rtlsdr using rtl-sdr.
    The exit code from rtl-sdr indicates if the attempt was successful, and hence shows if the rtlsdr is usable.

    Args:
        device_idx (int or str): Device index or serial number of the RTLSDR to test. Defaults to 0.
        rtl_sdr_path (str): Path to the rtl_sdr utility. Defaults to 'rtl_sdr' (i.e. look on the system path)

    Returns:
        bool: True if the RTLSDR device is accessible, False otherwise.
    """

    # Immediately return true for any SDR with a device ID that starts with TCP,
    # as this indicates this is not actually a RTLSDR, but a client connecting to some other
    # SDR server.
    if device_idx.startswith("TCP"):
        logging.debug("RTLSDR - TCP Device, skipping RTLSDR test step.")
        return True

    _rtl_cmd = "timeout 5 %s -d %s -n 200000 - > /dev/null" % (
        rtl_sdr_path,
        str(device_idx),
    )

    # First, check if the RTLSDR with a provided serial number is present.
    if device_idx == "0":
        # Check for the presence of any RTLSDRs.
        _rtl_exists = find_rtlsdr()

    else:
        # Otherwise, look for a particular RTLSDR
        _rtl_exists = find_rtlsdr(device_idx)

    if not _rtl_exists:
        logging.error(
            "RTLSDR - RTLSDR with serial #%s is not present!" % str(device_idx)
        )
        return False

    # So now we know the rtlsdr we are attempting to test does exist.
    # We make an attempt to read samples from it:

    _rtlsdr_retries = retries

    while _rtlsdr_retries > 0:
        try:
            FNULL = open(os.devnull, "w")  # Inhibit stderr output
            _ret_code = subprocess.check_call(_rtl_cmd, shell=True, stderr=FNULL)
            FNULL.close()
        except subprocess.CalledProcessError:
            # This exception means the subprocess has returned an error code of one.
            # This indicates either the RTLSDR doesn't exist, or
            pass
        else:
            # rtl-sdr returned OK. We can return True now.
            time.sleep(1)
            return True

        # If we get here, it means we failed to read any samples from the RTLSDR.
        # So, we attempt to reset it.
        if device_idx == "0":
            reset_all_rtlsdrs()
        else:
            reset_rtlsdr_by_serial(device_idx)

        # Decrement out retry count, then wait a bit before looping
        _rtlsdr_retries -= 1
        time.sleep(2)

    # If we run out of retries, clearly the RTLSDR isn't working.
    logging.error(
        "RTLSDR - RTLSDR with serial #%s was not recovered after %d reset attempts."
        % (str(device_idx), retries)
    )
    return False


# Earthmaths code by Daniel Richman (thanks!)
# Copyright 2012 (C) Daniel Richman; GNU GPL 3
def position_info(listener, balloon):
    """
    Calculate and return information from 2 (lat, lon, alt) tuples

    Returns a dict with:

     - angle at centre
     - great circle distance
     - distance in a straight line
     - bearing (azimuth or initial course)
     - elevation (altitude)

    Input and output latitudes, longitudes, angles, bearings and elevations are
    in degrees, and input altitudes and output distances are in meters.
    """

    # Earth:
    radius = 6371000.0

    (lat1, lon1, alt1) = listener
    (lat2, lon2, alt2) = balloon

    lat1 = radians(lat1)
    lat2 = radians(lat2)
    lon1 = radians(lon1)
    lon2 = radians(lon2)

    # Calculate the bearing, the angle at the centre, and the great circle
    # distance using Vincenty's_formulae with f = 0 (a sphere). See
    # http://en.wikipedia.org/wiki/Great_circle_distance#Formulas and
    # http://en.wikipedia.org/wiki/Great-circle_navigation and
    # http://en.wikipedia.org/wiki/Vincenty%27s_formulae
    d_lon = lon2 - lon1
    sa = cos(lat2) * sin(d_lon)
    sb = (cos(lat1) * sin(lat2)) - (sin(lat1) * cos(lat2) * cos(d_lon))
    bearing = atan2(sa, sb)
    aa = sqrt((sa ** 2) + (sb ** 2))
    ab = (sin(lat1) * sin(lat2)) + (cos(lat1) * cos(lat2) * cos(d_lon))
    angle_at_centre = atan2(aa, ab)
    great_circle_distance = angle_at_centre * radius

    # Armed with the angle at the centre, calculating the remaining items
    # is a simple 2D triangley circley problem:

    # Use the triangle with sides (r + alt1), (r + alt2), distance in a
    # straight line. The angle between (r + alt1) and (r + alt2) is the
    # angle at the centre. The angle between distance in a straight line and
    # (r + alt1) is the elevation plus pi/2.

    # Use sum of angle in a triangle to express the third angle in terms
    # of the other two. Use sine rule on sides (r + alt1) and (r + alt2),
    # expand with compound angle formulae and solve for tan elevation by
    # dividing both sides by cos elevation
    ta = radius + alt1
    tb = radius + alt2
    ea = (cos(angle_at_centre) * tb) - ta
    eb = sin(angle_at_centre) * tb
    elevation = atan2(ea, eb)

    # Use cosine rule to find unknown side.
    distance = sqrt((ta ** 2) + (tb ** 2) - 2 * tb * ta * cos(angle_at_centre))

    # Give a bearing in range 0 <= b < 2pi
    if bearing < 0:
        bearing += 2 * pi

    return {
        "listener": listener,
        "balloon": balloon,
        "listener_radians": (lat1, lon1, alt1),
        "balloon_radians": (lat2, lon2, alt2),
        "angle_at_centre": degrees(angle_at_centre),
        "angle_at_centre_radians": angle_at_centre,
        "bearing": degrees(bearing),
        "bearing_radians": bearing,
        "great_circle_distance": great_circle_distance,
        "straight_distance": distance,
        "elevation": degrees(elevation),
        "elevation_radians": elevation,
    }


def peak_decimation(freq, power, factor):
    """ Peak-preserving Decimation.

    Args:
        freq (list): Frequency Data.
        power (list): Power data.
        factor (int): Decimation factor.

    Returns:
        tuple: (freq, power)
    """

    _out_len = len(freq) // factor

    _freq_out = []
    _power_out = []

    try:
        for i in range(_out_len):
            _f_slice = freq[i * factor : i * factor + factor]
            _p_slice = power[i * factor : i * factor + factor]

            _freq_out.append(_f_slice[np.argmax(_p_slice)])
            _power_out.append(_p_slice.max())
    except:
        pass

    return (_freq_out, _power_out)


if __name__ == "__main__":
    import sys

    logging.basicConfig(
        format="%(asctime)s %(levelname)s:%(message)s", level=logging.DEBUG
    )
    check_autorx_version()
