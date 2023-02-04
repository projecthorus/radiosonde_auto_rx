#!/usr/bin/env python
#
# Radiosonde Auto RX Tools
# Log-to-KML Utilities
#
# 2018-02 Mark Jessop <vk5qi@rfhead.net>
#
# Note: This utility requires the fastkml and shapely libraries, which can be installed using:
# sudo pip install fastkml shapely
#

import sys
import time
import datetime
import traceback
import argparse
import glob
import os
import fastkml
from dateutil.parser import parse
from shapely.geometry import Point, LineString

def read_telemetry_csv(filename,
    datetime_field = 0,
    latitude_field = 3,
    longitude_field = 4,
    altitude_field = 5,
    delimiter=','):
    ''' 
    Read in a radiosonde_auto_rx generated telemetry CSV file.
    Fields to use can be set as arguments to this function.
    These have output like the following:
    2017-12-27T23:21:59.560,M2913374,982,-34.95143,138.52471,719.9,-273.0,RS92,401.520
    <datetime>,<serial>,<frame_no>,<lat>,<lon>,<alt>,<temp>,<sonde_type>,<freq>

    Note that the datetime field must be parsable by dateutil.parsers.parse.

    If any fields are missing, or invalid, this function will return None.

    The output data structure is in the form:
    [
        [datetime (as a datetime object), latitude, longitude, altitude, raw_line],
        [datetime (as a datetime object), latitude, longitude, altitude, raw_line],
        ...
    ]
    '''

    output = []

    f = open(filename,'r')

    for line in f:
        try:
            # Split line by comma delimiters.
            _fields = line.split(delimiter)

            if _fields[0] == 'timestamp':
                # First line in file - header line.
                continue

            # Attempt to parse fields.
            _datetime = parse(_fields[datetime_field])
            _latitude = float(_fields[latitude_field])
            _longitude = float(_fields[longitude_field])
            _altitude = float(_fields[altitude_field])

            output.append([_datetime, _latitude, _longitude, _altitude, line])
        except:
            traceback.print_exc()
            return None

    f.close()

    return output


def flight_burst_position(flight_path):
    ''' Search through flight data for the burst position and return it. '''

    # Read through array and hunt for max altitude point.
    current_alt = 0.0
    current_index = 0
    for i in range(len(flight_path)):
        if flight_path[i][3] > current_alt:
            current_alt = flight_path[i][3]
            current_index = i

    return flight_path[current_index]


ns = '{http://www.opengis.net/kml/2.2}'

def new_placemark(lat, lon, alt,
    placemark_id="Placemark ID",
    name="Placemark Name",
    absolute = False,
    icon = "http://maps.google.com/mapfiles/kml/shapes/placemark_circle.png",
    scale = 1.0):
    """ Generate a generic placemark object """

    if absolute:
        _alt_mode = 'absolute'
    else:
        _alt_mode = 'clampToGround'

    flight_icon_style = fastkml.styles.IconStyle(
        ns=ns, 
        icon_href=icon, 
        scale=scale)

    flight_style = fastkml.styles.Style(
        ns=ns,
        styles=[flight_icon_style])

    flight_placemark = fastkml.kml.Placemark(
        ns=ns, 
        id=placemark_id,
        name=name,
        description="",
        styles=[flight_style])

    flight_placemark.geometry = fastkml.geometry.Geometry(
        ns=ns,
        geometry=Point(lon, lat, alt),
        altitude_mode=_alt_mode)

    return flight_placemark



