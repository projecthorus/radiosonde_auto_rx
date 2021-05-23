#!/usr/bin/env python
#
#   Project Horus - Flight Data to Geometry
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import math
import traceback
import logging
import numpy as np
from .utils import position_info


def getDensity(altitude, get_pressure=False):
    """ 
    Calculate the atmospheric density for a given altitude in metres.
    This is a direct port of the oziplotter Atmosphere class
    """

    # Constants
    airMolWeight = 28.9644  # Molecular weight of air
    densitySL = 1.225  # Density at sea level [kg/m3]
    pressureSL = 101325  # Pressure at sea level [Pa]
    temperatureSL = 288.15  # Temperature at sea level [deg K]
    gamma = 1.4
    gravity = 9.80665  # Acceleration of gravity [m/s2]
    tempGrad = -0.0065  # Temperature gradient [deg K/m]
    RGas = 8.31432  # Gas constant [kg/Mol/K]
    R = 287.053
    deltaTemperature = 0.0

    # Lookup Tables
    altitudes = [0, 11000, 20000, 32000, 47000, 51000, 71000, 84852]
    pressureRels = [
        1,
        2.23361105092158e-1,
        5.403295010784876e-2,
        8.566678359291667e-3,
        1.0945601337771144e-3,
        6.606353132858367e-4,
        3.904683373343926e-5,
        3.6850095235747942e-6,
    ]
    temperatures = [288.15, 216.65, 216.65, 228.65, 270.65, 270.65, 214.65, 186.946]
    tempGrads = [-6.5, 0, 1, 2.8, 0, -2.8, -2, 0]
    gMR = gravity * airMolWeight / RGas

    # Pick a region to work in
    i = 0
    if altitude > 0:
        while altitude > altitudes[i + 1]:
            i = i + 1

    # Lookup based on region
    baseTemp = temperatures[i]
    tempGrad = tempGrads[i] / 1000.0
    pressureRelBase = pressureRels[i]
    deltaAltitude = altitude - altitudes[i]
    temperature = baseTemp + tempGrad * deltaAltitude

    # Calculate relative pressure
    if math.fabs(tempGrad) < 1e-10:
        pressureRel = pressureRelBase * math.exp(
            -1 * gMR * deltaAltitude / 1000.0 / baseTemp
        )
    else:
        pressureRel = pressureRelBase * math.pow(
            baseTemp / temperature, gMR / tempGrad / 1000.0
        )

    # Add temperature offset
    temperature = temperature + deltaTemperature

    # Finally, work out the density...
    speedOfSound = math.sqrt(gamma * R * temperature)
    pressure = pressureRel * pressureSL
    if get_pressure:
        return pressure

    density = densitySL * pressureRel * temperatureSL / temperature

    return density


def seaLevelDescentRate(descent_rate, altitude):
    """ Calculate the descent rate at sea level, for a given descent rate at altitude """

    rho = getDensity(altitude)
    return math.sqrt((rho / 1.22) * math.pow(descent_rate, 2))


def time_to_landing(
    current_altitude, current_descent_rate=-5.0, ground_asl=0.0, step_size=1
):
    """ Calculate an estimated time to landing (in seconds) of a payload, based on its current altitude and descent rate """

    # A few checks on the input data.
    if current_descent_rate > 0.0:
        # If we are still ascending, return none.
        return None

    if current_altitude <= ground_asl:
        # If the current altitude is *below* ground level, we have landed.
        return 0

    # Calculate the sea level descent rate.
    _desc_rate = math.fabs(seaLevelDescentRate(current_descent_rate, current_altitude))
    _drag_coeff = _desc_rate * 1.1045  # Magic multiplier from predict.php

    _alt = current_altitude
    _start_time = 0
    # Now step through the flight in <step_size> second steps.
    # Once the altitude is below our ground level, stop, and return the elapsed time.
    while _alt >= ground_asl:
        _alt += step_size * -1 * (_drag_coeff / math.sqrt(getDensity(_alt)))
        _start_time += step_size

    return _start_time


