#!/usr/bin/env python
#
#   radiosonde_auto_rx - SDR Abstraction
#
#   Copyright (C) 2022  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import autorx
import logging
import os.path
import platform
import subprocess
import numpy as np

from .utils import rtlsdr_test, reset_rtlsdr_by_serial, reset_all_rtlsdrs, timeout_cmd
from .ka9q import *


def test_sdr(
    sdr_type: str,
    rtl_device_idx = "0",
    sdr_hostname = "",
    sdr_port = 5555,
    ss_iq_path = "./ss_iq",
    ss_power_path = "./ss_power",
    check_freq = 401500000
):
    """
    Test the prescence / functionality of a SDR.

    sdr_type (str): 'RTLSDR', 'SpyServer' or 'KA9Q'

    Arguments for RTLSDRs:
    rtl_device_id (str) - Device ID for a RTLSDR

    Arguments for KA9Q SDR Server / SpyServer:
    sdr_hostname (str): Hostname of KA9Q Server
    sdr_port (int): Port number of KA9Q Server

    Arguments for SpyServer Client:
    ss_iq_path (str): Path to spyserver IQ client utility.
    ss_power_path (str): Path to spyserver power utility.
    """

    if sdr_type == "RTLSDR":
        # Use the existing rtlsdr_test code, which tries to read some samples
        # from the specified RTLSDR
        _ok = rtlsdr_test(rtl_device_idx)
        if not _ok:
            logging.error(f"RTLSDR #{rtl_device_idx} non-functional.")

        return _ok

    
    elif sdr_type == "KA9Q":
        # Test that a KA9Q server is working by attempting to start up a new narrowband channel on it.

        # Check for presence of KA9Q-radio binaries that we need
        # if not os.path.isfile('tune'):
        #     logging.critical("Could not find KA9Q-Radio 'tune' binary! This may need to be compiled and installed.")
        #     return False
        # if not os.path.isfile('pcmcat'):
        #     logging.critical("Could not find KA9Q-Radio 'pcmcat' binary! This may need to be compiled and installed.")
        #     return False
        # TBD - whatever we need for spectrum use.
        # if not os.path.isfile('TBD'):
        #     logging.critical("Could not find KA9Q-Radio 'tune' binary! This may need to be compiled and installed.")
        #     return False


        # Try and configure a channel at check_freq Hz
        # tune --samprate 48000 --frequency 404m09 --mode iq --ssrc 404090000 --radio sonde.local
        _cmd = (
            f"{timeout_cmd()} 5 " # Add a timeout, because connections to non-existing servers block for ages
            f"tune "
            f"--samprate 48000 --mode iq "
            f"--frequency {int(check_freq)} "
            f"--ssrc {int(check_freq)}314 "
            f"--radio {sdr_hostname}"
        )

        logging.debug(f"KA9Q - Testing using command: {_cmd}")

        try:
            _output = subprocess.check_output(
                _cmd, shell=True, stderr=subprocess.STDOUT
            )
        except subprocess.CalledProcessError as e:
            # Something went wrong...

            if e.returncode == 124:
                logging.critical(
                    f"KA9Q ({sdr_hostname}) - tune call failed with a timeout. Is the server running?"
                )
            elif e.returncode == 127:
                logging.critical(
                    f"KA9Q ({sdr_hostname}) - Could not find KA9Q-Radio 'tune' binary! This may need to be compiled and installed."
                )
            else:
                logging.critical(
                    f"KA9Q ({sdr_hostname}) - tune call failed with return code {e.returncode}."
                )
                # Look at the error output in a bit more details.
                #_output = e.output.decode("ascii")

            # TODO - see if we can look in the output for any error messages.
            return False
        
        # Now close the channel we just opened by setting the frequency to 0 Hz.
        _cmd = (
            f"{timeout_cmd()} 5 " # Add a timeout, because connections to non-existing servers block for ages
            f"tune "
            f"--samprate 48000 --mode iq "
            f"--frequency 0 "
            f"--ssrc {int(check_freq)}314 "
            f"--radio {sdr_hostname}"
        )

        logging.debug(f"KA9Q - Closing testing channel using command: {_cmd}")
        try:
            _output = subprocess.check_output(
                _cmd, shell=True, stderr=subprocess.STDOUT
            )
        except subprocess.CalledProcessError as e:
            # Something went wrong...
            logging.critical(
                f"KA9Q ({sdr_hostname}) - tune call (closing channel) failed with return code {e.returncode}."
            )
            # Look at the error output in a bit more details.
            #_output = e.output.decode("ascii")

            # TODO - see if we can look in the output for any error messages.
            return False

        return True

    elif sdr_type == "SpyServer":
        # Test connectivity to a SpyServer by trying to grab some samples.

        if not os.path.isfile(ss_iq_path):
            logging.critical("Could not find ss_iq binary! This may need to be compiled.")
            return False

        _cmd = (
            f"{timeout_cmd()} 10 "  # Add a timeout, because connections to non-existing IPs seem to block.
            f"{ss_iq_path} "
            f"-f {check_freq} "
            f"-s 48000 "
            f"-r {sdr_hostname} -q {sdr_port} -n 48000 - > /dev/null"
        )

        logging.debug(f"SpyServer - Testing using command: {_cmd}")

        try:
            _output = subprocess.check_output(
                _cmd, shell=True, stderr=subprocess.STDOUT
            )
        except subprocess.CalledProcessError as e:
            # Something went wrong...
            logging.critical(
                f"SpyServer ({sdr_hostname}:{sdr_port}) - ss_iq call failed with return code {e.returncode}."
            )
            # Look at the error output in a bit more details.
            _output = e.output.decode("ascii")
            if "outside currently allowed range" in _output:
                logging.critical(
                    f"SpyServer ({sdr_hostname}:{sdr_port}) - SpyServer does not cover required frequency {check_freq}, please check your SpyServer configuration!"
                )
            return False

        return True

    else:
        logging.error(f"Test SDR: Unknown SDR Type {sdr_type}")
        return False



