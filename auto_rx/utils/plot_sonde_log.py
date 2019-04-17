#!/usr/bin/env python
#
#   Radiosonde Log Plotter
#
#   Copyright (C) 2019  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later 
#
#   Note: This script is very much a first pass, and doesn't have any error checking of data.
#   
#    Usage: plot_sonde_log.py [-h] [--metric] [--alt-limit ALT_LIMIT]
#                             [--temp-limit TEMP_LIMIT] [--decimation DECIMATION]
#                             filename
#
#    positional arguments:
#      filename              Log File name.
#
#    optional arguments:
#      -h, --help            show this help message and exit
#      --metric              Use metric altitudes. (Default is to use Feet)
#      --alt-limit ALT_LIMIT
#                            Limit plot to supplied altitude (feet or metres,
#                            depending on user selection)
#      --temp-limit TEMP_LIMIT
#                            Limit plot to a lower temperature in degrees. (Default
#                            is no limit, plot will autoscale)
#      --decimation DECIMATION
#                            Decimate input data by X times. (Default = 10)
#

import argparse
import os.path
import sys
import numpy as np
import matplotlib.pyplot as plt
from dateutil.parser import parse
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


if __name__ == "__main__":
    # Data format:
    # 2019-04-17T00:40:40.000Z,P4740856,7611,-35.38981,139.47062,12908.1,-67.9,25.0,RS41,402.500,SATS 9,BATT -1.0


    parser = argparse.ArgumentParser()
    parser.add_argument("filename", type=str, help="Log File name.")
    parser.add_argument("--metric", action="store_true", default=False, help="Use metric altitudes. (Default is to use Feet)")
    parser.add_argument("--alt-limit", default=20000, type=int, help="Limit plot to supplied altitude (feet or metres, depending on user selection)")
    parser.add_argument("--temp-limit", default=None, type=float, help="Limit plot to a lower temperature in degrees. (Default is no limit, plot will autoscale)")
    parser.add_argument("--decimation", default=10, type=int, help="Decimate input data by X times. (Default = 10)")
    args = parser.parse_args()

    # Load in the file.
    data = np.genfromtxt(args.filename,delimiter=',', dtype=None)

    decimation = args.decimation

    # Extract fields.
    times = data['f0']
    latitude = data['f3']
    longitude = data['f4']
    altitude = data['f5']
    temperature = data['f6']
    humidity = data['f7']

    _output = [] # Altitude, Wind Speed, Wind Direction, Temperature, Dew Point
    # First entry, We assume all the values are unknown for now.
    _output.append([altitude[0], np.NaN, np.NaN, np.NaN, np.NaN])

    i = decimation
    while i < len(times):
        # Check if we are descending. If so, break.
        if altitude[i] < _output[-1][0]:
            break

        # If we have valid PTU data, calculate the dew point.
        if temperature[i] != -273:
            T = temperature[i]
            RH = humidity[i]
            DP = 243.04*(np.log(RH/100)+((17.625*T)/(243.04+T)))/(17.625-np.log(RH/100)-((17.625*T)/(243.04+T)))
        else:
            # Otherwise we insert NaNs, so data isn't plotted.
            T = np.NaN
            DP = np.NaN

        # Calculate time delta between telemetry frames.
        _time = parse(times[i])
        _time_old = parse(times[i-decimation])
        _delta_seconds = (_time - _time_old).total_seconds()

        # Calculate the movement direction and distance, and then calculate the movement speed.
        _movement = position_info((latitude[i], longitude[i], altitude[i]), (latitude[i-decimation], longitude[i-decimation], altitude[i-decimation]))
        _heading = _movement['bearing']
        _speed = (_movement['great_circle_distance']/_delta_seconds)*1.94384 # Convert to knots


        _output.append([altitude[i], _speed, _heading, T, DP])

        i += decimation

    # Convert our output data into something we can process easier.
    data_np = np.array(_output)
    
    if args.metric:
        _alt = data_np[:,0]
    else:
        _alt = data_np[:,0]*3.28084 # Convert to feet.

    _speed = data_np[:,1]
    _direction = data_np[:,2]/10.0
    _temp = data_np[:,3]
    _dp = data_np[:,4]

    # Produce a boolean array to limit the plotted data.
    _data_limit = _alt < args.alt_limit

    # Plot the data...
    plt.figure()
    plt.plot(_speed[_data_limit], _alt[_data_limit], label='Speed (kt)', color='g')
    plt.plot(_direction[_data_limit], _alt[_data_limit], label='Direction (deg/10)', color='m')
    plt.plot(_temp[_data_limit], _alt[_data_limit], label='Temp (deg C)', color='b')
    plt.plot(_dp[_data_limit], _alt[_data_limit], label='DP (deg C)', color='r')

    if args.metric:
        plt.ylabel("Altitude (metres)")
    else:
        plt.ylabel("Altitude (feet)")

    # Determine and set plot axis limits
    _axes = plt.gca()
    # Y limit is either a default value, or a user specified altitude.
    _axes.set_ylim(top=args.alt_limit, bottom=0)

    # X limits are based on a combination of data.
    # The upper limit is based on the maximum speed within our altitude window
    if args.temp_limit == None:
        _temp_in_range= _temp[_data_limit]
        _dp_in_range= _dp[_data_limit]
        _min_temp = np.min(_temp_in_range[~np.isnan(_temp_in_range)])
        _min_dp = np.min(_dp_in_range[~np.isnan(_dp_in_range)])
        _axes.set_xlim(left=min(_min_temp, _min_dp))
    else:
        _axes.set_xlim(left=args.temp_limit)

    plt.title("Sounding File: %s" % os.path.basename(args.filename))
    plt.grid(which='both')
    plt.legend(loc='upper right')
    plt.show()




