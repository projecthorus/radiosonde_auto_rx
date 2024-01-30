#!/usr/bin/env python
#
# Radiosonde Auto RX Tools
# Log-to-KML Utilities
#
# 2018-02 Mark Jessop <vk5qi@rfhead.net>
#

import traceback
import argparse
import glob
import xml.etree.ElementTree as ET
from dateutil.parser import parse


def read_telemetry_csv(filename,
                       datetime_field=0,
                       latitude_field=3,
                       longitude_field=4,
                       altitude_field=5,
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

    f = open(filename, 'r')

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


def new_placemark(lat, lon, alt,
                  name="Placemark Name",
                  absolute=False,
                  icon="http://maps.google.com/mapfiles/kml/shapes/placemark_circle.png",
                  scale=1.0):
    """ Generate a generic placemark object """

    placemark = ET.Element("Placemark")

    pm_name = ET.SubElement(placemark, "name")
    pm_name.text = name

    style = ET.SubElement(placemark, "Style")
    icon_style = ET.SubElement(style, "IconStyle")
    icon_scale = ET.SubElement(icon_style, "scale")
    icon_scale.text = str(scale)
    pm_icon = ET.SubElement(icon_style, "Icon")
    href = ET.SubElement(pm_icon, "href")
    href.text = icon

    point = ET.SubElement(placemark, "Point")
    if absolute:
        altitude_mode = ET.SubElement(point, "altitudeMode")
        altitude_mode.text = "absolute"
    coordinates = ET.SubElement(point, "coordinates")
    coordinates.text = f"{lon:.6f},{lat:.6f},{alt:.6f}"

    return placemark


def flight_path_to_geometry(flight_path,
                            name="Flight Path Name",
                            track_color="aaffffff",
                            poly_color="20000000",
                            track_width=2.0,
                            absolute=True,
                            extrude=True):
    ''' Produce a placemark object from a flight path array '''

    placemark = ET.Element("Placemark")

    pm_name = ET.SubElement(placemark, "name")
    pm_name.text = name

    style = ET.SubElement(placemark, "Style")
    line_style = ET.SubElement(style, "LineStyle")
    color = ET.SubElement(line_style, "color")
    color.text = track_color
    width = ET.SubElement(line_style, "width")
    width.text = str(track_width)
    poly_style = ET.SubElement(style, "PolyStyle")
    color = ET.SubElement(poly_style, "color")
    color.text = poly_color
    fill = ET.SubElement(poly_style, "fill")
    fill.text = "1"
    outline = ET.SubElement(poly_style, "outline")
    outline.text = "1"

    line_string = ET.SubElement(placemark, "LineString")
    if absolute:
        if extrude:
            ls_extrude = ET.SubElement(line_string, "extrude")
            ls_extrude.text = "1"
        altitude_mode = ET.SubElement(line_string, "altitudeMode")
        altitude_mode.text = "absolute"
    else:
        ls_tessellate = ET.SubElement(line_string, "tessellate")
        ls_tessellate.text = "1"
    coordinates = ET.SubElement(line_string, "coordinates")
    coordinates.text = " ".join(f"{lon:.6f},{lat:.6f},{alt:.6f}" for _, lat, lon, alt, _ in flight_path)

    return placemark


def write_kml(geom_objects,
              kml_file,
              comment=""):
    """ Write out flight path geometry objects to a kml file. """

    kml_root = ET.Element("kml", {"xmlns": "http://www.opengis.net/kml/2.2"})
    kml_doc = ET.SubElement(kml_root, "Document")

    if type(geom_objects) is not list:
        geom_objects = [geom_objects]

    for _flight in geom_objects:
        kml_doc.append(_flight)

    tree = ET.ElementTree(kml_root)
    tree.write(kml_file, encoding="UTF-8", xml_declaration=True)


def convert_single_file(filename, absolute=True, extrude=True, last_only=False):
    ''' Convert a single sonde log file to a KML Folder object '''

    # Read file.
    _flight_data = read_telemetry_csv(filename)

    # Extract the flight's serial number and launch time from the first line in the file.
    _first_line = _flight_data[0][4]
    _flight_serial = _first_line.split(',')[1]  # Serial number is the second field in the line.
    _launch_time = _flight_data[0][0].strftime("%Y%m%d-%H%M%SZ")
    # Generate a comment line to use in the folder and placemark descriptions
    _track_comment = "%s %s" % (_launch_time, _flight_serial)
    _landing_comment = "%s Last Position" % (_flight_serial)

    # Grab last-seen position
    _landing_pos = _flight_data[-1]

    _folder = ET.Element("Folder")
    _name = ET.SubElement(_folder, "name")
    _name.text = _track_comment
    _description = ET.SubElement(_folder, "description")
    _description.text = "Radiosonde Flight Path"

    # Generate the placemark & flight track.
    if not last_only:
        _folder.append(flight_path_to_geometry(_flight_data, name=_track_comment,
                                               absolute=absolute, extrude=extrude))
    _folder.append(new_placemark(_landing_pos[1], _landing_pos[2], _landing_pos[3],
                                 name=_landing_comment, absolute=absolute))

    return _folder


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

    _placemarks = []

    for _file in _file_list:
        print("Processing: %s" % _file)
        try:
            _placemarks.append(convert_single_file(_file, absolute=args.clamp,
                                                   extrude=args.noextrude, last_only=args.lastonly))
        except:
            print("Failed to process: %s" % _file)

    with open(args.output, "wb") as kml_file:
        write_kml(_placemarks, kml_file)

    print("Output saved to: %s" % args.output)
