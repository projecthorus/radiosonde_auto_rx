CFLAGS = -O3 -w -Wno-unused-variable
LDLIBS = -lm

PROGRAMS := rd94rd41drop

all: $(PROGRAMS)

rd94rd41drop: rd94rd41drop.o

rd94rd41drop.o : CFLAGS += -O3

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o)
