#!/usr/bin/python3.5


""" Module docstring """

# Global imports:
from pathlib import Path
import sys
import unittest

# Specific imports:
from libs.IniFileParser.IniFileParser import IniFileParser, ReturnCode

# Checking python version
if not sys.version_info >= (3, 5):
    print("[ERROR]\t\tThis script {} requires Python 3.5 or higher !".format(__file__))
    print("[ERROR]\t\tYou are using Python {}.{}.{}".format(sys.version_info.major,
                                                            sys.version_info.minor,
                                                            sys.version_info.micro))
    sys.exit()

# Constants definition
# Path to unit_test files /folder samples
current_directory = Path(__file__).parent.absolute()  # /home/straniello/Coding/AOC_Tester/libs/unit_tests
unit_test_path = {
    'root'      : current_directory.joinpath("files"),
    'inputs'    : current_directory.joinpath("files/inputs"),
    'generated' : current_directory.joinpath("files/generated"),
}


class TestLoad(unittest.TestCase):
    """ Testing 'load' method """

    def test_load_configuration_1(self):
        """ Testing 'load_configuration' with a real file """

        config = IniFileParser()
        input_file = unit_test_path['inputs'].joinpath("file.conf")
        self.assertGreater(config.load(input_file), 0)

    def test_load_configuration_2(self):
        """ Testing 'load_configuration' with non-existent input file """

        config = IniFileParser()
        input_file = unit_test_path['inputs'].joinpath("unknown_file.conf")
        self.assertEqual(config.load(input_file), ReturnCode.NOT_REGULAR_FILE)


class TestGetValue(unittest.TestCase):
    """ Testing 'get_value' method """

    def test_get_unknown_key(self):
        """ Testing 'get_value' with unknown key value """

        config = IniFileParser()
        input_file = unit_test_path['inputs'].joinpath("file.conf")
        config.load(input_file)

        self.assertEqual(config.get_value("unknown", str), ReturnCode.UNKOWN_KEY)

    def test_get_str_value(self):
        """ Testing 'get_value' with string value """

        config = IniFileParser()
        input_file = unit_test_path['inputs'].joinpath("file.conf")
        config.load(input_file)

        self.assertEqual(config.get_value("key_str", str), "valkey1")

    def test_get_int_value(self):
        """ Testing 'get_value' with int value """

        config = IniFileParser()
        input_file = unit_test_path['inputs'].joinpath("file.conf")
        config.load(input_file)

        self.assertEqual(config.get_value("key_int", int), 123)

    def test_get_bytes_value(self):
        """ Testing 'get_value' with UPPER CASE key """

        config = IniFileParser()
        input_file = unit_test_path['inputs'].joinpath("file.conf")
        config.load(input_file)

        self.assertEqual(config.get_value("key_bytes", bytes), b"")

    def test_get_path_value(self):
        """ Testing 'get_value' with UPPER CASE key """

        config = IniFileParser()
        input_file = unit_test_path['inputs'].joinpath("file.conf")
        config.load(input_file)

        self.assertEqual(config.get_value("key_path", Path), Path("~/Documents"))

    def test_get_unknown_type_value(self):
        """ Testing 'get_value' with unkown type """

        config = IniFileParser()
        input_file = unit_test_path['inputs'].joinpath("file.conf")
        config.load(input_file)

        self.assertEqual(config.get_value("key_int", 'unknown_type'), ReturnCode.UNKOWN_TYPE)

    def test_value_from_UpperCase_key(self):
        """ Testing 'get_value' with UPPER CASE key """

        config = IniFileParser()
        input_file = unit_test_path['inputs'].joinpath("file.conf")
        config.load(input_file)

        self.assertEqual(config.get_value("UPPERCASEKEY", int), 246)


class TestDisplayValue(unittest.TestCase):

    @unittest.skip("A été testé. Cette méthode fonctionne. Desactivation pour ne pas polluer le terminal")
    def test_display_value(self):
        """ Testing 'get_value' and display value on screen """

        config = IniFileParser()
        input_file = unit_test_path['inputs'].joinpath("file.conf")
        config.load(input_file)

        self.assertEqual(config.get_value("key_int", int, True), 123)


if __name__ == '__main__':
    unittest.main()
