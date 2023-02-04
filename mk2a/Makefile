# Makefile for scan programs

LDLIBS = -lm

PROGRAMS := mk2a_lms1680 mk2a1680mod

all: $(PROGRAMS)

mk2a_lms1680: mk2a_lms1680.o

mk2a1680mod: mk2a1680mod.o

mk2a1680mod.o: CFLAGS += -Ofast

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o)