def flight_path_to_geometry(flight_path,
    placemark_id="Flight Path ID",
    name="Flight Path Name",
    track_color="aaffffff",
    poly_color="20000000",
    track_width=2.0,
    absolute = True,
    extrude = True,
    tessellate = True):
    ''' Produce a fastkml geometry object from a flight path array '''

    # Handle selection of absolute altitude mode
    if absolute:
        _alt_mode = 'absolute'
    else:
        _alt_mode = 'clampToGround'

    # Convert the flight path array [time, lat, lon, alt, comment] into a LineString object.
    track_points = []
    for _point in flight_path:
        # Flight path array is in lat,lon,alt order, needs to be in lon,lat,alt
        track_points.append([_point[2],_point[1],_point[3]])

    _flight_geom = LineString(track_points)

    # Define the Line and Polygon styles, which are used for the flight path, and the extrusions (if enabled)
    flight_track_line_style = fastkml.styles.LineStyle(
        ns=ns,
        color=track_color,
        width=track_width)

    flight_extrusion_style = fastkml.styles.PolyStyle(
        ns=ns,
        color=poly_color)

    flight_track_style = fastkml.styles.Style(
        ns=ns,
        styles=[flight_track_line_style, flight_extrusion_style])

    # Generate the Placemark which will contain the track data.
    flight_line = fastkml.kml.Placemark(
        ns=ns,
        id=placemark_id,
        name=name,
        styles=[flight_track_style])

    # Add the track data to the Placemark
    flight_line.geometry = fastkml.geometry.Geometry(
        ns=ns,
        geometry=_flight_geom,
        altitude_mode=_alt_mode,
        extrude=extrude,
        tessellate=tessellate)

    return flight_line


def write_kml(geom_objects,
                filename="output.kml",
                comment=""):
    """ Write out flight path geometry objects to a kml file. """

    kml_root = fastkml.kml.KML()
    kml_doc = fastkml.kml.Document(
        ns=ns,
        name=comment)

    if type(geom_objects) is not list:
        geom_objects = [geom_objects]

    for _flight in geom_objects:
        kml_doc.append(_flight)

    with open(filename,'w') as kml_file:
        kml_file.write(kml_doc.to_string())
        kml_file.close()


def convert_single_file(filename, absolute=True, tessellate=True, last_only=False):
    ''' Convert a single sonde log file to a fastkml KML Folder object '''

    # Read file.
    _flight_data = read_telemetry_csv(filename)

    # Extract the flight's serial number and launch time from the first line in the file.
    _first_line = _flight_data[0][4]
    _flight_serial = _first_line.split(',')[1] # Serial number is the second field in the line.
    _launch_time = _flight_data[0][0].strftime("%Y%m%d-%H%M%SZ")
    # Generate a comment line to use in the folder and placemark descriptions
    _track_comment = "%s %s" % (_launch_time, _flight_serial)
    _landing_comment = "%s Last Position" % (_flight_serial)

    # Grab burst and last-seen positions
    _burst_pos = flight_burst_position(_flight_data)
    _landing_pos = _flight_data[-1]

    # Generate the placemark & flight track.
    _flight_geom = flight_path_to_geometry(_flight_data, name=_track_comment, absolute=absolute, tessellate=tessellate, extrude=tessellate)
    _landing_geom = new_placemark(_landing_pos[1], _landing_pos[2], _landing_pos[3], name=_landing_comment, absolute=absolute)

    _folder = fastkml.kml.Folder(ns, _flight_serial, _track_comment, 'Radiosonde Flight Path')
    if last_only == False:
        _folder.append(_flight_geom)
    _folder.append(_landing_geom)

    return _folder


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-i", "--input", type=str, default="../log/*_sonde.log", 
        help="Path to log file. May include wildcards, though the path must be wrapped in quotes. Default=../log/*_sonde.log")
    parser.add_argument("-o", "--output", type=str, default="sondes.kml", help="KML output file name. Default=sondes.kml")
    parser.add_argument('--clamp', action="store_false", default=True, help="Clamp tracks to ground instead of showing absolute altitudes.")
    parser.add_argument('--noextrude', action="store_false", default=True, help="Disable Extrusions for absolute flight paths.")
    parser.add_argument('--lastonly', action="store_true", default=False, help="Only plot last-seen sonde positions, not the flight paths.")
    args = parser.parse_args()

    _file_list = glob.glob(args.input)

    _placemarks = []

    for _file in _file_list:
        print("Processing: %s" % _file)
        try:
            _placemarks.append(convert_single_file(_file, absolute=args.clamp, tessellate=args.noextrude, last_only=args.lastonly))
        except:
            print("Failed to process: %s" % _file)

    write_kml(_placemarks, filename=args.output)

    print("Output saved to: %s" % args.output)

















