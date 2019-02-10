
/*
    GF(2^4) - RS(15,11)
    (no bit-packing, i.e. 1 nibble <-> 1 byte)

a)
  ka9q-fec:
    gcc -c init_rs_char.c
    gcc -c encode_rs_char.c
    gcc -c decode_rs_char.c

  gcc -DKA9Q ecc-rs_gf16.c init_rs_char.o encode_rs_char.o decode_rs_char.o -o ecc-rs15ccsds

b)
  gcc ecc-rs_gf16.c -o ecc-rs15ccsds

*/


#include <stdio.h>
#include <string.h>

#ifdef KA9Q
    #include "fec.h"
#endif


typedef unsigned char ui8_t;
typedef unsigned int  ui32_t;

#include "bch_ecc.c"


#define BFSIZE 512
#define rs_N 15
#define rs_R 4
#define rs_K (rs_N-rs_R)

ui8_t data[BFSIZE];

ui8_t cw[rs_N+1];  // cw[MAX_DEG+1]; // fixed in encode() ...
ui8_t par[rs_N], msg[rs_N];
int errs;
ui8_t err_pos[rs_R], err_val[rs_R];

#ifdef KA9Q
    void *rs;
    ui8_t codeword[rs_N];
    int errors, errpos[rs_R];
#endif


int main(int argc, char *argv[]) {
    int i, l;

    int len1, len2;
    char *str1 = NULL,
         *str2 = NULL;


    rs_init_RS15ccsds(); // 0x13: X^4 + X + 1, t=2, b=6

#ifdef KA9Q
    //rs = init_rs_char( 4, 0x13, 0, 1, 6, 0); // RS16_0
    rs = init_rs_char( 4, 0x13, 6, 1, 4, 0); // RS16ccsds
#endif

    if (argv[0] == NULL) return -1;

    for (i = 0; i < BFSIZE; i++) data[i] = 0;
    for (i = 0; i < rs_N; i++) cw[i] = 0;
#ifdef KA9Q
    for (i = 0; i < rs_N; i++) codeword[i] = 0;
#endif


    str1 = argv[1];

    len1 = strlen(str1);
    if (len1 > BFSIZE) return -1;

    for (i = 0; i < len1; i++) {
        l = sscanf(str1+i, "%1hhx", data+i);  if (l < 1) {/*len1 = i;*/}
    }
    // 1byte/nibble


    for (i = 0; i < rs_K; i++) cw[rs_R+i] = data[i]; // codeword[rs_N-1-i] = cw[i];

    for (i = 0; i < rs_N; i++) msg[i] = 0;
    for (i = 0; i < rs_N; i++) par[i] = 0;
    for (i = 0; i < rs_K; i++) msg[i] = data[rs_K-1-i];


//
// GF(16) %02X -> %1X  (1 nibble / 1 byte)
// printf("%1X", cw[i]); // dbg: printf("%02X", cw[i]);
// printf("%1X", cw[i]&0xF); // dbg: printf("%02X", cw[i]);
//


    printf("\n");

    if (argv[2]) {

        str2 = argv[2];
        len2 = strlen(str2);
        if (len2 > rs_R) len2 = rs_R;

        for (i = 0; i < len2; i++) {
            l = sscanf(str2+i, "%1hhx", par+i);  if (l < 1) {/*len2 = i;*/}
        }
        while (i < rs_R) par[i++] = 0;

        for (i = 0; i < rs_N; i++) cw[i] = 0;
        for (i = 0; i < rs_R; i++) cw[i] = par[i];
        for (i = 0; i < len1; i++) cw[rs_R+i] = msg[rs_K-1-i];


#ifdef KA9Q
        for (i = 0; i < rs_N; i++) codeword[rs_N-1-i] = cw[i];
#endif

        printf("(received)\n");
        printf("msg: ");
        for (i = rs_R; i < rs_N; i++) printf("%1X", cw[i]); // dbg: printf("%02X", cw[i]);
        printf("\n");
        printf("par: ");
        for (i = 0; i < rs_R; i++) printf("%1X", cw[i]);
        printf("\n");
        printf("msg+par:\n");
        for (i = 0; i < rs_N; i++) printf("%1X", cw[i]);
        printf("\n");

        printf("\n");


        printf("cw\n");
        errs = rs_decode(cw, err_pos, err_val);
        printf("errs: %d\n", errs);
        if (errs > 0) {
            printf("pos: ");
            for (i = 0; i < errs; i++) printf(" %d", err_pos[i]);
            printf("\n");
        }
        for (i = 0; i < rs_N; i++) printf("%1X", cw[i]); printf("\n");


#ifdef KA9Q
        printf("\n");

        printf("codeword\n");
        errors = decode_rs_char(rs, codeword, errpos, 0);
        printf("errors: %d\n", errors);
        if (errors > 0) {
            printf("pos: ");
            for (i = 0; i < errors; i++) printf(" %d", errpos[i]);
            printf("\n");
        }
        for (i = 0; i < rs_N; i++) printf("%1x", codeword[i]); printf("\n");
#endif

    } else {

        printf("msg: ");
        for (i = rs_R; i < rs_N; i++) printf("%1X", cw[i]); // dbg: printf("%02X", cw[i]);
        printf("\n");
        printf("\n");


        printf("cw\n");
        rs_encode(cw);
        printf("par: ");
        for (i = 0; i < rs_R; i++) printf("%1X", cw[i]); printf("\n");
        printf("cw-enc:\n");
        for (i = 0; i < rs_N; i++) {
            //if (i == rs_R) printf(" ");
            printf("%1X", cw[i]);
        }
        printf("\n");

        // check
        errs = rs_decode(cw, err_pos, err_val);
        if (errs) {
            printf("errs: %d\n", errs);
            printf("cw-dec:\n");
            for (i = 0; i < rs_N; i++) {
                //if (i == rs_R) printf(" ");
                printf("%1X", cw[i]);
            }
            printf("\n");
        }


#ifdef KA9Q
        printf("\n");

        printf("codeword\n");
        printf("message: ");
        for (i = 0; i < rs_K; i++) printf("%1x", msg[i]); printf("\n");
        encode_rs_char(rs, msg, par);
        printf("parity : ");
        for (i = 0; i < rs_R; i++) printf("%1x", par[i]); printf("\n");

        for (i = 0; i < rs_K; i++) codeword[i] = msg[i];
        for (i = 0; i < rs_R; i++) codeword[rs_K+i] = par[i];
        printf("codeword:\n");
        for (i = 0; i < rs_N; i++) printf("%1x", codeword[i]); printf("\n");
#endif

    }


    printf("\n");

    return 0;
}


/*

RS(15,11):
    codeword length 15
    message length 11
    parity length 4

./ecc-rs15 msg [par]
    msg: 11 nibbles
    par: 4 nibbles

ecc-rs15 input/output: nibbles
cw[]: 1 byte / 1 nibble


examples:

$ ./ecc-rs15ccsds 00000000001

msg: 00000000001

cw
par: 8281
cw-enc:
828100000000001

codeword
message: 10000000000
parity : 1828
codeword:
100000000001828

$ ./ecc-rs15ccsds 00000000001 8281

(received)
msg: 00000000001
par: 8281
msg+par:
828100000000001

cw
errs: 0
828100000000001

codeword
errors: 0
100000000001828

$ ./ecc-rs15ccsds 00000000001 8283

(received)
msg: 00000000001
par: 8283
msg+par:
828300000000001

cw
errs: 1
pos:  3
828100000000001

codeword
errors: 1
pos:  11
100000000001828

*/


