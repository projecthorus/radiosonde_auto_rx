#!/usr/bin/env python
#
#   radiosonde_auto_rx - Utility Classes & Functions
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under MIT License
#

from __future__ import division, print_function
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
REQUIRED_RS_UTILS = ['dft_detect', 'dfm09mod', 'm10mod', 'imet1rs_dft', 'rs41mod', 'rs92mod', 'fsk_demod', 'mk2a_lms1680', 'lms6Xmod', 'meisei100mod']

def check_rs_utils():
    """ Check the required RS decoder binaries exist
        Currently we just check there is a file present - we don't check functionality.
    """
    for _file in REQUIRED_RS_UTILS:
        if not os.path.isfile(_file):
            logging.critical("Binary %s does not exist - did you run build.sh?" % _file)
            return False

    return True


AUTORX_VERSION_URL = "https://raw.githubusercontent.com/projecthorus/radiosonde_auto_rx/master/auto_rx/autorx/__init__.py"
def check_autorx_version():
    """ Grab the latest __init__ file from Github and compare the version with our current version. """
    try:
        _r = requests.get(AUTORX_VERSION_URL,timeout=5)
    except Exception as e:
        logging.error("Version - Error determining latest master version - %s" % str(e))
        return

    _version = "Unknown"

    try:
        for _line in _r.text.split('\n'):
            if _line.startswith("__version__"):
                _version = _line.split('=')[1]
                _version = _version.replace("\"", "").strip()
                break
    except Exception as e:
        logging.error("Version - Error determining latest master version.")

    logging.info("Version - Local Version: %s  Current Master Version: %s" % (auto_rx_version, _version))





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


def detect_peaks(x, mph=None, mpd=1, threshold=0, edge='rising',
                 kpsh=False, valley=False, show=False, ax=None):

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

    x = np.atleast_1d(x).astype('float64')
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
        if edge.lower() in ['rising', 'both']:
            ire = np.where((np.hstack((dx, 0)) <= 0) & (np.hstack((0, dx)) > 0))[0]
        if edge.lower() in ['falling', 'both']:
            ife = np.where((np.hstack((dx, 0)) < 0) & (np.hstack((0, dx)) >= 0))[0]
    ind = np.unique(np.hstack((ine, ire, ife)))
    # handle NaN's
    if ind.size and indnan.size:
        # NaN's and values close to NaN's cannot be peaks
        ind = ind[np.in1d(ind, np.unique(np.hstack((indnan, indnan-1, indnan+1))), invert=True)]
    # first and last values of x cannot be peaks
    if ind.size and ind[0] == 0:
        ind = ind[1:]
    if ind.size and ind[-1] == x.size-1:
        ind = ind[:-1]
    # remove peaks < minimum peak height
    if ind.size and mph is not None:
        ind = ind[x[ind] >= mph]
    # remove peaks - neighbors < threshold
    if ind.size and threshold > 0:
        dx = np.min(np.vstack([x[ind]-x[ind-1], x[ind]-x[ind+1]]), axis=0)
        ind = np.delete(ind, np.where(dx < threshold)[0])
    # detect small peaks closer than minimum peak distance
    if ind.size and mpd > 1:
        ind = ind[np.argsort(x[ind])][::-1]  # sort ind by peak height
        idel = np.zeros(ind.size, dtype=bool)
        for i in range(ind.size):
            if not idel[i]:
                # keep peaks with the same height if kpsh is True
                idel = idel | (ind >= ind[i] - mpd) & (ind <= ind[i] + mpd) \
                    & (x[ind[i]] > x[ind] if kpsh else True)
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
        print('matplotlib is not available.')
    else:
        if ax is None:
            _, ax = plt.subplots(1, 1, figsize=(8, 4))

        ax.plot(x, 'b', lw=1)
        if ind.size:
            label = 'valley' if valley else 'peak'
            label = label + 's' if ind.size > 1 else label
            ax.plot(ind, x[ind], '+', mfc=None, mec='r', mew=2, ms=8,
                    label='%d %s' % (ind.size, label))
            ax.legend(loc='best', framealpha=.5, numpoints=1)
        ax.set_xlim(-.02*x.size, x.size*1.02-1)
        ymin, ymax = x[np.isfinite(x)].min(), x[np.isfinite(x)].max()
        yrange = ymax - ymin if ymax > ymin else 1
        ax.set_ylim(ymin - 0.1*yrange, ymax + 0.1*yrange)
        ax.set_xlabel('Data #', fontsize=14)
        ax.set_ylabel('Amplitude', fontsize=14)
        mode = 'Valley detection' if valley else 'Peak detection'
        ax.set_title("%s (mph=%s, mpd=%d, threshold=%s, edge='%s')"
                     % (mode, str(mph), mpd, str(threshold), edge))
        # plt.grid()
        plt.show()





