CFLAGS = -O3 -Wall -Wno-unused-variable -DVER_JSN_STR=\"$(AUTO_RX_VERSION)\" -DNOC34C50
LDLIBS = -lm

PROGRAMS := dft_detect

all: $(PROGRAMS)

dft_detect: dft_detect.o

dft_detect.o : CFLAGS += -Ofast

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o)
