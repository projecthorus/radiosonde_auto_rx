#!/usr/bin/env python
#
#   Radiosonde Log Plotter
#
#   Copyright (C) 2019  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later 
#
#   Note: This script is very much a first pass, and doesn't have much error checking of data.
#
#   Dependencies:
#       numpy
#       pytz
#       dateutil
#       metpy
#   You should be able to get the above with either your system package manager, or Pip.
#   I would strongly suggest running this under Python 3.5 or newer.
#
#
#   There are two general usage scenarios, plotting a single file, and plotting an entire directory.
#   
#   Single file plotting:
#   $ python plot_sonde_log.py --singlefile 20190424-105731_P4750324_RS41_401500_sonde.log
#
#   Plotting of a directory of files.
#   In this scenario we need to supply the following parameters:
#   --log-dir   The directory containing the sonde log files (usually radiosonde_auto_rx/auto_rx/log/)
#   --output-dir  Where to save the plots to.
#
#   A file called plot_status.txt will be created, which will keep track of which log files have already been
#   completely processed. Log files will be re-processed until:
#       - The sonde is detected to have burst.
#       - The last position is more than 15 minutes old.
#   Additionally, log files will not be processed if:
#       - They contain less than 500 positions.
#       - The first observed altitude is > 5km (indicative of a far-away sonde.)
#
#   Example call:
#   # python plot_sonde_log.py --log-dir=/home/pi/radiosonde_auto_rx/auto_rx/log/ --output-dir=/home/pi/soundings/
#
#   This can be called from a bash script, run by a cron-job. For example, create a file ~/generate_soundings.sh
#   containing the following:
#   
#    #!/bin/bash
#    # Generate Soundings
#    python plot_sonde_log.py --log-dir=/home/pi/radiosonde_auto_rx/auto_rx/log/ --output-dir=/home/pi/soundings/
#    # Copy to another server (SSH keys would need to be setup for this to work)
#    rsync -r /home/pi/soundings/ yourserver:~/path/to/soundings/
#
#   A cron-job could then be set up with the comamnd:
#   */20 23,0,1,2,11,12,13,14 * * * /home/pi/generate_soundings.sh
#
#   This will run the above script every 20 minutes during the hours when we expect to see 00Z and 12Z sondes.
#   NOTE: You will likely need to uncomment the two lines identified below to be able to run this on a headless
#   Raspberry Pi.

import argparse
import datetime
import glob
import json
import os.path
import pytz
import sys
import traceback
import numpy as np
# NOTE - If running on a headless system with no display, the following two lines will need to be uncommented.
#import matplotlib as mpl
#mpl.use('Agg')
import matplotlib.pyplot as plt
from dateutil.parser import parse
from math import radians, degrees, sin, cos, atan2, sqrt, pi
import metpy.calc as mpcalc
from metpy.plots import SkewT
from metpy.units import units

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


def read_log_file(filename, decimation=10, min_altitude=100):
    # Load in the file.
    # data = np.genfromtxt(filename,delimiter=',', dtype=None)

    # # Extract fields.
    # times = data['f0']
    # latitude = data['f3']
    # longitude = data['f4']
    # altitude = data['f5']
    # temperature = data['f6']
    # humidity = data['f7']

    times = []
    latitude = []
    longitude = []
    altitude = []
    temperature = []
    humidity = []
    snr = []
    ferror = []

    with open(filename, 'r') as _file:
        for line in _file:
            try:
                _fields = line.split(',')

                # Log fields:   0          1      2     3   4   5   6     7     8       9    10       11   12       13  14         15   16     17          18
                #               "timestamp,serial,frame,lat,lon,alt,vel_v,vel_h,heading,temp,humidity,type,freq_mhz,snr,f_error_hz,sats,batt_v,burst_timer,aux_data\n"

                # Attempt to parse the line
                _time = _fields[0]
                _lat = float(_fields[3])
                _lon = float(_fields[4])
                _alt = float(_fields[5])
                _temp = float(_fields[9])
                _hum = float(_fields[10])
                try:
                    # Attempt to extract SNR and frequency error fields.
                    # These may not be present on older log files.
                    _snr = float(_fields[13])
                    _ferror = float(_fields[14])
                except:
                    _snr = -99
                    _ferror = 0.0

                # Append data to arrays.
                times.append(_time)
                latitude.append(_lat)
                longitude.append(_lon)
                altitude.append(_alt)
                temperature.append(_temp)
                humidity.append(_hum)
                snr.append(_snr)
                ferror.append(_ferror)
            except Exception as e:
                print("Error reading line: ")

    print("Read %d data points from %s." % (len(times), filename))

    _output = [] # Altitude, Wind Speed, Wind Direction, Temperature, Dew Point
    # First entry, We assume all the values are unknown for now.
    _output.append([altitude[0], np.NaN, np.NaN, np.NaN, np.NaN, np.NaN, snr[0], ferror[0]])

    _burst = False
    _startalt = altitude[0]

    i = decimation
    while i < len(times):
        if altitude[i] < min_altitude:
            i += decimation
            continue

        # Check if we are descending. If so, break.
        if altitude[i] < _output[-1][0]:
            _burst = True
            print("Detected burst at %d metres." % altitude[i])
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
            RH = np.NaN

        # Calculate time delta between telemetry frames.
        _time = parse(times[i])
        _time_old = parse(times[i-decimation])
        _delta_seconds = (_time - _time_old).total_seconds()

        # Calculate the movement direction and distance, and then calculate the movement speed.
        _movement = position_info((latitude[i], longitude[i], altitude[i]), (latitude[i-decimation], longitude[i-decimation], altitude[i-decimation]))
        _heading = _movement['bearing']
        _velocity = _movement['great_circle_distance']/_delta_seconds

        _output.append([altitude[i], _velocity, _heading, T, DP, RH, snr[i], ferror[i]])

        i += decimation

    # Convert our output data into something we can process easier.
    return (np.array(_output), _burst, _startalt, times[-1], snr, ferror)


