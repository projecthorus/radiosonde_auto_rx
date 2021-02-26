#!/usr/bin/env python
#
#   radiosonde_auto_rx - Version Grabber
#
#   Copyright (C) 2021  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import autorx
import sys

if __name__ == "__main__":
    # Simple way to get the current auto_rx version when building the binaries.
    sys.stdout.write(autorx.__version__)
