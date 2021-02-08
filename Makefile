# Toplevel makefile to build all software

PROGRAMS := scan/dft_detect demod/mod/rs41mod demod/mod/dfm09mod demod/mod/rs92mod demod/mod/lms6mod demod/mod/lms6Xmod demod/mod/meisei100mod demod/mod/m10mod demod/mod/mXXmod demod/mod/imet54mod mk2a/mk2a_lms1680 imet/imet1rs_dft utils/fsk_demod

all:
	$(MAKE) -C demod/mod
	$(MAKE) -C imet
	$(MAKE) -C utils
	$(MAKE) -C scan
	$(MAKE) -C mk2a
	cp $(PROGRAMS) auto_rx/

.PHONY:
clean:
	$(MAKE) -C demod/mod clean
	$(MAKE) -C imet clean
	$(MAKE) -C utils clean
	$(MAKE) -C scan clean
	$(MAKE) -C mk2a clean