def plot_matplotlib(data_np, title="", metric=False, alt_limit = 20000, temp_limit=None):
    if metric:
        _alt = data_np[:,0]
    else:
        _alt = data_np[:,0]*3.28084 # Convert to feet.

    _speed = data_np[:,1]
    _direction = data_np[:,2]/10.0
    _temp = data_np[:,3]
    _dp = data_np[:,4]

    # Produce a boolean array to limit the plotted data.
    _data_limit = _alt < alt_limit

    # Plot the data...
    plt.figure()
    plt.plot(_speed[_data_limit], _alt[_data_limit], label='Speed (kt)', color='g')
    plt.plot(_direction[_data_limit], _alt[_data_limit], label='Direction (deg/10)', color='m')
    plt.plot(_temp[_data_limit], _alt[_data_limit], label='Temp (deg C)', color='b')
    plt.plot(_dp[_data_limit], _alt[_data_limit], label='DP (deg C)', color='r')

    if metric:
        plt.ylabel("Altitude (metres)")
    else:
        plt.ylabel("Altitude (feet)")

    # Determine and set plot axis limits
    _axes = plt.gca()
    # Y limit is either a default value, or a user specified altitude.
    _axes.set_ylim(top=alt_limit, bottom=0)

    # X limits are based on a combination of data.
    # The upper limit is based on the maximum speed within our altitude window
    if temp_limit == None:
        _temp_in_range= _temp[_data_limit]
        _dp_in_range= _dp[_data_limit]
        _min_temp = np.min(_temp_in_range[~np.isnan(_temp_in_range)])
        _min_dp = np.min(_dp_in_range[~np.isnan(_dp_in_range)])
        _axes.set_xlim(left=min(_min_temp, _min_dp))
    else:
        _axes.set_xlim(left=temp_limit)

    plt.title("Sounding File: %s" % title)
    plt.grid(which='both')
    plt.legend(loc='upper right')
    plt.show()


def plot_metpy(data, title="", saveplot=None, showplot=True):

    # Convert data into a suitable format for metpy.
    _altitude = data[:,0] * units('m')
    p = mpcalc.height_to_pressure_std(_altitude)
    T = data[:,3] * units.degC
    Td = data[:,4] * units.degC
    wind_speed = data[:,1] * units('m/s')
    wind_direction = data[:,2] * units.degrees
    u, v = mpcalc.wind_components(wind_speed, wind_direction)


    fig = plt.figure(figsize=(6,8))
    skew = SkewT(fig=fig)
    skew.plot(p, T, 'r')
    skew.plot(p, Td, 'g')

    my_interval = np.arange(300, 1000, 50) * units('mbar')
    ix = mpcalc.resample_nn_1d(p, my_interval)
    skew.plot_barbs(p[ix], u[ix], v[ix])
    skew.ax.set_ylim(1000,300)
    skew.ax.set_xlim(-40, 30)
    skew.plot_dry_adiabats()

    heights = np.array([1, 2, 3, 4, 5, 6, 7, 8, 9]) * units.km
    std_pressures = mpcalc.height_to_pressure_std(heights)
    for height_tick, p_tick in zip(heights, std_pressures):
        trans, _, _ = skew.ax.get_yaxis_text1_transform(0)
        skew.ax.text(0.02, p_tick, '---{:~d}'.format(height_tick), transform=trans)

    plt.title("Sounding: " + title)

    if saveplot != None:
        fig.savefig(saveplot, bbox_inches='tight')



#
#   Status file handling.
#   The status file contains a JSON blob with one entry per filename that has been opened.
#   Each entry contains if the flight is considered to be 'finished', which is when either
#   the payload has started to descend, or no data has been received for ~10 min.
#

