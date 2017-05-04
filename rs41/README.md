
## Radiosonde RS41

#### Files

  * `rs41ecc.c`, `RS/ecc/bch_ecc.c`

#### Compile
  (copy `bch_ecc.c`) <br />
  `gcc rs41ecc.c -lm -o rs41ecc`

#### Usage
  `./rs41ecc [options] <audio.wav>` <br />
  * `<audio.wav>`: FM-demodulated signal, recorded as wav audio file
  * `options`: <br />
      `-i`: invert signal/polarity <br />
      `-b`: alternative demod <br />
      `-r`: output raw data <br />
     `-v, -vx, -vv`: additional data/aux/info <br />
     `--ecc`: Reed-Solomon error correction <br />
     `--crc`: CRC blocks: 0-OK, 1-NO <br />
     `--sat`: additional Sat data <br />

  `./rs41gps -h`: list more options

#### Examples
  FSK-demodulation is kept very simple. If the signal quality is low and (default) zero-crossing-demod is used,
  a lowpass filter is recommended:
  * `sox 20170116_12Z.wav -t wav - lowpass 2800 2>/dev/null | ./rs41ecc --ecc --crc -vx`

  If timing/sync is not an issue, integrating the bit-samples (option `-b`) is better for error correction:
  * `./rs41ecc -b --ecc --crc -vx 20170116_12Z.wav`

  If the signal is inverted
  (depends on sdr-software and/or audio-card/settings), try option `-i`.

  (cf. /RS/rs92)

