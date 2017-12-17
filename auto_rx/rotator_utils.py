#!/usr/bin/env python
#
# Radiosonde Auto RX Tools - Rotator Control
#
# 2017-12 Mark Jessop <vk5qi@rfhead.net>
#
import socket
import logging
import traceback
import time
import numpy as np
from math import radians, degrees, sin, cos, atan2, sqrt, pi

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

def update_rotctld(hostname='127.0.0.1', port=4533, azimuth=0.0, elevation=0.0):
    '''
    Attempt to push an azimuth & elevation position command into rotctld.
    We take a fairly simplistic approach to this, and don't attempt to read the current
    rotator position. 
    '''

    # Bound Azimuth & Elevation to 0-360 / 0-90
    elevation = np.clip(elevation,0,90)
    azimuth = azimuth % 360.0

    try:
        # Connect to rotctld.
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(1)
        s.connect((hostname,port))

        # Produce rotctld command.
        msg = "P %.1f %.1f\n" % (azimuth, elevation)
        logging.debug("Rotctld - Sending command: %s" % msg)
        # Send.
        s.send(msg)
        # Listen for a response.
        resp = s.recv(1024)
        # Close socket.
        s.close()

        #
        if 'RPRT 0' in resp:
            logging.info("Rotctld - Commanded rotator to %.1f, %.1f." % (azimuth, elevation))
            return True
        elif 'RPRT -1' in resp:
            logging.warning("Rotctld - rotctld reported an error (RPRT -1).")
            return False
        else:
            logging.warning("Rotctld - Unknown or no response from rotctld.")
            return False
    except:
        logging.error("Rotctld - Connection Error: %s" % traceback.format_exc())

if __name__ == "__main__":
    # Test script, to poke some values into rotctld.
    logging.basicConfig(level=logging.DEBUG)
    
    az_range = np.linspace(0,360,10)
    el_range = np.linspace(0,90,10)

    for i in range(0,len(az_range)):
        update_rotctld(azimuth=az_range[i], elevation=el_range[i])
        time.sleep(10)