def read_status_file(filename):
    # Check the file exists..

    if not os.path.isfile(filename):
        # File does not exist, create a blank one.
        write_status_file(filename, {})


    # Now open and read the file.
    try:
        _f = open(filename,'r')
        data = json.loads(_f.read())
        _f.close()
        return data
    except Exception as e:
        print("Error reading status file - %s" % str(e))
        return None


def write_status_file(filename, data):
    _f = open(filename, 'w')
    _f.write(json.dumps(data))
    _f.close()



def process_directory(log_dir, output_dir, status_file, time_limit = 60):

    # Load the status file.
    _log_status = read_status_file(status_file)
    if _log_status is None:
        return

    # Get a list of log files in the directory.
    _files = glob.glob(os.path.join(log_dir, "*_sonde.log"))

    for _file in _files:
        _basename = os.path.basename(_file)
        # Check if we have touched this file before.
        if _basename in _log_status:
            if _log_status[_basename]['complete']:
                print("Already finished processing %s" % _basename)
                continue
            else:
                # This file needs to be re-processed
                pass
        else:
            # Add an entry for this file.
            _log_status[_basename] = {'complete': False}

        # Read in the file!
        try:
            (data, burst, startalt,  last_time, snr, ferror) = read_log_file(_file, decimation=10)

            # Don't process files with a starting altitude well above ground.
            # This indicates it's likely a sonde from a long way away.
            if startalt > 2000:
                _log_status[_basename]['complete'] = True
                print("Not processing %s." % _basename)
                continue


            # Calculate the age of the last data point in minutes.
            _data_age = (datetime.datetime.now(datetime.timezone.utc) - parse(last_time)).total_seconds() / 60.0
            if burst or (_data_age > time_limit):
                # We consider this file to be finished.
                _log_status[_basename]['complete'] = True

            # Plot the data, and save to disk.
            _out_file = os.path.join(output_dir, _basename[:-4]+".png")
            _file_timestamp = _basename.split('_')[0]
            _sonde_serial = _basename.split('_')[1]
            _title = _file_timestamp + " " + _sonde_serial

            print("Generating plot for: %s" % _basename)
            plot_metpy(data, title=_title, saveplot=_out_file, showplot=False)
        except Exception as e:
            traceback.print_exc()
            print("Error processing file %s - %s" % (_basename, str(e)))

    # Write out the status file
    write_status_file(status_file, _log_status)


if __name__ == "__main__":
    # Data format:
    # 2019-04-17T00:40:40.000Z,P4740856,7611,-35.38981,139.47062,12908.1,-67.9,25.0,RS41,402.500,SATS 9,BATT -1.0


    parser = argparse.ArgumentParser()
    parser.add_argument("--singlefile", default = "", type=str, help="Single log file to process.")
    parser.add_argument("--metric", action="store_true", default=False, help="Use metric altitudes. (Default is to use Feet)")
    parser.add_argument("--alt-limit", default=20000, type=int, help="Limit plot to supplied altitude (feet or metres, depending on user selection)")
    parser.add_argument("--temp-limit", default=None, type=float, help="Limit plot to a lower temperature in degrees. (Default is no limit, plot will autoscale)")
    parser.add_argument("--decimation", default=10, type=int, help="Decimate input data by X times. (Default = 10)")
    parser.add_argument("--log-dir", default="../log/", type=str, help="Directory containing sonde logs to process.")
    parser.add_argument("--output-dir", default="./plots/", type=str, help="Output directory to save plots to.")
    parser.add_argument("--plot-status-file", default="plot_status.txt", type=str, help="Plotting status file.")
    parser.add_argument("--snr", default=False, action='store_true', help="Plot SNR vs time.")
    parser.add_argument("--ferror", default=False, action='store_true', help="Plot Frequency Error vs time.")
    args = parser.parse_args()

    if args.singlefile != "":
        # Process a single file.

        (data_np, burst, startalt, last_time, snr, ferror) = read_log_file(args.singlefile, decimation=args.decimation)

        #plot_matplotlib(data_np, title=os.path.basename(args.filename), metric=args.metric, alt_limit=args.alt_limit, temp_limit=args.temp_limit)


        plot_metpy(data_np, saveplot=None, showplot=True)

        if args.snr:
            plt.figure()
            plt.plot(snr)
            plt.xlabel("Sample")
            plt.ylabel("SNR (dB)")
            plt.title("SNR vs Sample")
            plt.grid()
        
        if args.ferror:
            plt.figure()
            plt.plot(ferror)
            plt.xlabel("Sample")
            plt.ylabel("Frequency Error (Hz)")
            plt.title("Frequency Error vs Sample")
            plt.grid()

        plt.show()

    else:
        # do a batch process run.
        process_directory(args.log_dir, args.output_dir, args.plot_status_file)



