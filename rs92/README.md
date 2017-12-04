
## Radiosonde RS92

Tools for decoding RS92-SGP and RS92-AGP radiosonde signals.

### Files

* `nav_gps_vel.c` - include-file for `rs92gps.c`, `rs92gps_2dfix.c`, `rs92ecc.c`, `rs92agp.c`;
  `RS/ecc/bch_ecc.c`

* `rs92gps.c` - RS92-SGP decoder (includes `nav_gps_vel.c`)

  #### Compile
  `gcc rs92gps.c -lm -o rs92gps`

  #### Usage
  `./rs92gps [options] <file>` <br />
  * `file`: <br />
    1.1 `<audio.wav>`: FM-demodulated signal, recorded as wav audio file <br />
    2.1 `--rawin1 <raw_data>`: raw data file created with option `-r` <br />
    2.2 `--rawin2 <digitalsondeXX.txt>`: SM raw data
  * `options`: <br />
      `-i`: invert signal/polarity <br />
      `-r`: output raw data <br />
      `-a <almanacSEM>`: use SEM almanac (GPS satellites, orbital data)
                         (cf. https://celestrak.com/GPS/almanac/SEM/) <br />
      `-e <ephemperisRinex>`: use RINEX ephemerides (GPS satellites, orbital data)
                              (e.g. YYYY=2017: ftp://cddis.gsfc.nasa.gov/gnss/data/daily/2017/brdc/,
                               brdcDDD0.YYn.Z, YY - year, DDD - day) <br />
     `-v`: additional data/info <br />
     `--vel`: output velocity (vH: horizontal, D: direction/heading, vV: vertical) <br />
     `--crc`: output only frames with valid GPS-CRC <br />

  `./rs92gps -h`: list more options

  #### Examples
  The rs92-radiosonde transmits pseudorange GPS data and GPS time.
  To calculate its position, orbital data of the GPS satellites is needed.
  You can use either almanac data which is less accurate (but can be used +/- 3 days),
  or rinex ephemerides data (recommended) which is more accurate but should not be
  older than 2 hours.
  The recommended sample rate of the FM-demodulated signal is 48 kHz.
  The GPS-altitude is above ellipsoid (in europe, subtract 40-50m geoid height).
  * `./rs92gps -r 2015101_14Z.wav > raw.txt`  (raw output into `raw.txt`) <br />
    `./rs92gps -v -e brdc3050.15n --rawin1 raw.txt`
  * `./rs92gps -v -e brdc3050.15n 2015101_14Z.wav`
  * `./rs92gps -v --vel2 -e brdc3050.15n 2015101_14Z.wav | tee log.txt` (console output and into file `log.txt`)

  The FSK-demodulation is kept very simple. If the signal quality is low, a lowpass filter is recommended, e.g.
  (using `sox`)
  * `sox 2015101_14Z.wav -t wav - lowpass 2600 2>/dev/null | ./rs92gps -v -e brdc3050.15n`

  You can redirect live audio stream to the decoder via `sox`, e.g.
  * `sox -t oss /dev/dsp -t wav - lowpass 2600 2>/dev/null | ./rs92gps -v --vel -e brdc3050.15n`
  * `sox -t oss /dev/dsp -t wav - lowpass 2600 2>/dev/null | stdbuf -oL ./rs92gps -v --vel2 -e brdc3050.15n | tee log.txt` <br />

  If the signal is inverted
  (depends on sdr-software and/or audio-card/settings), try option `-i`.

* `rs92gps_2dfix.c` - test decoder for 2d-fix (if only 3 satellites are available). If the position altitude is known/given,
  a position can be calculated with 3 satellites.

  #### Compile
  `gcc rs92gps_2dfix.c -lm -o rs92gps_2dfix`

  #### Usage
  Same as `rs92gps`.
  Additional option `--2dalt <alt>`: <br />
    `<alt>` is the (estimated) altitude of the radiosonde in meters above ellipsoid.
    Default (without `--2dalt <alt>`) is 0m.

<!-- (commit 342) Reed-Solomon error correction (uses fec-lib by KA9Q)
  * ka9q-fec (fec-3.0.1): <br />
      `gcc -c init_rs_char.c` <br />
      `gcc -c decode_rs_char.c` <br />
  * `gcc init_rs_char.o decode_rs_char.o rs92ecc.c -lm -o rs92ecc` <br />
    (includes also `fec.h` from fec-3.0.1)
  Will be replaced by Reed-Solomon-Decoder in `RS/ecc/bch_ecc.c` (cf. `RS/rs41/rs41ecc.c`).
-->
* `rs92ecc.c` - RS92-SGP decoder with Reed-Solomon error correction (includes `nav_gps_vel.c`, `bch_ecc.c`)

  #### Compile
  * `gcc rs92ecc.c -lm -o rs92ecc` &nbsp;&nbsp; (copy `RS/ecc/bch_ecc.c`)

  #### Usage
  Same as `rs92gps`.
  Additional option `--ecc`.

* `rs92agp.c` - RS92-AGP, RS92-BGP decoder

* `rs92.txt` - infos

* `pos2kml.pl`, `pos2gpx.pl`, `pos2nmea.pl` <br />
  perl scripts for kml-, gpx-, or nmea-output, resp.
  #### Usage/Example
  `./rs92ecc --ecc --crc -v --vel2 -e brdc3050.15n 2015101_14Z.wav | ./pos2nmea.pl` <br />
  `stderr`: frame output <br />
  `stdout`: NMEA output <br />
  Only NMEA:
  `./rs92ecc --ecc --crc -v --vel2 -e brdc3050.15n 2015101_14Z.wav | ./pos2nmea.pl 2>/dev/null`


* `gps_navdata.c` - test tool/example, compares orbital data. Includes `nav_gps.c` and
  compares `almanac.sem.week0843.061440.txt`, `brdc2910.15n`, `nga18670.Z`.

<!-- * `rs92-almanac_gps_outage.jpg` -->


