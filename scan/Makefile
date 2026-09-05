CFLAGS = -O3 -w -Wno-unused-variable
LDLIBS = -lm

PKG_CONFIG ?= pkg-config
HAVE_FFTW3F := $(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && \
	$(PKG_CONFIG) --exists fftw3f && echo yes)

ifeq ($(HAVE_FFTW3F),yes)
CFLAGS += -DHAVE_FFTW3 $(shell $(PKG_CONFIG) --cflags fftw3f)
LDLIBS += $(shell $(PKG_CONFIG) --libs fftw3f)
$(info Building dft_detect with FFTW3 support)
else
$(info Building dft_detect with built-in FFT)
endif

PROGRAMS := dft_detect

all: $(PROGRAMS)

dft_detect: dft_detect.o

dft_detect.o: CFLAGS += -Ofast

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o)