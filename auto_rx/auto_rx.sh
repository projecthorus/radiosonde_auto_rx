#!/bin/bash
# Radiosonde Auto-RX Script

# Start auto_rx process with a 3 hour timeout.
timeout 10800 python auto_rx.py

# Clean up rtl_fm process.
killall rtl_fm