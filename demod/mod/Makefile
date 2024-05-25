LDLIBS = -lm

PROGRAMS := rs41mod dfm09mod rs92mod lms6Xmod meisei100mod m10mod m20mod imet54mod mp3h1mod mts01mod iq_dec

all: $(PROGRAMS)

rs41mod: rs41mod.o demod_mod.o bch_ecc_mod.o

dfm09mod: dfm09mod.o demod_mod.o

rs92mod: rs92mod.o demod_mod.o bch_ecc_mod.o

lms6Xmod: lms6Xmod.o demod_mod.o bch_ecc_mod.o

meisei100mod: meisei100mod.o demod_mod.o bch_ecc_mod.o

m10mod: m10mod.o demod_mod.o

m20mod: m20mod.o demod_mod.o

imet54mod: imet54mod.o demod_mod.o

mp3h1mod: mp3h1mod.o demod_mod.o

mts01mod: mts01mod.o demod_mod.o

bch_ecc_mod.o: bch_ecc_mod.h

demod_mod.o: CFLAGS += -Ofast
demod_mod.o: demod_mod.h

iq_dec: CFLAGS += -Ofast
iq_dec: iq_dec.o

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o) demod_mod.o bch_ecc_mod.o
