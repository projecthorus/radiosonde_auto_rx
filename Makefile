AUTO_RX_VERSION := $(shell PYTHONPATH=./auto_rx python -m autorx.version)

CFLAGS = -O3 -Wall -Wno-unused-variable -DVER_JSN_STR=\"$(AUTO_RX_VERSION)\"
export CFLAGS

SUBDIRS := \
	demod/mod \
	imet \
	mk2a \
	scan \
	utils \

all: $(SUBDIRS)

clean: $(SUBDIRS)

$(SUBDIRS):
	make -C $@ $(MAKECMDGOALS)

.PHONY: all clean $(SUBDIRS)
