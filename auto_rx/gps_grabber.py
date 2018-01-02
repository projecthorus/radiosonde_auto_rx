#!/usr/bin/env python
#
# Radiosonde Auto RX Tools - GPS Ephemeris / Almanac Grabber
#
# 2017-04 Mark Jessop <vk5qi@rfhead.net>
#
import ftplib
import urllib2
import datetime
import logging
import os

def get_ephemeris(destination="ephemeris.dat"):
	''' Download the latest GPS ephemeris file from the CDDIS's FTP server '''
	try:
		logging.info("Connecting to GSFC FTP Server...")
		ftp = ftplib.FTP("cddis.gsfc.nasa.gov", timeout=10)
		ftp.login("anonymous","anonymous")
		ftp.cwd("gnss/data/daily/%s/brdc/" % datetime.datetime.utcnow().strftime("%Y"))
		file_list= ftp.nlst()

		# We expect the latest files to be the last in the list.
		download_file = None
		file_suffix = datetime.datetime.utcnow().strftime("%yn.Z")

		if file_suffix in file_list[-1]:
			download_file = file_list[-1]
		elif file_suffix in file_list[-2]:
			download_file = file_list[-2]
		else:
			logging.error("Could not find appropriate ephemeris file.")
			return None

		logging.info("Downloading ephemeris data file: %s" % download_file)

		# Download file.
		f_eph = open(destination+".Z",'w')
		ftp.retrbinary("RETR %s" % download_file, f_eph.write)
		f_eph.close()
		ftp.close()

		# Unzip file.
		os.system("gunzip -q -f ./%s" % (destination+".Z"))

		logging.info("Ephemeris downloaded to %s successfuly!" % destination)

		return destination
	except:
		logging.error("Could not download ephemeris file.")
		return None

def get_almanac(destination="almanac.txt"):
	''' Download the latest GPS almanac file from the US Coast Guard website. '''
	try:
		req = urllib2.Request("https://www.navcen.uscg.gov/?pageName=currentAlmanac&format=sem")
		res = urllib2.urlopen(req)
		data = res.read()
		if "CURRENT.ALM" in data:
			f = open(destination,'wb')
			f.write(data)
			f.close()
			logging.info("Almanac downloaded to %s successfuly!" % destination)
			return destination
		else:
			logging.error("Downloaded file is not a GPS almanac.")
			return None
	except:
		logging.error("Failed to download almanac data")
		return None

if __name__ == "__main__":
	logging.basicConfig(level=logging.DEBUG)
	get_almanac()
	get_ephemeris()



