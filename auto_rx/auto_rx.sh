#!/bin/bash
# Radiosonde Auto-RX Script
#
# 2017-04 Mark Jessop <vk5qi@rfhead.net>
#
# NOTE: If running this from crontab, make sure to set the appropriate PATH env-vars,
# else utilities like rtl_power and rtl_fm won't be found.
#
#	WARNING - THIS IS DEPRECATED - PLEASE USE THE SYSTEMD SERVICE
#

# change into appropriate directory
cd $(dirname $0)

# Clean up old files
rm log_power*.csv

while true
do
    python3 auto_rx.py
    rc=$?
    echo auto_rx.py exited with result code $rc
    if [ $rc -gt 2 ]
    then
        echo "Performing power reset of SDR's"
        python3 sdr_reset.py
    fi
    if [ $rc -eq 0 ]
    then
        break
    fi
done
