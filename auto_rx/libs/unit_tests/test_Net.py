#!/usr/bin/python3.5


""" Module docstring """

# Global imports:
from pathlib import Path
import sys
import unittest


# Specific imports:
from libs.Net.Net import Net

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


class TestNet(unittest.TestCase):
    """ Testing 'is_valid_url' method """

    def test_is_valid_url_1(self):
        """ Testing 'is_valid_url' with a VALID url """

        valid_url_list = ["http://10.0.0.6:9001",
                          "http://10.0.0.6",
                          "http://10.0.0.6:9001/path/to/index.html"]

        for url in valid_url_list:
            self.assertTrue(Net.is_valid_url(url))

    def test_is_valid_url_2(self):
        """ Testing 'is_valid_url' with INVALID url """

        invalid_url_list = ["http:/10.0.0.6:9001",
                            "htt:/10.0.0.6;9001"]

        for url in invalid_url_list:
            self.assertFalse(Net.is_valid_url(url))


if __name__ == '__main__':
    unittest.main()
