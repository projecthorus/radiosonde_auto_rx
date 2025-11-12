#!/usr/bin/env python
#
#   radiosonde_auto_rx - Async Scanner for KA9Q-Radio
#
#   This module provides async/await versions of the scanner functions
#   to enable concurrent peak detection when using KA9Q-radio.
#
#   KA9Q-radio creates multiple virtual SDR channels from one physical SDR,
#   allowing true concurrent scanning across multiple frequencies.
#
#   NOTE: This is ONLY beneficial with KA9Q-radio. For standard RTLSDRs,
#   scanning must be sequential due to hardware limitations.
#
import asyncio
import logging
import os
import shlex
import time
from typing import Optional, Tuple
import threading

# Lazy imports to avoid dependency issues at module load time
# These will be imported when functions are actually called

# Lock to protect concurrent shutdown_sdr calls
# Key: (sdr_type, device_idx, hostname) -> Lock
_sdr_locks = {}
_sdr_locks_lock = threading.Lock()

def _get_sdr_lock(sdr_type, device_idx, hostname):
    """Get or create a lock for a specific SDR device"""
    key = (sdr_type, device_idx, hostname)
    with _sdr_locks_lock:
        if key not in _sdr_locks:
            _sdr_locks[key] = threading.Lock()
        return _sdr_locks[key]


