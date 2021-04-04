#!/usr/bin/python3.5

""" Module docstring """


# Global imports:
import csv
import pprint
from pathlib import Path
import sys
from typing import List, Optional, Tuple

# Specific imports:


class CsvHandler(list):
    """ This class allow to handle CSV file :
        - Read CSV file into list
        - Write CSV file from list
    """

    def __init__(self,
                 csv_file_path: Path,
                 encoding: str = 'utf-8',
                 delimiters: Tuple[str] = ('|', ',', ';', ':')) -> None:
        """
        Read and store a CSV file as a list.
        :param csv_file_path: The path to the input CSV file
        :param encoding: csv encoding
        :param delimiters: list of possible delimiters
        """

        super().__init__()

        self._encoding = encoding

        if not csv_file_path.is_file():
            # This creates the file if it does not exist.
            csv_file_path.open('w', encoding=self._encoding).close()
            print("1")
            return

        # Else read:
        with csv_file_path.open('r', encoding=self._encoding) as csv_file:

            # Guessing CSV dialect file:
            self.dialect = csv.Sniffer().sniff(csv_file.readline(), delimiters=delimiters)
            csv_file.seek(0)

            # Reading column names from CSV file
            reader = csv.DictReader(csv_file, dialect=self.dialect)
            self.fieldnames = reader.fieldnames

            try:
                for csv_line in reader:

                    # Checking comment char in first position
                    if len(csv_line) > 0 and not csv_line[self.fieldnames[0]].startswith('#'):
                        if '' in csv_line:
                            del csv_line['']
                        if None in csv_line:
                            del csv_line[None]

                        data_str = {}
                        for key, value in csv_line.items():
                            data_str[key] = value

                        self.append(data_str)

            except csv.Error as error:
                sys.exit("File {}, line {} : {}".format(csv_file_path, reader.line_num, error).encode(self._encoding))

    def build(self,
              headers: List,
              csv_file_path: Path,
              dialect: Optional[str] = None) -> None:
        """
        Generate CSV file.
        :param headers: The headers list
        :param csv_file_path: The path for output file
        :param dialect: input dialect
        """

        with csv_file_path.open('w', encoding=self._encoding) as csv_file:

            if dialect in ["excel", "excel-tab"]:
                writer = csv.DictWriter(csv_file, headers, dialect=dialect, extrasaction='ignore')

            else:
                writer = csv.DictWriter(csv_file, headers, delimiter=';', quotechar='"', quoting=csv.QUOTE_ALL, extrasaction='ignore')

            self.insert(0, dict((i, i) for i in headers))
            writer.writerows(self)
