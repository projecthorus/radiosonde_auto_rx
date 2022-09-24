LDLIBS = -lm

PROGRAMS := fsk_demod

all: $(PROGRAMS)

fsk_demod: fsk_demod.o fsk.o modem_stats.o kiss_fftr.o kiss_fft.o

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o) fsk.o modem_stats.o kiss_fftr.o kiss_fft.o
