CFLAGS += -Ofast
LDLIBS = -lm

PROGRAMS := c50iq

all: $(PROGRAMS)

c50iq: c50iq.o

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o)
