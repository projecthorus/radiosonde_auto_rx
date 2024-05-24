#!/bin/bash
#
# Auto Sonde Decoder build script.

set -e

# Get the auto-rx version.
AUTO_RX_VERSION="\"$(python3 -m autorx.version 2>/dev/null || python -m autorx.version)\""

echo "Building for radiosonde_auto_rx version: $AUTO_RX_VERSION"

cd $(dirname $0)

#cd ../utils/
# Build tsrc - this is only required for the test/test_demod.py script, so is not included in the standard build.
#gcc tsrc.c -o tsrc -lm -lsamplerate
# If running under OSX and using MacPorts, you may need to uncomment the following line to be able to find libsamplerate.
#gcc tsrc.c -o tsrc -lm -lsamplerate -I/opt/local/include -L/opt/local/lib
# ... and for homebrew users.
#gcc tsrc.c -o tsrc -lm -lsamplerate -I/opt/homebrew/include -L/opt/homebrew/lib

# Clean before build to ensure the auto_rx version is updated.
make -C .. clean all

# Copy all necessary files into this directory.
echo "Copying files into auto_rx directory."
cd ../auto_rx/
mv ../scan/dft_detect .
mv ../utils/fsk_demod .
mv ../imet/imet4iq .
mv ../mk2a/mk2a1680mod .
mv ../demod/mod/rs41mod .
mv ../demod/mod/dfm09mod .
mv ../demod/mod/m10mod .
mv ../demod/mod/m20mod .
mv ../demod/mod/rs92mod .
mv ../demod/mod/lms6Xmod .
mv ../demod/mod/meisei100mod .
mv ../demod/mod/imet54mod .
mv ../demod/mod/mp3h1mod .
mv ../demod/mod/mts01mod .
mv ../demod/mod/iq_dec .
mv ../weathex/weathex301d .

echo "Done!"
