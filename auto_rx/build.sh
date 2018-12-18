#!/bin/bash
#
# Auto Sonde Decoder build script.
#

# Build rs_detect.
echo "Building rs_detect"
cd ../scan/
gcc rs_detect.c -lm -o rs_detect

echo "Building RS92/RS41/DFM Demodulators"
cd ../demod/
gcc -c demod.c
gcc -c demod_dft.c
gcc rs92dm_dft.c demod_dft.o -lm -o rs92ecc -I../ecc/ -I../rs92
gcc rs41dm_dft.c demod_dft.o -lm -o rs41ecc -I../ecc/ -I../rs41
gcc dfm09dm_dft.c demod_dft.o -lm -o dfm09ecc -I../ecc/ -I../dfm
# Build M10 decoder
cd ../m10/
g++ M10.cpp M10Decoder.cpp M10GeneralParser.cpp M10GTopParser.cpp M10PtuParser.cpp -lm -o m10


# Copy all necessary files into this directory.
echo "Copying files into auto_rx directory."
cd ../auto_rx/
cp ../scan/rs_detect .
cp ../demod/rs92ecc .
cp ../demod/rs41ecc .
cp ../demod/dfm09ecc .
cp ../m10/m10 .

echo "Done!"
