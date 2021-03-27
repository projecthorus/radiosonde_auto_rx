# Makefile for imet programs

# Auto_RX version number - needs to match the contents of autorx/__init__.py
# This can probably be done automatically.
#AUTO_RX_VERSION="\"1.4.1-beta8\""
AUTO_RX_VERSION := $(shell PYTHONPATH=../auto_rx python -m autorx.version)

CFLAGS = -Ofast -Wall -Wno-unused-variable -DVER_JSN_STR=\"$(AUTO_RX_VERSION)\"
LDLIBS = -lm

PROGRAMS := imet1rs_dft

all: $(PROGRAMS)

imet1rs_dft: imet1rs_dft.o

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o)
