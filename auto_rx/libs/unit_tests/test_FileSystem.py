#!/usr/bin/python3.5


""" Module docstring """

# Global imports:
import os
from pathlib import Path
import sys
import unittest

# Specific imports:
from libs.FileSystem.FileSystem import remove_folder_content, get_newest_folder, get_file_list, get_folder_list

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


class TestFileSystem(unittest.TestCase):
    """ Testing TestFileSystem methods """

    def test_get_newest_folder_1(self):
        """ Testing 'get_newest_folder' with valid parameter """

        last_dir = get_newest_folder(unit_test_path['root'])
        self.assertEqual(last_dir, Path("/home/straniello/Coding/AOC_Tester/libs/unit_tests/files/generated"))

    def test_get_newest_folder_2(self):
        """ Testing 'get_newest_folder' without folder """

        path = unit_test_path['root'].joinpath("empty_folder")
        last_dir = get_newest_folder(path)
        self.assertEqual(last_dir, Path(""))

    def test_remove_folder_content_1(self):
        """ Testing 'remove_folder_content' with valid parameter """

        remove_folder_content(unit_test_path['generated'])

        file_list = os.listdir(str(unit_test_path['generated']))
        self.assertListEqual(file_list, list())

    def test_get_file_list_1(self):
        """ Testing 'get_file_list' with valid parameter """

        files_list = get_file_list(unit_test_path['inputs'], "*.csv")
        self.assertGreater(len(files_list), 0)

    def test_get_file_list_2(self):
        """ Testing 'get_file_list' with valid parameter """

        files_list = get_file_list(unit_test_path['inputs'], "*.csv")
        self.assertGreater(len(files_list), 0)

    def test_get_folder_list_1(self):
        """ Testing 'get_folder_list' with valid parameter """

        folder_list = get_folder_list(unit_test_path['root'])
        self.assertGreater(len(folder_list), 0)


if __name__ == '__main__':
    unittest.main()