async def detect_sonde_async(
    frequency: int,
    rs_path: str = "./",
    dwell_time: int = 10,
    sdr_type: str = "RTLSDR",
    sdr_hostname: str = "localhost",
    sdr_port: int = 5555,
    ss_iq_path: str = "./ss_iq",
    rtl_fm_path: str = "rtl_fm",
    rtl_device_idx: int = 0,
    ppm: int = 0,
    gain: int = -1,
    bias: bool = False,
    save_detection_audio: bool = False,
    ngp_tweak: bool = False,
    wideband_sondes: bool = False,
) -> Tuple[Optional[str], float]:
    """
    Async version of detect_sonde that uses asyncio subprocess for non-blocking execution.

    This allows multiple frequency detections to run concurrently instead of sequentially,
    dramatically reducing scan time when using KA9Q-radio.

    Returns:
        Tuple[Optional[str], float]: (sonde_type, frequency_offset) or (None, 0.0)
    """

    # Lazy import to avoid module-level dependency issues
    from .sdr_wrappers import (
        get_sdr_iq_cmd,
        get_sdr_fm_cmd,
        get_sdr_name,
        shutdown_sdr,
    )

    # Determine detection mode based on frequency band
    if frequency < 1000e6:
        # 400-406 MHz sondes
        _mode = "IQ"
        if wideband_sondes:
            _iq_bw = 96000
            _if_bw = 64
        else:
            _iq_bw = 48000
            _if_bw = 15
    else:
        # 1680 MHz sondes
        if ngp_tweak:
            # RS92-NGP detection
            _mode = "IQ"
            _iq_bw = 48000
            _if_bw = 32
        else:
            # LMS6-1680 Detection
            _mode = "FM"
            _rx_bw = 250000

    # Build the detection command
    if _mode == "IQ":
        # IQ decoding
        # Note: We rely on asyncio.wait_for for timeout, not external timeout_cmd
        rx_test_command = get_sdr_iq_cmd(
            sdr_type=sdr_type,
            frequency=frequency,
            sample_rate=_iq_bw,
            rtl_device_idx=rtl_device_idx,
            rtl_fm_path=rtl_fm_path,
            ppm=ppm,
            gain=gain,
            bias=bias,
            sdr_hostname=sdr_hostname,
            sdr_port=sdr_port,
            ss_iq_path=ss_iq_path,
            scan=True,
        )

        # Saving of Debug audio, if enabled
        if save_detection_audio:
            import autorx
            detect_iq_path = os.path.join(
                autorx.logging_path,
                f"detect_IQ_{frequency}_{_iq_bw}_{str(rtl_device_idx)}.raw",
            )
            rx_test_command += f" tee {shlex.quote(detect_iq_path)} |"

        dft_detect_path = os.path.join(rs_path, "dft_detect")
        rx_test_command += (
            shlex.quote(dft_detect_path)
            + " -t %d --iq --bw %d --dc - %d 16 2>/dev/null"
            % (dwell_time, _if_bw, _iq_bw)
        )

    elif _mode == "FM":
        # FM decoding
        # Note: We rely on asyncio.wait_for for timeout, not external timeout_cmd
        rx_test_command = get_sdr_fm_cmd(
            sdr_type=sdr_type,
            frequency=frequency,
            filter_bandwidth=_rx_bw,
            sample_rate=48000,
            highpass=20,
            lowpass=None,
            rtl_device_idx=rtl_device_idx,
            rtl_fm_path=rtl_fm_path,
            ppm=ppm,
            gain=gain,
            bias=bias,
            sdr_hostname="",
            sdr_port=1234,
        )

        # Saving of Debug audio, if enabled
        if save_detection_audio:
            import autorx
            detect_audio_path = os.path.join(
                autorx.logging_path,
                f"detect_audio_{frequency}_{str(rtl_device_idx)}.wav",
            )
            rx_test_command += f" tee {shlex.quote(detect_audio_path)} |"

        dft_detect_path = os.path.join(rs_path, "dft_detect")
        rx_test_command += (
            shlex.quote(dft_detect_path) + " -t %d 2>/dev/null" % dwell_time
        )

    _sdr_name = get_sdr_name(
        sdr_type,
        rtl_device_idx=rtl_device_idx,
        sdr_hostname=sdr_hostname,
        sdr_port=sdr_port,
    )

    logging.debug(
        f"Scanner ({_sdr_name}) - Using async detection command: {rx_test_command}"
    )

    process = None
    try:
        _start = time.time()

        # Use asyncio.create_subprocess_shell for non-blocking execution
        # This allows multiple detections to run concurrently
        process = await asyncio.create_subprocess_shell(
            rx_test_command,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
        )

        # Wait for completion with timeout
        try:
            stdout, _ = await asyncio.wait_for(
                process.communicate(), timeout=dwell_time * 2.5
            )
            ret_output = stdout.decode("utf8")
        except asyncio.TimeoutError:
            process.kill()
            await process.wait()
            logging.error(f"Scanner ({_sdr_name}) - dft_detect timed out on {frequency/1e6:.3f} MHz.")
            return (None, 0.0)

        _runtime = time.time() - _start

        # Check return code
        # Note: No longer checking for code 124 since we removed external timeout wrapper
        if process.returncode == 0 or process.returncode is None:
            # Success - sonde detected
            pass
        elif process.returncode >= 2:
            # Error but we have output - try to parse anyway
            pass
        else:
            # No sonde detected
            logging.debug(
                f"Scanner ({_sdr_name}) - dft_detect exited in {_runtime:.1f}s with return code {process.returncode}."
            )
            return (None, 0.0)

    except Exception as e:
        logging.error(
            f"Scanner ({_sdr_name}) - Error when running dft_detect - {str(e)}"
        )
        return (None, 0.0)
    finally:
        # Always clean up: kill subprocess if still running and release SDR
        if process is not None:
            try:
                if process.returncode is None:
                    # Process still running - kill it
                    process.kill()
                    await process.wait()
            except Exception as e:
                logging.debug(f"Scanner ({_sdr_name}) - Error killing process: {e}")

        # Release the SDR channel with locking to prevent race conditions
        try:
            sdr_lock = _get_sdr_lock(sdr_type, rtl_device_idx, sdr_hostname)
            with sdr_lock:
                shutdown_sdr(sdr_type, rtl_device_idx, sdr_hostname, frequency, scan=True)
        except Exception as e:
            logging.debug(f"Scanner ({_sdr_name}) - Error releasing SDR: {e}")

    # Parse output
    if ret_output is None or ret_output == "":
        return (None, 0.0)

    # Split the line into sonde type and correlation score
    _fields = ret_output.split(":")

    if len(_fields) < 2:
        logging.error(
            f"Scanner ({_sdr_name}) - malformed output from dft_detect: {ret_output.strip()}"
        )
        return (None, 0.0)

    _type = _fields[0]
    _score = _fields[1]

    # Detect any frequency correction information
    try:
        if "," in _score:
            _offset_est = float(_score.split(",")[1].split("Hz")[0].strip())
            _score = float(_score.split(",")[0].strip())
        else:
            _score = float(_score.strip())
            _offset_est = 0.0
    except Exception as e:
        logging.error(
            f"Scanner ({_sdr_name}) - Error parsing dft_detect output: {ret_output.strip()}"
        )
        return (None, 0.0)

    # Threshold checks based on sonde type
    _detected = None

    # Different thresholds for different sonde types
    if _type == "RS41":
        if _score > 0.5:
            _detected = "RS41"
    elif _type == "RS92":
        if _score > 0.3:
            _detected = "RS92"
    elif _type == "DFM":
        if _score > 0.4:
            _detected = "DFM"
    elif _type == "M10":
        if _score > 0.4:
            _detected = "M10"
    elif _type == "M20":
        if _score > 0.4:
            _detected = "M20"
    elif _type == "IMET":
        if _score > 0.4:
            _detected = "IMET"
    elif _type == "IMET5":
        if _score > 0.4:
            _detected = "IMET5"
    elif _type == "MK2LMS":
        if _score > 0.5:
            _detected = "MK2LMS"
    elif _type == "LMS":
        if _score > 0.5:
            _detected = "LMS"
    elif _type == "MEISEI":
        if _score > 0.4:
            _detected = "MEISEI"
    elif _type == "MRZ":
        if _score > 0.4:
            _detected = "MRZ"
    elif _type == "MTS01":
        if _score > 0.4:
            _detected = "MTS01"
    elif _type == "WXSONDE":
        if _score > 0.4:
            _detected = "WXSONDE"

    if _detected:
        logging.info(
            f"Scanner ({_sdr_name}) - Detected {_detected} on {frequency/1e6:.3f} MHz (score: {_score:.2f}, offset: {_offset_est:.0f} Hz) in {_runtime:.1f}s"
        )
        return (_detected, _offset_est)
    else:
        logging.debug(
            f"Scanner ({_sdr_name}) - {_type} score {_score:.2f} below threshold on {frequency/1e6:.3f} MHz"
        )
        return (None, 0.0)


