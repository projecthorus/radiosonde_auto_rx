
#!/bin/python3

#
# Receiver Statistics Calculator
#
# 2018-04 Mark Jessop <vk5qi@rfhead.net>
#


# Global imports
import argparse
from math import radians, degrees, sin, cos, atan2, sqrt, pi
from pathlib import Path

# Specific imports
from libs.CsvHandler import CsvHandler
from libs.FileSystem import get_file_list


FILE_NAME = Path(__file__).name


# Functions definitions
# Earthmaths code by Daniel Richman (thanks!)
# Copyright 2012 (C) Daniel Richman; GNU GPL 3
def position_info(listener: tuple,
                  balloon: tuple) -> dict:
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
    # radius = 6371000.0
    radius = 6364963.0  # Optimized for Australia :-)

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


# Classes definition
class ReceiverStats(object):
    """ ReceiverStats class """
    def __init__(self,
                 station_lat : float,
                 station_lon : float,
                 station_alt : float,
                 log_folder : Path) -> None:

        self._station_lat = station_lat
        self._station_lon = station_lon
        self._station_alt = station_alt
        self._log_folder  = log_folder

    def calculate_stats(self):
        """ Calculate statistics for all file """

        log_file_list = get_file_list(self._log_folder, "*.log")
        for counter, csv_file in enumerate(log_file_list):
            print("\n{}/{}\tAnalysing '{}'".format(counter+1, len(log_file_list), csv_file))
            csv_file_data = CsvHandler(csv_file)
            if len(csv_file_data) > 0:
                statistics = {
                    'furthest': {'range': 0},
                    'lowest': {'elev': 90}
                }

                for record in csv_file_data:

                    ballon_lat = float(record['lat'])
                    ballon_lon = float(record['lon'])
                    ballon_alt = float(record['alt'])

                    _stats = position_info((self._station_lat, self._station_lon, self._station_alt),
                                           (ballon_lat, ballon_lon, ballon_alt))

                    _range = _stats['straight_distance'] / 1000.0
                    _elev  = _stats['elevation']

                    # Note : Get stats for furthest range
                    if _range > statistics['furthest']['range']:
                        # Update the furthest distance
                        statistics['furthest']['range']   = _range
                        statistics['furthest']['elev']    = _stats['elevation']
                        statistics['furthest']['bearing'] = _stats['bearing']
                        # statistics['furthest']['telem']   = last_record

                    # Note : Get stats for lowest elevation
                    if _elev < statistics['lowest']['elev']:  # ) and (_range > 20):
                        statistics['lowest']['range']   = _range
                        statistics['lowest']['elev']    = _stats['elevation']
                        statistics['lowest']['bearing'] = _stats['bearing']
                        # statistics['lowest']['telem']   = last_record

                #  Note : Print stats for current file
                print("\t\tLongest distance: {:0.3f} km".format(statistics['furthest']['range']))
                print("\t\t\tDetail: {:0.3f} deg elevation, {:0.3f} deg bearing".format(statistics['furthest']['elev'], statistics['furthest']['bearing']))
                # print("\t\t\tLast Telemetry: {:0.3f}".format(statistics['furthest']['telem']))

                print("\t\tLowest elevation: {:0.3f} degrees".format(statistics['lowest']['elev']))
                print("\t\t\tDetail: {:0.3f} km range, {:0.3f} deg bearing".format(statistics['lowest']['range'], statistics['lowest']['bearing']))
                # print("\t\t\tLast Telemetry: {:0.3f}".format(statistics['lowest']['telem']))

                nb_frame_received = len(csv_file_data) - 1
                frame_count_begin = int(csv_file_data[0]['frame'])
                frame_count_end   = int(csv_file_data[nb_frame_received]['frame'])
                frame_count_diff  = frame_count_end - frame_count_begin

                print("\t\tRecords")
                # print("\t\t\tBegin frame counter        : {}".format(frame_count_begin))
                # print("\t\t\tEnd frame counter          : {}".format(frame_count_end))
                print("\t\t\tDiff frame counter         : {}".format(frame_count_diff))
                print("\t\t\tNumber of records received : {}".format(nb_frame_received))
                print("\t\t\tRecords received           : {:0.1f} %".format((nb_frame_received/frame_count_diff)*100.0))

            else:
                print("ERROR : Log file is empty")


def main():
    """ Main entry point """

    # Messages
    epilog_msg = '''\
        See above examples
            - [?]$ ./{0} --lat 48.0 --lon 2.0 --alt 50.0 ../log/
        '''.format(FILE_NAME)

    # Specify command arguments
    parser = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter,
                                     description="This script generate some statistics of the receiver from station's log",
                                     epilog=epilog_msg)

    parser.add_argument("--lat", "--station_lat",
                        default=48.90990378837107,
                        type=float,
                        help="Receiver latitude (unit : decimal degrees)")

    parser.add_argument("--lon", "--station_lon",
                        default=2.172286544644864,
                        type=float,
                        help="Receiver longitude (unit : decimal degrees)")

    parser.add_argument("--alt", "--station_alt",
                        default=50.0,
                        type=float,
                        help="Receiver altitude (unit : meter)")

    parser.add_argument("folder",
                        default="../log/",
                        help="Folder containing log files")

    args = parser.parse_args()

    ReceiverStats(args.lat,
                  args.lon,
                  args.alt,
                  Path(args.folder)).calculate_stats()


if __name__ == '__main__':
    main()
