LDLIBS = -lm

PROGRAMS := fsk_demod

all: $(PROGRAMS)

fsk_demod: fsk_demod.o fsk.o modem_stats.o kiss_fftr.o kiss_fft.o

kiss_fft.o: kiss_fft.c _kiss_fft_guts.h kiss_fft.h

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o) fsk.o modem_stats.o kiss_fftr.o kiss_fft.o
