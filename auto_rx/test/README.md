# Radiosonde Demodulator Testing Scripts

These scripts are used to test the signal processing performance of the scripts used in the radiosonde_auto_rx project. Our aim is to ensure performance is not degraded by any updates to the software.

## Dependencies
For these scripts to work, we need:
 * The following directories created: samples, generated
 * The various demodulator binaries (rs41mod, dft_detect, etc... ) located in ../  - Build these using the build.sh script.
 * CSDR installed and available on $PATH: https://github.com/simonyiszk/csdr
 * The base high-snr samples located in ./samples/. These can be downloaded from http://rfhead.net/sondes/sonde_samples.tar.gz
 * Python (2, will probably work in 3), with numpy available.

To perform the tests involving rtl_fm, we require tsrc (from codec2-dev/unittest) and Viproz's hacked rtl_fm to work.
Viproz's rtl_fm is available from here: https://github.com/Viproz/rtl-sdr/
Build this, but do NOT install it. Instead, copy the rtl_fm from build/src to the *this* directory, and re-name it to rtl_fm_stdin
You still need a RTLSDR connected to be able to run this.

tsrc is available at http://svn.code.sf.net/p/freetel/code/codec2-dev/misc/tsrc.c
HOWEVER there is a bug which makes it neglect the -c (complex) option.
Change line 59 to read: int channels = 2;
and compile with gcc tsrc.c -o tsrc -lm -lsamplerate
Then copy this file to *this* directory.

## generate_lowsnr.py
This script generates a set of low-SNR samples based on the base high-SNR samples in ./samples/
Calibrated-level noise is added to the sample to produce a file with a user-defined Eb/No ('SNR per-bit').
If everything works 'perfectly', we should expect all the different modems to have similar PER vs Eb/No performance.
However, real-world factors such as packet length, transmitter deviation, filter widths, etc will mess this up.
I wouldn't try and make too many comparions of the performance between different sonde demodulators. Better to strike
a baseline of current performance, and then try and improve on it.

The level of noise to add is determined based on the variance of the sample. Some checking of Eb/No of generated
samples has been performed with David Rowe's fsk demod, though only for the RS41 samples so far.

Modify the EBNO_RANGE variable to change the range of Eb/No values to generate. FSK demods generally fall over between about 10 and 16 dB.
Uncomment the various elements in the SAMPLES array to choose what sample to process.

Then, run with:
```
$ cd scripts
$ python generate_lowsnr.py
```


Notes:
 * I suspect the variance measurement for the m10 sample is off. Its performing suspiciously better than the other sondes.


## test_demod.py
This script run the generated samples above through different demodulation chains. 

Check the processing_type dict in the script for the differnet demodulation options.

Example:
```
# Demodulate all RS41 samples.
$ python test_demod.py -m rs41_csdr_fm_decode -f "./generated/rs41*.bin"

# Run dft_detect across all samples.
# python test_demod.py -m csdr_fm_dftdetect -f "./generated/*.bin"
```

The output is a csv of: filename, result

Depending on the mode, the result could be a packet count, or it could be a success/no success (in the case of the detection utilities).


# Sample Capture Information
- All captures have radiosonde signal at DC, or as close to DC as practicable.

- Captured using: rtl_sdr -f 402500400 -s 960k -g 49.6 -n 115200000 rs41_960k.bin
- Converted to 96k float IQ using: cat rs41_960k.bin | csdr convert_u8_f | csdr fir_decimate_cc 10 0.005 HAMMING > rs41_96k_float.bin


* rs41_96k_float.bin - Vaisala RS41, Serial Number N3920808, 120 packets
* rs92_96k_float.bin - Vaisala RS92, Serial Number M2513116, 120 packets
* dfm09_96k_float.bin - Graw DFM09, Serial Number 637797, 96 Packets
* m10_96k_float.bin - Meteomodem M10, 120 packets
* imet4_96k_float.bin - iMet-4, Serial Number 15236, 119 packets
* lms6-400_96k_float.bin - Lockheed Martin LMS6 (400 MHz variant), Serial number 8097164, 120 packets.
* imet54_96k_float.bin - iMet-54, Serial number 55064062, 240 packets.
* rsngp_96k_float.bin - Vaisala RS92-NGP (1680 MHz), Serial number P3213708, 120 packets
* mrz_96k_float.bin - Meteo-Radiy MRZ, ID MRZ-5667-39155, ~105 packets.

There are also a set of noise samples available [here](http://rfhead.net/sondes/noise_samples.tar.gz), which are useful for checking the detector scripts for false positives.


# Older Notes

These notes are here for legacy reasons, in case they are useful for future work.

## Reading data into Python
```
import numpy as np
data = np.fromfile('rs41_96k_float.bin', dtype='c8')
```


## Demodulation Examples
To run these examples, you will need csdr available on the path, and will need the various radiosonde demodulators as built by auto_rx.
Run the build.sh script in radiosonde_auto_rx/auto_rx to build these, then copy them to your working directory.

NOTE: These are not optimised. Have a look in the test_demod.py script for the 'optimal' demodulation commands.

### RS41

#### Using csdr as a FM demodulator:
$ cat samples/rs41_96k_float.bin | csdr fir_decimate_cc 2 0.005 HAMMING | csdr bandpass_fir_fft_cc -0.18 0.18 0.05 | csdr fmdemod_quadri_cf | csdr limit_ff | csdr convert_f_s16 | sox -t raw -r 48k -e signed-integer -b 16 -c 1 - -r 48000 -b 8 -t wav - | ../rs41ecc --ecc --ptu --crc


#### Using nanorx as a FM demodulator:
NOTE - Nanorx seems to invert the FM output, hence the -i option on rs41ecc
NOTE - As of v0.85, nanorx's FM demodulators are likely corrupting the RS41 signal

$ cat rs41_96k_float.bin | csdr convert_f_s16 | ./nanorx -i stdin -r 96k -m FM -t 10 -o - | sox -t raw -r 48k -e signed-integer -b 16 -c 1 - -r 48000 -b 8 -t wav - | ../rs41ecc --ecc --ptu --crc -i


### RS92

#### Using csdr as a FM demodulator:
$ cat rs92_96k_float.bin | csdr fir_decimate_cc 2 0.005 HAMMING | csdr bandpass_fir_fft_cc -0.18 0.18 0.05 | csdr fmdemod_quadri_cf | csdr limit_ff | csdr convert_f_s16 | sox -t raw -r 48k -e signed-integer -b 16 -c 1 - -r 48000 -b 8 -t wav - | ../rs92ecc


### DFM09

#### Using csdr as a FM demodulator:
$ cat dfm09_96k_float.bin | csdr fir_decimate_cc 2 0.005 HAMMING | csdr bandpass_fir_fft_cc -0.18 0.18 0.05 | csdr fmdemod_quadri_cf | csdr limit_ff | csdr convert_f_s16 | sox -t raw -r 48k -e signed-integer -b 16 -c 1 - -r 48000 -b 8 -t wav - | ../dfm09ecc --auto


### iMet 4
Note that the imet decoder isn't used in auto_rx yet. Build from the imet directory using:
$ gcc imet1rs_dft.c -lm -o imet1rs_dft

#### Using csdr as a FM demodulator:
$ cat imet4_96k_float.bin | csdr fir_decimate_cc 2 0.005 HAMMING | csdr bandpass_fir_fft_cc -0.18 0.18 0.05 | csdr fmdemod_quadri_cf | csdr limit_ff | csdr convert_f_s16 | sox -t raw -r 48k -e signed-integer -b 16 -c 1 - -r 48000 -b 8 -t wav - | ../imet1rs_dft


