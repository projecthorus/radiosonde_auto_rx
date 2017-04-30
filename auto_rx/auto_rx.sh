#!/bin/bash
# Radiosonde Auto-RX Script
#
# 2017-04 Mark Jessop <vk5qi@rfhead.net>
#
# NOTE: If running this from crontab, make sure to set the appropriate PATH env-vars,
# else utilities like rtl_power and rtl_fm won't be found.
#

# change into appropriate directory
cd ~/RS/auto_rx/

# Clean up old files
rm log_power.csv
rm almanac.txt

# Download latest almanac
wget -O almanac.txt "https://www.navcen.uscg.gov/?pageName=currentAlmanac&format=sem"

# Start auto_rx process with a 3 hour timeout.
timeout 10800 python auto_rx.py 2>error.log

# Clean up rtl_fm process.
killall rtl_fm