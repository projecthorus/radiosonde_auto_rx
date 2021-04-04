
#!/bin/python3
#
# Radiosonde Auto RX Tools
# Log-to-KML Utilities
#
# 2018-02 Mark Jessop <vk5qi@rfhead.net>
#
# Note: This utility requires the fastkml and shapely libraries, which can be installed using:
# sudo pip install fastkml shapely
#

# Global imports
import argparse
from dateutil.parser import *
import fastkml
import glob
from pathlib import Path
from shapely.geometry import Point, LineString
import traceback


FILE_NAME = Path(__file__).name


# Constants definition
ns = '{http://www.opengis.net/kml/2.2}'


# Functions definition
def read_telemetry_csv(filename: Path,
                       datetime_field: int = 0,
                       latitude_field: int = 3,
                       longitude_field: int = 4,
                       altitude_field: int = 5,
                       delimiter: str =',') -> dict or None:
    """
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

    :param filename: The path of the telemetry CSV file
    :param datetime_field: Index of the datetime field
    :param latitude_field: Index of the latitude field
    :param longitude_field: Index of the longitude field
    :param altitude_field: Index of the altitude field
    :param delimiter: The delimiter of the CSV file
    """

    output = []

    with filename.open(mode='r') as f:
        for line in f:
            try:
                # Split line by comma delimiters.
                _fields = line.split(delimiter)

                if _fields[0] == 'timestamp':
                    # First line in file - header line.
                    continue

                # Attempt to parse fields.
                _datetime  = parse(_fields[datetime_field])
                _latitude  = float(_fields[latitude_field])
                _longitude = float(_fields[longitude_field])
                _altitude  = float(_fields[altitude_field])

                output.append([_datetime, _latitude, _longitude, _altitude, line])
            except:
                traceback.print_exc()
                return None

    return output


def flight_burst_position(flight_path: dict):
    """ Search through flight data for the burst position and return it.

    :param flight_path : TODO DOC
    :return : Flight data of the burst position
    """

    # Read through array and hunt for max altitude point.
    current_alt   = 0.0
    current_index = 0
    for i in range(len(flight_path)):
        if flight_path[i][3] > current_alt:
            current_alt   = flight_path[i][3]
            current_index = i

    return flight_path[current_index]


def new_placemark(lat: float,
                  lon: float,
                  alt: float,
                  placemark_id: str = "Placemark ID",
                  name: str = "Placemark Name",
                  absolute: bool = False,
                  icon: str = "http://maps.google.com/mapfiles/kml/shapes/placemark_circle.png",
                  scale: float = 1.0) -> fastkml.kml.Placemark:
    """ Generate a generic placemark object

    :param lat: Latitude of the new placemark
    :param lon: Longitude of the new placemark
    :param alt: Altitude of the new placemark
    :param placemark_id: Id of the new placemark
    :param name: Name of the new placemark
    :param absolute: Set the placemark on the ground or absolute altitude
    :param icon: URL of the placemark icon
    :param scale: Initial scale of the new placemark

    :return The new placemark
    """

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
                            placemark_id: str = "Flight Path ID",
                            name: str = "Flight Path Name",
                            track_color: str = "aaffffff",
                            poly_color: str = "20000000",
                            track_width: float = 2.0,
                            absolute: bool = True,
                            extrude: bool = True,
                            tessellate: bool = True) -> fastkml.geometry.Geometry:
    """ Produce a fastkml geometry object from a flight path array

    :param flight_path: TODO DOC
    :param placemark_id: Id of the placemark
    :param name: Name of the placemark
    :param track_color: The color of the track
    :param poly_color: TODO DOC
    :param track_width: The width of the track
    :param absolute: Set the placemark on the ground or absolute altitude
    :param extrude: TODO DOC
    :param tessellate: TODO DOC
    :return: TODO DOC
    """

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
              filename: Path = "output.kml",
              comment: str = ""):
    """ Write out flight path geometry objects to a kml file.

    :param geom_objects: TODO DOC
    :param filename: The filename of KML output file
    :param comment: Comment of this KML file
    """

    kml_root = fastkml.kml.KML()
    kml_doc = fastkml.kml.Document(
        ns=ns,
        name=comment)

    if type(geom_objects) is not list:
        geom_objects = [geom_objects]

    for _flight in geom_objects:
        kml_doc.append(_flight)

    with filename.open(mode='w') as kml_file:
        kml_file.write(kml_doc.to_string())
        kml_file.close()


def convert_single_file(filename: Path,
                        absolute: bool = True,
                        tessellate: bool = True,
                        last_only: bool = False) -> fastkml.kml.Folder:
    """ Convert a single sonde log file to a fastkml KML Folder object

    :param filename : The filename path of CSV telemtry file
    :param absolute : Set placemark on the ground or absolute altitude
    :param tessellate : TODO DOC
    :param last_only : TODO DOC
    """

    # Read file.
    _flight_data = read_telemetry_csv(filename)

    # Extract the flight's serial number and launch time from the first line in the file.
    _first_line    = _flight_data[0][4]
    _flight_serial = _first_line.split(',')[1] # Serial number is the second field in the line.
    _launch_time   = _flight_data[0][0].strftime("%Y%m%d-%H%M%SZ")

    # Generate a comment line to use in the folder and placemark descriptions
    _track_comment   = "{} {}".format(_launch_time, _flight_serial)
    _landing_comment = "{} Last Position".format(_flight_serial)

    # Grab burst and last-seen positions
    _burst_pos   = flight_burst_position(_flight_data)
    _landing_pos = _flight_data[-1]

    # Generate the placemark & flight track.
    _flight_geom  = flight_path_to_geometry(_flight_data, name=_track_comment, absolute=absolute, tessellate=tessellate, extrude=tessellate)
    _landing_geom = new_placemark(_landing_pos[1], _landing_pos[2], _landing_pos[3], name=_landing_comment, absolute=absolute)

    _folder = fastkml.kml.Folder(ns, _flight_serial, _track_comment, 'Radiosonde Flight Path')
    if not last_only:
        _folder.append(_flight_geom)
    _folder.append(_landing_geom)

    return _folder


if __name__ == "__main__":
    """ Main entry point """

    # Messages
    epilog_msg = '''\
                    See above examples
                        - [?]$ ./{0} -i "../log/*_sonde.log"
                    '''.format(FILE_NAME)

    # Specify command arguments
    parser = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter,
                                     description="Plot an rtl_power output file.",
                                     epilog=epilog_msg)

    parser.add_argument("-i", "--input",
                        type=str, default="../log/*_sonde.log",
                        help="Path to log file. May include wildcards, though the path must be wrapped in quotes.")

    parser.add_argument("-o", "--output",
                        type=str, default="sondes.kml",
                        help="KML output file name.")

    parser.add_argument('--clamp',
                        action="store_false", default=True,
                        help="Clamp tracks to ground instead of showing absolute altitudes.")

    parser.add_argument('--noextrude',
                        action="store_false", default=True,
                        help="Disable Extrusions for absolute flight paths.")

    parser.add_argument('--lastonly',
                        action="store_true", default=False,
                        help="Only plot last-seen sonde positions, not the flight paths.")

    args = parser.parse_args()

    _placemarks = []

    _file_list = glob.glob(args.input)
    for _file in _file_list:
        print("Processing: {}".format(_file))
        try:
            _placemarks.append(convert_single_file(Path(_file), absolute=args.clamp, tessellate=args.noextrude, last_only=args.lastonly))
        except:
            print("Failed to process: {}".format(_file))

    write_kml(_placemarks, filename=Path(args.output))

    print("Output saved to: {}".format(args.output))
