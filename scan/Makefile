# Auto-detect single-precision FFTW3 (fftw3f). When available, dft_detect is
# built with FFTW backing its core FFT for a meaningful speedup in the
# correlation inner loop. When not available, the build falls back to the
# in-tree naive radix-2 FFT and behavior is unchanged. No install-doc
# changes are required: users who already have libfftw3-dev installed (e.g.
# the Docker build, anyone with KA9Q-radio) automatically get the faster
# path, and everyone else builds as before.
HAVE_FFTW3F := $(shell pkg-config --exists fftw3f 2>/dev/null && echo 1)

ifeq ($(HAVE_FFTW3F),1)
    FFTW_CFLAGS := -DHAVE_FFTW3 $(shell pkg-config --cflags fftw3f)
    FFTW_LDLIBS := $(shell pkg-config --libs fftw3f)
else
    FFTW_CFLAGS :=
    FFTW_LDLIBS :=
endif

CFLAGS = -O3 -w -Wno-unused-variable -DNOC34C50 -DNOIMET1AB $(FFTW_CFLAGS)
LDLIBS = -lm $(FFTW_LDLIBS)

PROGRAMS := dft_detect

all: $(PROGRAMS)

dft_detect: dft_detect.o

dft_detect.o : CFLAGS += -Ofast

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o)
