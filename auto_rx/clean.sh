#!/bin/bash
#
# Auto Sonde Decoder clean script.
#

# Clean all binaries
echo "Cleaning all binaries."
make -C .. clean


echo "Removing binaries in the auto_rx directory."
cd ../auto_rx/
rm dft_detect
rm fsk_demod
rm imet4iq
rm mk2a1680mod
rm rs41mod
rm rs92mod
rm dfm09mod
rm m10mod
rm m20mod
rm lms6Xmod
rm meisei100mod
rm mp3h1mod
rm imet54mod
rm mts01mod
rm iq_dec


echo "Done!"
