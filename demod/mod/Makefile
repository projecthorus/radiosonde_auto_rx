# Makefile for demod programs

# Auto_RX version number - needs to match the contents of autorx/__init__.py
# This can probably be done automatically.
#AUTO_RX_VERSION="\"1.4.1-beta8\""
AUTO_RX_VERSION := $(shell PYTHONPATH=../../auto_rx python -m autorx.version)

CFLAGS = -O3 -Wall -Wno-unused-variable -DVER_JSN_STR=\"$(AUTO_RX_VERSION)\"
LDLIBS = -lm

PROGRAMS := rs41mod dfm09mod rs92mod lms6mod lms6Xmod meisei100mod m10mod m20mod imet54mod

all: $(PROGRAMS)

rs41mod: rs41mod.o demod_mod.o bch_ecc_mod.o

dfm09mod: dfm09mod.o demod_mod.o

rs92mod: rs92mod.o demod_mod.o bch_ecc_mod.o

lms6mod: lms6mod.o demod_mod.o bch_ecc_mod.o

lms6Xmod: lms6Xmod.o demod_mod.o bch_ecc_mod.o

meisei100mod: meisei100mod.o demod_mod.o bch_ecc_mod.o

m10mod: m10mod.o demod_mod.o

m20mod: m20mod.o demod_mod.o

imet54mod: imet54mod.o demod_mod.o

mp3h1mod: mp3h1mod.o demod_mod.o

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o) demod_mod.o bch_ecc_mod.o
