
#!/bin/python3

#
# Receiver Statistics Calculator
#
# 2018-04 Mark Jessop <vk5qi@rfhead.net>
#


# Global imports
import argparse
from pathlib import Path
import sys

# Specific imports
sys.path.append("..")
from libs.csvhandler import CsvHandler
from libs.filesystem import get_file_list
from libs.commun import position_info


FILE_NAME = Path(__file__).name


# Functions definition
def calculate_stats(station_lat : float,
                    station_lon : float,
                    station_alt : float,
                    log_folder : Path) -> None:
    """ Calculate statistics for all file """

    log_file_list = get_file_list(log_folder, "*.log")
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

                _stats = position_info((station_lat, station_lon, station_alt),
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


if __name__ == '__main__':
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
                        default=48.90,
                        type=float,
                        help="Receiver latitude (unit : decimal degrees)")

    parser.add_argument("--lon", "--station_lon",
                        default=2.17,
                        type=float,
                        help="Receiver longitude (unit : decimal degrees)")

    parser.add_argument("--alt", "--station_alt",
                        default=60.0,
                        type=float,
                        help="Receiver altitude (unit : meter)")

    parser.add_argument("folder",
                        default="../log/",
                        help="Folder containing log files")

    args = parser.parse_args()

    calculate_stats(args.lat,
                  args.lon,
                  args.alt,
                  Path(args.folder))