def reset_sdr(
    sdr_type: str,
    rtl_device_idx = "0",
    sdr_hostname = "",
    sdr_port = 5555
    ):
    """
    Attempt to reset a SDR. Only used for RTLSDRs.

    sdr_type (str): 'RTLSDR', 'Spyserver' or 'KA9Q'

    Arguments for RTLSDRs:
    rtl_device_id (str) - Device ID for a RTLSDR

    Arguments for KA9Q SDR Server / SpyServer:
    sdr_hostname (str): Hostname of KA9Q Server
    sdr_port (int): Port number of KA9Q Server
    """

    if sdr_type == "RTLSDR":
        # Attempt to reset the RTLSDR.
        if rtl_device_idx == "0":
            # If the device ID is 0, we assume we only have a single RTLSDR on this system.
            reset_all_rtlsdrs()
        else:
            # Otherwise, we reset the specific RTLSDR
            reset_rtlsdr_by_serial(rtl_device_idx)

    else:
        logging.debug(f"No reset ability for SDR type {sdr_type}")


def get_sdr_name(
    sdr_type: str,
    rtl_device_idx = "0",
    sdr_hostname = "",
    sdr_port = 5555
    ):
    """
    Get a human-readable name of the currenrt SDR

    sdr_type (str): 'RTLSDR', 'Spyserver' or 'KA9Q'

    Arguments for RTLSDRs:
    rtl_device_id (str) - Device ID for a RTLSDR

    Arguments for KA9Q SDR Server / SpyServer:
    sdr_hostname (str): Hostname of KA9Q Server
    sdr_port (int): Port number of KA9Q Server
    """

    if sdr_type == "RTLSDR":
        return f"RTLSDR {rtl_device_idx}"

    elif sdr_type == "KA9Q":
        return f"KA9Q {sdr_hostname}"
    
    elif sdr_type == "SpyServer":
        return f"SpyServer {sdr_hostname}:{sdr_port}"
    
    else:
        return f"UNKNOWN {sdr_type}"


def shutdown_sdr(
    sdr_type: str,
    sdr_id: str,
    sdr_hostname = "",
    frequency: int = None
    ):
    """
    Function to trigger shutdown/cleanup of some SDR types.
    Currently only required for the KA9Q server.

    sdr_type (str): 'RTLSDR', 'Spyserver' or 'KA9Q'
    sdr_id (str): The global ID of the SDR to be shut down.
    """

    if sdr_type == "KA9Q":
        logging.debug(f"KA9Q - Closing Channel for {sdr_hostname} @ {frequency} Hz.")
        ka9q_close_channel(sdr_hostname, frequency)
        pass
    else:
        logging.debug(f"No shutdown action required for SDR type {sdr_type}")

    return



