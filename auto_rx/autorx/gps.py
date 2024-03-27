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
    """ Download the latest GPS ephemeris file from the ESA's FTP server """
    try:
        logging.debug("GPS Grabber - Connecting to ESA's FTP Server...")
        ftp = ftplib.FTP("gssc.esa.int", timeout=10)
        ftp.login("anonymous", "anonymous")
        ftp.cwd("gnss/data/daily/%s/" % datetime.datetime.now(datetime.timezone.utc).strftime("%Y"))
        # Ideally we would grab this data from: YYYY/brdc/brdcDDD0.YYn.Z
        # .. but the ESA brdc folder seems to be getting of date. The daily directories are OK though!
        # So instead, we use: YYYY/DDD/brdcDDD0.YYn.Z
        # ESA posts new file at 2200 UTC
        ephemeris_time = datetime.datetime.now(
            datetime.timezone.utc
        ) - datetime.timedelta(hours=22)
        download_file = "brdc%s0.%sn.gz" % (
            ephemeris_time.strftime("%j"),
            ephemeris_time.strftime("%y"),
        )

        # CWD into the current day.
        ftp.cwd(ephemeris_time.strftime("%j"))

        logging.debug("GPS Grabber - Current Directory: " + ftp.pwd())

        logging.debug(
            "GPS Grabber - Downloading ephemeris data file: %s" % download_file
        )

        # Download file.
        f_eph = open(destination + ".gz", "wb")
        ftp.retrbinary("RETR %s" % download_file, f_eph.write)
        f_eph.close()
        ftp.close()

        # Unzip file.
        os.system("gunzip -q -f ./%s" % (destination + ".gz"))

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
        logging.error(f"GPS Grabber - Failed to download almanac data - {str(e)}")
        return None


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    get_almanac()
    get_ephemeris()
