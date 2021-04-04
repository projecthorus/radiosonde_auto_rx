#!/usr/bin/python3.5

""" Module docstring """

# Global imports:
import sys
from urllib.parse import urlparse


# Checking python version
if not sys.version_info >= (3, 5):
    print("[ERROR]\t\tThis script {} requires Python 3.5 or higher !".format(__file__))
    print("[ERROR]\t\tYou are using Python {}.{}.{}".format(sys.version_info.major,
                                                            sys.version_info.minor,
                                                            sys.version_info.micro))
    sys.exit()


class Net(object):
    """ Net class """

    @staticmethod
    def is_valid_url(url: str) -> bool:
        """ Check if string is a valid URL

        :param url: The string to check
        :return: True if its valid, else False
        """

        try:
            result = urlparse(url)
            return all([result.scheme, result.netloc])
        except:
            return False
