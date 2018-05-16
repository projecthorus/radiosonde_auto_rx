#!/usr/bin/env python
#
#   radiosonde_auto_rx - Radiosonde Scanner
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#
import logging
import numpy as np
import os
import platform
from .utils import detect_peaks

try:
    # Python 2
    from StringIO import StringIO
except ImportError:
    # Python 3
    from io import StringIO


def run_rtl_power(start, stop, step, filename="log_power.csv", dwell = 20, sdr_power='rtl_power', device_idx = 0, ppm = 0, gain = -1, bias = False):
    """ Capture spectrum data using rtl_power (or drop-in equivalent), and save to a file.

    Args:
        start (int): Start of search window, in Hz.
        stop (int): End of search window, in Hz.
        step (int): Search step, in Hz.
        filename (str): Output results to this file. Defaults to ./log_power.csv
        dwell (int): How long to average on the frequency range for.
        sdr_power (str): Path to the rtl_power utility.
        device_idx (int): SDR Device index. Defaults to 0 (the first SDR found).
        ppm (int): SDR Frequency accuracy correction, in ppm.
        gain (float): SDR Gain setting, in dB.
        bias (bool): If True, enable the bias tee on the SDR.

    Returns:
        bool: True if rtl_power ran successfuly, False otherwise.

    """
    # Example: rtl_power -T -f 400400000:403500000:800 -i20 -1 -c 20% -p 0 -g 26.0 log_power.csv

    # Add a -T option if bias is enabled
    bias_option = "-T " if bias else ""

    # Add a gain parameter if we have been provided one.
    if gain != -1:
        gain_param = '-g %.1f ' % gain
    else:
        gain_param = ''

    # Add -k 30 option, to SIGKILL rtl_power 30 seconds after the regular timeout expires.
    # Note that this only works with the GNU Coreutils version of Timeout, not the IBM version,
    # which is provided with OSX (Darwin).
    if 'Darwin' in platform.platform():
        timeout_kill = ''
    else:
        timeout_kill = '-k 30 '

    rtl_power_cmd = "timeout %s%d %s %s-f %d:%d:%d -i %d -1 -c 20%% -p %d -d %d %s%s" % (timeout_kill, dwell+10, sdr_power, bias_option, start, stop, step, dwell, int(ppm), int(device_idx), gain_param, filename)
    logging.info("Scanner - Running frequency scan.")
    logging.debug("Scanner - Running command: %s" % rtl_power_cmd)
    ret_code = os.system(rtl_power_cmd)
    if ret_code == 1:
        logging.critical("rtl_power call failed!")
        return False
    else:
        return True


