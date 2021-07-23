#!/bin/bash
#
# Auto Sonde Decoder clean script.
#

# TODO: Convert this to a makefile! Any takers?

# rs_detect.
echo "Cleaning dft_detect"
cd ../scan/
rm dft_detect

echo "Cleaning RS92/RS41/DFM/LMS6/iMS Demodulators"


# New demodulators
cd ../demod/mod/

rm *.o
rm rs41mod
rm dfm09mod
rm rs92mod
rm lms6mod
rm lms6Xmod
rm meisei100mod
rm m10mod
rm mXXmod

# LMS6-1680 Decoder
echo "Cleaning LMS6-1680 Demodulator."
cd ../../mk2a/

rm mk2mod

echo "Cleaning iMet Demodulator."
cd ../imet/

rm imet1rs_dft


echo "Cleaning fsk_demod"
cd ../utils/

rm fsk_demod


echo "Removing binaries in the auto_rx directory."
cd ../auto_rx/
rm dft_detect
rm fsk_demod
rm imet1rs_dft
rm mk2a_lms1680
rm mk2mod
rm rs41mod
rm rs92mod
rm dfm09mod
rm m10mod
rm mXXmod
rm lms6Xmod
rm meisei100mod


echo "Done!"
