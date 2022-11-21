#!/usr/bin/env python
#
# Receiver Statistics Calculator
#
# 2018-04 Mark Jessop <vk5qi@rfhead.net>
#
# Usage:
# python receiver_stats.py --lat=yourlat --lon=yourlon --alt=youralt ../log/
# lat and lon are in decimal degrees.
#
import argparse
import glob
import numpy as np
import sys
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
    #radius = 6371000.0
    radius = 6364963.0 # Optimized for Australia :-)

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

    # Use pythagorean theorem to find unknown side.
    distance = sqrt((ea ** 2) + (eb ** 2))

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



def read_last_position(filename):
    ''' Read in a a sonde log file, and return the last lat/lon/alt '''

    _last = None
    _f = open(filename, 'r')
    for line in _f:
        _last = line
    
    return _last.split(',')


if __name__ == '__main__':

    parser = argparse.ArgumentParser()
    parser.add_argument("--lat", default=-34.0, type=float, help="Receiver Latitude")
    parser.add_argument("--lon", default=138.0, type=float, help="Receiver Longitude")
    parser.add_argument("--alt", default=0.0, type=float, help="Receiver Altitude")
    parser.add_argument("folder", default="../log/", help="Folder containing log files.")
    args = parser.parse_args()


    _file_list = glob.glob(args.folder + "*_sonde.log")
    #print(_file_list)


    statistics = {
        'furthest': {'range':0},
        'lowest': {'elev':90}
        }


    for _file in _file_list:
        try:
            _last_entry = read_last_position(_file)
        except Exception as e:
            print("Error processing file %s: " % _file + str(e))
            continue

        _last_pos = (float(_last_entry[3]),float(_last_entry[4]),float(_last_entry[5]))

        _stats = position_info((args.lat, args.lon, args.alt), _last_pos)

        _range = _stats['straight_distance']/1000.0
        _elev = _stats['elevation']

        if _range > statistics['furthest']['range']:
            # Update the furthest distance
            statistics['furthest']['range'] = _range
            statistics['furthest']['elev'] = _stats['elevation']
            statistics['furthest']['bearing'] = _stats['bearing']
            statistics['furthest']['telem'] = _last_entry

        if (_elev < statistics['lowest']['elev']) and (_range > 20) :
            statistics['lowest']['range'] = _range
            statistics['lowest']['elev'] = _stats['elevation']
            statistics['lowest']['bearing'] = _stats['bearing']
            statistics['lowest']['telem'] = _last_entry

    print("Longest Distance: %.1f km" % statistics['furthest']['range'])
    print("    Detail: %.1f deg elev, %.1f deg bearing" % (
        statistics['furthest']['elev'],
        statistics['furthest']['bearing']))
    print("    Last Telemetry: %s" % str(",".join(statistics['furthest']['telem'])))
    

    print("Lowest Elevation: %.1f degrees" % statistics['lowest']['elev'])
    print("    Detail: %.1f km range, %.1f deg bearing" % (
        statistics['lowest']['range'],
        statistics['lowest']['bearing']))
    print("    Last Telemetry: %s" % str(",".join(statistics['lowest']['telem'])))







