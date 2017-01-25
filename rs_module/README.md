
##### separate modules

preliminary/test version (rs41, rs92)

compile:

```

gcc -c rs_datum.c
gcc -c rs_demod.c
gcc -c rs_bch_ecc.c

gcc -c rs_rs41.c
gcc -c rs_rs92.c


gcc -c rs_main41.c
gcc rs_main41.o rs_rs41.o rs_bch_ecc.o rs_demod.o rs_datum.o -lm -o rs41mod


gcc -c rs_main92.c
gcc rs_main92.o rs_rs92.o rs_bch_ecc.o rs_demod.o rs_datum.o -lm -o rs92mod

```


