AUTO_RX_VERSION := $(shell PYTHONPATH=./auto_rx python3 -m autorx.version 2>/dev/null || python -m autorx.version)

# Uncomment to use clang as a compiler.
#CC = clang
#export CC

CFLAGS = -O3 -w -Wno-unused-variable -DVER_JSN_STR=\"$(AUTO_RX_VERSION)\"
export CFLAGS

SUBDIRS := \
	demod/mod \
	imet \
	mk2a \
	scan \
	utils \
	weathex \

all: $(SUBDIRS)

clean: $(SUBDIRS)

$(SUBDIRS):
	make -C $@ $(MAKECMDGOALS)

.PHONY: all clean $(SUBDIRS)
