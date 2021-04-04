#!/usr/bin/python3.5


""" Module IniFileParser """

# Global imports:
from io import StringIO
import os
from enum import unique, IntEnum

# Specific imports:
from configparser import ConfigParser
from pathlib import Path
from typing import Union


@unique
class ReturnCode(IntEnum):
    """ Enum for last error """
    NO_ERROR          = 0
    NOT_REGULAR_FILE  = -1
    UNKOWN_KEY        = -2
    UNKOWN_TYPE       = -3


class IniFileParser(object):
    """ IniFileParser class """

    def __init__(self):
        """ Constructor """
        self._values = dict()

    def load(self,
             filename: Union[str, Path]) -> int:
        """
        Load parameters from ini file.
        :param filename: File to add to the current known values
        :return: int: Number of value
        """

        if isinstance(filename, Path):
            filename = str(filename)

        if os.path.isfile(filename):
            ini_str = "[DUMMY_SECTION]\n"  # + open(self._ini_filename, 'r').read()

            with open(filename, 'r') as ini_file:
                ini_str += ini_file.read()

            ini_fp = StringIO(ini_str)

            config = ConfigParser()
            config.read_file(ini_fp)

            # Loop through all parameters
            for section in config.sections():
                if section not in self._values.keys():
                    self._values[section] = dict()
                for option in config.options(section):
                    self._values[section][option] = config.get(section, option).strip('"')
            ret = len(self._values[section])
        else:
            ret = ReturnCode.NOT_REGULAR_FILE

        return ret

    def get_value(self,
                  key: str,
                  val_type: type,
                  to_display: bool = False) -> Union[str, int]:
        """
        Get value of parameter in section from .ini file.

        :param key: The name of the parameter to get
        :param val_type: The type of value to return (all values fetch from .ini are str)
        :param to_display: Flag to display converted variable
        :return: The value of parameter in section from .ini file
        """

        # Note : Etant données que les clés sont stockées en minuscule, on est obligé de convertir la casse de la clé spécifiée
        key_lower = key.lower()

        if key_lower not in self._values['DUMMY_SECTION'].keys():
            return ReturnCode.UNKOWN_KEY

        raw_val = self._values['DUMMY_SECTION'][key_lower]

        if val_type is str:
            val = raw_val

        elif val_type is bytes:
            val = raw_val.encode('utf8')

        elif val_type is int:
            val = int(raw_val)

        elif val_type is Path:
            val = Path(raw_val)

        else:
            val = ReturnCode.UNKOWN_TYPE

        if to_display:
            print("{:20} = {}".format(key_lower, val))

        return val
