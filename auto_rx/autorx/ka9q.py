#!/usr/bin/env python
#
#   radiosonde_auto_rx - SDR Abstraction - KA9Q-Radio
#
#   Copyright (C) 2022  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#

import logging
import os.path
import platform
import subprocess
from .utils import timeout_cmd


def ka9q_setup_channel(
    sdr_hostname,
    frequency,
    sample_rate,
    scan,
    channel_filter = None
):
    if scan:
        ssrc="04"
    else:
        ssrc="01"

    # tune --samprate 48000 --frequency 404m09 --mode iq --ssrc 404090000 --radio sonde.local

    if channel_filter:
        _low = int(channel_filter * -1.0)
        _high = int(channel_filter)
    else:
        _low = int(int(sample_rate) / (-2.4))
        _high = int(int(sample_rate) / 2.4)

    _cmd = (
        f"{timeout_cmd()} 5 " # Add a timeout, because connections to non-existing servers block for ages
        f"tune "
        f"--samprate {int(sample_rate)} "
        f"--mode iq "
        f"--low {_low} --high {_high} "
        f"--frequency {int(frequency)} "
        f"--ssrc {round(frequency / 1000)}{ssrc} "
        f"--radio {sdr_hostname}"
    )

    logging.debug(f"KA9Q - Starting channel at {frequency} Hz, with command: {_cmd}")

    try:
        _output = subprocess.check_output(
            _cmd, shell=True, stderr=subprocess.STDOUT
        )
    except subprocess.CalledProcessError as e:
        # Something went wrong...

        if e.returncode == 124:
            logging.critical(
                f"KA9Q ({sdr_hostname}) - tune call failed while opening channel with a timeout. Is the server running?"
            )
        elif e.returncode == 127:
            logging.critical(
                f"KA9Q ({sdr_hostname}) - Could not find KA9Q-Radio 'tune' binary! This may need to be compiled and installed."
            )
        else:
            logging.critical(
                f"KA9Q ({sdr_hostname}) - tune call failed while opening channel with return code {e.returncode}."
            )
            # Look at the error output in a bit more details.
            #_output = e.output.decode("ascii")

        # TODO - see if we can look in the output for any error messages.
        return False

    return True


def ka9q_close_channel(
    sdr_hostname,
    frequency,
    scan
):
    if scan:
        ssrc="04"
    else:
        ssrc="01"

    _cmd = (
        f"{timeout_cmd()} 5 " # Add a timeout, because connections to non-existing servers block for ages
        f"tune "
        f"--samprate 48000 "
        f"--mode iq "
        f"--frequency 0 "
        f"--ssrc {round(frequency / 1000)}{ssrc} "
        f"--radio {sdr_hostname}"
    )

    logging.debug(f"KA9Q - Closing channel at {frequency} Hz, with command: {_cmd}")

    try:
        _output = subprocess.check_output(
            _cmd, shell=True, stderr=subprocess.STDOUT
        )
    except subprocess.CalledProcessError as e:
        # Something went wrong...

        if e.returncode == 124:
            logging.critical(
                f"KA9Q ({sdr_hostname}) - tune call failed while closing channel with a timeout. Is the server running?"
            )
        elif e.returncode == 127:
            logging.critical(
                f"KA9Q ({sdr_hostname}) - Could not find KA9Q-Radio 'tune' binary! This may need to be compiled and installed."
            )
        else:
            logging.critical(
                f"KA9Q ({sdr_hostname}) - tune call failed while closing chanel with return code {e.returncode}."
            )
            # Look at the error output in a bit more details.
            #_output = e.output.decode("ascii")

        # TODO - see if we can look in the output for any error messages.
        return False

    return True


def ka9q_get_iq_cmd(
        sdr_hostname,
        frequency,
        sample_rate,
        scan,
        channel_filter = None
):
    if scan:
        ssrc="04"
    else:
        ssrc="01"
    
    # We need to setup a channel before we can use it!
    _setup_success = ka9q_setup_channel(sdr_hostname, frequency, sample_rate, scan, channel_filter)

    if not _setup_success:
        logging.critical(f"KA9Q ({sdr_hostname}) - Could not setup rx channel! Decoder will likely timeout.")

    # Get the 'PCM' version of the server name, where as assume -pcm is added to the first part of the hostname.
    _pcm_host = sdr_hostname.split('.')[0] + "-pcm." + ".".join(sdr_hostname.split(".")[1:])

    # Example: pcmrecord --ssrc 404090001 --catmode --raw sonde-pcm.local
    # -2 option was removed sometime in early 2024.
    _cmd = (
        f"pcmrecord "
        f"--ssrc {round(frequency / 1000)}{ssrc} "
        f"--catmode "
        f"--raw "
        f"{_pcm_host} |"
    )

    return _cmd