#
#   RTLSDR Utility Functions
#

# Regexes to help parse lsusb's output
_INDENTATION_RE = re.compile(r'^( *)')
_LSUSB_BUS_DEVICE_RE = re.compile(r'^Bus (\d{3}) Device (\d{3}):')
_LSUSB_ENTRY_RE = re.compile(r'^ *([^ ]+) +([^ ]+) *([^ ].*)?$')
_LSUSB_GROUP_RE = re.compile(r'^ *([^ ]+.*):$')

# USB Reset ioctl argument
_USBDEVFS_RESET = ord('U') << 8 | 20

# List of known RTLSDR-Compatible devices, taken from
# https://github.com/steve-m/librtlsdr/blob/master/src/librtlsdr.c#L313
KNOWN_RTLSDR_DEVICES = [
    [ '0x0bda', '0x2832', "Generic RTL2832U" ],
    [ '0x0bda', '0x2838', "Generic RTL2832U OEM" ],
    [ '0x0413', '0x6680', "DigitalNow Quad DVB-T PCI-E card" ],
    [ '0x0413', '0x6f0f', "Leadtek WinFast DTV Dongle mini D" ],
    [ '0x0458', '0x707f', "Genius TVGo DVB-T03 USB dongle (Ver. B)" ],
    [ '0x0ccd', '0x00a9', "Terratec Cinergy T Stick Black (rev 1)" ],
    [ '0x0ccd', '0x00b3', "Terratec NOXON DAB/DAB+ USB dongle (rev 1)" ],
    [ '0x0ccd', '0x00b4', "Terratec Deutschlandradio DAB Stick" ],
    [ '0x0ccd', '0x00b5', "Terratec NOXON DAB Stick - Radio Energy" ],
    [ '0x0ccd', '0x00b7', "Terratec Media Broadcast DAB Stick" ],
    [ '0x0ccd', '0x00b8', "Terratec BR DAB Stick" ],
    [ '0x0ccd', '0x00b9', "Terratec WDR DAB Stick" ],
    [ '0x0ccd', '0x00c0', "Terratec MuellerVerlag DAB Stick" ],
    [ '0x0ccd', '0x00c6', "Terratec Fraunhofer DAB Stick" ],
    [ '0x0ccd', '0x00d3', "Terratec Cinergy T Stick RC (Rev.3)" ],
    [ '0x0ccd', '0x00d7', "Terratec T Stick PLUS" ],
    [ '0x0ccd', '0x00e0', "Terratec NOXON DAB/DAB+ USB dongle (rev 2)" ],
    [ '0x1554', '0x5020', "PixelView PV-DT235U(RN)" ],
    [ '0x15f4', '0x0131', "Astrometa DVB-T/DVB-T2" ],
    [ '0x15f4', '0x0133', "HanfTek DAB+FM+DVB-T" ],
    [ '0x185b', '0x0620', "Compro Videomate U620F"],
    [ '0x185b', '0x0650', "Compro Videomate U650F"],
    [ '0x185b', '0x0680', "Compro Videomate U680F"],
    [ '0x1b80', '0xd393', "GIGABYTE GT-U7300" ],
    [ '0x1b80', '0xd394', "DIKOM USB-DVBT HD" ],
    [ '0x1b80', '0xd395', "Peak 102569AGPK" ],
    [ '0x1b80', '0xd397', "KWorld KW-UB450-T USB DVB-T Pico TV" ],
    [ '0x1b80', '0xd398', "Zaapa ZT-MINDVBZP" ],
    [ '0x1b80', '0xd39d', "SVEON STV20 DVB-T USB & FM" ],
    [ '0x1b80', '0xd3a4', "Twintech UT-40" ],
    [ '0x1b80', '0xd3a8', "ASUS U3100MINI_PLUS_V2" ],
    [ '0x1b80', '0xd3af', "SVEON STV27 DVB-T USB & FM" ],
    [ '0x1b80', '0xd3b0', "SVEON STV21 DVB-T USB & FM" ],
    [ '0x1d19', '0x1101', "Dexatek DK DVB-T Dongle (Logilink VG0002A)" ],
    [ '0x1d19', '0x1102', "Dexatek DK DVB-T Dongle (MSI DigiVox mini II V3.0)" ],
    [ '0x1d19', '0x1103', "Dexatek Technology Ltd. DK 5217 DVB-T Dongle" ],
    [ '0x1d19', '0x1104', "MSI DigiVox Micro HD" ],
    [ '0x1f4d', '0xa803', "Sweex DVB-T USB" ],
    [ '0x1f4d', '0xb803', "GTek T803" ],
    [ '0x1f4d', '0xc803', "Lifeview LV5TDeluxe" ],
    [ '0x1f4d', '0xd286', "MyGica TD312" ],
    [ '0x1f4d', '0xd803', "PROlectrix DV107669" ],
    ]


