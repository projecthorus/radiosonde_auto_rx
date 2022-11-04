LDLIBS = -lm

PROGRAMS := rs41mod dfm09mod rs92mod lms6mod lms6Xmod meisei100mod m10mod m20mod imet54mod mp3h1mod
SRC=$(PROGRAMS:=.c) bch_ecc_mod.c demod_mod.c

all: depend $(PROGRAMS)

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

demod_mod.o: CFLAGS += -Ofast

depend: .depend

.depend: $(SRC)
	$(CC) $(CFLAGS) -MM $^ > .depend

-include .depend

clean:
	$(RM) $(PROGRAMS) $(SRC:=.o)

.PHONY: all clean depend
