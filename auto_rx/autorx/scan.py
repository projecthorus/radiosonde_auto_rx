#!/usr/bin/env python
#
#   radiosonde_auto_rx - Radiosonde Scanner
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import autorx
import datetime
import logging
import math
import numpy as np
import os
import sys
import platform
import subprocess
import time
import traceback
from io import StringIO
from threading import Thread, Lock
from types import FunctionType, MethodType
from .utils import (
    detect_peaks,
    timeout_cmd
)
from .sdr_wrappers import test_sdr, reset_sdr, get_sdr_name, get_sdr_iq_cmd, get_sdr_fm_cmd, get_power_spectrum, shutdown_sdr


try:
    from .web import flask_emit_event
except ImportError:
    # Running in a test scenario. Make a dummy flask_emit_event function.
    def flask_emit_event(event_name, data):
        print("Running in a test scenario, no data emitted to flask.")
        pass


# Global for latest scan result
scan_result = {
    "freq": [],
    "power": [],
    "peak_freq": [],
    "peak_lvl": [],
    "timestamp": "No data yet.",
    "threshold": 0,
}


def run_rtl_power(
    start,
    stop,
    step,
    filename="log_power.csv",
    dwell=20,
    rtl_power_path="rtl_power",
    device_idx=0,
    ppm=0,
    gain=-1,
    bias=False,
):
    """Capture spectrum data using rtl_power (or drop-in equivalent), and save to a file.

    Args:
        start (int): Start of search window, in Hz.
        stop (int): End of search window, in Hz.
        step (int): Search step, in Hz.
        filename (str): Output results to this file. Defaults to ./log_power.csv
        dwell (int): How long to average on the frequency range for.
        rtl_power_path (str): Path to the rtl_power utility.
        device_idx (int or str): Device index or serial number of the RTLSDR. Defaults to 0 (the first SDR found).
        ppm (int): SDR Frequency accuracy correction, in ppm.
        gain (float): SDR Gain setting, in dB.
        bias (bool): If True, enable the bias tee on the SDR.

    Returns:
        bool: True if rtl_power ran successfuly, False otherwise.

    """
    # Example: rtl_power -f 400400000:403500000:800 -i20 -1 -c 25% -p 0 -d 0 -g 26.0 log_power.csv

    # Add a -T option if bias is enabled
    bias_option = "-T " if bias else ""

    # Add a gain parameter if we have been provided one.
    if gain != -1:
        gain_param = "-g %.1f " % gain
    else:
        gain_param = ""

    # If the output log file exists, remove it.
    if os.path.exists(filename):
        os.remove(filename)

    rtl_power_cmd = (
        "%s %d %s %s-f %d:%d:%d -i %d -1 -c 25%% -p %d -d %s %s%s"
        % (
            timeout_cmd(),
            dwell + 10,
            rtl_power_path,
            bias_option,
            start,
            stop,
            step,
            dwell,
            int(ppm),  # Should this be an int?
            str(device_idx),
            gain_param,
            filename,
        )
    )

    logging.info("Scanner #%s - Running frequency scan." % str(device_idx))
    logging.debug(
        "Scanner #%s - Running command: %s" % (str(device_idx), rtl_power_cmd)
    )

    try:
        _output = subprocess.check_output(
            rtl_power_cmd, shell=True, stderr=subprocess.STDOUT
        )
    except subprocess.CalledProcessError as e:
        # Something went wrong...
        logging.critical(
            "Scanner #%s - rtl_power call failed with return code %s."
            % (str(device_idx), e.returncode)
        )
        # Look at the error output in a bit more details.
        _output = e.output.decode("ascii")
        if "No supported devices found" in _output:
            logging.critical(
                "Scanner #%s - rtl_power could not find device with ID %s, is your configuration correct?"
                % (str(device_idx), str(device_idx))
            )
        elif "illegal option" in _output:
            if bias:
                logging.critical(
                    "Scanner #%s - rtl_power reported an illegal option was used. Are you using a rtl_power version with bias tee support?"
                    % str(device_idx)
                )
            else:
                logging.critical(
                    "Scanner #%s - rtl_power reported an illegal option was used. (This shouldn't happen... are you running an ancient version?)"
                    % str(device_idx)
                )
        else:
            # Something else odd happened, dump the entire error output to the log for further analysis.
            logging.critical(
                "Scanner #%s - rtl_power reported error: %s"
                % (str(device_idx), _output)
            )

        return False
    else:
        # No errors reported!
        return True


def read_rtl_power(filename):
    """Read in frequency samples from a single-shot log file produced by rtl_power

    Args:
        filename (str): Filename to read in.

    Returns:
        tuple: A tuple consisting of:
            freq (np.array): List of centre frequencies in Hz
            power (np.array): List of measured signal powers, in dB.
            freq_step (float): Frequency step between points, in Hz

    """

    # Output buffers.
    freq = np.array([])
    power = np.array([])

    freq_step = 0

    # Open file.
    f = open(filename, "r")

    # rtl_power log files are csv's, with the first 6 fields in each line describing the time and frequency scan parameters
    # for the remaining fields, which contain the power samples.

    for line in f:
        # Split line into fields.
        fields = line.split(",")

        if len(fields) < 6:
            logging.error(
                "Scanner - Invalid number of samples in input file - corrupt?"
            )
            raise Exception(
                "Scanner - Invalid number of samples in input file - corrupt?"
            )

        start_date = fields[0]
        start_time = fields[1]
        start_freq = float(fields[2])
        stop_freq = float(fields[3])
        freq_step = float(fields[4])
        n_samples = int(fields[5])

        # freq_range = np.arange(start_freq,stop_freq,freq_step)
        samples = np.loadtxt(StringIO(",".join(fields[6:])), delimiter=",")
        freq_range = np.linspace(start_freq, stop_freq, len(samples))

        # Add frequency range and samples to output buffers.
        freq = np.append(freq, freq_range)
        power = np.append(power, samples)

    f.close()

    # Sanitize power values, to remove the nan's that rtl_power puts in there occasionally.
    power = np.nan_to_num(power)

    return (freq, power, freq_step)