def lsusb():
    """Call lsusb and return the parsed output.

    Returns:
        (list): List of dictionaries containing the device information for each USB device.
    """
    try:
        FNULL = open(os.devnull, 'w')
        lsusb_raw_output = subprocess.check_output(['lsusb', '-v'], stderr=FNULL)
        FNULL.close()
        # Convert from bytes.
        lsusb_raw_output = lsusb_raw_output.decode('utf8')
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
                device = {
                    'bus': m.group(1),
                    'device': m.group(2)
                }
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
                '_value': m.group(2),
                '_desc': m.group(3),
            }
            cur[m.group(1)] = new_entry
            depth_stack.append(new_entry)
            continue

        logging.debug('lsusb parsing error: unrecognized line: "%s"', line)

    if device:
      devices.append(device)

    return devices


def reset_usb(bus, device):
    """Reset the USB device with the given bus and device."""
    usb_file_path = '/dev/bus/usb/%03d/%03d' % (bus, device)
    with open(usb_file_path, 'w') as usb_file:
        #logging.debug('fcntl.ioctl(%s, %d)', usb_file_path, _USBDEVFS_RESET)
        try:
            fcntl.ioctl(usb_file, _USBDEVFS_RESET)

        except IOError:
            logging.error("RTLSDR - USB Reset Failed.")


def is_rtlsdr(vid,pid):
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
    if platform.system() != 'Linux':
        return

    lsusb_info = lsusb()
    bus_num = None
    device_num = None

    for device in lsusb_info:
        try:
            device_serial = device['Device Descriptor']['iSerial']['_desc']
            device_product = device['Device Descriptor']['iProduct']['_desc']
            device_pid = device['Device Descriptor']['idProduct']['_value']
            device_vid = device['Device Descriptor']['idVendor']['_value']
        except:
            # If we hit an exception, the device likely doesn't have one of the required fields.
            continue

        if (device_serial == serial) and is_rtlsdr(device_vid, device_pid) :
            bus_num = int(device['bus'])
            device_num = int(device['device'])

    if bus_num and device_num:
        logging.info("RTLSDR - Attempting to reset: /dev/bus/usb/%03d/%03d" % (bus_num, device_num))
        reset_usb(bus_num, device_num)
    else:
        logging.error("RTLSDR - Could not find RTLSDR with serial %s!" % serial)
        return False


