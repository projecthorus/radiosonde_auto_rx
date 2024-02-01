#!/bin/bash
# Radiosonde Auto-RX Script
#
# 2017-04 Mark Jessop <vk5qi@rfhead.net>
#
# NOTE: If running this from crontab, make sure to set the appropriate PATH env-vars,
# else utilities like rtl_power and rtl_fm won't be found.
#
#	WARNING - THIS IS DEPRECATED - PLEASE USE THE SYSTEMD SERVICE OR DOCKER IMAGE
#   See: https://github.com/projecthorus/radiosonde_auto_rx/wiki#451-option-1---operation-as-a-systemd-service-recommended
#   Or: https://github.com/projecthorus/radiosonde_auto_rx/wiki/Docker
#

# change into appropriate directory
cd $(dirname $0)

python3 auto_rx.py -t 180