class GenericTrack(object):
    """
    A Generic 'track' object, which stores track positions for a payload or chase car.
    Telemetry is added using the add_telemetry method, which takes a dictionary with time/lat/lon/alt keys (at minimum).
    This object performs a running average of the ascent/descent rate, and calculates the predicted landing rate if the payload
    is in descent.
    The track history can be exported to a LineString using the to_line_string method.
    """

    def __init__(self, ascent_averaging=6, landing_rate=5.0, max_elements=None):
        """ Create a GenericTrack Object. """

        # Averaging rate.
        self.ASCENT_AVERAGING = ascent_averaging
        # Payload state.
        self.landing_rate = landing_rate
        self.max_elements = max_elements
        self.ascent_rate = 0.0
        self.heading = 0.0
        self.speed = 0.0
        self.is_descending = False

        # Internal store of track history data.
        # Data is stored as a list-of-lists, with elements of [datetime, lat, lon, alt, comment]
        self.track_history = []

    def add_telemetry(self, data_dict):
        """ 
        Accept telemetry data as a dictionary with fields 
        datetime, lat, lon, alt, comment
        """

        try:
            _datetime = data_dict["time"]
            _lat = data_dict["lat"]
            _lon = data_dict["lon"]
            _alt = data_dict["alt"]
            if "comment" in data_dict.keys():
                _comment = data_dict["comment"]
            else:
                _comment = ""

            self.track_history.append([_datetime, _lat, _lon, _alt, _comment])

            # Clip size of track history if a maximum number of elements is set.
            if self.max_elements:
                if len(self.track_history) > self.max_elements:
                    self.track_history = self.track_history[1:]

            self.update_states()
            return self.get_latest_state()
        except ValueError:
            # ValueErrors show up when the positions used are too close together, or when
            # altitudes are the same between positions (divide-by-zero error)
            # We can safely skip over these.
            pass
        except Exception as e:
            logging.debug(
                "Track - Error adding new telemetry to GenericTrack %s" % str(e)
            )

    def get_latest_state(self):
        """ Get the latest position of the payload """

        if len(self.track_history) == 0:
            return None
        else:
            _latest_position = self.track_history[-1]
            _state = {
                "time": _latest_position[0],
                "lat": _latest_position[1],
                "lon": _latest_position[2],
                "alt": _latest_position[3],
                "ascent_rate": self.ascent_rate,
                "is_descending": self.is_descending,
                "landing_rate": self.landing_rate,
                "heading": self.heading,
                "speed": self.speed,
            }
            return _state

    def calculate_ascent_rate(self):
        """ Calculate the ascent/descent rate of the payload based on the available data """
        if len(self.track_history) <= 1:
            return 0.0
        elif len(self.track_history) == 2:
            # Basic ascent rate case - only 2 samples.
            _time_delta = (
                self.track_history[-1][0] - self.track_history[-2][0]
            ).total_seconds()
            _altitude_delta = self.track_history[-1][3] - self.track_history[-2][3]

            return _altitude_delta / _time_delta

        else:
            _num_samples = min(len(self.track_history), self.ASCENT_AVERAGING)
            _asc_rates = []

            for _i in range(-1 * (_num_samples - 1), 0):
                _time_delta = (
                    self.track_history[_i][0] - self.track_history[_i - 1][0]
                ).total_seconds()
                _altitude_delta = (
                    self.track_history[_i][3] - self.track_history[_i - 1][3]
                )
                _asc_rates.append(_altitude_delta / _time_delta)

            return np.mean(_asc_rates)

    def calculate_heading(self):
        """ Calculate the heading of the payload """
        if len(self.track_history) <= 1:
            return 0.0
        else:
            _pos_1 = self.track_history[-2]
            _pos_2 = self.track_history[-1]

            _pos_info = position_info(
                (_pos_1[1], _pos_1[2], _pos_1[3]), (_pos_2[1], _pos_2[2], _pos_2[3])
            )

            return _pos_info["bearing"]

    def calculate_speed(self):
        """ Calculate Payload Speed in metres per second """
        if len(self.track_history) <= 1:
            return 0.0
        else:
            _time_delta = (
                self.track_history[-1][0] - self.track_history[-2][0]
            ).total_seconds()
            _pos_1 = self.track_history[-2]
            _pos_2 = self.track_history[-1]

            _pos_info = position_info(
                (_pos_1[1], _pos_1[2], _pos_1[3]), (_pos_2[1], _pos_2[2], _pos_2[3])
            )

            _speed = _pos_info["great_circle_distance"] / _time_delta

            return _speed

    def update_states(self):
        """ Update internal states based on the current data """
        self.ascent_rate = self.calculate_ascent_rate()
        self.heading = self.calculate_heading()
        self.speed = self.calculate_speed()
        self.is_descending = self.ascent_rate < 0.0

        if self.is_descending:
            _current_alt = self.track_history[-1][3]
            self.landing_rate = seaLevelDescentRate(self.ascent_rate, _current_alt)

    def to_polyline(self):
        """ Generate and return a Leaflet PolyLine compatible array """
        # Copy array into a numpy representation for easier slicing.
        if len(self.track_history) == 0:
            return []
        elif len(self.track_history) == 1:
            # LineStrings need at least 2 points. If we only have a single point,
            # fudge it by duplicating the single point.
            _track_data_np = np.array([self.track_history[0], self.track_history[0]])
        else:
            _track_data_np = np.array(self.track_history)
        # Produce new array
        _track_points = np.column_stack(
            (_track_data_np[:, 1], _track_data_np[:, 2], _track_data_np[:, 3])
        )

        return _track_points.tolist()
