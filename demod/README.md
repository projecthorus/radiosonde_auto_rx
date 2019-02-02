
## Radiosonde decoders

alternative decoders using cross-correlation for better header-synchronization

#### Files

  * `demod_dft.c`, `demod_dft.h`, <br />
    `rs41dm_dft.c`, `rs92dm_dft.c`, `dfm09dm_dft.c`, `m10dm_dft.c`, `lms6dm_dft.c`, <br />
    `RS/ecc/bch_ecc.c`

#### Compile
  (copy `bch_ecc.c`) <br />
  `gcc -c demod_dft.c` <br />
  `gcc rs41dm_dft.c demod_dft.o -lm -o rs41dm_dft` <br />
  `gcc dfm09dm_dft.c demod_dft.o -lm -o dfm09dm_dft` <br />
  `gcc m10dm_dft.c demod_dft.o -lm -o m10dm_dft` <br />
  `gcc lms6dm_dft.c demod_dft.o -lm -o lms6dm_dft` <br />
  `gcc rs92dm_dft.c demod_dft.o -lm -o rs92dm_dft` (needs `RS/rs92/nav_gps_vel.c`)

#### Usage/Examples
  `./rs41dm_dft --ecc2 --crc -vx --ptu <audio.wav>` <br />
  `./dfm09dm_dft --ecc -v --ptu <audio.wav>` (add `-i` for dfm06)<br />
  `./m10dm_dft --dc -vv --ptu -c <audio.wav>` <br />
  `./lms6dm_dft --vit --ecc -v <audio.wav>` <br />


