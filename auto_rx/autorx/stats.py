#!/usr/bin/env python
#
#   radiosonde_auto_rx - Some utilities to generate statistics plots
#   based on log files.
#
#   Note: Requires matplotlib be available. May be very slow on a RPi.
#
#   Plot Radio Horizon: 
#       python3 -m autorx.stats --horizon
#
#   Plot SNR Map: 
#       python3 -m autorx.stats --snrmap
#
#   Copyright (C) 2021  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import autorx
import autorx.config
import datetime
import glob
import logging
import os.path
import math
import time

import numpy as np
import matplotlib.pyplot as plt

from dateutil.parser import parse
from autorx.utils import (
    short_type_lookup,
    readable_timedelta,
    strip_sonde_serial,
    position_info,
)
from autorx.log_files import list_log_files, read_log_by_serial


def radio_horizon_plot(log_files, min_range_km=10, max_range_km=1000, save_figure=None):
    """
    Generate an estimated radio horizon plot based on the last
    observed position from each radiosonde in the log directory.
    """

    _title = autorx.config.global_config['habitat_uploader_callsign'] + " Radio Horizon"

    _bearings = []
    _elevations = []
    _ranges = []

    for _entry in log_files:
        if 'last' in _entry:
            if (_entry['last']['range_km'] > min_range_km) and (_entry['last']['range_km'] < max_range_km):
                _bearings.append(_entry['last']['bearing'])
                _elevations.append(_entry['last']['elevation'])
                _ranges.append(_entry['last']['range_km'])

    logging.info(f"Found {len(_bearings)} datapoints for radio horizon plot.")


    plt.figure(figsize=(12,4))

    # Plot data
    plt.scatter(_bearings, _elevations, c=_ranges)
    plt.colorbar(label="Range (km)")

    # Setup plot
    plt.title(_title)
    plt.xlabel("Bearing (degrees True)")
    plt.ylabel("Elevation (degrees)")
    # Limit axes to only show the horizon area
    plt.ylim(-1.5,10)
    plt.xlim(0,360)
    plt.grid()


def normalised_snr(log_files, min_range_km=10, max_range_km=1000, maxsnr=False, meansnr=True, normalise=True, norm_range=50):
    """ Read in ALL log files and store snr data into a set of bins, normalised to 50km range. """

    _norm_range = norm_range # km

    _snr_count = 0

    _title = autorx.config.global_config['habitat_uploader_callsign'] + " SNR Map"

    # Initialise output array
    _map = np.ones((360,90))*-100.0

    _station = (
        autorx.config.global_config['station_lat'],
        autorx.config.global_config['station_lon'],
        autorx.config.global_config['station_alt']
    )

    for _log in log_files:

        if 'has_snr' in _log:
            if _log['has_snr'] == False:
                continue
        
        # Read in the file.
        _data = read_log_by_serial(_log['serial'])

        if 'snr' not in _data:
            # No SNR information, move on.
            continue

        logging.debug(f"Got SNR data ({len(_data['snr'])}) for {_log['serial']}")
        for _i in range(len(_data['path'])):
            _snr = _data['snr'][_i]
            # Discard obviously sus SNR values
            if _snr > 40.0 or _snr < 5.0:
                continue

            _balloon = _data['path'][_i]
            _pos_info = position_info(_station, _balloon)

            _range = _pos_info['straight_distance']/1000.0
            _bearing = int(math.floor(_pos_info['bearing']))
            _elevation = int(math.floor(_pos_info['elevation']))

            if _range < min_range_km or _range > max_range_km:
                continue

            # Limit elevation data to 0-90
            if _elevation < 0:
                _elevation = 0
            
            if normalise:
                _snr =  _snr + 20*np.log10(_range/_norm_range)

            #print(f"{_bearing},{_elevation}: {_range} km, {_snr} dB, {_norm_snr} dB")

            if _map[_bearing,_elevation] < -10.0:
                _map[_bearing,_elevation] = _snr
            else:
                if meansnr:
                    _map[_bearing,_elevation] = np.mean([_map[_bearing,_elevation],_snr])
                elif maxsnr:
                    if _snr > _map[_bearing,_elevation]:
                        _map[_bearing,_elevation] = _snr



    print(_map)

    plt.figure(figsize=(12,6))
    plt.imshow(np.flipud(_map.T), vmin=0, vmax=40, extent=[0,360,0,90])
    plt.xlabel("Bearing (degrees true)")
    plt.ylabel("Elevation (degrees)")
    plt.title(_title)
    
    if normalise:
        plt.colorbar(label="Normalised SNR (dB)", shrink=0.5)
    elif maxsnr:
        plt.colorbar(label="Peak SNR (dB)", shrink=0.5)


if __name__ == "__main__":
    import argparse
    from autorx.config import read_auto_rx_config

    # Command line arguments.
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-c",
        "--config",
        default="station.cfg",
        help="Receive Station Configuration File. Default: station.cfg",
    )
    parser.add_argument(
        "-l",
        "--log",
        default="./log/",
        help="Receive Station Log Path. Default: ./log/",
    )
    parser.add_argument(
        "--horizon",
        action="store_true",
        default=False,
        help="Generate Radio Horizon Plot"
    )
    parser.add_argument(
        "--snrmap",
        action="store_true",
        default=False,
        help="Generate SNR Map (Mean Normalised)"
    )
    parser.add_argument(
        "--snrmapmax",
        action="store_true",
        default=False,
        help="Generate SNR Map (Maximum SNR)"
    )
    parser.add_argument(
        "--snrmapmaxnorm",
        action="store_true",
        default=False,
        help="Generate Normalised SNR Map (Maximum SNR)"
    )
    parser.add_argument(
        "--normrange",
        type=float,
        default=50,
        help="Normalistion Range (km, default=50)"
    )

    parser.add_argument(
        "-v", "--verbose", help="Enable debug output.", action="store_true"
    )
    args = parser.parse_args()

    if args.verbose:
        _log_level = logging.DEBUG
    else:
        _log_level = logging.INFO

    logging.basicConfig(
        format="%(asctime)s %(levelname)s:%(message)s", level=_log_level
    )

    autorx.logging_path = args.log

    # Read in the config and make it available to other functions
    _temp_cfg = read_auto_rx_config(args.config, no_sdr_test=True)
    autorx.config.global_config = _temp_cfg


    # Read in the log files.
    logging.info("Quick-Looking Log Files")
    log_list = list_log_files(quicklook=True, custom_log_dir=args.log)
    logging.info(f"Loaded in {len(log_list)} log files.")


    if args.horizon:
        radio_horizon_plot(log_list)

    if args.snrmap:
        normalised_snr(log_list, norm_range=args.normrange)
    
    if args.snrmapmax:
        normalised_snr(log_list, meansnr=False, maxsnr=True, normalise=False)

    if args.snrmapmaxnorm:
        normalised_snr(log_list, meansnr=False, maxsnr=True, normalise=True, norm_range=args.normrange)

    plt.show()