def find_rtlsdr(serial=None):
    """ Search through lsusb and see if an RTLSDR exists """

    # If not Linux, return immediately, and assume the RTLSDR exists..
    if platform.system() != 'Linux':
        return True

    lsusb_info = lsusb()
    bus_num = None
    device_num = None

    for device in lsusb_info:
        try:
            device_serial = device['Device Descriptor']['iSerial']['_desc']
            device_product = device['Device Descriptor']['iProduct']['_desc']
            device_pid = device['Device Descriptor']['idProduct']['_value']
            device_vid = device['Device Descriptor']['idVendor']['_value']
        except:
            # If we hit an exception, the device likely doesn't have one of the required fields.
            continue

        if is_rtlsdr(device_vid, device_pid):
            # We have found a RTLSDR! If we're not looking for a particular serial number, we can just quit now.
            if serial == None:
                return True
            else:
                if (device_serial == serial):
                    bus_num = int(device['bus'])
                    device_num = int(device['device'])

    if bus_num and device_num:
        # We have found an RTLSDR with this serial number!
        return True

    else:
        # Otherwise, nope.
        return False


def reset_all_rtlsdrs():
    """ Reset all RTLSDR devices found in the lsusb tree """

    # If not Linux, return immediately.
    if platform.system() != 'Linux':
        return

    lsusb_info = lsusb()
    bus_num = None
    device_num = None

    for device in lsusb_info:
        try:
            device_product = device['Device Descriptor']['iProduct']['_desc']
            device_pid = device['Device Descriptor']['idProduct']['_value']
            device_vid = device['Device Descriptor']['idVendor']['_value']
        except:
            # If we hit an exception, the device likely doesn't have one of the required fields.
            continue

        if is_rtlsdr(device_vid, device_pid) :
            bus_num = int(device['bus'])
            device_num = int(device['device'])

            logging.info("RTLSDR - Attempting to reset: Bus: %d  Device: %d" % (bus_num, device_num))
            reset_usb(bus_num, device_num)

    if device_num is None:
        logging.error("RTLSDR - Could not find any RTLSDR devices to reset!")



def rtlsdr_test(device_idx='0', rtl_sdr_path="rtl_sdr", retries = 5):
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
    if device_idx.startswith('TCP'):
        return True

    _rtl_cmd = "timeout 5 %s -d %s -n 200000 - > /dev/null" % (rtl_sdr_path, str(device_idx))


    # First, check if the RTLSDR with a provided serial number is present.
    if device_idx == '0':
        # Check for the presence of any RTLSDRs.
        _rtl_exists = find_rtlsdr()

    else:
        # Otherwise, look for a particular RTLSDR
        _rtl_exists = find_rtlsdr(device_idx)
        
    if not _rtl_exists:
        logging.error("RTLSDR - RTLSDR with serial #%s is not present!" % str(device_idx))
        return False

    # So now we know the rtlsdr we are attempting to test does exist.
    # We make an attempt to read samples from it:

    _rtlsdr_retries = retries

    while _rtlsdr_retries > 0:
        try:
            FNULL = open(os.devnull, 'w') # Inhibit stderr output
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
        if device_idx == '0':
            reset_all_rtlsdrs()
        else:
            reset_rtlsdr_by_serial(device_idx)

        # Decrement out retry count, then wait a bit before looping
        _rtlsdr_retries -= 1
        time.sleep(2)

    # If we run out of retries, clearly the RTLSDR isn't working.
    logging.error("RTLSDR - RTLSDR with serial #%s was not recovered after %d reset attempts." % (str(device_idx),retries))
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
        "listener": listener, "balloon": balloon,
        "listener_radians": (lat1, lon1, alt1),
        "balloon_radians": (lat2, lon2, alt2),
        "angle_at_centre": degrees(angle_at_centre),
        "angle_at_centre_radians": angle_at_centre,
        "bearing": degrees(bearing),
        "bearing_radians": bearing,
        "great_circle_distance": great_circle_distance,
        "straight_distance": distance,
        "elevation": degrees(elevation),
        "elevation_radians": elevation
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

    _out_len = len(freq)//factor

    _freq_out =[]
    _power_out = []

    try:
        for i in range(_out_len):
            _f_slice = freq[i*factor : i*factor + factor]
            _p_slice = power[i*factor : i*factor + factor]

            _freq_out.append(_f_slice[np.argmax(_p_slice)])
            _power_out.append(_p_slice.max())
    except:
        pass

    return (_freq_out, _power_out)


if __name__ == "__main__":
    import sys
    logging.basicConfig(format='%(asctime)s %(levelname)s:%(message)s', level=logging.DEBUG)
    check_autorx_version()
