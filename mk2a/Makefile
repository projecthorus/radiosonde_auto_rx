# Makefile for scan programs

# Auto_RX version number - needs to match the contents of autorx/__init__.py
# This can probably be done automatically.
#AUTO_RX_VERSION="\"1.4.1-beta8\""
AUTO_RX_VERSION := $(shell PYTHONPATH=../auto_rx python -m autorx.version)

CFLAGS = -O3 -Wall -Wno-unused-variable -DVER_JSN_STR=\"$(AUTO_RX_VERSION)\"
LDLIBS = -lm

PROGRAMS := mk2a_lms1680 mk2a1680mod

all: $(PROGRAMS)

mk2a_lms1680: mk2a_lms1680.o

mk2a1680mod: mk2a1680mod.o

mk2a1680mod.o: CFLAGS += -Ofast

clean:
	$(RM) $(PROGRAMS) $(PROGRAMS:=.o)
