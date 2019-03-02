# 2019-03-02 Detector Change: rs_detect to dft_detect

Mark Jessop - 2019-03-02


## History
Up until now, the radiosonde detection functions of auto_rx have been performed by [rs_detect](https://github.com/rs1729/RS/blob/master/scan/rs_detect.c). This takes fm-demodulated input (48 kHz, unsigned 8-bit samples) and looks for the presence of known radiosonde headers. 

rs1729 has recently developed the improved [dft_detect](https://github.com/rs1729/RS/blob/master/scan/dft_detect.c), which uses correlation techniques to perform the same function. dft_detect also adds support for the iMet-4 and LMS6 radiosonde types, which is important for the radiosonde_auto_rx goal of tracking ALL the radiosondes!


### Demodulation Signal Processing
rs_detect and dft_detect require a FM-demodulated signal as input. To provide this using a RTLSDR, auto_rx uses the following command line:
```
$ rtl_fm -T -p 0 -M fm -g 26.0 -s 22k -f 401500000 | sox -t raw -r 22k -e s -b 16 -c 1 - -r 48000 -t wav - highpass 20 | ./rs_detect -z -t 8
```
This performs the following actions:
* rtl_fm takes samples from the RTLSDR, and performs FM demodulation. Samples are output at 22 kHz sample rate, in signed-int16 format. 
  * The FM channel bandwidth used for demodulation is still a bit unclear. Based on performance tests with an alternate FM demodulator, I suspect it's a bit less than half the output sample rate, so about a 11 kHz bandwidth.
* The samples from rtl_fm are resampled up to 48 kHz sample rate, converted to signed 16-bit int samples. A 20 Hz highpass filter is applied to compensate for some degree of DC offset.


### Previous Changes
When [Meteomodem M10 support](https://github.com/projecthorus/radiosonde_auto_rx/pull/101) was added, the rtl_fm output sample rate was changed from 15 kHz to 22 kHz, to better handle the wider-bandwidth M10 telemetry. 

At the time, there was no repeatable performance testing ability in auto_rx, and so detection performance was evaluated on observation of 'real-world' radiosonde signals. After some testing on a few stations, the change was deemed suitable for use and released. 

Now there is the ability to do controlled, repeatable performance tests, the change from 15 kHz to 22 kHz has been re-visited. Below is the detection performance of the above-mentioned decode chain (using rs_detect), with a 15 kHz and 22 kHz receive sample rate:

**rs_detect + rtl_fm 15 kHz sample rate**

SNR | RS41 | RS92 | DFM | M10
----|------|------|-----|----
10 | NO | NO | NO| M10
10.5 | RS41 | NO | NO | M10
11 | NO | RS92 | NO | M10
11.5 | NO | RS92 | NO | M10
12 | RS41 | RS92 | DFM | M10
12.5 | M10 | RS92 | DFM | M10
13 | RS41 | RS92 | DFM | M10
13.5 | RS41 | RS92 | DFM | M10
14.0 | RS41 | RS92 | DFM | M10
14.5 | RS41 | RS92 | DFM | M10
15 | RS41 | RS92 | DFM | M10
15.5 | RS41 | RS92 | DFM | M10
16 | RS41 | RS92 | DFM | M10
16.5 | RS41 | RS92 | DFM | M10
17 | RS41 | RS92 | DFM | M10
17.5 | RS41 | RS92 | DFM | M10
18 | RS41 | RS92 | DFM | M10
18.5 | RS41 | RS92 | DFM | M10
19 | RS41 | RS92 | DFM | M10
19.5 | RS41 | RS92 | DFM | M10

**rs_detect + rtl_fm 22 kHz sample rate**

SNR | RS41 | RS92 | DFM | M10
----|------|------|-----|----
10 | NO | NO | NO | M10
10.5 | NO | NO | NO | M10
11 | NO | NO | NO | M10
11.5 | NO | NO | NO | M10
12 | NO | NO | NO | M10
12.5 | NO | NO | NO | M10
13 | NO | NO | NO | M10
13.5 | NO | NO | NO | M10
14.0 | NO | NO | NO | M10
14.5 | NO | NO | NO | M10
15 | NO | NO | NO | M10
15.5 | RS41 | NO | NO | M10
16 | NO | NO | NO | M10
16.5 | NO | RS92 | NO | M10
17 | NO | RS92 | NO | M10
17.5 | RS41 | RS92 | DFM | M10
18 | NO | RS92 | DFM | M10
18.5 | RS41 | RS92 | DFM | M10
19 | RS41 | RS92 | DFM | M10
19.5 | RS41 | RS92 | DFM | M10

We can see that while M10 detection performance suffered no degradation, the detection performance of all other supported radiosonde types was degraded significantly - by up to 5.5 dB in the case of the Graw DFM sondes. Note that there is still some uncertainty around the validity of the M10 Eb/N0 calculation, so the M10's extremely good performance should be viewed with caution.

My apologies to those users that may have been affected by this performance degradation! This really shows the value of having a repeatable performance testing system.

## dfm_detect Performance
The following shows the detection performance when rs_detect is replaced with dft_detect. dft_detect has a set of internal thresholds, which are intended to prevent false-positives and mis-identification of radiosonde types. A discussion on how these thresholds have been optimized is available in [this report](./2019-03-01_dft_detect_optimization.md).

**dft_detect + rtl_fm 22 kHz sample rate**

SNR | RS41 | RS92 | DFM | M10
----|------|------|-----|----
5 | NO | NO | NO | M10
5.5 | NO | NO | NO | M10
6 | NO | NO | NO | M10
6.5 | NO | NO | NO | M10
7 | NO | NO | NO | M10
7.5 | NO | NO | NO | M10
8 | RS41 | NO | NO | M10
8.5 | RS41 | NO | NO | M10
9 | RS41 | NO | NO | M10
9.5 | RS41 | NO | NO | M10
10 | RS41 | NO | NO | M10
10.5 | RS41 | RS92 | NO | M10
11 | RS41 | RS92 | NO | M10
11.5 | RS41 | RS92 | DFM | M10
12 | RS41 | RS92 | DFM | M10
12.5 | RS41 | RS92 | DFM | M10
13 | RS41 | RS92 | DFM | M10
13.5 | RS41 | RS92 | DFM | M10
14.0 | RS41 | RS92 | DFM | M10
14.5 | RS41 | RS92 | DFM | M10
15 | RS41 | RS92 | DFM | M10
15.5 | RS41 | RS92 | DFM | M10
16 | RS41 | RS92 | DFM | M10
16.5 | RS41 | RS92 | DFM | M10
17 | RS41 | RS92 | DFM | M10
17.5 | RS41 | RS92 | DFM | M10
18 | RS41 | RS92 | DFM | M10
18.5 | RS41 | RS92 | DFM | M10
19 | RS41 | RS92 | DFM | M10
19.5 | RS41 | RS92 | DFM | M10

We can clearly see an improvement in detection performance on all radiosonde types. The M10 results are still questionable, though as mentioned previously the very good detection performanance is likely due to the shorter header size.

## Caveats and Other Issues

### Increased CPU Load
The CPU load of dft_detect is about 4x higher than that of rs_detect. However, with rs1729's latest updates it runs faster than realtime on a RPi2, so is considered to be fit for purpose.

Some analysis 

### Performance with Frequency Offsets
As with any FM-demod based, data-slicer decode system, frequency offsets in the signal will result in a DC offset that, with added noise, will impact performance.

As a rough guide:
* 1 kHz Offset:
  * RS41: 0.5 dB Degradation
  * RS92: 1dB Degradation
  * DFM: No Degradation
  * M10: No Degradation

* 5 kHz Offset:
  * RS41: 2.5 dB Degradation
  * RS92: 4 dB Degradation
  * DFM: 1.5 dB Degradation
  * M10: About 10dB degradation! (Likely as the signal is now over the edge of the FM passband.)

Some further analysis on this degradation is probably warranted.
