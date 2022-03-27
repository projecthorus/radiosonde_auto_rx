#!/usr/bin/env python
#
#   radiosonde_auto_rx - SDR Abstraction
#
#   Copyright (C) 2022  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import logging

from .utils import rtlsdr_test


def test_sdr(
    sdr_type: str,
    rtl_device_idx = "0",
    sdr_hostname = "",
    sdr_port = 1234
):
    """
    Test the prescence / functionality of a SDR.

    sdr_type (str): 'RTLSDR', 'Spyserver' or 'KA9Q'

    Arguments for RTLSDRs:
    rtl_device_id (str) - Device ID for a RTLSDR

    Arguments for KA9Q SDR Server / SpyServer:
    sdr_hostname (str): Hostname of KA9Q Server
    sdr_port (int): Port number of KA9Q Server

    """

    if sdr_type == "RTLSDR":
        _ok = rtlsdr_test(rtl_device_idx)
        if not _ok:
            logging.error(f"RTLSDR #{rtl_device_idx} non-functional.")

        return _ok

    
    elif sdr_type == "KA9Q":
        # To be implemented
        _ok = False

        if not _ok:
            logging.error(f"KA9Q Server {sdr_hostname}:{sdr_port} non-functional.")

        return _ok

    else:
        logging.error(f"Test SDR: Unknown SDR Type {sdr_type}")
        return False



def get_sdr_iq_cmd(
    sdr_type: str,
    frequency: int,
    sample_rate: int,
    rtl_device_idx = "0",
    rtl_fm_path = "rtl_fm",
    ppm = 0,
    gain = None,
    bias = False,
    sdr_hostname = "",
    sdr_port = 1234,
):
    """
    Get a command-line argument to get IQ (signed 16-bit) from a SDR
    for a given frequency and bandwidth.

    sdr_type (str): 'RTLSDR', 'Spyserver' or 'KA9Q'
    frequency (int): Centre frequency in Hz
    sample_rate (int): Sample rate in Hz

    Arguments for RTLSDRs:
    rtl_device_idx (str) - Device ID for a RTLSDR
    rtl_fm_path (str) - Path to rtl_fm. Defaults to just "rtl_fm"
    ppm (int): SDR Frequency accuracy correction, in ppm.
    gain (int): SDR Gain setting, in dB. A gain setting of -1 enables the RTLSDR AGC.
    bias (bool): If True, enable the bias tee on the SDR.

    Arguments for KA9Q SDR Server / SpyServer:
    sdr_hostname (str): Hostname of KA9Q Server
    sdr_port (int): Port number of KA9Q Server

    """

    if sdr_type == "RTLSDR":
        _gain = ""
        if gain:
            if gain >= 0:
                _gain = f"-g {gain:.1f} "

        _cmd = (
            f"{rtl_fm_path} -M raw -F9 "
            f"{'-T ' if bias else ''}"
            f"-p {int(ppm)} "
            f"-d {str(rtl_device_idx)} "
            f"{_gain}"
            f"-s {int(sample_rate)} "
            f"-f {int(frequency)} "
            f"- 2>/dev/null | "
        )

        return _cmd

    else:
        return None



def get_sdr_fm_cmd(
    sdr_type: str,
    frequency: int,
    filter_bandwidth: int,
    sample_rate: int,
    highpass = None,
    lowpass = None,
    rtl_device_idx = "0",
    rtl_fm_path = "rtl_fm",
    ppm = 0,
    gain = -1,
    bias = False,
    sdr_hostname = "",
    sdr_port = 1234,
):
    """
    Get a command-line argument to get FM demodulated audio (signed 16-bit) from a SDR
    for a given frequency and bandwidth.

    sdr_type (str): 'RTLSDR', 'Spyserver' or 'KA9Q'
    frequency (int): Centre frequency in Hz
    filter_bandwidth (int): FM Demodulator filter bandwidth in Hz
    sample_rate (int): Output sample rate in Hz

    Optional arguments
    highpass (int): If provided, add a high-pass filter after the FM demodulator.
    lowpass (int): If provided, add a low-pass filter after the FM demodulator.

    Arguments for RTLSDRs:
    rtl_device_idx (str) - Device ID for a RTLSDR
    rtl_fm_path (str) - Path to rtl_fm. Defaults to just "rtl_fm"
    ppm (int): SDR Frequency accuracy correction, in ppm.
    gain (int): SDR Gain setting, in dB. A gain setting of -1 enables the RTLSDR AGC.
    bias (bool): If True, enable the bias tee on the SDR.

    Arguments for KA9Q SDR Server / SpyServer:
    sdr_hostname (str): Hostname of KA9Q Server
    sdr_port (int): Port number of KA9Q Server

    """


    if sdr_type == "RTLSDR":
        _gain = ""
        if gain:
            if gain >= 0:
                _gain = f"-g {gain:.1f} "

        _cmd = (
            f"{rtl_fm_path} -M fm -F9 "
            f"{'-T ' if bias else ''}"
            f"-p {int(ppm)} "
            f"-d {str(rtl_device_idx)} "
            f"{_gain}"
            f"-s {int(filter_bandwidth)} "
            f"-f {int(frequency)} "
            f" 2>/dev/null | "
        )

        # Add in resampling / convertion to wav using sox.
        _cmd += f"sox -t raw -f {int(filter_bandwidth)} -e s -b 16 -c 1 - -r {int(sample_rate)} -b 16 -t wav - "

        if highpass:
            _cmd += f"highpass {int(highpass)} "
        
        if lowpass:
            _cmd += f"lowpass {int(lowpass)} "

        _cmd += "- 2> /dev/null | "

        return _cmd

    else:
        return None


if __name__ == "__main__":

    _sdr_type = "RTLSDR"

    #print(f"Test RTLSDR 0: {test_sdr(_sdr_type)}")

    _freq = 401500000
    _sample_rate = 48000
    _fm_bw = 15000
    _device_idx = "00000004"

    print(f"RTLSDR IQ (AGC): {get_sdr_iq_cmd(_sdr_type, _freq, _sample_rate)}")
    print(f"RTLSDR IQ (AGC + Fixed device): {get_sdr_iq_cmd(_sdr_type, _freq, _sample_rate, rtl_device_idx=_device_idx)}")
    print(f"RTLSDR IQ (Fixed Gain): {get_sdr_iq_cmd(_sdr_type, _freq, _sample_rate, gain=30.0, bias=True)}")

    print(f"RTLSDR FM (AGC): {get_sdr_fm_cmd(_sdr_type, _freq, _fm_bw, _sample_rate)}")
    print(f"RTLSDR FM (Fixed Gain): {get_sdr_fm_cmd(_sdr_type, _freq, _fm_bw, _sample_rate, gain=30.0, bias=True, highpass=20)}")