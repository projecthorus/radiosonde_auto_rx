Radiosonde Auto-RX Utilities
============================
This fork of rs1279's excellent RS codebase is intended to produce a set of utilities to allow automatic
reception and uploading of radiosonde positions to APRS-IS and Habitat. 
Eventually I hope to have an automatic receive station installed at a strategic location in the 
Adelaide South Australia area, to better assist with gathering of RS41 radiosondes once they start being
launched.

This folder contains a collection of Python scripts used to automatically scan for and decode Vaisala RS92 and RS41 radiosondes.
It is intended to be an improvement upon the 'rtlsdr_scan.pl' script, and will run happily on a Raspberry Pi 3. 

Features:
* Automatic downloading of GPS Ephemeris or Almanac data, if the sonde is a Vaisala RS92.
* Scanning and detection of Vaisala RS92 and RS41 radiosondes over a user-definable frequency range, using the rs_detect, rs92mod and rs41mod utilities from the master repository. 
* Uploading to:
 * APRS, with user-definable position comment.
 * Habitat (TODO)

Dependencies
------------
* Currently runs on Python 2.7 (yeah, I know). Will probably work under Python 3.
* Needs the following python packages (get them with `pip install <package>`)
 * numpy
 * crcmod

Usage
-----
* Run `sh build.sh` to build needed binaries from elsewhere in this repository.
* Make a copy of station.cfg.example as station.cfg, and edit as appropriate.
* Run `python auto_rx.py -c station.cfg` to start the sca
* Wait. A log file will be written to log/<timestamp.log detailing what's going on.
