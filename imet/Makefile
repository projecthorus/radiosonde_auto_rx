CFLAGS += -Ofast
LDLIBS = -lm

PROGRAMS := imet1rs_dft imet4iq

all: $(PROGRAMS)

imet1rs_dft: imet1rs_dft.o

imet4iq: imet4iq.o

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o)
