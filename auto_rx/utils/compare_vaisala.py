
#!/bin/python3
#
#   Vaisala vs Auto_rx Comparisons
#
#   Copyright (C) 2019  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
#   Compares the auto_rx calcuated temperature & humidity values with
#   truth data produced by a Vaisala ground station.
#
#   The 'truth' data must be in vaisalas 'metdata' tab-delimited text format.
#
#   Run with:
#       python3 compare_vaisala.py originalmetdata_20190521_0418_R0230900.txt 20190521-042102_R0230900_RS41_402200_sonde.log
#
#   TODO:
#       [ ] Calculate temp/rh error vs altitude
#       [ ] Calculate rh error vs temperature
#

# Global imports
import argparse
from dateutil.parser import parse
# NOTE - If running on a headless system with no display, the following two lines will need to be uncommented.
#import matplotlib as mpl
#mpl.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path
import sys

# Specific imports
sys.path.append("..")
from libs.commun import position_info


FILE_NAME = Path(__file__).name


# Functions definition
def read_vaisala_metdata(filename: Path):
    """ Read in a Vaisala 'metdata' tab-delimtied text file, as produced by the MW32 ground station

    :param filename: The path of CSV file
    """

    with filename.open(mode='r') as _f:

        # Skip past the header.
        for i in range(22):
            _f.readline()


        output = []
        # Read in lines of data.
        #    n  Elapsed time    HeightMSL       Pc     Pm    Temp   RH   VirT          Lat         Lon  HeightE Speed   Dir
        for line in _f:
            try:
                _fields        = line.split('\t')
                _count         = int(_fields[0])
                _flight_time   = int(_fields[1])
                _height_msl    = int(_fields[2])
                _pressure_calc = float(_fields[3])
                _pressure_meas = float(_fields[4])
                _temp          = float(_fields[5])
                _relhum        = int(_fields[6])
                _virt          = float(_fields[7])
                _lat           = float(_fields[8])
                _lon           = float(_fields[9])
                _alt           = float(_fields[10])
                _vel_h         = float(_fields[11])
                _heading       = float(_fields[12])

                output.append([_count, _flight_time, _height_msl, _pressure_calc, _pressure_meas, _temp, _relhum, _virt, _lat, _lon, _alt, _vel_h, _heading])

            except:
                pass

    return np.array(output)

def read_log_file(filename: Path,
                  decimation: int = 10,
                  min_altitude: int = 100):
    """ TODO DOC

    :param filename: The path of the CSV file
    :param decimation: TODO DOC
    :param min_altitude: TODO DOC
    :return:
    """

    # Load in the file.
    # data = np.genfromtxt(filename,delimiter=',', dtype=None)

    # # Extract fields.
    # times       = data['f0']
    # latitude    = data['f3']
    # longitude   = data['f4']
    # altitude    = data['f5']
    # temperature = data['f6']
    # humidity    = data['f7']

    times       = []
    latitude    = []
    longitude   = []
    altitude    = []
    temperature = []
    humidity    = []

    with filename.open(mode='r') as _file:
        for line in _file:
            try:
                _fields = line.split(',')

                # Attempt to parse the line
                _time = _fields[0]
                _lat  = float(_fields[3])
                _lon  = float(_fields[4])
                _alt  = float(_fields[5])
                _temp = float(_fields[6])
                _hum  = float(_fields[7])

                # Append data to arrays.
                times.append(_time)
                latitude.append(_lat)
                longitude.append(_lon)
                altitude.append(_alt)
                temperature.append(_temp)
                humidity.append(_hum)
            except Exception as e:
                print("Error reading line: {}".format(e))

    print("Read {} data points from {}.".format(len(times), filename))

    _output = list() # Altitude, Wind Speed, Wind Direction, Temperature, Dew Point
    # First entry, We assume all the values are unknown for now.
    _output.append([altitude[0], np.NaN, np.NaN, np.NaN, np.NaN, np.NaN])

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
            print("Detected burst at {} metres.".format(altitude[i]))
            break

        # If we have valid PTU data, calculate the dew point.
        if temperature[i] != -273:
            T  = temperature[i]
            RH = humidity[i]
            DP = 243.04*(np.log(RH/100)+((17.625*T)/(243.04+T)))/(17.625-np.log(RH/100)-((17.625*T)/(243.04+T)))
        else:
            # Otherwise we insert NaNs, so data isn't plotted.
            T  = np.NaN
            DP = np.NaN
            RH = np.NaN

        # Calculate time delta between telemetry frames.
        _time          = parse(times[i])
        _time_old      = parse(times[i-decimation])
        _delta_seconds = (_time - _time_old).total_seconds()

        # Calculate the movement direction and distance, and then calculate the movement speed.
        _movement = position_info((latitude[i], longitude[i], altitude[i]), (latitude[i-decimation], longitude[i-decimation], altitude[i-decimation]))
        _heading  = _movement['bearing']
        _velocity = _movement['great_circle_distance']/_delta_seconds

        _output.append([altitude[i], _velocity, _heading, T, DP, RH])

        i += decimation

    # Convert our output data into something we can process easier.
    return (np.array(_output), _burst, _startalt, times[-1])

