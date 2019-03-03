# 2019-03-02 auto_rx Performance Baseline

Mark Jessop - 2019-03-02


The scripts in the `auto_rx/test` directory provide the ability to measure the various decoders performance with a set of radiosonde signal samples, with calibrated [Eb/N0](https://en.wikipedia.org/wiki/Eb/N0) (SNR-per-bit) values. 

A few notes:
* All measurements have been performed using the demodulator (rtl_fm, filtering) settings as per the 2019-02-27 auto_rx release. 
* All decoders (rs41, rs92, etc...) have any available error-correction options enabled.
* Packet Error Rates have been calculated based on a comparison with the known number of packets in the high-SNR source file.

FM Demodulation is performed with the following rtl_fm sample rates:
* RS41: 15 kHz
* RS92: 12 kHz
* DFM09: 20 kHz
* M10: 22 kHz

![PER Performance 2019-03-02](http://rfhead.net/sondes/plots/per_20190302.png)

The traces for the error-corrected decoders (RS41, RS92, DFM), exhibit the typical 'cliff' response, with the PER going from 1.0 to 0 in only a few dB.

A few things to look into:
* This is a plot of *packet*-error-rate. Can we get a measure of the number of bits in a 'packet' (whatever that may be for each sonde; it may mean multiple frames) to be able to estimate a *bit*-error-rate? How does this compare to theoretical FSK modem performance?
* Why is ths RS92 'cliff' offset from the others? Are the packets bigger? Is there something sub-optimal in the demodulation chain?
* Further investigation into what the rtl_fm samples rates mean for pre-FM-demod filter bandwidth is required, to ensure they are set optimally.
* Double and triple-checking of the Eb/N0 calculations in `generate_lowsnr.py`, to be sure we're generating our low-Eb/N0 samples correctly.