#!/usr/bin/env python
#
#   radiosonde_auto_rx - Utility Classes & Functions
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under MIT License
#

from __future__ import division, print_function
import os
import subprocess
import threading
import numpy as np
from math import radians, degrees, sin, cos, atan2, sqrt, pi
try:
    # Python 2
    from Queue import Queue
except ImportError:
    # Python 3
    from queue import Queue


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

def rtlsdr_test(device_idx=0, rtl_sdr_path="rtl_sdr"):
    """ Test that a RTLSDR with supplied device ID is accessible.

    This function attempts to read a small set of samples from a rtlsdr using rtl-sdr.
    The exit code from rtl-sdr indicates if the attempt was successful, and hence shows if the rtlsdr is usable.

    Args:
        device_idx (int): Device index of the RTLSDR to test. Defaults to 0.
        rtl_sdr_path (str): Path to the rtl_sdr utility. Defaults to 'rtl_sdr' (i.e. look on the system path)

    Returns:
        bool: True if the RTLSDR device is accessible, False otherwise.
    """


    _rtl_cmd = "%s -d %d -n 200000 - > /dev/null" % (rtl_sdr_path, int(device_idx))

    try:
        FNULL = open(os.devnull, 'w') # Inhibit stderr output
        _ret_code = subprocess.check_call(_rtl_cmd, shell=True, stderr=FNULL)
        FNULL.close()
    except subprocess.CalledProcessError:
        # This exception means the subprocess has returned an error code of one.
        return False
    else:
        return True


def rtlsdr_reset(device_idx=0, rs_path="./"):
    """ Issue a USB reset to a specific RTLSDR device

    Args:
        device_idx (int): Device index of the RTLSDR to issue the reset command to.
        rs_path (str): Path to the compiled RS binaries (in this case, we need reset_usb)

    Returns:
        bool: True if the reset command was issued without error. This does not ensure the RTLSDR is operational again.

    """

    # Determine /dev/ path to specific RTLSDR (probably by parsing of lsusb -v output... only on linux!)

    # Issue reset command (via the reset_usb binary)

    # TODO.
    return True



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

