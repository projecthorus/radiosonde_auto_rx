# 2019-03-03 generate_lowsnr.py Validation

All of the performance testing scripts in use rely on samples with 'calibrated' Signal-to-noise ratio (SNR) - we add noise to a 'golden' sample to produce a sample with a known SNR.

However, instead of using SNR in the signal bandwidth, we use [Eb/N0](https://en.wikipedia.org/wiki/Eb/N0) (SNR-per-bit). This normalises the SNR, and allows comparison between modems operating at different baud rates. Eventually, this will also allow comparison of the modems with the theoretical acheivable performance of a FSK modem. 

The calibrated SNR samples are generated using [generate_lowsnr.py](../generate_lowsnr.py). A set of [golden samples](http://rfhead.net/sondes/sonde_samples.tar.gz) (one per radiosonde type) have very high SNRs, usually about 40dB or so - enough such that all packets are easily decoded. For each sample, we measure the signal power in the sample. Then, using a calculation based on the baud rate, the sample rate, the number of bits-per-symbol, and the desired SNR, we can generate white noise with a given noise power. This is added to the sample, which is then normalised (to +/-1.0) and saved. These samples can then be run through the various demodulation chains to measure their performance.

To be sure these measurements are meainingful, we need some confidence that the right amount of noise is being applied. We can do this by generating a FSK signal with known bits, and running it through the `generate_lowsnr.py` script. The 'noisy' signals can then be passed back into a FSK demodulator, and the Bit-Error-Eate (BER) measured. [David Rowe's FSK modem](http://svn.code.sf.net/p/freetel/code/codec2-dev/README_fsk.txt) has previously been demonstrated to have performance essentially equal to that of a theoretical non-coherent FSK modem, and so is ideal for this purpose.

## Generation of FSK
The codec2-dev repository (which contains the fsk modem mentioned above) has a suite of utilities for testing modem performance. For our testing we will use the following utilities:

* **fsk_get_test_bits**: Generate a sequence of test bits, based off a known pseudorandom seed.
* **fsk_mod**: Generate a FSK signal with provided baud rate, sample rate, centre frequency, and shift. Samples are accepted as one-byte-per-bit via stdin. fsk_mod usually generated real-valued outputs, but with a small modification can produce a complex output.
* **fsk_demod**: Demodulate a FSK signal, with provided sample rate and baud rate. fsk_demod can accept real and complex-valued samples - we are using compex-valued samples.
* **fsk_put_test_bits**: Receive the sequence of test bits, and provide BER statistics.

These utilities can be chained together using bash pipes, with a basic example being:
```
./fsk_get_test_bits - 100000 | ./fsk_mod 2 96000 4800 22000 4800 - - | ./fsk_demod --cs16 2 96000 4800  - - | ./fsk_put_test_bits -
```
Which produces output:
```
errs: 0 FSK BER 0.000000, bits tested 100, bit errors 0
errs: 0 FSK BER 0.000000, bits tested 200, bit errors 0
errs: 0 FSK BER 0.000000, bits tested 300, bit errors 0
errs: 0 FSK BER 0.000000, bits tested 400, bit errors 0
<lots of lines here>
errs: 0 FSK BER 0.000000, bits tested 99900, bit errors 0
```
The settings used in this example (96 kHz sample rate, 4800 baud) are typical of some of the sample radiosonde signals.

The test signal can be saved to a file easily using:
```
./fsk_get_test_bits - 100000 | ./fsk_mod 2 96000 4800 22000 4800 - - > test_bits.bin
```
The `generate_lowsnr.py` works on complex float (64-bit) samples, so we convert the file using [csdr](https://github.com/simonyiszk/csdr):
```
cat test_bits.bin | csdr convert_s16_f > test_bits_f.bin
```

## Adding Noise
`generate_lowsnr.py` has a list of samples to process near the top of the source file. To process test_bits_f, an entry is added to the list, and all other entries commented out:

```
SAMPLES = [
    ['../test_bits_f.bin', 4800, -100.0, 96000],
    #['rs41_96k_float.bin', 4800, -20.0, 96000], 
    #['rs92_96k_float.bin', 2400, -100, 96000], # No threshold set, as signal is continuous.
    #['dfm09_96k_float.bin', 2500, -100, 96000], # Weird baud rate. No threshold set, as signal is continuous.
    #['m10_96k_float.bin', 9616, -10.0, 96000]  # Really weird baud rate.
]
```
The parameters in the list are:
* The source sample filename.
* The baud rate of the source file
* A threshold, which is used to identify sections of the file containing packets, so as to accurately calculate the signal power. As this file contains a continuous FSK signal, the threshold is set very low, so the entire file is used for calculations.
* The sample rate of the file. 

The script is then run, and produces a set of files named `test_bits_f_16.0dB.bin` or similar. These can then be run back through the demodulator code using:
```
cat test_bits_f_16.0dB.bin | csdr convert_f_s16 | ./fsk_demod --cs16 2 96000 4800  - - | ./fsk_put_test_bits -
```

Collating the results, we obtain the following results:

![low-SNR results](http://rfhead.net/sondes/plots/low_snr_check.png)

The resulting BER closely matches the [theoretical](www.atlantarf.com/FSK_Modulation.php) FSK performance curve (some error due to the non-infinite number of packets used)! This provides good confidence that `generate_lowsnr.py` is producing samples with the correct Eb/N0.
