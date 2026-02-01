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

# Timeout multiplier for subprocess detection
# We give subprocess dwell_time * this value to complete
DETECTION_TIMEOUT_MULTIPLIER = 2.5

# Timeout for process.wait() after sending kill signal
# Process should exit almost immediately after SIGKILL, so 3 seconds is generous
PROCESS_WAIT_TIMEOUT = 3.0

# Timeout for SDR cleanup (shutdown_sdr has internal 10s timeout, give a bit more)
SDR_CLEANUP_TIMEOUT = 15.0

# Timeout for SDR setup (ka9q_setup_channel has internal 10s timeout, give a bit more)
SDR_SETUP_TIMEOUT = 15.0


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
    # Note: get_sdr_iq_cmd/get_sdr_fm_cmd can block (ka9q_setup_channel uses subprocess)
    # so we run them in an executor to avoid blocking the event loop
    try:
        loop = asyncio.get_running_loop()  # Python 3.7+, preferred
    except RuntimeError:
        loop = asyncio.get_event_loop()  # Fallback

    if _mode == "IQ":
        # IQ decoding
        # Note: We rely on asyncio.wait_for for timeout, not external timeout_cmd
        logging.debug(f"Scanner - Setting up KA9Q channel for {frequency/1e6:.3f} MHz")
        try:
            rx_test_command = await asyncio.wait_for(
                loop.run_in_executor(
                    None,
                    lambda: get_sdr_iq_cmd(
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
                ),
                timeout=SDR_SETUP_TIMEOUT
            )
        except asyncio.TimeoutError:
            logging.error(f"Scanner - KA9Q channel setup timed out after {SDR_SETUP_TIMEOUT}s for {frequency/1e6:.3f} MHz")
            raise

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
        # get_sdr_fm_cmd doesn't block for KA9Q, but we add timeout for consistency
        rx_test_command = await asyncio.wait_for(
            loop.run_in_executor(
                None,
                lambda: get_sdr_fm_cmd(
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
            ),
            timeout=SDR_SETUP_TIMEOUT
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
    ret_output = ""
    try:
        _start = time.time()

        # Use asyncio.create_subprocess_shell for non-blocking execution
        # This allows multiple detections to run concurrently
        logging.debug(f"Scanner ({_sdr_name}) - Starting detection subprocess for {frequency/1e6:.3f} MHz")
        process = await asyncio.create_subprocess_shell(
            rx_test_command,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        # Wait for completion with timeout
        try:
            stdout, stderr = await asyncio.wait_for(
                process.communicate(), timeout=dwell_time * DETECTION_TIMEOUT_MULTIPLIER
            )
            ret_output = stdout.decode("utf8")
            stderr_output = stderr.decode("utf8") if stderr else ""
        except asyncio.TimeoutError:
            process.kill()
            try:
                await asyncio.wait_for(process.wait(), timeout=PROCESS_WAIT_TIMEOUT)
            except asyncio.TimeoutError:
                logging.warning(f"Scanner ({_sdr_name}) - Process did not exit within {PROCESS_WAIT_TIMEOUT}s after kill")
            logging.error(f"Scanner ({_sdr_name}) - dft_detect timed out on {frequency/1e6:.3f} MHz.")
            return (None, 0.0)

        _runtime = time.time() - _start

        # Check return code
        # Note: No longer checking for code 124 since we removed external timeout wrapper
        if process.returncode == 0:
            # Success - sonde detected
            pass
        elif process.returncode is None:
            # Defensive: returncode should always be set after communicate(), but handle None gracefully
            logging.warning(f"Scanner ({_sdr_name}) - process returncode is None after completion (unexpected)")
        elif process.returncode >= 2:
            # Error but we have output - try to parse anyway
            if stderr_output:
                logging.debug(f"Scanner ({_sdr_name}) - dft_detect stderr: {stderr_output.strip()}")
        else:
            # No sonde detected
            logging.debug(
                f"Scanner ({_sdr_name}) - dft_detect exited in {_runtime:.1f}s with return code {process.returncode}."
            )
            if stderr_output:
                logging.debug(f"Scanner ({_sdr_name}) - dft_detect stderr: {stderr_output.strip()}")
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
                    try:
                        await asyncio.wait_for(process.wait(), timeout=PROCESS_WAIT_TIMEOUT)
                    except asyncio.TimeoutError:
                        logging.warning(f"Scanner ({_sdr_name}) - Process did not exit within {PROCESS_WAIT_TIMEOUT}s after kill (cleanup)")
            except Exception as e:
                logging.debug(f"Scanner ({_sdr_name}) - Error killing process: {e}")

        # Always release the SDR channel, even on failure
        # Each frequency uses a unique SSRC, so concurrent cleanup calls don't conflict
        # We run this in an executor to avoid blocking the event loop with subprocess calls
        try:
            logging.debug(f"Scanner ({_sdr_name}) - Cleaning up SDR channel for {frequency/1e6:.3f} MHz")
            def release_sdr():
                shutdown_sdr(sdr_type, rtl_device_idx, sdr_hostname, frequency, scan=True)

            # Run in executor with timeout - shutdown_sdr has internal 10s timeout
            # Reuse the loop variable from earlier in the function
            await asyncio.wait_for(
                loop.run_in_executor(None, release_sdr),
                timeout=SDR_CLEANUP_TIMEOUT
            )
            logging.debug(f"Scanner ({_sdr_name}) - SDR cleanup completed for {frequency/1e6:.3f} MHz")
        except asyncio.TimeoutError:
            logging.warning(f"Scanner ({_sdr_name}) - SDR cleanup timed out for {frequency/1e6:.3f} MHz")
        except Exception as e:
            logging.debug(f"Scanner ({_sdr_name}) - Error releasing SDR: {e}")

    # Parse output using shared function from scan.py to ensure consistency
    from .scan import parse_dft_detect_output
    return parse_dft_detect_output(ret_output, _sdr_name)


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

    logging.info(f"Async scan starting: {len(peak_frequencies)} frequencies with max_concurrent={max_concurrent}")

    async def detect_with_semaphore(freq):
        """Wrapper to limit concurrency"""
        async with semaphore:
            try:
                _task_start = time.time()
                detected, offset_est = await detect_sonde_async(
                    frequency=freq,
                    **detect_kwargs
                )
                _task_time = time.time() - _task_start
                logging.debug(f"Detection task for {freq/1e6:.3f} MHz completed in {_task_time:.1f}s")
                if detected:
                    # Quantize the detected frequency with offset to 1 kHz
                    freq_corrected = round((freq + offset_est) / 1000.0) * 1000.0
                    return (freq_corrected, detected)
                return None
            except asyncio.CancelledError:
                # Task was cancelled - clean exit
                logging.debug(f"Detection task for {freq/1e6:.3f} MHz cancelled")
                raise

    # Create tasks for all peaks
    tasks = [asyncio.create_task(detect_with_semaphore(float(freq))) for freq in peak_frequencies]

    _scan_start = time.time()
    try:
        # Wait for all to complete
        results = await asyncio.gather(*tasks, return_exceptions=True)
    except asyncio.CancelledError:
        # If we're cancelled, cancel all subtasks
        for task in tasks:
            if not task.done():
                task.cancel()
        # Wait for all cancellations to complete
        await asyncio.gather(*tasks, return_exceptions=True)
        raise

    # Filter out None results and exceptions
    detections = []
    for result in results:
        if isinstance(result, asyncio.CancelledError):
            # Task was cancelled, skip it
            continue
        elif isinstance(result, Exception):
            logging.error(f"Detection task failed: {result}")
        elif result is not None:
            detections.append(result)

    _scan_time = time.time() - _scan_start
    logging.info(f"Async scan completed: {len(detections)} detections from {len(peak_frequencies)} frequencies in {_scan_time:.1f}s")

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
    # Calculate a reasonable global timeout as a safety net
    # Time per task: dwell_time * timeout_multiplier + setup/cleanup overhead
    dwell_time = detect_kwargs.get('dwell_time', 10)
    time_per_task = dwell_time * DETECTION_TIMEOUT_MULTIPLIER + SDR_CLEANUP_TIMEOUT + 10  # +10 for setup
    num_batches = (len(peak_frequencies) + max_concurrent - 1) // max_concurrent
    global_timeout = num_batches * time_per_task + 60  # +60 buffer

    async def run_with_timeout():
        try:
            return await asyncio.wait_for(
                scan_peaks_concurrent(peak_frequencies, max_concurrent, **detect_kwargs),
                timeout=global_timeout
            )
        except asyncio.TimeoutError:
            logging.error(f"Async scan timed out after {global_timeout:.0f}s - possible hang detected")
            return []

    # Check if there's already an event loop running
    # Note: Python 3.6 compatible - get_running_loop() was added in 3.7
    loop_running = False
    try:
        # Python 3.7+
        loop = asyncio.get_running_loop()
        loop_running = True
    except AttributeError:
        # Python 3.6 fallback
        try:
            loop = asyncio.get_event_loop()
            loop_running = loop.is_running()
        except RuntimeError:
            loop_running = False
    except RuntimeError:
        # No running loop (Python 3.7+)
        loop_running = False

    if loop_running:
        # An event loop is running - we can't use asyncio.run() here
        # Solution: Run the async code in a separate thread with its own event loop
        # Note: We create the coroutine inside the worker thread to avoid cross-thread issues
        import concurrent.futures
        logging.debug(f"Async scan: Running in separate thread (event loop already running)")
        def run_in_thread():
            return asyncio.run(run_with_timeout())
        with concurrent.futures.ThreadPoolExecutor() as executor:
            future = executor.submit(run_in_thread)
            # Add timeout to future.result() to prevent indefinite blocking
            # Use global_timeout + 10s buffer to allow internal timeout to fire first
            try:
                return future.result(timeout=global_timeout + 10)
            except concurrent.futures.TimeoutError:
                logging.error(f"Async scan thread timed out after {global_timeout + 10:.0f}s - thread may be hung")
                return []
    else:
        # No event loop running - we can use asyncio.run() directly
        logging.debug(f"Async scan: Running directly (no event loop running)")
        return asyncio.run(run_with_timeout())