def detect_sonde(
    frequency,
    rs_path="./",
    dwell_time=10,
    sdr_type="RTLSDR",
    sdr_hostname="localhost",
    sdr_port=5555,
    ss_iq_path = "./ss_iq",
    rtl_fm_path="rtl_fm",
    rtl_device_idx=0,
    ppm=0,
    gain=-1,
    bias=False,
    save_detection_audio=False,
    ngp_tweak=False,
    wideband_sondes=False
):
    """Receive some FM and attempt to detect the presence of a radiosonde.

    Args:
        frequency (int): Frequency to perform the detection on, in Hz.
        rs_path (str): Path to the RS binaries (i.e rs_detect). Defaults to ./
        dwell_time (int): Timeout before giving up detection.
        rtl_fm_path (str): Path to rtl_fm, or drop-in equivalent. Defaults to 'rtl_fm'
        rtl_device_idx (int or str): Device index or serial number of the RTLSDR. Defaults to 0 (the first SDR found).
        ppm (int): SDR Frequency accuracy correction, in ppm.
        gain (int): SDR Gain setting, in dB. A gain setting of -1 enables the RTLSDR AGC.
        bias (bool): If True, enable the bias tee on the SDR.
        save_detection_audio (bool): Save the audio used in detection to a file.
        ngp_tweak (bool): When scanning in the 1680 MHz sonde band, use a narrower FM filter for better RS92-NGP detection.
        wideband_sondes (bool): Use a wider detection filter to allow detection of Weathex and wideband iMet sondes.

    Returns:
        str/None: Returns None if no sonde found, otherwise returns a sonde type, from the following:
            'RS41' - Vaisala RS41
            'RS92' - Vaisala RS92
            'DFM' - Graw DFM06 / DFM09 (similar telemetry formats)
            'M10' - MeteoModem M10
            'M20' - MeteoModem M20
            'iMet' - interMet iMet
            'MK2LMS' - LMS6, 1680 MHz variant (using MK2A 9600 baud telemetry)

    """

    # Notes:
    # 400 MHz sondes
    #  Normal mode: 48 kHz sample rate, 20 kHz IF BW
    #  Wideband mode: 96 kHz sample rate, 64 kHz IF BW
    # 1680 MHz RS92 Setting: --bw 32
    # 1680 MHz LMS6-1680: Use FM demod. as usual.

    # Example command (for command-line testing):
    # rtl_fm -T -p 0 -M fm -g 26.0 -s 15k -f 401500000 | sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -t wav - highpass 20 | ./rs_detect -z -t 8

    # Add a -T option if bias is enabled
    bias_option = "-T " if bias else ""

    # Add a gain parameter if we have been provided one.
    if gain != -1:
        gain_param = "-g %.1f " % gain
    else:
        gain_param = ""

    # Adjust the detection bandwidth based on the band the scanning is occuring in.
    if frequency < 1000e6:
        # 400-406 MHz sondes
        _mode = "IQ"
        if wideband_sondes:
            _iq_bw = 96000
            _if_bw = 64
        else:
            _iq_bw = 48000
            _if_bw = 20

            # Try and avoid the RTLSDR 403.2 MHz spur.
            # Note that this is only goign to work if we are detecting on 403.210 or 403.190 MHz.
            if (abs(403200000 - frequency) < 20000) and (sdr_type == "RTLSDR"):
                logging.debug("Scanner - Narrowing detection IF BW to avoid RTLSDR spur.")
                _if_bw = 15
        
    else:
        # 1680 MHz sondes
        # Both the RS92-NGP and 1680 MHz LMS6 have a much wider bandwidth than their 400 MHz counterparts.
        # The RS92-NGP is maybe 25 kHz wide, and the LMS6 is 175 kHz (!!) wide.
        # Given the huge difference between these two, we default to using a very wide FM bandwidth, but allow the user
        # to narrow this if only RS92-NGPs are expected.
        if ngp_tweak:
            # RS92-NGP detection
            _mode = "IQ"
            _iq_bw = 48000
            _if_bw = 32
        else:
            # LMS6-1680 Detection
            _mode = "FM"
            _rx_bw = 250000 # Expanded to 250 kHz 2021-07-17. Results in better off-freq detection.

    if _mode == "IQ":
        # IQ decoding
        rx_test_command = f"{timeout_cmd()} {dwell_time * 2} "

        rx_test_command += get_sdr_iq_cmd(
            sdr_type=sdr_type,
            frequency=frequency,
            sample_rate=_iq_bw,
            rtl_device_idx = rtl_device_idx,
            rtl_fm_path = rtl_fm_path,
            ppm = ppm,
            gain = gain,
            bias = bias,
            sdr_hostname = sdr_hostname,
            sdr_port = sdr_port,
            ss_iq_path = ss_iq_path,
            scan = True
        )

        # rx_test_command = (
        #     "%s %ds %s %s-p %d -d %s %s-M raw -F9 -s %d -f %d 2>/dev/null |"
        #     % (
        #         timeout_cmd(),
        #         dwell_time * 2,
        #         rtl_fm_path,
        #         bias_option,
        #         int(ppm),
        #         str(device_idx),
        #         gain_param,
        #         _iq_bw,
        #         frequency,
        #     )
        # )
        # Saving of Debug audio, if enabled,
        if save_detection_audio:
            detect_iq_path = os.path.join(autorx.logging_path, f"detect_IQ_{frequency}_{_iq_bw}_{str(rtl_device_idx)}.raw")
            rx_test_command += f" tee {detect_iq_path} |"

        rx_test_command += os.path.join(
            rs_path, "dft_detect"
        ) + " -t %d --iq --bw %d --dc - %d 16 2>/dev/null" % (
            dwell_time,
            _if_bw,
            _iq_bw,
        )

    elif _mode == "FM":
        # FM decoding

        # Sample Source (rtl_fm)

        rx_test_command = f"{timeout_cmd()} {dwell_time * 2} "

        rx_test_command += get_sdr_fm_cmd(
            sdr_type=sdr_type,
            frequency=frequency,
            filter_bandwidth=_rx_bw,
            sample_rate=48000,
            highpass = 20,
            lowpass = None,
            rtl_device_idx = rtl_device_idx,
            rtl_fm_path = rtl_fm_path,
            ppm = ppm,
            gain = gain,
            bias = bias,
            sdr_hostname = "",
            sdr_port = 1234,
        )

        # rx_test_command = (
        #     "%s %ds %s %s-p %d -d %s %s-M fm -F9 -s %d -f %d 2>/dev/null |"
        #     % (
        #         timeout_cmd(),
        #         dwell_time * 2,
        #         rtl_fm_path,
        #         bias_option,
        #         int(ppm),
        #         str(device_idx),
        #         gain_param,
        #         _rx_bw,
        #         frequency,
        #     )
        # )
        # # Sample filtering
        # rx_test_command += (
        #     "sox -t raw -r %d -e s -b 16 -c 1 - -r 48000 -t wav - highpass 20 2>/dev/null | "
        #     % _rx_bw
        # )

        # Saving of Debug audio, if enabled,
        if save_detection_audio:
            detect_audio_path = os.path.join(autorx.logging_path, f"detect_audio_{frequency}_{str(rtl_device_idx)}.wav")
            rx_test_command += f" tee {detect_audio_path} |"

        # Sample decoding / detection
        # Note that we detect for dwell_time seconds, and timeout after dwell_time*2, to catch if no samples are being passed through.
        rx_test_command += (
            os.path.join(rs_path, "dft_detect") + " -t %d 2>/dev/null" % dwell_time
        )

    _sdr_name = get_sdr_name(
        sdr_type, 
        rtl_device_idx = rtl_device_idx, 
        sdr_hostname = sdr_hostname, 
        sdr_port = sdr_port
    )

    logging.debug(
        f"Scanner ({_sdr_name}) - Using detection command: {rx_test_command}"
    )
    logging.debug(
        f"Scanner ({_sdr_name})- Attempting sonde detection on {frequency/1e6 :.3f} MHz"
    )

    try:
        FNULL = open(os.devnull, "w")
        _start = time.time()
        ret_output = subprocess.check_output(rx_test_command, shell=True, stderr=FNULL)
        FNULL.close()
        ret_output = ret_output.decode("utf8")

        # Release the SDR channel if necessary
        shutdown_sdr(sdr_type, rtl_device_idx, sdr_hostname, frequency, scan=True)

    except subprocess.CalledProcessError as e:
        # dft_detect returns a code of 1 if no sonde is detected.
        # logging.debug("Scanner - dfm_detect return code: %s" % e.returncode)
        if e.returncode == 124:
            logging.error(f"Scanner ({_sdr_name}) - dft_detect timed out.")
            raise IOError("Possible SDR lockup.")

        elif e.returncode >= 2:
            ret_output = e.output.decode("utf8")
        else:
            _runtime = time.time() - _start
            logging.debug(
                f"Scanner ({_sdr_name}) - dft_detect exited in {_runtime:.1f} seconds with return code {e.returncode}."
            )
            return (None, 0.0)
    except Exception as e:
        # Something broke when running the detection function.
        logging.error(
            f"Scanner ({_sdr_name}) - Error when running dft_detect - {str(e)}"
        )
        return (None, 0.0)

    _runtime = time.time() - _start
    logging.debug(
        "Scanner - dft_detect exited in %.1f seconds with return code 1." % _runtime
    )

    # Check for no output from dft_detect.
    if ret_output is None or ret_output == "":
        # logging.error("Scanner - dft_detect returned no output?")
        return (None, 0.0)

    # Split the line into sonde type and correlation score.
    _fields = ret_output.split(":")

    if len(_fields) < 2:
        logging.error(
            "Scanner - malformed output from dft_detect: %s" % ret_output.strip()
        )
        return (None, 0.0)

    _type = _fields[0]
    _score = _fields[1]

    # Detect any frequency correction information:
    try:
        if "," in _score:
            _offset_est = float(_score.split(",")[1].split("Hz")[0].strip())
            _score = float(_score.split(",")[0].strip())
        else:
            _score = float(_score.strip())
            _offset_est = 0.0

        
    except Exception as e:
        logging.error(
            "Scanner - Error parsing dft_detect output: %s" % ret_output.strip()
        )
        return (None, 0.0)

    _sonde_type = None

    if "RS41" in _type:
        logging.debug(
            "Scanner (%s) - Detected a RS41! (Score: %.2f, Offset: %.1f Hz)"
            % (_sdr_name, _score, _offset_est)
        )
        _sonde_type = "RS41"
    elif "RS92" in _type:
        logging.debug(
            "Scanner (%s) - Detected a RS92! (Score: %.2f, Offset: %.1f Hz)"
            % (_sdr_name, _score, _offset_est)
        )
        _sonde_type = "RS92"
    elif "DFM" in _type:
        logging.debug(
            "Scanner (%s) - Detected a DFM Sonde! (Score: %.2f, Offset: %.1f Hz)"
            % (_sdr_name, _score, _offset_est)
        )
        _sonde_type = "DFM"
    elif "M10" in _type:
        logging.debug(
            "Scanner (%s) - Detected a M10 Sonde! (Score: %.2f, Offset: %.1f Hz)"
            % (_sdr_name, _score, _offset_est)
        )
        _sonde_type = "M10"
    elif "M20" in _type:
        logging.debug(
            "Scanner (%s) - Detected a M20 Sonde! (Score: %.2f, Offset: %.1f Hz)"
            % (_sdr_name, _score, _offset_est)
        )
        _sonde_type = "M20"
    elif "IMET4" in _type:
        logging.debug(
            "Scanner (%s) - Detected a iMet-4 Sonde! (Score: %.2f, Offset: %.1f Hz)"
            % (_sdr_name, _score, _offset_est)
        )
        _sonde_type = "IMET"
    elif "IMET1" in _type:
        # This could actually be a wideband iMet sonde. We treat this as a IMET4.
        logging.debug(
            "Scanner (%s) - Possible detection of a Wideband iMet Sonde! (Type %s) (Score: %.2f)"
            % (_sdr_name, _type, _score)
        )
        # Override the type to IMET4.
        _sonde_type = "IMET"
    elif "IMETafsk" in _type:
        logging.debug(
            "Scanner (%s) - Detected a iMet Sonde! (Type %s - Unsupported) (Score: %.2f)"
            % (_sdr_name, _type, _score)
        )
        _sonde_type = "IMET1"
    elif "IMET5" in _type:
        logging.debug(
            "Scanner (%s) - Detected a iMet-54 Sonde! (Score: %.2f)"
            % (_sdr_name, _score)
        )
        _sonde_type = "IMET5"
    elif "LMS6" in _type:
        logging.debug(
            "Scanner (%s) - Detected a LMS6 Sonde! (Score: %.2f, Offset: %.1f Hz)"
            % (_sdr_name, _score, _offset_est)
        )
        _sonde_type = "LMS6"
    elif "C34" in _type:
        logging.debug(
            "Scanner (%s) - Detected a Meteolabor C34/C50 Sonde! (Not yet supported...) (Score: %.2f)"
            % (_sdr_name, _score)
        )
        _sonde_type = "C34C50"
    elif "MRZ" in _type:
        logging.debug(
            "Scanner (%s) - Detected a Meteo-Radiy MRZ Sonde! (Score: %.2f)"
            % (_sdr_name, _score)
        )
        if _score < 0:
            _sonde_type = "-MRZ"
        else:
            _sonde_type = "MRZ"

    elif "MK2LMS" in _type:
        logging.debug(
            "Scanner (%s) - Detected a 1680 MHz LMS6 Sonde (MK2A Telemetry)! (Score: %.2f, Offset: %.1f Hz)"
            % (_sdr_name, _score, _offset_est)
        )
        if _score < 0:
            _sonde_type = "-MK2LMS"
        else:
            _sonde_type = "MK2LMS"

    elif "MEISEI" in _type:
        logging.debug(
            "Scanner (%s) - Detected a Meisei Sonde! (Score: %.2f, Offset: %.1f Hz)"
            % (_sdr_name, _score, _offset_est)
        )
        # Not currently sure if we expect to see inverted Meisei sondes.
        if _score < 0:
            _sonde_type = "-MEISEI"
        else:
            _sonde_type = "MEISEI"

    elif "MTS01" in _type:
        logging.debug(
            "Scanner (%s) - Detected a Meteosis MTS01 Sonde! (Score: %.2f, Offset: %.1f Hz)"
            % (_sdr_name, _score, _offset_est)
        )
        # Not currently sure if we expect to see inverted Meteosis sondes.
        if _score < 0:
            _sonde_type = "-MTS01"
        else:
            _sonde_type = "MTS01"

    elif "WXR301" in _type:
        logging.debug(
            "Scanner (%s) - Detected a Weathex WxR-301D Sonde! (Score: %.2f, Offset: %.1f Hz)"
            % (_sdr_name, _score, _offset_est)
        )
        _sonde_type = "WXR301"
        # Clear out the offset estimate for WxR-301's as it's not accurate
        # to do no whitening on the signal.
        _offset_est = 0.0

    elif "WXRPN9" in _type:
        logging.debug(
            "Scanner (%s) - Detected a Weathex WxR-301D Sonde (PN9 Variant)! (Score: %.2f, Offset: %.1f Hz)"
            % (_sdr_name, _score, _offset_est)
        )
        _sonde_type = "WXRPN9"
        
    else:
        _sonde_type = None

    return (_sonde_type, _offset_est)


