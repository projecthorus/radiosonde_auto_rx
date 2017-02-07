
## Radiosonde M10

Tools for decoding M10 radiosonde signals.

### Files

* `m10x.c` - M10 decoder

  ##### Compile
  `gcc m10x.c -lm -o m10x`

  ##### Usage
  `./m10x [options] <audio.wav>` <br />
  * `<audio.wav>`: FM-demodulated signal, recorded as wav audio file <br />
  * `options`: <br />
     `-r`: output raw data <br />
     `-v`, `-vv`: additional data/info (velocities, SN, checksum) <br />
     `-c`: colored output <br />


  ##### Examples
  * `./m10x -v 20150701_402MHz.wav` <br />
    `./m10x -vv -c 20150701_402MHz.wav` <br />
    `./m10x -r  -c 20150701_402MHz.wav` <br />
    `sox 20150701_402MHz.wav -t wav - lowpass 6000 2>/dev/null | ./m10x -vv -c` <br />

 #####
   <br />


* `pilotsonde/m12.c` - Pilotsonde

  ##### Compile
  `gcc m12.c -lm -o m12`