def read_rtl_power(filename):
    """ Read in frequency samples from a single-shot log file produced by rtl_power 

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
    f = open(filename,'r')

    # rtl_power log files are csv's, with the first 6 fields in each line describing the time and frequency scan parameters
    # for the remaining fields, which contain the power samples. 

    for line in f:
        # Split line into fields.
        fields = line.split(',')

        if len(fields) < 6:
            logging.error("Invalid number of samples in input file - corrupt?")
            raise Exception("Invalid number of samples in input file - corrupt?")

        start_date = fields[0]
        start_time = fields[1]
        start_freq = float(fields[2])
        stop_freq = float(fields[3])
        freq_step = float(fields[4])
        n_samples = int(fields[5])

        #freq_range = np.arange(start_freq,stop_freq,freq_step)
        samples = np.loadtxt(StringIO(",".join(fields[6:])),delimiter=',')
        freq_range = np.linspace(start_freq,stop_freq,len(samples))

        # Add frequency range and samples to output buffers.
        freq = np.append(freq, freq_range)
        power = np.append(power, samples)

    f.close()

    # Sanitize power values, to remove the nan's that rtl_power puts in there occasionally.
    power = np.nan_to_num(power)

    return (freq, power, freq_step)


def detect_sonde(frequency, rs_path="./", dwell_time=10, sdr_fm='rtl_fm', device_idx=0, ppm=0, gain=-1, bias=False):
    """ Receive some FM and attempt to detect the presence of a radiosonde. 

    Args:
        frequency (int): Frequency to perform the detection on, in Hz.
        rs_path (str): Path to the RS binaries (i.e rs_detect). Defaults to ./
        dwell_time (int): Timeout before giving up detection.
        sdr_fm (str): Path to rtl_fm, or drop-in equivalent. Defaults to 'rtl_fm'
        device_idx (int): SDR Device index. Defaults to 0 (the first SDR found).
        ppm (int): SDR Frequency accuracy correction, in ppm.
        gain (int): SDR Gain setting, in dB.
        bias (bool): If True, enable the bias tee on the SDR.

    Returns:
        str/None: Returns None if no sonde found, otherwise returns a sonde type, from the following:
            'RS41' - Vaisala RS41
            'RS92' - Vaisala RS92
            'DFM' - Graw DFM06 / DFM09 (similar telemetry formats)
            'M10' - MeteoModem M10
            'iMet' - interMet iMet

    """

    # Example command (for command-line testing):
    # rtl_fm -T -p 0 -M fm -g 26.0 -s 15k -f 401500000 | sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -t wav - highpass 20 | ./rs_detect -z -t 8

    # Add a -T option if bias is enabled
    bias_option = "-T " if bias else ""

    # Add a gain parameter if we have been provided one.
    if gain != -1:
        gain_param = '-g %.1f ' % gain
    else:
        gain_param = ''

    rx_test_command = "timeout %ds %s %s-p %d -d %d %s-M fm -F9 -s 15k -f %d 2>/dev/null |" % (dwell_time, sdr_fm, bias_option, int(ppm), int(device_idx), gain_param, frequency) 
    rx_test_command += "sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -t wav - highpass 20 2>/dev/null |"
    rx_test_command += os.path.join(rs_path,"rs_detect") + " -z -t 8 2>/dev/null"

    logging.info("Scanner - Attempting sonde detection on %.3f MHz" % (frequency/1e6))
    logging.debug("Scanner - Running command: %s" % rx_test_command)

    ret_code = os.system(rx_test_command)

    # Shift down by a byte... for some reason.
    ret_code = ret_code >> 8

    # Default is non-inverted FM.
    inv = ""

    # Check if the inverted bit is set
    if (ret_code & 0x80) > 0: 
        # If the inverted bit is set, we have to do some munging of the return code to get the sonde type.
        ret_code = abs(-1 * (0x100 - ret_code))
        # Currently ignoring the inverted flag, as rs_detect appears to detect some sondes as inverted incorrectly. 
        #inv = "-"

    else:
        ret_code = abs(ret_code)

    if ret_code == 3:
        logging.info("Scanner - Detected a RS41!")
        return inv+"RS41"
    elif ret_code == 4:
        logging.info("Scanner - Detected a RS92!")
        return inv+"RS92"
    elif ret_code == 2:
        logging.info("Scanner - Detected a DFM Sonde!")
        return inv+"DFM"
    elif ret_code == 5:
        logging.info("Scanner - Detected a M10 Sonde! (Unsupported)")
        return inv+"M10"
    elif ret_code == 6:
        logging.info("Scanner - Detected a iMet Sonde! (Unsupported)")
        return inv+"iMet"
    else:
        return None



def sonde_search(min_freq = 400.0,
    max_freq = 403.0,
    search_step = 800.0,
    whitelist = [],
    greylist = [],
    blacklist = [],
    snr_threshold = 10,
    min_distance = 1000,
    quantization = 10000,
    scan_dwell_time = 20,
    detect_dwell_time = 5,
    max_peaks = 10,
    first_only = False,
    rs_path = "./",
    sdr_power = "rtl_power",
    sdr_fm = "rtl_fm",
    device_idx = 0,
    gain = -1,
    ppm = 0,
    bias = False):
    """ Perform a frequency scan across a defined frequency range, and test each detected peak for the presence of a radiosonde.

    In order, this function:
    - Runs rtl_power to capture spectrum data across the frequency range of interest.
    - Thresholds and quantises peaks detected in the spectrum.
    - On each peak run rs_detect to determine if a radiosonce is present.
    - Returns either the first, or a list of all detected sondes.

    Apologies for the huge number of args...

    Args:
        min_freq (float): Minimum search frequency, in MHz.
        max_freq (float): Maximum search frequency, in MHz.
        search_step (float): Search step, in *Hz*. Defaults to 800 Hz, which seems to work well.
        whitelist (list): If provided, *only* scan on these frequencies. Frequencies provided as a list in MHz.
        greylist (list): If provided, add these frequencies to the start of each scan attempt.
        blacklist (list): If provided, remove these frequencies from the detected peaks before scanning.
        snr_threshold (float): SNR to threshold detections at. (dB)
        min_distance (float): Minimum allowable distance between detected peaks, in Hz.
            Helps avoid detection of numerous peaks due to ripples within the signal bandwidth.
        quantization (float): Quantize search results to this value in Hz. Defaults to 10 kHz.
            Essentially all radiosondes transmit on 10 kHz channel steps.
        scan_dwell_time (int): Number of seconds for rtl_power to average spectrum over. Default = 20 seconds.
        detect_dwell_time (int): Number of seconds to allow rs_detect to attempt to detect a sonde. Default = 5 seconds.
        max_peaks (int): Maximum number of peaks to search over. Peaks are ordered by signal power before being limited to this number.
        first_only (bool): If True, return after detecting the first sonde. Otherwise continue to scan through all peaks.
        rs_path (str): Path to the RS binaries (i.e rs_detect). Defaults to ./
        sdr_power (str): Path to rtl_power, or drop-in equivalent. Defaults to 'rtl_power'
        sdr_fm (str): Path to rtl_fm, or drop-in equivalent. Defaults to 'rtl_fm'
        device_idx (int): SDR Device index. Defaults to 0 (the first SDR found).
        ppm (int): SDR Frequency accuracy correction, in ppm.
        gain (int): SDR Gain setting, in dB.
        bias (bool): If True, enable the bias tee on the SDR.

    Returns:
        list: An empty list [] if no sondes are detected otherwise, a list of list, containing entries of [frequency (Hz), Sonde Type],
            i.e. [[402500000,'RS41'],[402040000,'RS92']]
    """

    _search_results = []

    if len(whitelist) == 0 :
        # No whitelist frequencies provided - perform a scan.
        run_rtl_power(min_freq*1e6,
            max_freq*1e6,
            search_step,
            filename="log_power.csv",
            dwell=scan_dwell_time,
            sdr_power=sdr_power,
            device_idx=device_idx,
            ppm=ppm,
            gain=gain,
            bias=bias)

        # Read in result.
        # This step will throw an IOError if the file does not exist.
        (freq, power, step) = read_rtl_power('log_power.csv')
        # Sanity check results.
        if step == 0 or len(freq)==0 or len(power)==0:
            # Otherwise, if a file has been written but contains no data, it can indicate
            # an issue with the RTLSDR. Sometimes these issues can be resolved by issuing a usb reset to the RTLSDR.
            raise Exception("Invalid Log File")


        # Rough approximation of the noise floor of the received power spectrum.
        power_nf = np.mean(power)

        # Detect peaks.
        peak_indices = detect_peaks(power, mph=(power_nf+snr_threshold), mpd=(min_distance/step), show = False)

        # If we have found no peaks, and no greylist has been provided, re-scan.
        if (len(peak_indices) == 0) and (len(greylist) == 0):
            logging.info("Scanner - No peaks found.")
            return []

        # Sort peaks by power.
        peak_powers = power[peak_indices]
        peak_freqs = freq[peak_indices]
        peak_frequencies = peak_freqs[np.argsort(peak_powers)][::-1]

        # Quantize to nearest x Hz
        peak_frequencies = np.round(peak_frequencies/quantization)*quantization

        # Remove any duplicate entries after quantization, but preserve order.
        _, peak_idx = np.unique(peak_frequencies, return_index=True)
        peak_frequencies = peak_frequencies[np.sort(peak_idx)]

        # Remove any frequencies in the blacklist.
        for _frequency in np.array(blacklist)*1e6:
            _index = np.argwhere(peak_frequencies==_frequency)
            peak_frequencies = np.delete(peak_frequencies, _index)

        # Limit to the user-defined number of peaks to search over.
        if len(peak_frequencies) > max_peaks:
            peak_frequencies = peak_frequencies[:max_peaks]

        # Append on any frequencies in the supplied greylist
        peak_frequencies = np.append(np.array(greylist)*1e6, peak_frequencies)

        if len(peak_frequencies) == 0:
            logging.info("Scanner - No peaks found after blacklist frequencies removed.")
            return []
        else:
            logging.info("Scanner - Performing scan on %d frequencies (MHz): %s" % (len(peak_frequencies),str(peak_frequencies/1e6)))

    else:
        # We have been provided a whitelist - scan through the supplied frequencies.
        peak_frequencies = np.array(whitelist)*1e6
        logging.info("Scanner - Scanning on whitelist frequencies (MHz): %s" % str(peak_frequencies/1e6))

    # Run rs_detect on each peak frequency, to determine if there is a sonde there.
    for freq in peak_frequencies:
        detected = detect_sonde(freq,
            sdr_fm=sdr_fm,
            device_idx=device_idx,
            ppm=ppm,
            gain=gain,
            bias=bias,
            dwell_time=detect_dwell_time)

        if detected != None:
            # Add a detected sonde to the output array
            _search_results.append([freq, detected])
            # If we only want the first detected sonde, then return now.
            if first_only:
                return _search_results

            # Otherwise, we continue....

    if len(_search_results) == 0:
        logging.info("Scanner - No sondes detected.")
    else:
        logging.info("Scanner - Detected Sondes: %s" % str(_search_results))

    return _search_results



if __name__ == "__main__":
    # Basic test script - run a scan using default parameters.
    logging.basicConfig(format='%(asctime)s %(levelname)s:%(message)s', level=logging.DEBUG)

    # Call sonde_search with various parameter options
    # Standard call
    print(sonde_search())
    # Whitelist
    #print(sonde_search(whitelist=[401.0]))
    # Blacklist
    #print(sonde_search(blacklist=[402.5]))
    # Greylist
    #print(sonde_search(greylist=[401.0]))
