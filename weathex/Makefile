CFLAGS += -Ofast
LDLIBS = -lm

PROGRAMS := weathex301d

all: $(PROGRAMS)

weathex301d: weathex301d.o

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o)
