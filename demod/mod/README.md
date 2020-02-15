
## Radiosonde decoders

alternative decoders using cross-correlation for better header-synchronization

#### Files

  * `demod_mod.c`, `demod_mod.h`, <br />
    `rs41mod.c`, `rs92mod.c`, `dfm09mod.c`, `m10mod.c`, `lms6mod.c`, `lms6Xmod.c`, `meisei100mod.c`, <br />
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
  `./rs41mod --ecc2 --crc -vx --ptu <audio.wav>` <br />
  `./dfm09mod --ecc -v --ptu <audio.wav>` (add `-i` for dfm06)<br />
  `./m10mod --dc -vv --ptu -c <audio.wav>` <br />
  `./lms6Xmod --vit --ecc -v <audio.wav>` <br />


