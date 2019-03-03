#!/bin/bash
#
# Auto Sonde Decoder build script.
#

# Build rs_detect.
echo "Building dft_detect"
cd ../scan/
gcc dft_detect.c -lm -o dft_detect -DNOC34C50

echo "Building RS92/RS41/DFM Demodulators"
cd ../demod/
gcc -c demod.c
gcc -c demod_dft.c
gcc rs92dm_dft.c demod_dft.o -lm -o rs92ecc -I../ecc/ -I../rs92
gcc rs41dm_dft.c demod_dft.o -lm -o rs41ecc -I../ecc/ -I../rs41 -w
gcc dfm09dm_dft.c demod_dft.o -lm -o dfm09ecc -I../ecc/ -I../dfm
# Build M10 decoder
echo "Building M10 Demodulator."
cd ../m10/
g++ M10.cpp M10Decoder.cpp M10GeneralParser.cpp M10GtopParser.cpp M10TrimbleParser.cpp AudioFile.cpp -lm -o m10 -std=c++11

echo "Building fsk-demod utils from codec2"
cd ../utils/
# This produces a static build of fsk_demod
gcc fsk_demod.c fsk.c modem_stats.c kiss_fftr.c kiss_fft.c -lm -o fsk_demod
# Build tsrc
gcc tsrc.c -o tsrc -lm -lsamplerate
# If running under OSX, you may need to uncomment the following line to be able to find libsamplerate.
#gcc tsrc.c -o tsrc -lm -lsamplerate -I/opt/local/include -L/opt/local/lib


# Copy all necessary files into this directory.
echo "Copying files into auto_rx directory."
cd ../auto_rx/
cp ../scan/dft_detect .
cp ../demod/rs92ecc .
cp ../demod/rs41ecc .
cp ../demod/dfm09ecc .
cp ../m10/m10 .
cp ../utils/fsk_demod .
cp ../utils/tsrc .

echo "Done!"
