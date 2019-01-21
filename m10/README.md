
## Radiosonde M10

#### Compile
  `g++ -O M10.cpp M10Decoder.cpp M10GeneralParser.cpp M10GTopParser.cpp M10PtuParser.cpp -o m10`

#### Usage
`./m10 [options] filename`<br />
  * filename needs to be in wav format and blank or - for stdin<br />
  * `options`:<br />
       `-v, --verbose`: Display even when CRC is wrong<br />
       `-R`: Show result at the end decoded/total<br />
       `-b`: Try alternative method after main method if it failed, recommended<br />
       `--ch2`: Decode the second channel<br />
       <br />

#### Examples
  Running from file :
  * `./m10 -b -R 20181227.wav`
  
  Running with sox on live audio
  * `sox -t oss /dev/dsp -t wav - 2>/dev/null | ./m10 -b`
  
  It can also run on windows, use Cygwin Terminal if you want to use sox.
  
<br />
This software is a C++ adaptation of https://github.com/rs1729/RS/blob/master/m10 with improvement to the decoding performances.
