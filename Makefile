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