def get_sdr_iq_cmd(
    sdr_type: str,
    frequency: int,
    sample_rate: int,
    rtl_device_idx = "0",
    rtl_fm_path = "rtl_fm",
    fast_filter: bool = False,
    dc_block: bool = False,
    ppm = 0,
    gain = None,
    bias = False,
    sdr_hostname = "",
    sdr_port = 5555,
    ss_iq_path = "./ss_iq"
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
    fast_filter (bool): If true, drop the -F9 higher quality filter for rtl_fm
    dc_block (bool): If true, enable a DC block step.

    Arguments for KA9Q SDR Server / SpyServer:
    sdr_hostname (str): Hostname of KA9Q Server
    sdr_port (int): Port number of KA9Q Server

    Arguments for SpyServer Client:
    ss_iq_path (str): Path to spyserver IQ client utility.

    """

    # DC removal commmand, using rs1729's IQ_dec utility.
    # This helps remove the residual DC offset in the 16-bit outputs from
    # both rtl_fm and ss_iq. 
    # We currently only use this on narrowband sondes.
    if sample_rate > 80000:
        _dc_ifbw = f"--IFbw {int(sample_rate/1000)} "
    else:
        _dc_ifbw = ""
    _dc_remove = f"./iq_dec --bo 16 {_dc_ifbw}- {int(sample_rate)} 16 2>/dev/null |"

    if sdr_type == "RTLSDR":
        _gain = ""
        _agc = ""
        if gain:
            if gain >= 0:
                _gain = f"-g {gain:.1f} "
            elif gain == -2:
                _agc = f"-E agc "

        _cmd = (
            f"{rtl_fm_path} -M raw "
            f"{'' if fast_filter else '-F9 '}"
            f"{'-T ' if bias else ''}"
            f"-p {int(ppm)} "
            f"-d {str(rtl_device_idx)} "
            f"{_gain}"
            f"{_agc}"
            f"-s {int(sample_rate)} "
            f"-f {int(frequency)} "
            f"- 2>/dev/null | "
        )

        if dc_block:
            _cmd += _dc_remove

        return _cmd

    if sdr_type == "SpyServer":
        _cmd = (
            f"{ss_iq_path} "
            f"-f {frequency} "
            f"-s {int(sample_rate)} "
            f"-r {sdr_hostname} -q {sdr_port} - 2>/dev/null|"
        )

        if dc_block:
            _cmd += _dc_remove

        return _cmd
    
    if sdr_type == "KA9Q":
        _cmd = ka9q_get_iq_cmd(sdr_hostname, frequency, sample_rate)

        if dc_block:
            _cmd += _dc_remove

        return _cmd

    else:
        logging.critical(f"IQ Source - Unsupported SDR type {sdr_type}")
        return "false |"



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
            f"2>/dev/null | "
        )

        # Add in resampling / convertion to wav using sox.
        _cmd += f"sox -t raw -r {int(filter_bandwidth)} -e s -b 16 -c 1 - -r {int(sample_rate)} -b 16 -t wav - "

        if highpass:
            _cmd += f"highpass {int(highpass)} "
        
        if lowpass:
            _cmd += f"lowpass {int(lowpass)} "

        _cmd += "2> /dev/null | "

        return _cmd

    else:
        logging.critical(f"FM Demod Source - Unsupported SDR type {sdr_type}")
        return "false |"


def read_rtl_power_log(log_filename, sdr_name):
    """
    Read in a rtl_power compatible log output file.

    Arguments:
    log_filename (str): Filename to read
    sdr_name (str): SDR name used for logging errors.
    """

    # OK, now try to read in the saved data.
    # Output buffers.
    freq = np.array([])
    power = np.array([])

    freq_step = 0

    # Open file.
    f = open(log_filename, "r")

    # rtl_power log files are csv's, with the first 6 fields in each line describing the time and frequency scan parameters
    # for the remaining fields, which contain the power samples.

    for line in f:
        # Split line into fields.
        fields = line.split(",", 6)

        if len(fields) < 6:
            logging.error(
                f"Scanner ({sdr_name}) - Invalid number of samples in input file - corrupt?"
            )
            raise Exception(
                f"Scanner ({sdr_name}) - Invalid number of samples in input file - corrupt?"
            )

        start_date = fields[0]
        start_time = fields[1]
        start_freq = float(fields[2])
        stop_freq = float(fields[3])
        freq_step = float(fields[4])
        n_samples = int(fields[5])
        # freq_range = np.arange(start_freq,stop_freq,freq_step)
        samples = np.fromstring(fields[6], sep=",")
        freq_range = np.linspace(start_freq, stop_freq, len(samples))

        # Add frequency range and samples to output buffers.
        freq = np.append(freq, freq_range)
        power = np.append(power, samples)

    f.close()

    # Sanitize power values, to remove the nan's that rtl_power puts in there occasionally.
    power = np.nan_to_num(power)

    return (freq, power, freq_step)


def get_power_spectrum(
    sdr_type: str,
    frequency_start: int = 400050000,
    frequency_stop: int = 403000000,
    step: int = 800,
    integration_time: int = 20,
    rtl_device_idx = "0",
    rtl_power_path = "rtl_power",
    ppm = 0,
    gain = None,
    bias = False,
    sdr_hostname = "",
    sdr_port = 5555,
    ss_power_path = "./ss_power"
):
    """
    Get power spectral density data from a SDR.

    Arguments:

    sdr_type (str): 'RTLSDR', 'Spyserver' or 'KA9Q' 

    frequency_start (int): Start frequency for the PSD, Hz
    frequency_stop (int): Stop frequency for the PSD, Hz
    step (int): Requested frequency step for the PSD, Hz. May not always be honoured.
    integration_time (int): Integration time in seconds.

    Arguments for RTLSDRs:
    rtl_device_idx (str): Device ID for a RTLSDR
    rtl_power_path (str): Path to rtl_power. Defaults to just "rtl_power"
    ppm (int): SDR Frequency accuracy correction, in ppm.
    gain (int): SDR Gain setting, in dB. A gain setting of -1 enables the RTLSDR AGC.
    bias (bool): If True, enable the bias tee on the SDR.

    Arguments for KA9Q SDR Server / SpyServer:
    sdr_hostname (str): Hostname of KA9Q Server
    sdr_port (int): Port number of KA9Q Server

    Arguments for SpyServer Client:
    ss_power_path (str): Path to spyserver power utility.
    ss_iq_path (str): Path to spyserver IQ client utility.


    Returns:
    (freq, power, step) Tuple

    freq (np.array): Array of frequencies, in Hz
    power (np.array): Array of uncalibrated power estimates, in Hz
    step (float): Frequency step of the output data, in Hz.

    Returns (None, None, None) if an error occurs.

    """

    # No support for getting spectrum data on any other SDR source right now.
    # Override sdr selection. 


    if sdr_type == "RTLSDR":
        # Use rtl_power to obtain power spectral density data

        # Create filename to output to.
        _log_filename = os.path.join(autorx.logging_path, f"log_power_{rtl_device_idx}.csv")
        
        # If the output log file exists, remove it.
        if os.path.exists(_log_filename):
            os.remove(_log_filename)

        _timeout_cmd = f"{timeout_cmd()} {integration_time+10} "

        _gain = ""
        if gain:
            if gain >= 0:
                _gain = f"-g {gain:.1f} "

        _rtl_power_cmd = (
            f"{_timeout_cmd} {rtl_power_path} "
            f"{'-T ' if bias else ''}"
            f"-p {int(ppm)} "
            f"-d {str(rtl_device_idx)} "
            f"{_gain}"
            f"-f {frequency_start}:{frequency_stop}:{step} "
            f"-i {integration_time} -1 -c 25% "
            f"{_log_filename}"
        )

        _sdr_name = get_sdr_name(
            sdr_type=sdr_type,
            rtl_device_idx=rtl_device_idx,
            sdr_hostname=sdr_hostname,
            sdr_port=sdr_port
            )

        logging.info(f"Scanner ({_sdr_name}) - Running frequency scan.")
        logging.debug(
            f"Scanner ({_sdr_name}) - Running command: {_rtl_power_cmd}"
        )

        try:
            _output = subprocess.check_output(
                _rtl_power_cmd, shell=True, stderr=subprocess.STDOUT
            )
        except subprocess.CalledProcessError as e:
            # Something went wrong...
            logging.critical(
                f"Scanner ({_sdr_name}) - rtl_power call failed with return code {e.returncode}."
            )
            # Look at the error output in a bit more details.
            _output = e.output.decode("ascii")
            if "No supported devices found" in _output:
                logging.critical(
                    f"Scanner ({_sdr_name}) - rtl_power could not find device with ID {rtl_device_idx}, is your configuration correct?"
                )
            elif "illegal option" in _output:
                if bias:
                    logging.critical(
                        f"Scanner ({_sdr_name}) - rtl_power reported an illegal option was used. Are you using a rtl_power version with bias tee support?"
                    )
                else:
                    logging.critical(
                       f"Scanner ({_sdr_name}) - rtl_power reported an illegal option was used. (This shouldn't happen... are you running an ancient version?)"
                    )
            else:
                # Something else odd happened, dump the entire error output to the log for further analysis.
                logging.critical(
                    f"Scanner ({_sdr_name}) - rtl_power reported error: {_output}"
                )

            return (None, None, None)

        return read_rtl_power_log(_log_filename, _sdr_name)

    elif sdr_type == "SpyServer":
        # Use a spyserver to obtain power spectral density data

        # Create filename to output to.
        _log_filename = os.path.join(autorx.logging_path, f"log_power_spyserver.csv")
        
        # If the output log file exists, remove it.
        if os.path.exists(_log_filename):
            os.remove(_log_filename)

        _timeout_cmd = f"{timeout_cmd()} {integration_time+10} "

        _frequency_centre = int(frequency_start + (frequency_stop-frequency_start)/2.0)

        # Note we are using the '-o' option here, which allows us to still get
        # spectrum data even if we have specified a frequency which is out of 
        # the range of a locked spyserver.
        _ss_power_cmd = (
            f"{_timeout_cmd} {ss_power_path} "
            f"-f {_frequency_centre} "
            f"-i {integration_time} -1 -o "
            f"-r {sdr_hostname} -q {sdr_port} "
            f"{_log_filename}"
        )

        _sdr_name = get_sdr_name(
            sdr_type=sdr_type,
            rtl_device_idx=rtl_device_idx,
            sdr_hostname=sdr_hostname,
            sdr_port=sdr_port
            )

        logging.info(f"Scanner ({_sdr_name}) - Running frequency scan.")
        logging.debug(
            f"Scanner ({_sdr_name}) - Running command: {_ss_power_cmd}"
        )

        try:
            _output = subprocess.check_output(
                _ss_power_cmd, shell=True, stderr=subprocess.STDOUT
            )
        except subprocess.CalledProcessError as e:
            # Something went wrong...
            logging.critical(
                f"Scanner ({_sdr_name}) - ss_power call failed with return code {e.returncode}."
            )
            # Look at the error output in a bit more details.
            _output = e.output.decode("ascii")

            if "outside currently allowed range" in _output:
                logging.critical(
                    f"Scanner ({_sdr_name}) - Centre of scan range ({_frequency_centre} Hz) outside of allowed SpyServer tuning range."
                )
            else:
                logging.critical(
                    f"Scanner ({_sdr_name}) - Other Error: {_output}"
                )

            return (None, None, None)

        return read_rtl_power_log(_log_filename, _sdr_name)

    else:
        # Unsupported SDR Type
        logging.debug(f"Get PSD - Unsupported SDR Type: {sdr_type}")
        return (np.array([0,1,2]),np.array([0,1,2]),1)
        #return (None, None, None)

if __name__ == "__main__":

    logging.basicConfig(
        format="%(asctime)s %(levelname)s:%(message)s", level=logging.DEBUG
    )

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

    # (freq, power, step) = get_power_spectrum(
    #     sdr_type="RTLSDR"
    # )

    (freq, power, step) = get_power_spectrum(
        sdr_type="SpyServer",
        sdr_hostname="10.0.0.222",
        sdr_port=5555,
        frequency_start=400100000,
        frequency_stop=404900000
    )
    print(freq)
    print(power)
    print(step)