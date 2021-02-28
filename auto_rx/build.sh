#!/bin/bash
#
# Auto Sonde Decoder build script.
#

# TODO: Convert this to a makefile! Any takers?

# Build rs_detect.
echo "Building dft_detect"
cd ../scan/
gcc dft_detect.c -lm -o dft_detect -DNOC34C50 -w

echo "Building RS92/RS41/DFM/LMS6/iMS/M10/M20 Demodulators"
#cd ../demod/
#gcc -c demod.c
#gcc -c demod_dft.c
#gcc dfm09dm_dft.c demod_dft.o -lm -o dfm09ecc

# New demodulators
cd ../demod/mod/
gcc -c demod_mod.c -w
gcc -c bch_ecc_mod.c -w
gcc rs41mod.c demod_mod.o bch_ecc_mod.o -lm -o rs41mod -w
gcc dfm09mod.c demod_mod.o -lm -o dfm09mod -w
gcc rs92mod.c demod_mod.o bch_ecc_mod.o -lm -o rs92mod -w
gcc lms6mod.c demod_mod.o bch_ecc_mod.o -lm -o lms6mod -w
gcc lms6Xmod.c demod_mod.o bch_ecc_mod.o -lm -o lms6Xmod -w
gcc meisei100mod.c demod_mod.o bch_ecc_mod.o -lm -o meisei100mod -w
gcc m10mod.c demod_mod.o -lm -o m10mod -w
gcc mXXmod.c demod_mod.o -lm -o mXXmod -w

# Build LMS6-1680 Decoder
echo "Building LMS6-1680 Demodulator."
cd ../../mk2a/
gcc mk2a_lms1680.c -lm -o mk2a_lms1680

# Build M10 decoder
#echo "Building M10 Demodulator."
#cd ../m10/
#g++ M10.cpp M10Decoder.cpp M10GeneralParser.cpp M10GtopParser.cpp M10TrimbleParser.cpp AudioFile.cpp -lm -o m10 -std=c++11

echo "Building iMet Demodulator."
cd ../imet/
gcc imet1rs_dft.c -lm -o imet1rs_dft


echo "Building fsk-demod utils from codec2"
cd ../utils/
# This produces a static build of fsk_demod
gcc fsk_demod.c fsk.c modem_stats.c kiss_fftr.c kiss_fft.c -lm -o fsk_demod
# Build tsrc - this is only required for the test/test_demod.py script, so is not included in the standard build.
#gcc tsrc.c -o tsrc -lm -lsamplerate
# If running under OSX and using MacPorts, you may need to uncomment the following line to be able to find libsamplerate.
#gcc tsrc.c -o tsrc -lm -lsamplerate -I/opt/local/include -L/opt/local/lib


# Copy all necessary files into this directory.
echo "Copying files into auto_rx directory."
cd ../auto_rx/
cp ../scan/dft_detect .
cp ../utils/fsk_demod .
cp ../imet/imet1rs_dft .
cp ../mk2a/mk2a_lms1680 .

cp ../demod/mod/rs41mod .
cp ../demod/mod/dfm09mod .
cp ../demod/mod/m10mod .
cp ../demod/mod/mXXmod .
cp ../demod/mod/rs92mod .
cp ../demod/mod/lms6Xmod .
cp ../demod/mod/meisei100mod .

echo "Done!"