async def scan_peaks_concurrent(
    peak_frequencies: list,
    max_concurrent: int = 2,
    **detect_kwargs
) -> list:
    """
    Scan multiple peaks concurrently instead of sequentially.

    This is the KEY optimization for KA9Q-radio - instead of scanning peaks
    sequentially, we scan them with limited concurrency using KA9Q's virtual channels.

    Args:
        peak_frequencies: List of frequencies to scan
        max_concurrent: Maximum number of concurrent detection tasks (default: 2)
                       Tune based on CPU cores and system resources
        **detect_kwargs: Arguments passed to detect_sonde_async

    Returns:
        List of tuples: [(frequency, sonde_type), ...]
    """

    # Create a semaphore to limit concurrent operations
    semaphore = asyncio.Semaphore(max_concurrent)

    async def detect_with_semaphore(freq):
        """Wrapper to limit concurrency"""
        async with semaphore:
            detected, offset_est = await detect_sonde_async(
                frequency=freq,
                **detect_kwargs
            )
            if detected:
                # Quantize the detected frequency with offset to 1 kHz
                freq_corrected = round((freq + offset_est) / 1000.0) * 1000.0
                return (freq_corrected, detected)
            return None

    # Create tasks for all peaks
    tasks = [detect_with_semaphore(float(freq)) for freq in peak_frequencies]

    # Wait for all to complete
    results = await asyncio.gather(*tasks, return_exceptions=True)

    # Filter out None results and exceptions
    detections = []
    for result in results:
        if isinstance(result, Exception):
            logging.error(f"Detection task failed: {result}")
        elif result is not None:
            detections.append(result)

    return detections


def run_async_scan(peak_frequencies: list, max_concurrent: int = 2, **detect_kwargs) -> list:
    """
    Synchronous wrapper for async scanning. Call this from existing sync code.

    Example usage in scan.py:
        from .scan_async import run_async_scan

        # Instead of sequential loop:
        # for freq in peak_frequencies:
        #     detected, offset = detect_sonde(freq, ...)

        # Use concurrent scanning:
        detections = run_async_scan(
            peak_frequencies,
            max_concurrent=2,  # Tune based on CPU cores
            rs_path=self.rs_path,
            dwell_time=self.detect_dwell_time,
            sdr_type=self.sdr_type,
            # ... other args
        )
    """
    # Check if there's already an event loop running
    try:
        asyncio.get_running_loop()
        # An event loop is running - we can't use asyncio.run() here
        # Solution: Run the async code in a separate thread with its own event loop
        import concurrent.futures
        with concurrent.futures.ThreadPoolExecutor() as executor:
            future = executor.submit(
                asyncio.run,
                scan_peaks_concurrent(peak_frequencies, max_concurrent, **detect_kwargs)
            )
            return future.result()
    except RuntimeError:
        # No event loop running - we can use asyncio.run() directly
        return asyncio.run(
            scan_peaks_concurrent(peak_frequencies, max_concurrent, **detect_kwargs)
        )
