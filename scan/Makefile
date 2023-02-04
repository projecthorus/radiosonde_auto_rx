CFLAGS = -O3 -w -Wno-unused-variable -DNOC34C50 -DNOIMET1AB
LDLIBS = -lm

PROGRAMS := dft_detect

all: $(PROGRAMS)

dft_detect: dft_detect.o

dft_detect.o : CFLAGS += -Ofast

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o)
