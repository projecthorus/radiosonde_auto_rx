#!/usr/bin/python3.5

""" Module docstring """


# Global imports
from pathlib import Path
import sys
import unittest

# Specific imports:
from libs.CsvHandler.CsvHandler import CsvHandler
from libs.FileSystem.FileSystem import remove_folder_content


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

    # def setUp(self):
    #     """ Delete generated files """
    #     remove_folder_content(unit_test_path['generated']))

    def test_load_csv_file_1(self):
        """ Testing CsvHandler constructor """

        src_file_path = unit_test_path['inputs'].joinpath("file1.csv")
        csv_file = CsvHandler(src_file_path)
        self.assertGreater(len(csv_file), 0)

    def test_load_csv_file_2(self):
        """ Testing CsvHandler constructor with file that doesn't exists """

        src_file_path = unit_test_path['inputs'].joinpath("unexciting_file.csv")
        csv_file = CsvHandler(src_file_path)
        self.assertEqual(len(csv_file), 0)


class TestReadValue(unittest.TestCase):
    """ Testing 'read' method """

    def test_read_values(self):
        """ Testing reading value from existing CSV file """

        src_file_path = unit_test_path['inputs'].joinpath("file1.csv")
        csv_file = CsvHandler(src_file_path)

        for line, current_line in enumerate(csv_file):
            val = current_line['header1']
            self.assertEqual(val, "val{}".format(line))


class TestBuild(unittest.TestCase):
    """ Testing 'build' method """

    # def setUp(self):
    #     """ Delete generated files """
    #     remove_folder_content(unit_test_path['generated'])

    # TODO (@valid) DEV : A verifier le fichier CSV généré contient uniquement les headers")
    # def test_build(self):
    #     """ Testing 'build' method """
    #
    #     # Specification des chemins utiles
    #     csv_file_src_path = os.path.join(unit_test_path['inputs'], "file2.csv")
    #     csv_file_dst_path = os.path.join(unit_test_path['generated'], "file2_cpy.csv")
    #
    #     csv_file_src = CsvHandler(csv_file_src_path)
    #
    #     csv_file_dst = CsvHandler(csv_file_dst_path)
    #     csv_file_dst.build(csv_filepath=csv_file_dst_path,
    #                        headers=csv_file_src.fieldnames,
    #                        dialect=csv_file_src.dialect)
    #
    #     self.assertTrue(os.path.exists(csv_file_dst_path))
    #     self.assertTrue(filecmp.cmp(csv_file_src_path, csv_file_dst_path))


if __name__ == '__main__':
    unittest.main()
