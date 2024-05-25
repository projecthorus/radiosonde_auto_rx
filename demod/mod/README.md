
## Radiosonde decoders

alternative decoders using cross-correlation for better header-synchronization

#### Files

  * `demod_mod.c`, `demod_mod.h`, <br />
    `rs41mod.c`, `rs92mod.c`, `dfm09mod.c`, `m10mod.c`, `lms6Xmod.c`, `meisei100mod.c`, <br />
    `bch_ecc_mod.c`, `bch_ecc_mod.h`

#### Compile
  `gcc -c demod_mod.c` <br />
  `gcc -c bch_ecc_mod.c` <br />
  `gcc rs41mod.c demod_mod.o bch_ecc_mod.o -lm -o rs41mod` <br />
  `gcc dfm09mod.c demod_mod.o -lm -o dfm09mod` <br />
  `gcc m10mod.c demod_mod.o -lm -o m10mod` <br />
  `gcc lms6Xmod.c demod_mod.o bch_ecc_mod.o -lm -o lms6Xmod` <br />
  `gcc meisei100mod.c demod_mod.o bch_ecc_mod.o -lm -o meisei100mod` <br />
  `gcc rs92mod.c demod_mod.o bch_ecc_mod.o -lm -o rs92mod` (needs `RS/rs92/nav_gps_vel.c`)

#### Usage/Examples
  `./rs41mod --ecc2 -vx --ptu <audio.wav>` <br />
  `./dfm09mod --ecc -v --ptu <audio.wav>` (add `-i` for dfm06; or use `--auto`) <br />
  `./m10mod --dc -vv --ptu -c <audio.wav>` <br />
  `./lms6Xmod --vit --ecc -v <audio.wav>` <br />

  IQ data:<br />
  If the IQ data is downsampled and centered (IF band), use <br />
  `./rs41mod --iq2 <iq_data.wav>` <br />
  or with lowpass filter <br />
  `./rs41mod --iq2 --lp <iq_data.wav>` <br />
  For baseband IQ data, use
  `./rs41mod --IQ <fq> <iq_data.wav>` <br />
  where `<fq>` is the relative frequency in `-0.5 .. 0.5`;
  e.g. if the receiver is tuned to 403MHz and the (complex) sample rate is 2MHz,
  a signal at 402.5MHz would be -0.5MHz off, i.e. `<fq> = -0.5/2 = -0.25`. <br />
  For IQ data (i.e. 2 channels) it is possible to read raw data (without wav header): <br />
  `./rs41mod --IQ <fq> - <sr> <bs> <iq_data.raw>` <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `<sr>`: sample rate <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `<bs>=8,16,32`: bits per (real) sample (u8, s16 or f32)

#### Remarks
  FM-demodulation is sensitive to noise at higher frequencies. A narrow low-pass filter is needed before demodulation.
  For weak signals and higher modulation indices IQ-decoding is usually better.
  <br />

  DFM:<br />
  The high modulation index has advantages in IQ-decoding. <br />
  `--ecc2` uses soft decision for 2-error words. If weak signals frequently produce errors, it is likely that
  more than 2 errors occur in a received word. Since there is no additional frame protection (e.g. CRC), the
  frames will not be decoded reliably in weak conditions. The `--dist` option has a thredshold for the number
  of errors per packet.
  <br />

  LMS6-403:<br />
  `lms6Xmod_soft.c` (testing) provides a soft viterbi decoding option `--vit2`;
  IQ-decoding is recommended for soft decoding (noisy/spikey FM-signals don't always help soft decision).
  The difference between hard and soft viterbi becomes only apparent at lower SNR. The inner convolutional
  code does most of the error correction. The concatenated outer Reed-Solomon code kicks in only at low SNR.

  soft input:<br />
  Option `--softin` expects float32 symbols as input, with `s>0` corresponding to `bit=1`.<br />
  (remark/caution: often soft bits are defined as `bit=0 -> s=+1` and `bit=1 -> s=-1` such that the identity element `0`
  for addition mod 2 corresponds to the identity element `+1` for multiplication.)