#
# Radiosonde Scanner Class
#
class SondeScanner(object):
    """Radiosonde Scanner
    Continuously scan for radiosondes using a SDR, and pass results onto a callback function
    """

    # Allow up to X consecutive scan errors before giving up.
    SONDE_SCANNER_MAX_ERRORS = 5

    def __init__(
        self,
        callback=None,
        auto_start=True,
        min_freq=400.0,
        max_freq=403.0,
        search_step=800.0,
        only_scan=[],
        always_scan=[],
        never_scan=[],
        snr_threshold=10,
        min_distance=1000,
        quantization=10000,
        scan_dwell_time=20,
        detect_dwell_time=5,
        scan_delay=10,
        max_peaks=10,
        scan_check_interval=10,
        rs_path="./",

        sdr_type="RTLSDR",
        sdr_hostname="localhost",
        sdr_port=5555,
        ss_iq_path = "./ss_iq",
        ss_power_path = "./ss_power",

        rtl_power_path="rtl_power",
        rtl_fm_path="rtl_fm",
        rtl_device_idx=0,
        gain=-1,
        ppm=0,
        bias=False,

        save_detection_audio=False,
        temporary_block_list={},
        temporary_block_time=60,
        ngp_tweak=False,
        wideband_sondes=False
    ):
        """Initialise a Sonde Scanner Object.

        Apologies for the huge number of args...

        Args:
            callback (function): A function to pass results from the sonde scanner to (when a sonde is found).
            auto_start (bool): Start up the scanner automatically.
            min_freq (float): Minimum search frequency, in MHz.
            max_freq (float): Maximum search frequency, in MHz.
            search_step (float): Search step, in *Hz*. Defaults to 800 Hz, which seems to work well.
            only_scan (list): If provided, *only* scan on these frequencies. Frequencies provided as a list in MHz.
            always_scan (list): If provided, add these frequencies to the start of each scan attempt.
            never_scan (list): If provided, remove these frequencies from the detected peaks before scanning.
            snr_threshold (float): SNR to threshold detections at. (dB)
            min_distance (float): Minimum allowable distance between detected peaks, in Hz.
                Helps avoid detection of numerous peaks due to ripples within the signal bandwidth.
            quantization (float): Quantize search results to this value in Hz. Defaults to 10 kHz.
                Essentially all radiosondes transmit on 10 kHz channel steps.
            scan_dwell_time (int): Number of seconds for rtl_power to average spectrum over. Default = 20 seconds.
            detect_dwell_time (int): Number of seconds to allow rs_detect to attempt to detect a sonde. Default = 5 seconds.
            scan_delay (int): Delay X seconds between scan runs.
            max_peaks (int): Maximum number of peaks to search over. Peaks are ordered by signal power before being limited to this number.
            scan_check_interval (int): If we are using a only_scan list, re-check the RTLSDR works every X scan runs.
            rs_path (str): Path to the RS binaries (i.e rs_detect). Defaults to ./

            sdr_type (str): 'RTLSDR', 'Spyserver' or 'KA9Q'

            Arguments for KA9Q SDR Server / SpyServer:
            sdr_hostname (str): Hostname of KA9Q Server
            sdr_port (int): Port number of KA9Q Server

            Arguments for RTLSDRs:
            rtl_power_path (str): Path to rtl_power, or drop-in equivalent. Defaults to 'rtl_power'
            rtl_fm_path (str): Path to rtl_fm, or drop-in equivalent. Defaults to 'rtl_fm'
            rtl_device_idx (int or str): Device index or serial number of the RTLSDR. Defaults to 0 (the first SDR found).
            ppm (int): SDR Frequency accuracy correction, in ppm.
            gain (int): SDR Gain setting, in dB. A gain setting of -1 enables the RTLSDR AGC.
            bias (bool): If True, enable the bias tee on the SDR.

            device_idx (int): SDR Device index. Defaults to 0 (the first SDR found).
            ppm (int): SDR Frequency accuracy correction, in ppm.
            gain (int): SDR Gain setting, in dB. A gain setting of -1 enables the RTLSDR AGC.
            bias (bool): If True, enable the bias tee on the SDR.

            save_detection_audio (bool): Save the audio used in each detecton to detect_<device_idx>.wav
            temporary_block_list (dict): A dictionary where each attribute represents a frequency that should be blocked for a set time.
            temporary_block_time (int): How long (minutes) frequencies in the temporary block list should remain blocked for.
            ngp_tweak (bool): Narrow the detection filter when searching for 1680 MHz sondes, to enhance detection of RS92-NGPs.
            wideband_sondes (bool): Use a wider detection filter to allow detection of Weathex and wideband iMet sondes.
        """

        # Thread flag. This is set to True when a scan is running.
        self.sonde_scanner_running = True

        # Copy parameters

        self.min_freq = min_freq
        self.max_freq = max_freq
        self.search_step = search_step
        self.only_scan = only_scan
        self.always_scan = always_scan
        self.never_scan = never_scan
        self.snr_threshold = snr_threshold
        self.min_distance = min_distance
        self.quantization = quantization
        self.scan_dwell_time = scan_dwell_time
        self.detect_dwell_time = detect_dwell_time
        self.scan_delay = scan_delay
        self.max_peaks = max_peaks
        self.rs_path = rs_path

        self.sdr_type = sdr_type

        self.sdr_hostname = sdr_hostname
        self.sdr_port = sdr_port
        self.ss_iq_path = ss_iq_path
        self.ss_power_path = ss_power_path

        self.rtl_power_path = rtl_power_path
        self.rtl_fm_path = rtl_fm_path
        self.rtl_device_idx = rtl_device_idx
        self.gain = gain
        self.ppm = ppm
        self.bias = bias

        self.callback = callback
        self.save_detection_audio = save_detection_audio
        self.wideband_sondes = wideband_sondes

        # Temporary block list.
        self.temporary_block_list = temporary_block_list.copy()
        self.temporary_block_list_lock = Lock()
        self.temporary_block_time = temporary_block_time

        # Alert the user if there are temporary blocks in place.
        if len(self.temporary_block_list.keys()) > 0:
            self.log_info(
                "Temporary blocks in place for frequencies: %s"
                % str(list(self.temporary_block_list.keys()))
            )

        # Error counter.
        self.error_retries = 0

        # Count how many scans we have performed.
        self.scan_counter = 0
        # If we run a only_scan list, check the SDR every X scan loops.
        self.scan_check_interval = scan_check_interval

        # This will become our scanner thread.
        self.sonde_scan_thread = None

        # Test if the supplied SDR is working.
        _sdr_ok = test_sdr(
            self.sdr_type, 
            rtl_device_idx = self.rtl_device_idx, 
            sdr_hostname = self.sdr_hostname, 
            sdr_port = self.sdr_port,
            ss_iq_path = self.ss_iq_path,
            check_freq = 1e6*(self.max_freq+self.min_freq)/2.0
            )

        if not _sdr_ok:
            self.sonde_scanner_running = False
            self.exit_state = "FAILED SDR"
            return

        self.exit_state = "OK"

        if auto_start:
            self.start()

    def start(self):
        # Start the scan loop (if not already running)
        if self.sonde_scan_thread is None:
            self.sonde_scan_thread = Thread(target=self.scan_loop)
            self.sonde_scan_thread.start()
            self.sonde_scanner_running = True
        else:
            self.log_warning("Sonde scan already running!")

    def send_to_callback(self, results):
        """Send scan results to a callback.

        Args:
            results (list): List consisting of [freq, type)]

        """
        try:
            # Only send scan results to the callback if we are still running.
            # This avoids sending scan results when the scanner is being shutdown.
            if (self.callback != None) and self.sonde_scanner_running:
                self.callback(results)
        except Exception as e:
            self.log_error("Error handling scan results - %s" % str(e))

    def scan_loop(self):
        """Continually perform scans, and pass any results onto the callback function"""

        self.log_info("Starting Scanner Thread")
        while self.sonde_scanner_running:

            # If we have hit the maximum number of permissable errors, quit.
            if self.error_retries > self.SONDE_SCANNER_MAX_ERRORS:
                self.log_error(
                    "Exceeded maximum number of consecutive RTLSDR errors. Closing scan thread."
                )
                break

            # If we are using a only_scan list, we don't have an easy way of checking the RTLSDR
            # is producing useful data, so, test it.
            if len(self.only_scan) > 0:
                self.scan_counter += 1
                if (self.scan_counter % self.scan_check_interval) == 0:
                    self.log_debug("Performing periodic check of SDR.")

                    _sdr_ok = test_sdr(
                        self.sdr_type, 
                        rtl_device_idx = self.rtl_device_idx, 
                        sdr_hostname = self.sdr_hostname, 
                        sdr_port = self.sdr_port,
                        ss_iq_path = self.ss_iq_path,
                        check_freq = 1e6*(self.max_freq+self.min_freq)/2.0
                        )

                    if not _sdr_ok:
                        self.log_error(
                            "Unrecoverable SDR error. Closing scan thread."
                        )
                        break

            try:
                _results = self.sonde_search()

            except (IOError, ValueError) as e:
                # No log file produced. Reset the SDR and try again.
                # traceback.print_exc()
                self.log_warning("SDR produced no output... resetting and retrying.")
                self.error_retries += 1
                # Attempt to reset the SDR, if possible.
                try:
                    reset_sdr(
                        self.sdr_type, 
                        rtl_device_idx = self.rtl_device_idx, 
                        sdr_hostname = self.sdr_hostname, 
                        sdr_port = self.sdr_port
                    )
                except Exception as e:
                    self.log_error(f"Caught error when trying to reset SDR - {str(e)}")

                for _ in range(10):
                    if not self.sonde_scanner_running:
                        break
                    time.sleep(1)
                continue
            except Exception as e:
                traceback.print_exc()
                self.log_error("Caught other error: %s" % str(e))
                for _ in range(10):
                    if not self.sonde_scanner_running:
                        break
                    time.sleep(1)
            else:
                # Scan completed successfuly! Reset the error counter.
                self.error_retries = 0

            # Sleep before starting the next scan.
            for _ in range(self.scan_delay):
                if not self.sonde_scanner_running:
                    self.log_debug("Breaking out of scan loop.")
                    break
                time.sleep(1)

        self.log_info("Scanner Thread Closed.")
        self.sonde_scanner_running = False
        self.sonde_scanner_thread = None

    def sonde_search(self, first_only=False):
        """Perform a frequency scan across a defined frequency range, and test each detected peak for the presence of a radiosonde.

        In order, this function:
        - Runs rtl_power to capture spectrum data across the frequency range of interest.
        - Thresholds and quantises peaks detected in the spectrum.
        - On each peak run rs_detect to determine if a radiosonce is present.
        - Returns either the first, or a list of all detected sondes.

        Performing a search can take some time (many minutes if there are lots of peaks detected). This function can be exited quickly
        by setting self.sonde_scanner_running to False, which will also close the sonde scanning thread if running.

        Args:
            first_only (bool): If True, return after detecting the first sonde. Otherwise continue to scan through all peaks.

        Returns:
            list: An empty list [] if no sondes are detected otherwise, a list of list, containing entries of [frequency (Hz), Sonde Type],
                i.e. [[402500000,'RS41'],[402040000,'RS92']]
        """
        global scan_result

        _search_results = []

        if len(self.only_scan) == 0:
            # No only_scan frequencies provided - perform a scan.

            (freq, power, step) = get_power_spectrum(
                sdr_type=self.sdr_type,
                frequency_start=self.min_freq * 1e6,
                frequency_stop=self.max_freq * 1e6,
                step=self.search_step,
                integration_time=self.scan_dwell_time,
                rtl_device_idx=self.rtl_device_idx,
                rtl_power_path=self.rtl_power_path,
                ppm=self.ppm,
                gain=self.gain,
                bias=self.bias,
                sdr_hostname=self.sdr_hostname,
                sdr_port=self.sdr_port,
                ss_power_path = self.ss_power_path
            )

            # Exit opportunity.
            if self.sonde_scanner_running == False:
                return []

            # Sanity check results.
            if step == None or len(freq) == 0 or len(power) == 0:
                # Otherwise, if a file has been written but contains no data, it can indicate
                # an issue with the RTLSDR. Sometimes these issues can be resolved by issuing a usb reset to the RTLSDR.
                raise ValueError("Error getting PSD")

            # Update the global scan result
            scan_result["freq"] = [round(x,6) for x in list(freq/1e6)]
            scan_result["power"] = [round(x,2) for x in list(power)]
            scan_result["timestamp"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
            scan_result["peak_freq"] = []
            scan_result["peak_lvl"] = []

            # Rough approximation of the noise floor of the received power spectrum.
            # Switched to use a Median instead of a Mean 2022-04-02. Should remove outliers better.
            power_nf = np.median(power)
            logging.debug(f"Noise Floor Estimate: {power_nf:.1f} dB uncal")
            # Pass the threshold data to the web client for plotting
            scan_result["threshold"] = power_nf

            # Detect peaks.
            peak_indices = detect_peaks(
                power,
                mph=(power_nf + self.snr_threshold),
                mpd=(self.min_distance / step),
                show=False,
            )

            # If we have found no peaks, and no always_scan list has been provided, re-scan.
            if (len(peak_indices) == 0) and (len(self.always_scan) == 0):
                self.log_debug("No peaks found.")
                # Emit a notification to the client that a scan is complete.
                flask_emit_event("scan_event")
                return []

            # Sort peaks by power.
            peak_powers = power[peak_indices]
            peak_freqs = freq[peak_indices]
            peak_frequencies = peak_freqs[np.argsort(peak_powers)][::-1]

            # Quantize to nearest x Hz
            peak_frequencies = (
                np.round(peak_frequencies / self.quantization) * self.quantization
            )

            # Remove any duplicate entries after quantization, but preserve order.
            _, peak_idx = np.unique(peak_frequencies, return_index=True)
            peak_frequencies = peak_frequencies[np.sort(peak_idx)]

            # Remove outside min_freq and max_freq.
            _index = np.argwhere(
                (peak_frequencies < (self.min_freq * 1e6 - (self.quantization / 2.0))) |
                (peak_frequencies > (self.max_freq * 1e6 + (self.quantization / 2.0)))
            )
            peak_frequencies = np.delete(peak_frequencies, _index)

            # Never scan list & Temporary block list behaviour change as of v1.2.3
            # Was: peak_frequencies==_frequency   (This only matched an exact frequency in the never_scan list)
            # Now (1.2.3): Block if the peak frequency is within +/-quantization/2.0 of a never_scan or blocklist frequency.

            # Remove any frequencies in the never_scan list.
            for _frequency in np.array(self.never_scan) * 1e6:
                _index = np.argwhere(
                    np.abs(peak_frequencies - _frequency) < (self.quantization / 2.0)
                )
                peak_frequencies = np.delete(peak_frequencies, _index)

            # Limit to the user-defined number of peaks to search over.
            if len(peak_frequencies) > self.max_peaks:
                peak_frequencies = peak_frequencies[: self.max_peaks]

            # Append on any frequencies in the supplied always_scan list
            peak_frequencies = np.append(
                np.array(self.always_scan) * 1e6, peak_frequencies
            )

            # Remove any frequencies in the temporary block list
            self.temporary_block_list_lock.acquire()
            for _frequency in self.temporary_block_list.copy().keys():
                # Check the time the block was added.
                if self.temporary_block_list[_frequency] > (
                    time.time() - self.temporary_block_time * 60
                ):
                    # We should still be blocking this frequency, so remove any peaks with this frequency.
                    _index = np.argwhere(
                        np.abs(peak_frequencies - _frequency)
                        < (self.quantization / 2.0)
                    )
                    peak_frequencies = np.delete(peak_frequencies, _index)
                    if len(_index) > 0:
                        self.log_debug(
                            "Peak on %.3f MHz was removed due to temporary block."
                            % (_frequency / 1e6)
                        )

                else:
                    # This frequency doesn't need to be blocked any more, remove it from the block list.
                    self.temporary_block_list.pop(_frequency)
                    self.log_info(
                        "Removed %.3f MHz from temporary block list."
                        % (_frequency / 1e6)
                    )

            self.temporary_block_list_lock.release()

            # Get the level of our peak search results, to send to the web client.
            # This is actually a bit of a pain to do...
            _peak_freq = []
            _peak_lvl = []
            _search_radius = math.ceil((self.quantization / 2) / self.search_step)
            for _peak in peak_frequencies:
                try:
                    # Find the index of the peak within our decimated frequency array.
                    _peak_power_idx = np.argmin(
                        np.abs(scan_result["freq"] - _peak / 1e6)
                    )
                    # Because we've decimated the freq & power data, the peak location may
                    # not be exactly at this frequency, so we take the maximum of an area
                    # around this location.
                    _peak_search_min = max(0, _peak_power_idx - _search_radius)
                    _peak_search_max = min(
                        len(scan_result["freq"]) - 1, _peak_power_idx + _search_radius
                    )
                    # Grab the maximum value, and append it and the frequency to the output arrays
                    _peak_lvl.append(
                        max(scan_result["power"][_peak_search_min:_peak_search_max + 1])
                    )
                    _peak_freq.append(_peak / 1e6)
                except:
                    pass
            # Add the peak results to our global scan result dictionary.
            scan_result["peak_freq"] = _peak_freq
            scan_result["peak_lvl"] = _peak_lvl
            # Tell the web client we have new data.
            flask_emit_event("scan_event")

            if len(peak_frequencies) == 0:
                self.log_debug("No peaks found after never_scan frequencies removed.")
                return []
            else:
                self.log_info(
                    "Detected peaks on %d frequencies (MHz): %s"
                    % (len(peak_frequencies), str(peak_frequencies / 1e6))
                )

        else:
            # We have been provided a only_scan list - scan through the supplied frequencies.
            peak_frequencies = np.array(self.only_scan) * 1e6
            self.log_info(
                "Scanning only frequencies (MHz): %s" % str(peak_frequencies / 1e6)
            )

        # Run rs_detect on each peak frequency, to determine if there is a sonde there.
        for freq in peak_frequencies:

            _freq = float(freq)

            # Exit opportunity.
            if self.sonde_scanner_running == False:
                return []

            (detected, offset_est) = detect_sonde(
                _freq,
                sdr_type=self.sdr_type,
                sdr_hostname=self.sdr_hostname,
                sdr_port=self.sdr_port,
                ss_iq_path = self.ss_iq_path,
                rtl_fm_path=self.rtl_fm_path,
                rtl_device_idx=self.rtl_device_idx,
                ppm=self.ppm,
                gain=self.gain,
                bias=self.bias,
                dwell_time=self.detect_dwell_time,
                save_detection_audio=self.save_detection_audio,
                wideband_sondes=self.wideband_sondes
            )

            if detected != None:
                # Quantize the detected frequency (with offset) to 1 kHz
                _freq = round((_freq + offset_est) / 1000.0) * 1000.0

                # Add a detected sonde to the output array
                _search_results.append([_freq, detected])

                # Immediately send this result to the callback.
                self.send_to_callback([[_freq, detected]])
                # If we only want the first detected sonde, then return now.
                if first_only:
                    return _search_results

                # Otherwise, we continue....

        if len(_search_results) == 0:
            self.log_debug("No sondes detected.")
        else:
            self.log_debug("Scan Detected Sondes: %s" % str(_search_results))

        return _search_results

    def oneshot(self, first_only=False):
        """Perform a once-off scan attempt

        Args:
            first_only (bool): If True, return after detecting the first sonde. Otherwise continue to scan through all peaks.

        Returns:
            list: An empty list [] if no sondes are detected otherwise, a list of list, containing entries of [frequency (Hz), Sonde Type],
                i.e. [[402500000,'RS41'],[402040000,'RS92']]

        """
        # If we already have a scanner thread active, bomb out.
        if self.sonde_scanner_running:
            self.log_error("Oneshot scan attempted with scan thread running!")
            return []
        else:
            # Otherwise, attempt a scan.
            self.sonde_scanner_running = True
            _result = self.sonde_search(first_only=first_only)
            self.sonde_scanner_running = False
            return _result

    def stop(self, nowait=False):
        """Stop the Scan Loop"""
        if self.sonde_scanner_running:
            self.log_info("Waiting for current scan to finish...")
            self.sonde_scanner_running = False

            # Wait for the sonde scanner thread to close, if there is one.
            if self.sonde_scan_thread != None and (not nowait):
                self.sonde_scan_thread.join(60)
                if self.sonde_scan_thread.is_alive():
                    self.log_error("Scanning thread did not finish, terminating")
                    sys.exit(4)

    def running(self):
        """Check if the scanner is running"""
        return self.sonde_scanner_running

    def add_temporary_block(self, frequency):
        """Add a frequency to the temporary block list.

        Args:
            frequency (float): Frequency to be blocked, in Hz
        """
        # Acquire a lock on the block list, so we don't accidentally modify it
        # while it is being used in a scan.
        self.temporary_block_list_lock.acquire()
        self.temporary_block_list[frequency] = time.time()
        self.temporary_block_list_lock.release()
        self.log_info(
            "Adding temporary block for frequency %.3f MHz." % (frequency / 1e6)
        )

    def log_debug(self, line):
        """Helper function to log a debug message with a descriptive heading.
        Args:
            line (str): Message to be logged.
        """
        _sdr_name = get_sdr_name(
            self.sdr_type, 
            rtl_device_idx = self.rtl_device_idx, 
            sdr_hostname = self.sdr_hostname, 
            sdr_port = self.sdr_port
        )
        logging.debug(f"Scanner ({_sdr_name}) - {line}")

    def log_info(self, line):
        """Helper function to log an informational message with a descriptive heading.
        Args:
            line (str): Message to be logged.
        """
        _sdr_name = get_sdr_name(
            self.sdr_type, 
            rtl_device_idx = self.rtl_device_idx, 
            sdr_hostname = self.sdr_hostname, 
            sdr_port = self.sdr_port
        )
        logging.info(f"Scanner ({_sdr_name}) - {line}")

    def log_error(self, line):
        """Helper function to log an error message with a descriptive heading.
        Args:
            line (str): Message to be logged.
        """
        _sdr_name = get_sdr_name(
            self.sdr_type, 
            rtl_device_idx = self.rtl_device_idx, 
            sdr_hostname = self.sdr_hostname, 
            sdr_port = self.sdr_port
        )
        logging.error(f"Scanner ({_sdr_name}) - {line}")

    def log_warning(self, line):
        """Helper function to log a warning message with a descriptive heading.
        Args:
            line (str): Message to be logged.
        """
        _sdr_name = get_sdr_name(
            self.sdr_type, 
            rtl_device_idx = self.rtl_device_idx, 
            sdr_hostname = self.sdr_hostname, 
            sdr_port = self.sdr_port
        )
        logging.warning(f"Scanner ({_sdr_name}) - {line}")


if __name__ == "__main__":
    # Basic test script - run a scan using default parameters.
    logging.basicConfig(
        format="%(asctime)s %(levelname)s:%(message)s", level=logging.DEBUG
    )

    # Callback to handle scan results
    def print_result(scan_result):
        print("SCAN RESULT: " + str(scan_result))

    # Local spurs at my house :-)
    never_scan = [401.7, 401.32, 402.09, 402.47, 400.17, 402.85]

    # Instantiate scanner with default parameters.
    _scanner = SondeScanner(callback=print_result, never_scan=never_scan)

    try:
        # Oneshot approach.
        _result = _scanner.oneshot(first_only=True)
        print("Oneshot search result: %s" % str(_result))

        # Continuous scanning:
        _scanner.start()

        # Run until Ctrl-C, then exit cleanly.
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        _scanner.stop()
        print("Exited cleanly.")
