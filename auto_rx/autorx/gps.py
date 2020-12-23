#!/usr/bin/env python
#
# Radiosonde Auto RX Tools - GPS Ephemeris / Almanac Grabber
#
# 2017-04 Mark Jessop <vk5qi@rfhead.net>
#
import ftplib
import requests
import datetime
import logging
import os


def get_ephemeris(destination="ephemeris.dat"):
    """ Download the latest GPS ephemeris file from the CDDIS's FTP server """
    try:
        logging.debug("GPS Grabber - Connecting to GSFC FTP Server...")
        ftp = ftplib.FTP("cddis.gsfc.nasa.gov", timeout=10)
        ftp.login("anonymous", "anonymous")
        ftp.cwd("gnss/data/daily/%s/brdc/" % datetime.datetime.utcnow().strftime("%Y"))
        file_list = ftp.nlst()

        # We expect the latest files to be the last in the list.
        download_file = None
        file_suffix = datetime.datetime.utcnow().strftime("%yn.Z")

        if file_suffix in file_list[-1]:
            download_file = file_list[-1]
        elif file_suffix in file_list[-2]:
            download_file = file_list[-2]
        else:
            logging.error("GPS Grabber - Could not find appropriate ephemeris file.")
            return None

        logging.debug(
            "GPS Grabber - Downloading ephemeris data file: %s" % download_file
        )

        # Download file.
        f_eph = open(destination + ".Z", "wb")
        ftp.retrbinary("RETR %s" % download_file, f_eph.write)
        f_eph.close()
        ftp.close()

        # Unzip file.
        os.system("gunzip -q -f ./%s" % (destination + ".Z"))

        logging.info(
            "GPS Grabber - Ephemeris downloaded to %s successfuly!" % destination
        )

        return destination
    except Exception as e:
        logging.error("GPS Grabber - Could not download ephemeris file. - %s" % str(e))
        return None


def get_almanac(destination="almanac.txt", timeout=20):
    """ Download the latest GPS almanac file from the US Coast Guard website. """
    try:
        _r = requests.get(
            "https://www.navcen.uscg.gov/?pageName=currentAlmanac&format=sem",
            timeout=timeout,
        )
        data = _r.text
        if "CURRENT.ALM" in data:
            f = open(destination, "w")
            f.write(data)
            f.close()
            logging.info(
                "GPS Grabber - Almanac downloaded to %s successfuly!" % destination
            )
            return destination
        else:
            logging.error("GPS Grabber - Downloaded file is not a GPS almanac.")
            return None
    except Exception as e:
        logging.error("GPS Grabber - Failed to download almanac data - " % str(e))
        return None


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    get_almanac()
    get_ephemeris()