def comparison_plots(vaisala_data,
                     autorx_data,
                     serial):
    """ TODO DOC

    :param vaisala_data: TODO DOC
    :param autorx_data: TODO DOC
    :param serial: TODO DOC
    """

    _vaisala_alt  = vaisala_data[:,2]
    _vaisala_temp = vaisala_data[:,5]
    _vaisala_rh   = vaisala_data[:,6]

    _autorx_alt  = autorx_data[:,0]
    _autorx_temp = autorx_data[:,3]
    _autorx_rh   = autorx_data[:,5]

    # Interpolation
    _interp_min_alt = max(np.min(_vaisala_alt), np.min(_autorx_alt))
    _interp_max_alt = min(np.max(_vaisala_alt), np.max(_autorx_alt))

    # Define the altitude range we interpolate over.
    _interp_x = np.linspace(_interp_min_alt, _interp_max_alt, 2000)

    # Produce interpolated temperature and humidity data.
    _vaisala_interp_temp = np.interp(_interp_x, _vaisala_alt, _vaisala_temp)
    _vaisala_interp_rh   = np.interp(_interp_x, _vaisala_alt, _vaisala_rh)
    _autorx_interp_temp  = np.interp(_interp_x, _autorx_alt, _autorx_temp)
    _autorx_interp_rh    = np.interp(_interp_x, _autorx_alt, _autorx_rh)

    # Calculate the error in auto_rx's calculations.
    _autorx_temp_error = _autorx_interp_temp - _vaisala_interp_temp
    _autorx_rh_error   = _autorx_interp_rh - _vaisala_interp_rh


    plt.figure()
    plt.plot(_vaisala_alt, _vaisala_temp, label="Vaisala")
    plt.plot(_autorx_alt, _autorx_temp, label="auto_rx")
    plt.xlabel("Altitude (m)")
    plt.ylabel("Temperature (degC)")
    plt.title("Temperature - {}".format(serial))
    plt.legend()
    plt.grid()

    plt.figure()
    plt.plot(_vaisala_alt, _vaisala_rh, label="Vaisala")
    plt.plot(_autorx_alt, _autorx_rh, label="auto_rx")
    plt.xlabel("Altitude (m)")
    plt.ylabel("Relative Humidity (%)")
    plt.title("Relative Humidity - {}".format(serial))
    plt.legend()
    plt.grid()

    plt.figure()
    plt.plot(_interp_x, _autorx_temp_error)
    plt.xlabel("Altitude (m)")
    plt.ylabel("Error (degC)")
    plt.title("auto_rx RS41 Temperature Calculation Error - %s" % serial)
    plt.grid()

    plt.figure()
    plt.plot(_interp_x, _autorx_rh_error)
    plt.xlabel("Altitude (m)")
    plt.ylabel("Error (% RH)")
    plt.title("auto_rx RS41 Humidity Calculation Error - %s" % serial)
    plt.grid()

    plt.figure()
    plt.plot(_vaisala_interp_temp, _autorx_rh_error)
    plt.xlabel("Temperature (degC)")
    plt.ylabel("Error (% RH)")
    plt.title("auto_rx RS41 Humidity Calculation Error - %s" % serial)
    plt.grid()

    plt.show()


if __name__ == "__main__":
    """ Main entry point """

    # Messages
    epilog_msg = '''\
               See above examples
                   - [?]$ ./{0} TODO
               '''.format(FILE_NAME)

    # Specify command arguments
    parser = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter,
                                     description="This script compares the auto_rx calcuated temperature & humidity values with truth data produced by a Vaisala ground station.",
                                     epilog=epilog_msg)

    parser.add_argument("vaisala_filename",
                        type=str)

    parser.add_argument("autorx_filename",
                        type=str)

    args = parser.parse_args()

    _serial = str(args.autorx_filename).split('_')[1]

    vaisala_data                             = read_vaisala_metdata(Path(args.vaisala_filename))
    (autorx_data, burst, startalt, lasttime) = read_log_file(Path(args.autorx_filename), decimation=1)

    comparison_plots(vaisala_data, autorx_data, _serial)
