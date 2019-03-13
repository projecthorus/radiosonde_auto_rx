# dft_detect Threshold Optimzation

Mark Jessop - 2019-03-02

**Note: The following is valid for dft_detect as configured in commit 4e48867. Further changes to dft_detect may require re-analysis of the threshold values.**

auto_rx uses the [dft_detect](https://github.com/rs1729/RS/blob/master/scan/dft_detect.c) utility, developed by rs1729. This utility accepts fm-demodulated samples (fs=48kHz, unsigned 8-bit) and correlates the incoming samples against a set of known radiosonde packet headers.

The incoming sample stream is considered to contain a radiosonde signal when the correlation score for a particular radiosonde type header exceeds a threshold, defined in the code [here](https://github.com/rs1729/RS/blob/master/scan/dft_detect.c#L92). 
These threshold values need to be selected such that detection occurs at the lowest signal SNR as possible, though without producing false positive, or mis-detections.

To assist with selection of these thresholds, an investigation was performed where dft_detect was run over a set of radiosonde samples, with calibrated SNRs (Eb/N0) between 5dB and 20 dB (using generate_lowsnr.py). dft_detect was also run over noise samples.

The following analysis was performed using the rtlfm FM demodulator, using a 22 kHz output sample rate. The following command-line was used to process samples:
```
$ cat test_file.bin | csdr convert_f_s16 | ./tsrc - - 1.8333 | csdr convert_s16_f | csdr shift_addition_cc -0.25000 2>/dev/null | csdr convert_f_u8 | ./rtl_fm_stdin -M fm -f 401000000 -F9 -s 22000  2>/dev/null| sox -t raw -r 22000 -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - highpass 20 2>/dev/null |../dft_detect --debug
```

## dft_detect 'noise floor'
Correlation of the known headers with noise (no radiosonde signals present) will result in a non-zero correlation score. These scores will set the minimum setting possible for the threshold, to avoid false detections.

dft_detect was run over a set of noise samples (noise1_96k_float.bin, noise2_96k_float.bin, noise3_96k_float.bin), which represent commonly observed background noise on my auto_rx station. The three samples have slightly different noise characteristics and contain different types of local interference.

The worst-case (highest) correlation scores out of the three samples are shown for each radiosonde type:

Sonde Type | Worse-Case Correlation Score
-----------|-----------------------------
DFM | 0.3050
RS41 | 0.3261
RS92 | 0.2961
M10 | 0.6106
LMS6 | 0.3157

We can see that the M10 radiosonde has a very high correlation score against noise. This is due to the M10 header being about half the size of the other radiosondes.

Note that as we don't have any LMS6 IQ samples at the moment, we will not be including LMS6 results in any further analysis.

## Threshold Experiments
Using these minimum threshold figures, the next aim is to determine thresholds which result in no false detections of other sonde types. We can do this by running dft_detect over all the calibrated-SNR samples and observing the resultant correlation scores.

We start out with thresholds set 0.1 higher than the 'correlation noise floor'. The following plots show the correlation score for each set of radiosonde samples (RS41, RS92, DFM, M10), vs Eb/N0. The dotted points indicate a point in which the correlation score exceeds the threshold, and hence we considered a sonde type to be detected.

![RS41](http://rfhead.net/sondes/plots/dft_detect_thresholds/rs41_minthresh.png)
![RS92](http://rfhead.net/sondes/plots/dft_detect_thresholds/rs92_minthresh.png)
![DFM](http://rfhead.net/sondes/plots/dft_detect_thresholds/dfm_minthresh.png)
![M10](http://rfhead.net/sondes/plots/dft_detect_thresholds/m10_minthresh.png)

We can see that while detection of the wanted radiosonde type occurs from a very low SNR, there are also a large number of mis-detections, particularly at higher SNRs. By looking at the maximum observed correlation scores for each dataset, we can set a new, higher threshold to avoid mis-detections.

Dataset / Sonde Type: | RS41 | RS92 | DFM | M10
----------------------|------|------|-----|-----
RS41 | 0.9162 | 0.4390 | 0.5299 | 0.7209
RS92 | 0.4070 | 0.9050 | 0.4836 | 0.7151
DFM | 0.4352 | 0.4421 | 0.9037 | 0.5781
M10 | 0.3519 | 0.3585 | 0.3707 | 0.9810

Based on the above table, we can define new thresholds at just above the highest 'unwanted' detection score:
* RS41: 0.53
* RS92: 0.54
* DFM: 0.62
* M10: 0.75

We can now re-generate the above plots, with the new threshold values:

![RS41](http://rfhead.net/sondes/plots/dft_detect_thresholds/rs41_newthresh.png)
![RS92](http://rfhead.net/sondes/plots/dft_detect_thresholds/rs92_newthresh.png)
![DFM](http://rfhead.net/sondes/plots/dft_detect_thresholds/dfm_newthresh.png)
![M10](http://rfhead.net/sondes/plots/dft_detect_thresholds/m10_newthresh.png)

Much improvement! From the plots, we can now determine the minimum SNR at which detection is possible:
* RS41: 8.5 dB 
* RS92: 10.5 dB
* DFM: 10.5 dB
* M10: 5.0 dB (There may be a SNR miscalibration issue with the M10 samples)

## Future Work
* It would probably be advantagous to tweak these detection thresholds such that the minimum detection threshold matches the minimum SNR for valid telemetry decoding - but that's for another time!
* Work out how the current peak-detection threshold SNR lines up with the above Eb/N0 figures for different radiosonde types. Can we even do reliable peak detection (i.e. not too many peaks) with signals with such a low SNR?