#!/usr/bin/env python
#
# Radiosonde Auto RX Tools
# Log-to-KML Utilities
#
# 2018-02 Mark Jessop <vk5qi@rfhead.net>
#

import argparse
import glob
import sys
from os.path import dirname, abspath

parent_dir = dirname(dirname(abspath(__file__)))
sys.path.append(parent_dir)
from autorx.log_files import log_files_to_kml


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-i", "--input", type=str, default="../log/*_sonde.log",
                        help="Path to log file. May include wildcards, though the path "
                             "must be wrapped in quotes. Default=../log/*_sonde.log")
    parser.add_argument("-o", "--output", type=str, default="sondes.kml",
                        help="KML output file name. Default=sondes.kml")
    parser.add_argument('--clamp', action="store_false", default=True,
                        help="Clamp tracks to ground instead of showing absolute altitudes.")
    parser.add_argument('--noextrude', action="store_false", default=True,
                        help="Disable Extrusions for absolute flight paths.")
    parser.add_argument('--lastonly', action="store_true", default=False,
                        help="Only plot last-seen sonde positions, not the flight paths.")
    args = parser.parse_args()

    _file_list = glob.glob(args.input)
    _file_list.sort(reverse=True)

    with open(args.output, "wb") as kml_file:
        log_files_to_kml(_file_list, kml_file, absolute=args.clamp,
                         extrude=args.noextrude, last_only=args.lastonly)

    print("Output saved to: %s" % args.output)
