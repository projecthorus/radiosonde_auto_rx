/*
  ka9q-fec:
    gcc -c init_rs_char.c
    gcc -c decode_rs_char.c
  gcc init_rs_char.o decode_rs_char.o ecc-rs_vaisala.c -o ecc-rs_vaisala
*/

#include <stdio.h>
#include <string.h>
#include "fec.h"

typedef unsigned char ui8_t;
typedef unsigned int  ui32_t;
#include "bch_ecc.c"


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

// ka9q-fec301: p(x) = p[0]x^(N-1) + ... + p[N-2]x + p[N-1]
// fec.h
void *rs;
unsigned char data[BFSIZE], frame[BFSIZE],
              codeword1[N], codeword2[N];
int errors1, errors2,
    errpos1[R], errpos2[R];

// bch_ecc.c: cw[i] = codeword[N-1-i]
ui8_t cw1[N], cw2[N];
int errs1, errs2;
ui8_t err_pos1[R], err_pos2[R],
      err_val1[R], err_val2[R];


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


    rs = init_rs_char( 8, 0x11d, 0, 1, R, 0); // fec.c

    rs_init_RS255(); // bch_ecc.c

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

            for (i = 0; i < N; i++) cw1[i] = codeword1[N-1-i];

            errors1 = decode_rs_char(rs, codeword1, errpos1, 0);

            errs1 = rs_decode(cw1, err_pos1, err_val1);

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

            for (i = 0; i < N; i++) {
                if (cw1[i] != codeword1[N-1-i]) {
                    printf("diff[%3d]:  cw: %02x  codeword: %02x\n", i, cw1[i], codeword1[N-1-i]);
                }
            }
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

            for (i = 0; i < N; i++) cw1[i] = codeword1[N-1-i];
            for (i = 0; i < N; i++) cw2[i] = codeword2[N-1-i];

            errors1 = decode_rs_char(rs, codeword1, errpos1, 0);
            errors2 = decode_rs_char(rs, codeword2, errpos2, 0);

            errs1 = rs_decode(cw1, err_pos1, err_val1);
            errs2 = rs_decode(cw2, err_pos2, err_val2);

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

            for (i = 0; i < N; i++) {
                if (cw1[i] != codeword1[N-1-i]) {
                    printf("diff[%3d]:  cw1: %02x  codeword1: %02x\n", i, cw1[i], codeword1[N-1-i]);
                }
            }
            for (i = 0; i < N; i++) {
                if (cw2[i] != codeword2[N-1-i]) {
                    printf("diff[%3d]:  cw2: %02x  codeword2: %02x\n", i, cw2[i], codeword2[N-1-i]);
                }
            }

        }

        if (fp) str = fgets(strbuf, 2*BFSIZE, fp);
        else    str = NULL;
    }


    return 0;
}

/*
./ecc-rs_vaisala <frame_str>

RS92:
parity[0..23]=frame[216..239], msg[0..209]=frame[6..215], pad: msg[210..230]=0
2a2a2a2a2a106510e81820204b34393533393334006100083d3d07b342bb3e9809d3bc3f754c963bebfb690cca2b0fd9670f00670f8c0f11a458111e8810da410d30430ddc84673db01c852167ab07298b3cb05a536e0fffcf4faf7f4f3fff8fcfffffffff7fe5918aef20e8110175879900d0e0e900061f8c048aa393009edea1fe2557dc0019549f04b5160d00ae998b00e12a4200370b8608cacc1900b3e08905bd60040126da9303e1d4b4006d04a00573469700d6a98b00699f120195828f046ed7680503030000000000000000b27dff0202000200f0be2a40a7cd69b9ed0668ec12182e8560ea6dd0733612a1

RS41:
1) 320 byte
8635f44093df1a602c87e0fa0521e8943d9cef4c7a67393f6d39fb546461f2111b6447ab79a746c80350cda5344157f8c0c12234f46902220f792816174b313933303239331a00000300000a00002f0007322ce53e31991abf12dada3eb68468c16755d51c7a2a15310216060245f302000d08a31607821e08bb210219060243f302000000000000000000000000000000220d7c1e0807d03cdc071fd81ddb19d70a8d0eb602b60cb518d40692ff00ff00ff001c277d59b8d83301ff0f881f0f38f4fe18b283038735ff000000003eb8ff4947201e6e3aff55415f13fc6e005440440cf100009e9f7406f85800832b631719d70010bebc172a8b00000000000000000000000000000000000000000000a48b7b15366181193ef05d07e1245b1be0f721f801f60804107b0b76110000000000000000000000000000000000ecc7
2) 518 byte
parity1[0..23]=frame[ 8..31], msg1[0..230]=frame[56..516:2] 
parity2[0..23]=frame[32..55], msg2[0..230]=frame[57..517:2]
8635f44093df1a608f9b1025bf8ec9e28ad68413c31788307e9881c5cb2f37f754fa49b711c5c39977ed8fbf22377b3e5e1cee59bc644b19f0792896134b343032303234341c00000100000c00007a0007320f00000000008920bac20000000000000092697a2ae9030226fd015de502363208522a075f330874040228fd015de502000000000000000000000000000000e7917c1e4d0750f1921703fb01f8068d1fd811f70bd604d50afa17f913d90c8b20f9a16a7d5921103501ff440000006c1f00cd977e059ab7009566fd191d1affd82fbf143fb8ff5277180991faff9ca1d10d441b01927bf211dd190190999f0553a1ff9120b10c3847ff06eeee0e571301a2c0891c000000cddd1a0882d10011167b153c154217941930005fc50b1eb9fde107d2050902115a537ea6ed343030313030303120313037393020202033312e37203036373520303334392030373030203132383636203630303520313339333120363031342031343038322035383830203738313420383032372031303039203930392039353631353632203935303839323220343238383339313633382032393335383636203539343238203335323439203636393920333738332034363837203637303120363930312037393939049a762d000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f35a
*/

