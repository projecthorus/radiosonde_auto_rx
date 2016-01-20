/*
  ka9q-fec:
    gcc -c init_rs_char.c
    gcc -c encode_rs_char.c
    gcc -c decode_rs_char.c
  gcc init_rs_char.o encode_rs_char.o decode_rs_char.o ecc-rs_vaisala.c -o ecc-rs_vaisala
*/

#include <stdio.h>
#include <string.h>
#include "fec.h"


const unsigned char
  header_rs92sgp[] = { 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x10},
  //header_rs92bgp[], header_rs92bgpx[],
  header_rs41x[]   = { 0x86, 0x35, 0xf4, 0x40, 0x93, 0xdf, 0x1a, 0x60},
  header_rs41[]    = { 0x10, 0xB6, 0xCA, 0x11, 0x22, 0x96, 0x12, 0xF8};

typedef struct {
    int typ;
    int msglen;
    int msgpos;
    int parpos;
    int hdrlen;
    int frmlen;
} rscfg_t;

rscfg_t cfg, cfg0 = { 0, 0, 0, 0, 0, 0},
        cfg_rs92 = { 92,   240-6-24,  6, 240-24, 6, 240},
        cfg_rs41 = { 41, (320-56)/2, 56,      8, 8, 320};


#define BFSIZE 520
#define N 255
#define R 24
#define K (N-R)

void *rs;
unsigned char data[BFSIZE], frame[BFSIZE],
              codeword1[N], codeword2[N];
int errors1, errors2,
    errpos1[R], errpos2[R];


rscfg_t detect(unsigned char data[], int len) {
    int i;
    rscfg_t cfg;

    for (i = 0; i < cfg_rs92.hdrlen; i++) {
        if (data[i] != header_rs92sgp[i]) break;
    }
    if (i == cfg_rs92.hdrlen) return cfg_rs92;
/*
    for (i = 0; i < cfg_rs92.hdrlen; i++) {
        if (data[i] != header_rs92bgp[i]) break;
    }
    if (i == cfg_rs92.hdrlen) return cfg_rs92;
*/
    for (i = 0; i < cfg_rs41.hdrlen; i++) {
        if (data[i] != header_rs41[i]) break;
    }
    if (i < cfg_rs41.hdrlen) {
        for (i = 0; i < cfg_rs41.hdrlen; i++) {
            if (data[i] != header_rs41x[i]) break;
        }
    }
    if (i == cfg_rs41.hdrlen) {
        cfg = cfg_rs41;
        if (len > 518) len = 518;
        if (len > 320) {
            cfg.msglen = (len-56)/2;
            cfg.frmlen = len;
        }
    }
    else cfg = cfg0;

    return cfg;
}


int main(int argc, char *argv[]) {
    int i, len, l;
    char *str = NULL, strbuf[2*BFSIZE+1];
    FILE *fp = NULL;


    rs = init_rs_char( 8, 0x11d, 0, 1, R, 0);


    if (argc < 2) {
        fp = stdin;
        str = fgets(strbuf, 2*BFSIZE, fp);
    }
    else {
        str = argv[1];
    }

    while (str) {

        len = strlen(str)/2;
        if (len > BFSIZE) return -1;

        for (i = 0; i < BFSIZE; i++)  data[i] = 0;
        for (i = 0; i < N; i++) codeword1[i] = codeword2[i] = 0;

        for (i = 0; i < len; i++) {
            l = sscanf(str+2*i, "%2hhx", data+i);  if (l < 1) {/*len = i;*/}
        }

        cfg = detect(data, len);

        if ( cfg.typ == 92 ) {

            for (i = 0; i < cfg.msglen; i++) codeword1[K-1-i] = data[cfg.msgpos+i];
            for (i = 0; i < R; i++) codeword1[N-1-i] = data[cfg.parpos+i];

            errors1 = decode_rs_char(rs, codeword1, errpos1, 0);

            if (fp == NULL) {
                printf("RS%d\n", cfg.typ);
                printf("codeword\n");
                printf("errors: %d\n", errors1);
                if (errors1 > 0) {
                    printf("pos: ");
                    for (i = 0; i < errors1; i++) printf(" %d", errpos1[i]);
                    printf("\n");
                }
                for (i = 0; i < N; i++) printf("%02X", codeword1[i]); printf("\n");
                printf("frame:\n");
            }

            for (i = 0; i < cfg.hdrlen; i++) frame[i] = data[i];
            for (i = 0; i < cfg.msglen; i++) frame[cfg.msgpos+i] = codeword1[K-1-i];
            for (i = 0; i < R; i++)          frame[cfg.parpos+i] = codeword1[N-1-i];

            for (i = 0; i < cfg.frmlen; i++) printf("%02x", frame[i]); printf("\n");
        }
        else
          if ( cfg.typ == 41 ) {

            for (i = 0; i < cfg.msglen; i++) {
                codeword1[K-1-i] = data[cfg.msgpos+  2*i];
                codeword2[K-1-i] = data[cfg.msgpos+1+2*i];
            }

            for (i = 0; i < R; i++) {
                codeword1[N-1-i] = data[cfg.parpos+  i];
                codeword2[N-1-i] = data[cfg.parpos+R+i];
            }

            errors1 = decode_rs_char(rs, codeword1, errpos1, 0);
            errors2 = decode_rs_char(rs, codeword2, errpos2, 0);

            if (fp == NULL) {
                printf("RS%d\n", cfg.typ);
                printf("codeword1\n");
                printf("errors: %d\n", errors1);
                if (errors1 > 0) {
                    printf("pos: ");
                    for (i = 0; i < errors1; i++) printf(" %d", errpos1[i]);
                    printf("\n");
                }
                for (i = 0; i < N; i++) printf("%02X", codeword1[i]); printf("\n");

                printf("codeword2\n");
                printf("errors: %d\n", errors2);
                if (errors2 > 0) {
                    printf("pos: ");
                    for (i = 0; i < errors2; i++) printf(" %d", errpos2[i]);
                    printf("\n");
                }
                for (i = 0; i < N; i++) printf("%02X", codeword2[i]); printf("\n");
                printf("frame:\n");
            }

            for (i = 0; i < cfg.hdrlen; i++) frame[i] = data[i];
            for (i = 0; i < R; i++) {
                frame[cfg.parpos+  i] = codeword1[N-1-i];
                frame[cfg.parpos+R+i] = codeword2[N-1-i];
            }
            for (i = 0; i < cfg.msglen; i++) {
                frame[cfg.msgpos+  2*i] = codeword1[K-1-i];
                frame[cfg.msgpos+1+2*i] = codeword2[K-1-i];
            }

            for (i = 0; i < cfg.frmlen; i++) printf("%02x", frame[i]); printf("\n");
        }

        if (fp) str = fgets(strbuf, 2*BFSIZE, fp);
        else    str = NULL;
    }


    return 0;
}

