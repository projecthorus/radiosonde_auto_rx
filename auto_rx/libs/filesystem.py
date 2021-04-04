#!/usr/bin/python3.5

""" Module docstring """


# Global imports
import os
import sys

# Specific imports:
from pathlib import Path

# Checking python version:
if not sys.version_info >= (3, 5):
    print("[ERROR]\t\tThis script {} requires Python 3.5 or higher !".format(__file__))
    print("[ERROR]\t\tYou are using Python {}.{}.{}".format(sys.version_info.major,
                                                            sys.version_info.minor,
                                                            sys.version_info.micro))
    sys.exit()


# Functions definition
def get_folder_list(root_dir: Path) -> list:
    """ Get the list of folder in root_dir

    :param root_dir: The root path to search from
    :return: The list of dir
    """

    folder_list = [p for p in root_dir.iterdir() if p.is_dir()]
    return folder_list


def get_newest_folder(root_dir: Path) -> Path:
    """ Get the newest directory in root_dir

    :param root_dir: The root path to search from
    :return: The path of the newest dir
    """

    # Note : Check if there is content if folder
    if sum(1 for _ in root_dir.glob('*')) > 0:

        time, file_path = max((f.stat().st_mtime, f) for f in root_dir.iterdir() if f.is_dir())
        return file_path
    else:
        return Path("")


def remove_folder_content(folder_path: Path) -> None:
    """ Remove all file from folder_path """

    for root, dirs, files in os.walk(str(folder_path)):
        for file in files:
            os.remove(os.path.join(root, file))


def get_file_list(root_dir: Path,
                  file_pattern: str) -> list:
    """ Search recursively (or not) file pattern in directory and return list

    :param root_dir : The root dir for searching
    :param file_pattern : The file pattern to search
    :return File path list matching parameters
    """

    file_list = sorted(root_dir.rglob(file_pattern))
    return file_list
