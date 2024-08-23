
/*
 *  rs92
 *  sync header: correlation/matched filter
 *  files: rs92mod.c nav_gps_vel.c bch_ecc_mod.c bch_ecc_mod.h demod_mod.c demod_mod.h
 *  compile:
 *  (a)
 *      gcc -c demod_mod.c
 *      gcc -DINCLUDESTATIC rs92mod.c demod_mod.o -lm -o rs92mod
 *  (b)
 *      gcc -c demod_mod.c
 *      gcc -c bch_ecc_mod.c
 *      gcc rs92mod.c demod_mod.o bch_ecc_mod.o -lm -o rs92mod
 *
 *  author: zilog80
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef CYGWIN
  #include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif

// optional JSON "version"
//  (a) set global
//      gcc -DVERSION_JSN [-I<inc_dir>] ...
#ifdef VERSION_JSN
  #include "version_jsn.h"
#endif
// or
//  (b) set local compiler option, e.g.
//      gcc -DVER_JSN_STR=\"0.0.2\" ...


//typedef unsigned char  ui8_t;
//typedef unsigned short ui16_t;
//typedef unsigned int   ui32_t;

#include "demod_mod.h"

//#define  INCLUDESTATIC 1
#ifdef INCLUDESTATIC
    #include "bch_ecc_mod.c"
#else
    #include "bch_ecc_mod.h"
#endif


typedef struct {
    i8_t vbs;  // verbose output
    i8_t raw;  // raw frames
    i8_t crc;  // CRC check output
    i8_t ecc;  // Reed-Solomon ECC
    i8_t sat;  // GPS sat data
    i8_t ptu;  // PTU: temperature
    i8_t inv;
    i8_t aut;
    i8_t aux;  // aux/ozone
    i8_t jsn;  // JSON output (auto_rx)
    i8_t ngp;
    i8_t dbg;
} option_t;

typedef struct {
    int typ;
    int msglen;
    int msgpos;
    int parpos;
    int hdrlen;
    int frmlen;
} rscfg_t;

static rscfg_t cfg_rs92 = { 92, 240-6-24, 6, 240-24, 6, 240};


/* --- RS92-SGP: 8N1 manchester --- */
#define BITS (1+8+1)  // 10
//#define HEADLEN 60

#define FRAMESTART  6  //((HEADLEN)/BITS)
#define FRAME_LEN 240

/*                                2A                  10*/
static char rs92_rawheader[] = //"10100110011001101001"
                               //"10100110011001101001"
                               //"10100110011001101001"
                                 "10100110011001101001"
                                 "1010011001100110100110101010100110101001";

static ui8_t rs92_header_bytes[6] = { 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x10};


#include "nav_gps_vel.c"

typedef struct {
    i8_t opt_vergps;
    i8_t opt_iter;
    i8_t opt_vel;
    float dop_limit; // 9.9
    float d_err; // 10000
    int almanac;
    int ephem;
    int exSat; // -1
    ui8_t WEEK1024epoch; // SEM almanac, GPS epoch (1: 1999-2019)
    ui8_t sat_status[12];
    ui8_t prn[12];  // valide PRN 0,..,k-1
    ui8_t prn32toggle; // 0x1
    ui8_t prn32next;
    EPHEM_t alm[33];
    EPHEM_t *ephs;
    SAT_t sat[33];
    SAT_t sat1s[33];
} GPS_t;

typedef struct {
    int frnr;
    char id[11];
    int week; int gpssec;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double alt;
    double vH; double vD; double vU;
    int sats[4];
    double dop;
    ui16_t conf_kt; // kill timer (sec)
    int freq;       // freq/kHz (RS92)
    int jsn_freq;   // freq/kHz (SDR)
    ui32_t crc;
    ui8_t frame[FRAME_LEN]; // { 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x10}
    //
    ui8_t cal_state[2];
    ui8_t calfrms;
    ui8_t calibytes[32*16];
    ui8_t calfrchk[32];
    float cal_f32[256];
    float T;
    float _RH; float RH;
    float _P; float P;
    //
    ui8_t xcal16[16];
    ui8_t xptu16[16];
    int rs_type;
    //
    unsigned short aux[4];
    double diter;
    option_t option;
    RS_t RS;
    GPS_t gps;
} gpx_t;

/* --- RS92-SGP ------------------- */


#define MASK_LEN 64
static ui8_t mask[MASK_LEN] = { 0x96, 0x83, 0x3E, 0x51, 0xB1, 0x49, 0x08, 0x98,
                                0x32, 0x05, 0x59, 0x0E, 0xF9, 0x44, 0xC6, 0x26,
                                0x21, 0x60, 0xC2, 0xEA, 0x79, 0x5D, 0x6D, 0xA1,
                                0x54, 0x69, 0x47, 0x0C, 0xDC, 0xE8, 0x5C, 0xF1,
                                0xF7, 0x76, 0x82, 0x7F, 0x07, 0x99, 0xA2, 0x2C,
                                0x93, 0x7C, 0x30, 0x63, 0xF5, 0x10, 0x2E, 0x61,
                                0xD0, 0xBC, 0xB4, 0xB6, 0x06, 0xAA, 0xF4, 0x23,
                                0x78, 0x6E, 0x3B, 0xAE, 0xBF, 0x7B, 0x4C, 0xC1};
/* LFSR: ab i=8 (mod 64):
 * m[16+i] = m[i] ^ m[i+2] ^ m[i+4] ^ m[i+6]
 * ________________3205590EF944C6262160C2EA795D6DA15469470CDCE85CF1
 * F776827F0799A22C937C3063F5102E61D0BCB4B606AAF423786E3BAEBF7B4CC196833E51B1490898
 */

/* ------------------------------------------------------------------------------------ */

#define BAUD_RATE 4800

/* ------------------------------------------------------------------------------------ */

// manchester1 1->10,0->01: 1.bit
// manchester2 0->10,1->01: 2.bit
// RS92-SGP: 8N1 manchester2
static int bits2byte(char bits[]) {
    int i, byteval=0, d=1;

    //if (bits[0] != 0) return 0x100; // erasure?
    //if (bits[9] != 1) return 0x100; // erasure?

    for (i = 1; i <= 8; i++) {   // little endian
    /* for (i = 8; i > 1; i--) { // big endian */
        if      (bits[i] == 1)  byteval += d;
        else if (bits[i] == 0)  byteval += 0;
        d <<= 1;
    }
    return byteval;
}


/*
ui8_t xorbyte(int pos) {
    return  xframe[pos] ^ mask[pos % MASK_LEN];
}
*/

/* ------------------------------------------------------------------------------------ */

//#define GPS_WEEK1024  1  // SEM almanac
#define WEEKSEC       604800

/*
 * Convert GPS Week and Seconds to Modified Julian Day.
 * - Adapted from sci.astro FAQ.
 * - Ignores UTC leap seconds.
 */
// in : week, gpssec
// out: jahr, monat, tag
static void Gps2Date(gpx_t *gpx) {
    long GpsDays, Mjd;
    long J, C, Y, M;

    GpsDays = gpx->week * 7 + (gpx->gpssec / 86400);
    Mjd = 44244 + GpsDays;

    J = Mjd + 2468570;
    C = 4 * J / 146097;
    J = J - (146097 * C + 3) / 4;
    Y = 4000 * (J + 1) / 1461001;
    J = J - 1461 * Y / 4 + 31;
    M = 80 * J / 2447;
    gpx->tag = J - 2447 * M / 80;
    J = M / 11;
    gpx->monat = M + 2 - (12 * J);
    gpx->jahr = 100 * (C - 49) + Y + J;
}

/* ------------------------------------------------------------------------------------ */

#define RS92SGP 0
#define RS92AGP 1
#define RS92NGP 2


#define crc_FRAME    (1<<0)
#define pos_FrameNb   0x08  // 2 byte
#define pos_SondeID   0x0C  // 8 byte  // oder: 0x0A, 10 byte?
#define pos_CalData   0x17  // 1 byte, counter 0x00..0x1f
#define pos_Calfreq   0x1A  // 2 byte, calfr 0x00

#define crc_GPS      (1<<2)
#define posGPS_TOW    0x48  // 4 byte
#define posGPS_PRN    0x4E  // 12*5 bit in 8 byte
#define posGPS_STATUS 0x56  // 12 byte
#define posGPS_DATA   0x62  // 12*8 byte

#define crc_PTU      (1<<1)
#define pos_PTU       0x2C  // 24 byte

#define crc_AUX      (1<<3)
#define pos_AUX       0xC6  // 10 byte
#define pos_AuxData   0xC8  // 8 byte


#define BLOCK_CFG 0x6510  // frame[pos_FrameNb-2], frame[pos_FrameNb-1]
#define BLOCK_PTU 0x690C
#define BLOCK_GPS 0x673D  // frame[posGPS_TOW-2], frame[posGPS_TOW-1]
#define BLOCK_AUX 0x6805

#define LEN_CFG (2*(BLOCK_CFG & 0xFF))
#define LEN_GPS (2*(BLOCK_GPS & 0xFF))
#define LEN_PTU (2*(BLOCK_PTU & 0xFF))
#define LEN_AUX (2*(BLOCK_AUX & 0xFF))


static int crc16(gpx_t *gpx, int start, int len) {
    int crc16poly = 0x1021;
    int rem = 0xFFFF, i, j;
    int byte;

    if (start+len >= FRAME_LEN) return -1;

    for (i = 0; i < len; i++) {
        byte = gpx->frame[start+i];
        rem = rem ^ (byte << 8);
        for (j = 0; j < 8; j++) {
            if (rem & 0x8000) {
                rem = (rem << 1) ^ crc16poly;
            }
            else {
                rem = (rem << 1);
            }
            rem &= 0xFFFF;
        }
    }
    return rem;
}

static int get_FrameNb(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t frnr_bytes[2];
    int frnr;

    for (i = 0; i < 2; i++) {
        byte = gpx->frame[pos_FrameNb + i];
        frnr_bytes[i] = byte;
    }

    frnr = frnr_bytes[0] + (frnr_bytes[1] << 8);
    gpx->frnr = frnr;

    return 0;
}


// calib rows 0x0F,0x10,0x11, 0x14,0x15,0x16,0x17, 0x18(10bytes) constant across rs92 ?
// cal block 0x40..0x40+0x14A: (66*5=330=0x14A byte)
// 0x0a, 0xcf, 0xcf, 0xb8, 0xc3, 0x0b, 0xd7, 0x9d, 0xf5, 0x41, 0x0c, 0x46, 0xe6, 0xa2, 0x43, 0x0d,
// 0x3f, 0x07, 0xc6, 0xc2, 0x0e, 0x6c, 0x04, 0x90, 0x41, 0x0f, 0xf0, 0xde, 0xa5, 0xbf, 0x11, 0x8f,
// 0xc2, 0x75, 0x3f, 0x14, 0x77, 0x16, 0xf9, 0xc4, 0x15, 0x54, 0x1f, 0x78, 0x45, 0x16, 0xf0, 0xfb,
// 0x4f, 0xc5, 0x17, 0xcb, 0x75, 0xa9, 0x44, 0x1b, 0x8f, 0xc2, 0x75, 0x3f, 0x3c, 0x00, 0x5b, 0x9b,
// 0x3d, 0x3d, 0x24, 0xaf, 0xd2, 0xbc, 0x3e, 0xae, 0x80, 0x84, 0xbc, 0x3f, 0x2e, 0xd1, 0x51, 0x3b,
// 0x46, 0x12, 0x6a, 0x90, 0x39, 0x47, 0xbd, 0xdd, 0xab, 0xb9, 0x48, 0x20, 0x4b, 0x6d, 0xb8, 0x49,
// 0x2f, 0x14, 0xc7, 0x36, 0x50, 0xa2, 0x59, 0x63, 0xb6, 0x51, 0xed, 0xa0, 0x3f, 0x36, 0x52, 0x26,
// 0xd8, 0x4a, 0xb4, 0x1e, 0x08, 0xdc, 0x37, 0xc3, 0x1f, 0x2f, 0xd9, 0xbf, 0x42, 0x20, 0x03, 0x46,
// 0x0d, 0xc2, 0x21, 0xa6, 0x72, 0x42, 0x41, 0x22, 0x36, 0xcc, 0xfc, 0xbf, 0x23, 0xf5, 0x80, 0xf9,
// 0x3d, 0x25, 0xbb, 0x74, 0x57, 0x3f, 0x28, 0x10, 0x08, 0x16, 0xc5, 0x29, 0x0c, 0xe1, 0xb2, 0x45,
// 0x2a, 0xd7, 0xed, 0x7b, 0xc5, 0x2b, 0x16, 0x3b, 0x5d, 0x44, 0x2f, 0x8f, 0xc2, 0x75, 0x3f, 0x6e,
// 0xab, 0xcf, 0x05, 0x3f, 0x6f, 0x1e, 0xa7, 0xe8, 0xbc, 0x70, 0x45, 0xf2, 0x95, 0x39, 0x71, 0x5f,  // 0x0F0
// 0xc9, 0x70, 0x35, 0x72, 0x4f, 0x2c, 0xad, 0xb0, 0x78, 0x9e, 0xb1, 0x89, 0x3f, 0x79, 0x60, 0xe5,  // 0x100
// 0x50, 0xbe, 0x7a, 0x7c, 0x9a, 0x13, 0x3c, 0x7b, 0x26, 0x87, 0x74, 0xb8, 0x7c, 0x21, 0x96, 0x8b,  // 0x110
// 0xb5, 0x32, 0xa6, 0xa3, 0x42, 0xc5, 0x33, 0xd9, 0xb6, 0xd7, 0x45, 0x34, 0xe1, 0x7d, 0x92, 0xc5,
// 0x35, 0x4a, 0x0c, 0x7c, 0x44, 0x39, 0x8f, 0xc2, 0x75, 0x3f, 0x82, 0xab, 0xcf, 0x05, 0x3f, 0x83,
// 0x1e, 0xa7, 0xe8, 0xbc, 0x84, 0x45, 0xf2, 0x95, 0x39, 0x85, 0x5f, 0xc9, 0x70, 0x35, 0x86, 0x4f,  // 0x140
// 0x2c, 0xad, 0xb0, 0x8c, 0x9e, 0xb1, 0x89, 0x3f, 0x8d, 0x60, 0xe5, 0x50, 0xbe, 0x8e, 0x7c, 0x9a,  // 0x150
// 0x13, 0x3c, 0x8f, 0x26, 0x87, 0x74, 0xb8, 0x90, 0x21, 0x96, 0x8b, 0xb5, 0x97, 0xac, 0x64, 0x9f,  // 0x160
// 0x36, 0x98, 0x92, 0x25, 0x6b, 0xb3, 0x99, 0xe1, 0x57, 0x05, 0x30, 0x9a, 0xfe, 0x51, 0xf4, 0xab,  // 0x170
// 0x9d, 0x33, 0x33, 0x33, 0x3f, 0xa7, 0x33, 0x33, 0x33, 0x3f

static ui8_t rs92calx170[16] = {
0x36, 0x98, 0x92, 0x25, 0x6b, 0xb3, 0x99, 0xe1, 0x57, 0x05, 0x30, 0x9a, 0xfe, 0x51, 0xf4, 0xab}; // 0x170

static int chk_toggle_type(gpx_t *gpx) {
    int toggle = 0;
    int n;

    // constant rs92-coeffs
    //gpx->xcal16[ 0] == 0 && gpx->xcal16[ 1] == 0 &&
    //gpx->xcal16[ 5] == 0 && gpx->xcal16[ 6] == 0 &&
    //gpx->xcal16[10] == 0 && gpx->xcal16[11] == 0
    if ( memcmp(gpx->calibytes+0x170, rs92calx170, 16) == 0 ) {
        gpx->rs_type = RS92SGP;
    }
    else {
        gpx->rs_type = RS92NGP;
    }
    // rs92sgp: conf/calib data after 0x40+0x14A ... zero ?
    // check ptu-float32 plausibility

    if (gpx->rs_type == RS92SGP && gpx->option.ngp)  toggle = 1;
    if (gpx->rs_type == RS92NGP && !gpx->option.ngp) toggle = 2;

    if (toggle) gpx->option.ngp ^= 1;

    return toggle;
}

static int xor_ptu(gpx_t *gpx) {
    int j, k;
    ui32_t a, c, tmp;
    ui8_t *pcal = gpx->calibytes+0x24;

    for (j = 0; j < 8; j++) {

        tmp = 0x1d89;

        for (k = 0; k < 4; k++) {

            a = pcal[j+k] & 0xFF;
            c = tmp;

            //add(A, C, A);
            a = a + c;

            c = a;

            //shl_add(A, 10, C, A);
            a = (a << 10) + c;

            c = a;

            //shr_xor(A, 6, C, A);
            a = (a >> 6) ^ c;
            tmp = a;
        }

        a = tmp;
        c = a;

        //shl_add(A, 3, C, A);
        a = (a << 3) + c;

        c = a;

        //shr_xor(A, 11, C, A);
        a = (a >> 11) ^ c;

        c = a;

        //shl_add(A, 15, C, A);
        a = (a << 15) + c;

        //y = a & 0xFFFF;

        gpx->xptu16[2*j  ] =  a     & 0xFF;
        gpx->xptu16[2*j+1] = (a>>8) & 0xFF;
    }

    return 0;
}

static int get_SondeID(gpx_t *gpx) {
    int i, ret=0;
    unsigned byte;
    ui8_t sondeid_bytes[10];
    ui8_t calfr;
    int crc_frame, crc;

    // BLOCK_CFG == frame[pos_FrameNb-2 .. pos_FrameNb-1] ?
    crc_frame = gpx->frame[pos_FrameNb+LEN_CFG] | (gpx->frame[pos_FrameNb+LEN_CFG+1] << 8);
    crc = crc16(gpx, pos_FrameNb, LEN_CFG);
    if (crc_frame != crc) gpx->crc |= crc_FRAME;

    ret = 0;
    if (gpx->option.crc  &&  crc != crc_frame) {
        ret = -2;  // erst wichtig, wenn Cal/Cfg-Data
    }

    if (crc == crc_frame)
    {
        for (i = 0; i < 8; i++) {
            byte = gpx->frame[pos_SondeID + i];
            if ((byte < 0x20) || (byte > 0x7E)) return -1;
            sondeid_bytes[i] = byte;
        }
        sondeid_bytes[8] = '\0';

        if ( strncmp(gpx->id, sondeid_bytes, 8) != 0 ) {
            memset(gpx->calibytes, 0, 32*16);
            memset(gpx->calfrchk, 0, 32);
            memset(gpx->cal_f32, 0, 256*4);
            gpx->calfrms = 0;
            gpx->T = -275.15f;
            gpx->_RH = -1.0f;
            gpx->_P  = -1.0f;
            gpx->RH  = -1.0f;
            gpx->P   = -1.0f;
            // new ID:
            memcpy(gpx->id, sondeid_bytes, 8);
        }

        memcpy(gpx->cal_state, gpx->frame+(pos_FrameNb + 12), 2);

        calfr = gpx->frame[pos_CalData]; // 0..31
        if (calfr < 32) {
            if (gpx->calfrchk[calfr] == 0) // const?
            {
                for (i = 0; i < 16; i++) {
                    gpx->calibytes[calfr*16 + i] = gpx->frame[pos_CalData+1+i];
                }
                gpx->calfrchk[calfr] = 1;
            }
        }

        if (gpx->calfrms < 32) {
            gpx->calfrms = 0;
            for (i = 0; i < 32; i++) gpx->calfrms += (gpx->calfrchk[i]>0);
        }
        if (gpx->calfrms == 32)
        {
            ui8_t xcal[66*5];
            ui8_t *xcal16 = gpx->xcal16;
            ui8_t *p = gpx->calibytes+0x170;
            ui8_t *q = rs92calx170;
            int cal_chk = 0;

            gpx->calfrms += 1;

            xor_ptu(gpx);
            if (gpx->option.dbg) {
                printf("XPTU:"); for (int j = 0; j < 16; j++) printf(" %02X", gpx->xptu16[j]); printf("\n");
            }

            //Xx17: __ 98 __ __ __ __ 99 __ __ __ __ 9a __ __ __ __
            // p[0], p[1]=idx, p[2+1], p[3+1], p[4-2], p[5], p[6]=idx, ...
            for (int k = 0; k < 3; k++) {
                xcal16[5*k]     = p[5*k]^q[5*k];
                xcal16[5*k+1]   = p[5*k+1]^q[5*k+1];
                xcal16[5*k+2+1] = p[5*k+2+1]^q[5*k+2];
                xcal16[5*k+3+1] = p[5*k+3+1]^q[5*k+3];
                xcal16[5*k+4-2] = p[5*k+4-2]^q[5*k+4];
            }
            xcal16[5*3] = p[5*3]^q[5*3];

            if (gpx->option.dbg) {
                printf("XCAL:"); for (int j = 0; j < 16; j++) printf(" %02X", xcal16[j]); printf("\n");
            }

            int tgl = chk_toggle_type(gpx);

            for (int j = 0; j < 66*5; j++) {
                xcal[j] = gpx->calibytes[0x40+j];
                if (gpx->option.ngp) {
                    xcal[j] ^= gpx->xcal16[j%16];
                }
            }

            for (int j = 0; j < 66; j++) {
                ui8_t idx = xcal[5*j];
                ui8_t *dat = xcal+(5*j+1);
                ui32_t le_dat32 = dat[0] | (dat[1]<<8) | (dat[2]<<16) | (dat[3]<<24);
                ui32_t xx_dat32 = dat[1] | (dat[2]<<8) | (dat[0]<<16) | (dat[3]<<24);
                float *pf32 = (float*)&le_dat32;
                if (gpx->option.ngp) {
                    pf32 = (float*)&xx_dat32;
                }
                gpx->cal_f32[idx] = *pf32;

                if (gpx->option.dbg)
                {
                    if (idx/10 == 3 || idx/10 == 4 || idx/10 == 5)
                    {
                        printf(" %3d :", idx);
                        for (int i = 1; i < 5; i++) {
                            printf(" %02x", xcal[5*j+i]);
                        }
                        printf(" : %f", *pf32);
                        printf("\n");
                    }
                }
            }
        }
    }

    return ret;
}


// ----------------------------------------------------------------------------------------------------

// PTU
// cf. Haeberli (2001),
//     https://brmlab.cz/project/weathersonde/telemetry_decoding
//
static float poly5(float x, float *a) {
    float p = 0.0;
    p = ((((a[5]*x+a[4])*x+a[3])*x+a[2])*x+a[1])*x+a[0];
    return p;
}
static float nu(float t, float t0, float y0) {
    // t=1/f2-1/f , t0=1/f2-1/f1 , 1/freq=meas24
    float y = t / t0;
    return 1.0f / (y0 - y);
}

static int get_Meas(gpx_t *gpx) {
    ui32_t temp, pres, hum1, hum2, ref1, ref2, ref3, ref4;
    ui8_t *meas24 = gpx->frame+pos_PTU;
    float T, U1, U2, _P, _rh, x;

    if ( gpx->option.ngp && (gpx->crc & crc_FRAME) ) return -2; // frame number

    for (int j = 0; j < 24; j++) {
        ui8_t byte = meas24[j];

        if (gpx->option.ngp) {
            byte ^= gpx->frame[pos_FrameNb+(j&1)];
            byte ^= gpx->xptu16[j%16];
        }

        meas24[j] = byte;
    }

    temp = meas24[ 0] | (meas24[ 1]<<8) | (meas24[ 2]<<16);  // ch1
    hum1 = meas24[ 3] | (meas24[ 4]<<8) | (meas24[ 5]<<16);  // ch2
    hum2 = meas24[ 6] | (meas24[ 7]<<8) | (meas24[ 8]<<16);  // ch3
    ref1 = meas24[ 9] | (meas24[10]<<8) | (meas24[11]<<16);  // ch4
    ref2 = meas24[12] | (meas24[13]<<8) | (meas24[14]<<16);  // ch5
    pres = meas24[15] | (meas24[16]<<8) | (meas24[17]<<16);  // ch6
    ref3 = meas24[18] | (meas24[19]<<8) | (meas24[20]<<16);  // ch7
    ref4 = meas24[21] | (meas24[22]<<8) | (meas24[23]<<16);  // ch8

    if (gpx->calfrms > 0x20)
    {
        // Temperature
        x = nu( (float)(ref1 - temp), (float)(ref1 - ref4), gpx->cal_f32[37] );
        T = poly5(x, gpx->cal_f32+30);
        if (T > -120.0f && T < 80.0f)  gpx->T = T;
        else gpx->T = -273.15f;

        // rel. Humidity (ref3 or ref4 ?)
        x = nu( (float)(ref1 - hum1), (float)(ref1 - ref3), gpx->cal_f32[47] );
        U1 = poly5(x, gpx->cal_f32+40); // c[44]=c[45]=0
        x = nu( (float)(ref1 - hum2), (float)(ref1 - ref3), gpx->cal_f32[57] );
        U2 = poly5(x, gpx->cal_f32+50); // c[54]=c[55]=0
        _rh = U1 > U2 ? U1 : U2; // max(U1,U2), vgl. cal_state[1].bit3
        gpx->_RH = _rh;
        if (gpx->_RH < 0.0f) gpx->_RH = 0.0f;
        if (gpx->_RH > 100.0f) gpx->_RH = 100.0f;
        // correction for higher RH-sensor temperature (at low temperatures)?
        // (cf. amt-7-4463-2014)
        // if (T<-60C || P<100hPa): cal_state[1].bit2=0
        // (Hyland and Wexler)
        // if (T>-60C && P>100hPa): rh = _rh*vaporSatP(Trh)/vaporSatP(T) ...
        // estimate Trh ?

        // (uncorrected) Pressure
        x = nu( (float)(ref1 - pres), (float)(ref1 - ref4), gpx->cal_f32[17] );
        _P = poly5(x, gpx->cal_f32+10);
        if (_P < 0.0f && _P > 2000.0f) _P = -1.0f;
        gpx->_P = _P;
        // correction for x and coefficients?
    }

    return 0;
}

// ----------------------------------------------------------------------------------------------------

static int get_PTU(gpx_t *gpx) {
    int ret=0;
    int crc_frame, crc;

    crc_frame = gpx->frame[pos_PTU+LEN_PTU] | (gpx->frame[pos_PTU+LEN_PTU+1] << 8);
    crc = crc16(gpx, pos_PTU, LEN_PTU);
    if (crc_frame != crc) gpx->crc |= crc_PTU;

    ret = 0;
    if (gpx->option.crc  &&  crc != crc_frame) {
        ret = -2;
    }

    if (ret == 0) {
        if (gpx->calfrms > 0x20) ret = get_Meas(gpx);
    }

    return ret;
}

//char weekday[7][3] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
static char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static int get_GPStime(gpx_t *gpx) {
    int i, ret=0;
    unsigned byte;
    ui8_t gpstime_bytes[4];
    ui32_t gpstime = 0; // 32bit
    int day;
    int ms;
    int crc_frame, crc;

    // BLOCK_GPS == frame[posGPS_TOW-2 .. posGPS_TOW-1] ?
    crc_frame = gpx->frame[posGPS_TOW+LEN_GPS] | (gpx->frame[posGPS_TOW+LEN_GPS+1] << 8);
    crc = crc16(gpx, posGPS_TOW, LEN_GPS);
    if (crc_frame != crc) gpx->crc |= crc_GPS;

    ret = 0;
    if (gpx->option.crc  &&  crc != crc_frame) {
        ret = -2;
    }

    for (i = 0; i < 4; i++) {
        byte = gpx->frame[posGPS_TOW + i];
        gpstime_bytes[i] = byte;
    }

    memcpy(&gpstime, gpstime_bytes, 4);
    ms = gpstime % 1000;
    gpstime /= 1000;

    gpx->gpssec = gpstime;

    day = (gpstime / (24 * 3600)) % 7;        // besser CRC-check, da auch
    //if ((day < 0) || (day > 6)) return -1;  // gpssec=604800,604801 beobachtet

    gpstime %= (24*3600);

    gpx->wday = day;
    gpx->std =  gpstime / 3600;
    gpx->min = (gpstime % 3600) / 60;
    gpx->sek =  gpstime % 60 + ms/1000.0;

    return ret;
}

static int get_Aux(gpx_t *gpx) {
    int i, ret=0;
    unsigned short byte;
    int crc_frame, crc;

    crc_frame = gpx->frame[pos_AUX+LEN_AUX] | (gpx->frame[pos_AUX+LEN_AUX+1] << 8);
    crc = crc16(gpx, pos_AUX, LEN_AUX);
    if (crc_frame != crc) gpx->crc |= crc_AUX;

    ret = 0;
    if (gpx->option.crc  &&  crc != crc_frame) {
        ret = -2;
    }

    for (i = 0; i < 4; i++) {
        byte = gpx->frame[pos_AuxData+2*i] + (gpx->frame[pos_AuxData+2*i+1]<<8);
        gpx->aux[i] = byte;
    }

    return ret;
}

static int get_Cal(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t calfr = 0;
    //ui8_t burst = 0;
    ui8_t bytes[2];
    int freq = 0;
    ui16_t killtime = 0;

    byte = gpx->frame[pos_CalData];
    calfr = byte;

    if (gpx->option.vbs == 4) {
        fprintf(stdout, "\n");
        fprintf(stdout, "[%5d] ", gpx->frnr);
        fprintf(stdout, "  0x%02x:", calfr);
        for (i = 0; i < 16; i++) {
            byte = gpx->frame[pos_CalData+1+i];
            fprintf(stdout, " %02x", byte);
        }
        if ((gpx->crc & crc_FRAME)==0) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
    }

    if (gpx->option.aux) {
        if (gpx->option.vbs == 4) {
            fprintf(stdout, "  #  ");
            for (i = 0; i < 8; i++) {
                byte = gpx->frame[pos_AuxData+i];
                fprintf(stdout, "%02x ", byte);
            }
        }
    }

    if (calfr == 0x00) {
        for (i = 0; i < 2; i++) {
            bytes[i] = gpx->frame[pos_Calfreq + i];
        }
        byte = bytes[0] + (bytes[1] << 8);
        //fprintf(stdout, ":%04x ", byte);
        freq = 400000 + 10*byte; // kHz;
        if (gpx->option.ngp) freq = 1600000 + 10*byte; // kHz
        gpx->freq = freq;
        fprintf(stdout, ": fq %d", freq);
        for (i = 0; i < 2; i++) {
            bytes[i] = gpx->frame[pos_Calfreq + 2 + i];
        }
        killtime = bytes[0] + (bytes[1] << 8); // signed?
        if (killtime < 0xFFFF && gpx->option.vbs == 4) {
            fprintf(stdout, "; KT:%ds", killtime);
        }
        gpx->conf_kt = killtime;
    }

    return 0;
}


/* ---------------------------------------------------------------------------------------------------- */


static int prnbits_le(ui16_t byte16, ui8_t bits[64], int block) {
    int i; /* letztes bit Ueberlauf, wenn 3. PRN = 32 */
    for (i = 0; i < 15; i++) {
        bits[15*block+i] = byte16 & 1;
        byte16 >>= 1;
    }
    bits[60+block] = byte16 & 1;
    return byte16 & 1;
}

static void prn12(GPS_t *gps, ui8_t *prn_le, ui8_t prns[12]) {
    int i, j, d;
    ui8_t ind_prn32 = 32;

    for (i = 0; i < 12; i++) {
        prns[i] = 0;
        d = 1;
        for (j = 0; j < 5; j++) {
          if (prn_le[5*i+j]) prns[i] += d;
          d <<= 1;
        }
    }
    for (i = 0; i < 12; i++) {
        // PRN-32 overflow
        if ( (prns[i] == 0) && (gps->sat_status[i] & 0x0F) ) { // 5 bit: 0..31
            if (  ((i % 3 == 2) && (prn_le[60+i/3] & 1))       // Spalte 2
               || ((i % 3 != 2) && (prn_le[5*(i+1)] & 1)) ) {  // Spalte 0,1
                prns[i] = 32; ind_prn32 = i;
            }
        }
        else if ((gps->sat_status[i] & 0x0F) == 0) {  // erste beiden bits: 0x03 ?
            prns[i] = 0;
        }
    }

    gps->prn32next = 0;
    if (ind_prn32 < 12) {
        // PRN-32 overflow
        if (ind_prn32 % 3 != 2) { // -> ind_prn32<11                            // vorausgesetzt im Block folgt auf PRN-32
            if ((gps->sat_status[ind_prn32+1] & 0x0F)  &&  prns[ind_prn32+1] > 1) {  // entweder PRN-1 oder PRN-gerade
                                               // &&  prns[ind_prn32+1] != 3 ?
                for (j = 0; j < ind_prn32; j++) {
                    if (prns[j] == (prns[ind_prn32+1]^gps->prn32toggle)  &&  (gps->sat_status[j] & 0x0F)) break;
                }
                if (j < ind_prn32) { gps->prn32toggle ^= 0x1; }
                else {
                    for (j = ind_prn32+2; j < 12; j++) {
                        if (prns[j] == (prns[ind_prn32+1]^gps->prn32toggle)  &&  (gps->sat_status[j] & 0x0F)) break;
                    }
                    if (j < 12) { gps->prn32toggle ^= 0x1; }
                }
                prns[ind_prn32+1] ^= gps->prn32toggle;
                /*
                  // nochmal testen
                  for (j = 0; j < ind_prn32; j++) { if (prns[j] == prns[ind_prn32+1]) break; }
                  if (j < ind_prn32) prns[ind_prn32+1] = 0;
                  else {
                      for (j = ind_prn32+2; j < 12; j++) { if (prns[j] == prns[ind_prn32+1]) break; }
                      if (j < 12) prns[ind_prn32+1] = 0;
                  }
                  if (prns[ind_prn32+1] == 0) { gps->prn32toggle ^= 0x1; }
                */
            }
            gps->prn32next = prns[ind_prn32+1];  // ->  ind_prn32<11  &&  ind_prn32 % 3 != 2
        }
    }
}

static int calc_satpos_alm(gpx_t *gpx, double t, SAT_t *satp) {
    double X, Y, Z, vX, vY, vZ;
    int j;
    int week;
    double cl_corr, cl_drift;
    int rollover = 0;
    EPHEM_t *alm = gpx->gps.alm;

    for (j = 1; j < 33; j++) {
        if (alm[j].prn > 0 && alm[j].health == 0) {  // prn==j

            // Woche hat 604800 sec
            if      (t-alm[j].toa >  WEEKSEC/2) rollover = +1;
            else if (t-alm[j].toa < -WEEKSEC/2) rollover = -1;
            else rollover = 0;
            week = alm[j].week - rollover;
            /*if (j == 1)*/ gpx->week = week + gpx->gps.WEEK1024epoch*1024;

            if (gpx->gps.opt_vel >= 2) {
                GPS_SatellitePositionVelocity_Ephem(
                    week, t, alm[j],
                    &cl_corr, &cl_drift, &X, &Y, &Z, &vX, &vY, &vZ
                );
                satp[alm[j].prn].clock_drift = cl_drift;
                satp[alm[j].prn].vX = vX;
                satp[alm[j].prn].vY = vY;
                satp[alm[j].prn].vZ = vZ;
            }
            else {
                GPS_SatellitePosition_Ephem(
                    week, t, alm[j],
                    &cl_corr, &X, &Y, &Z
                );
            }

            satp[alm[j].prn].X = X;
            satp[alm[j].prn].Y = Y;
            satp[alm[j].prn].Z = Z;
            satp[alm[j].prn].clock_corr = cl_corr;

        }
    }

    return 0;
}

static int calc_satpos_rnx2(gpx_t *gpx, double t, SAT_t *satp) {
    double X, Y, Z, vX, vY, vZ;
    int j;
    int week;
    double cl_corr, cl_drift;
    double tdiff, td;
    int count, count0, satfound;
    int rollover = 0;
    EPHEM_t *eph = gpx->gps.ephs;

    for (j = 1; j < 33; j++) {

        count = count0 = 0;
        satfound = 0;

        // Woche hat 604800 sec
        tdiff = WEEKSEC;

        while (eph[count].prn > 0) {

            if (eph[count].prn == j && eph[count].health == 0) {

                satfound += 1;

                if      (t - eph[count].toe >  WEEKSEC/2) rollover = +1;
                else if (t - eph[count].toe < -WEEKSEC/2) rollover = -1;
                else rollover = 0;
                td = fabs( t - eph[count].toe - rollover*WEEKSEC);

                if ( td < tdiff ) {
                    tdiff = td;
                    week = eph[count].week - rollover;
                    gpx->week = eph[count].gpsweek - rollover;
                    count0 = count;
                }
            }
            count += 1;
        }

        if ( satfound )
        {
            if (gpx->gps.opt_vel >= 2) {
                GPS_SatellitePositionVelocity_Ephem(
                    week, t, eph[count0],
                    &cl_corr, &cl_drift, &X, &Y, &Z, &vX, &vY, &vZ
                );
                satp[j].clock_drift = cl_drift;
                satp[j].vX = vX;
                satp[j].vY = vY;
                satp[j].vZ = vZ;
            }
            else {
                GPS_SatellitePosition_Ephem(
                    week, t, eph[count0],
                    &cl_corr, &X, &Y, &Z
                );
            }

            satp[j].X = X;
            satp[j].Y = Y;
            satp[j].Z = Z;
            satp[j].clock_corr = cl_corr;
            satp[j].ephtime = eph[count0].toe;
        }

    }

    return 0;
}


typedef struct {
    ui32_t tow;
    ui8_t status;
    int chips;
    int deltachips;
} RANGE_t;

// pseudo.range = -df*pseudo.chips
//           df = lightspeed/(chips/sec)/2^10
const double df = 299792.458/1023.0/1024.0; //0.286183844 // c=299792458m/s, 1023000chips/s
//           dl = L1/(chips/sec)/4
const double dl = 1575.42/1.023/4.0; //385.0 // GPS L1 1575.42MHz=154*10.23MHz, dl=154*10/4

static int get_pseudorange(gpx_t *gpx) {
    ui32_t gpstime;
    ui8_t gpstime_bytes[4];
    ui8_t pseudobytes[4];
    unsigned chipbytes, deltabytes;
    int i, j, k;
    ui8_t bytes[4];
    ui16_t byte16;
    double  pr0, prj;
    ui8_t prn_le[12*5+4]; // le - little endian
    ui8_t prns[12]; // PRNs in data
    RANGE_t range[33];

    memset(prn_le, 0, sizeof(prn_le));
    memset(prns, 0, sizeof(prns));
    memset(range, 0, sizeof(range));

    // GPS-TOW in ms
    for (i = 0; i < 4; i++) {
        gpstime_bytes[i] = gpx->frame[posGPS_TOW + i];
    }
    memcpy(&gpstime, gpstime_bytes, 4);

    // Sat Status
    for (i = 0; i < 12; i++) {
        gpx->gps.sat_status[i] = gpx->frame[posGPS_STATUS + i];
    }

    // PRN-Nummern
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 2; j++) {
            bytes[j] = gpx->frame[posGPS_PRN+2*i+j];
        }
        memcpy(&byte16, bytes, 2);
        prnbits_le(byte16, prn_le, i);
    }
    prn12(&gpx->gps, prn_le, prns);


    // GPS Sat Pos (& Vel)
    if (gpx->gps.almanac) calc_satpos_alm( gpx, gpstime/1000.0, gpx->gps.sat);
    if (gpx->gps.ephem)   calc_satpos_rnx2(gpx, gpstime/1000.0, gpx->gps.sat);

    // GPS Sat Pos t -= 1s
    if (gpx->gps.opt_vel == 1) {
        if (gpx->gps.almanac) calc_satpos_alm( gpx, gpstime/1000.0-1, gpx->gps.sat1s);
        if (gpx->gps.ephem)   calc_satpos_rnx2(gpx, gpstime/1000.0-1, gpx->gps.sat1s);
    }

    k = 0;
    for (j = 0; j < 12; j++) {

        // Pseudorange/chips
        for (i = 0; i < 4; i++) {
            pseudobytes[i] = gpx->frame[posGPS_DATA+8*j+i];
        }
        memcpy(&chipbytes, pseudobytes, 4);

        // delta_pseudochips / 385
        for (i = 0; i < 3; i++) {
            pseudobytes[i] = gpx->frame[posGPS_DATA+8*j+4+i];
        }
        deltabytes = 0; // bzw. pseudobytes[3]=0 (24 bit);  deltabytes & (0xFF<<24) als
        memcpy(&deltabytes, pseudobytes, 3); // gemeinsamer offset relevant in --vel1 !

        //if ( (prns[j] == 0) && (gpx->gps.sat_status[j] & 0x0F) )  prns[j] = 32;
        range[prns[j]].tow = gpstime;
        range[prns[j]].status = gpx->gps.sat_status[j];

        if ( chipbytes == 0x7FFFFFFF  ||  chipbytes == 0x55555555 ) {
             range[prns[j]].chips = 0;
             continue;
        }
        if (gpx->gps.opt_vergps != 8) {
        if ( chipbytes >  0x10000000  &&  chipbytes <  0xF0000000 ) {
             range[prns[j]].chips = 0;
             continue;
        }}

        range[prns[j]].chips = chipbytes;
        range[prns[j]].deltachips = deltabytes;

        /*
        if (range[prns[j]].deltachips == 0x555555) {
            range[prns[j]].deltachips = 0;
            continue;
        }
        */
        if (  (prns[j] > 0)  &&  ((gpx->gps.sat_status[j] & 0x0F) == 0xF)
           && (dist(gpx->gps.sat[prns[j]].X, gpx->gps.sat[prns[j]].Y, gpx->gps.sat[prns[j]].Z, 0, 0, 0) > 6700000) )
        {
            for (i = 0; i < k; i++) { if (gpx->gps.prn[i] == prns[j]) break; }
            if (i == k  &&  prns[j] != gpx->gps.exSat) {
                //if ( range[prns[j]].status & 0xF0 )  // Signalstaerke > 0 ?
                {
                    gpx->gps.prn[k] = prns[j];
                    k++;
                }
            }
        }

    }


    for (j = 0; j < 12; j++) {    // 0x013FB0A4
        gpx->gps.sat[prns[j]].pseudorange = /*0x01400000*/ - range[prns[j]].chips * df;
        gpx->gps.sat1s[prns[j]].pseudorange = -(range[prns[j]].chips - range[prns[j]].deltachips/dl)*df;
                                   //+ sat[prns[j]].clock_corr - gpx->gps.sat1s[prns[j]].clock_corr
        gpx->gps.sat[prns[j]].pseudorate = - range[prns[j]].deltachips * df / dl;

        gpx->gps.sat[prns[j]].prn = prns[j];
        gpx->gps.sat1s[prns[j]].prn = prns[j];
    }


    pr0 = (double)0x01400000;
    for (j = 0; j < k; j++) {
        prj = gpx->gps.sat[gpx->gps.prn[j]].pseudorange + gpx->gps.sat[gpx->gps.prn[j]].clock_corr;
        if (prj < pr0) pr0 = prj;
    }
    for (j = 0; j < k; j++) gpx->gps.sat[gpx->gps.prn[j]].PR =  gpx->gps.sat[gpx->gps.prn[j]].pseudorange
                                                              + gpx->gps.sat[gpx->gps.prn[j]].clock_corr - pr0 + 20e6;
    // es kann PRNs geben, die zeitweise stark abweichende PR liefern;
    // eventuell Standardabweichung ermitteln und fehlerhafte Sats weglassen
    for (j = 0; j < k; j++) {                      //  sat/sat1s...             PR-check
        gpx->gps.sat1s[gpx->gps.prn[j]].PR =  gpx->gps.sat1s[gpx->gps.prn[j]].pseudorange
                                            + gpx->gps.sat[gpx->gps.prn[j]].clock_corr - pr0 + 20e6;
    }

    return k;
}

static int get_GPSvel(double lat, double lon, double vel_ecef[3],
               double *vH, double *vD, double *vU) {
    // ECEF-Velocities
    // ECEF-Vel -> NorthEastUp
    double phi = lat*M_PI/180.0;
    double lam = lon*M_PI/180.0;
    double vN = -vel_ecef[0]*sin(phi)*cos(lam) - vel_ecef[1]*sin(phi)*sin(lam) + vel_ecef[2]*cos(phi);
    double vE = -vel_ecef[0]*sin(lam) + vel_ecef[1]*cos(lam);
    *vU =  vel_ecef[0]*cos(phi)*cos(lam) + vel_ecef[1]*cos(phi)*sin(lam) + vel_ecef[2]*sin(phi);
    // NEU -> HorDirVer
    *vH = sqrt(vN*vN+vE*vE);
    *vD = atan2(vE, vN) * 180 / M_PI;
    if (*vD < 0) *vD += 360;

    return 0;
}

static int get_GPSkoord(gpx_t *gpx, int N) {
    double lat, lon, alt, rx_cl_bias;
    double vH, vD, vU;
    double lat1s, lon1s, alt1s,
           lat0 , lon0 , alt0 , pos0_ecef[3];
    double pos_ecef[3], pos1s_ecef[3], dpos_ecef[3],
           vel_ecef[3], dvel_ecef[3];
    double gdop, gdop0 = 1000.0;
    //double hdop, vdop, pdop;
    double DOP[4];
    int i0, i1, i2, i3, j, k, n;
    int nav_ret = 0;
    int num = 0;
    SAT_t Sat_A[4];
    SAT_t Sat_B[12]; // N <= 12
    SAT_t Sat_B1s[12];
    SAT_t Sat_C[12]; // 11
    double diter;
    int exN = -1;

    if (gpx->gps.opt_vergps == 8) {
        fprintf(stdout, "  sats: ");
        for (j = 0; j < N; j++) fprintf(stdout, "%02d ", gpx->gps.prn[j]);
        fprintf(stdout, "\n");
    }

    gpx->lat = gpx->lon = gpx->alt = 0;
    DOP[0] = DOP[1] = DOP[2] = DOP[3] = 0.0;

    if (gpx->gps.opt_vergps != 2) {
    for (i0=0;i0<N;i0++) { for (i1=i0+1;i1<N;i1++) { for (i2=i1+1;i2<N;i2++) { for (i3=i2+1;i3<N;i3++) {

        Sat_A[0] = gpx->gps.sat[gpx->gps.prn[i0]];
        Sat_A[1] = gpx->gps.sat[gpx->gps.prn[i1]];
        Sat_A[2] = gpx->gps.sat[gpx->gps.prn[i2]];
        Sat_A[3] = gpx->gps.sat[gpx->gps.prn[i3]];
        nav_ret = NAV_ClosedFormSolution_FromPseudorange( Sat_A, &lat, &lon, &alt, &rx_cl_bias, pos_ecef );

        if (nav_ret == 0) {
            num += 1;
            if (calc_DOPn(4, Sat_A, pos_ecef, DOP) == 0) {
                gdop = sqrt(DOP[0]+DOP[1]+DOP[2]+DOP[3]);
                //fprintf(stdout, " DOP : %.1f ", gdop);

                NAV_LinP(4, Sat_A, pos_ecef, rx_cl_bias, dpos_ecef, &rx_cl_bias);
                diter = dist(0, 0, 0, dpos_ecef[0], dpos_ecef[1], dpos_ecef[2]);
                for (j = 0; j < 3; j++) pos_ecef[j] += dpos_ecef[j];
                ecef2elli(pos_ecef[0], pos_ecef[1], pos_ecef[2], &lat, &lon, &alt);
                if ( gpx->gps.opt_vel == 4 ) {
                    vel_ecef[0] = vel_ecef[1] = vel_ecef[2] = 0;
                    NAV_LinV(4, Sat_A, pos_ecef, vel_ecef, 0.0, dvel_ecef, &rx_cl_bias);
                    for (j=0; j<3; j++) vel_ecef[j] += dvel_ecef[j];
                    NAV_LinV(4, Sat_A, pos_ecef, vel_ecef, rx_cl_bias, dvel_ecef, &rx_cl_bias);
                    for (j=0; j<3; j++) vel_ecef[j] += dvel_ecef[j];
                    get_GPSvel(lat, lon, vel_ecef, &vH, &vD, &vU);
                }
                if (gpx->gps.opt_vergps == 8) {
                    // gdop = sqrt(DOP[0]+DOP[1]+DOP[2]+DOP[3]); // s.o.
                    //hdop = sqrt(DOP[0]+DOP[1]);
                    //vdop = sqrt(DOP[2]);
                    //pdop = sqrt(DOP[0]+DOP[1]+DOP[2]);
                    if (gdop < gpx->gps.dop_limit) {
                        fprintf(stdout, "       ");
                        fprintf(stdout, "lat: %.5f , lon: %.5f , alt: %.1f ", lat, lon, alt);
                        fprintf(stdout, " (d:%.1f)", diter);
                        if ( gpx->gps.opt_vel == 4 ) {
                            fprintf(stdout, "  vH: %4.1f  D: %5.1f  vV: %3.1f ", vH, vD, vU);
                        }
                        fprintf(stdout, "  sats: ");
                        fprintf(stdout, "%02d %02d %02d %02d  ", gpx->gps.prn[i0], gpx->gps.prn[i1], gpx->gps.prn[i2], gpx->gps.prn[i3]);
                        fprintf(stdout, " GDOP : %.1f  ", gdop);
                        //fprintf(stdout, " HDOP: %.1f  VDOP: %.1f ", hdop, vdop);
                        //fprintf(stdout, " PDOP: %.1f  ", pdop);
                        fprintf(stdout, "\n");
                    }
                }
            }
            else gdop = -1;

            if (gdop > 0 && gdop < gdop0) {  // wenn fehlerhafter Sat, diter wohl besserer Indikator
                gpx->lat = lat;
                gpx->lon = lon;
                gpx->alt = alt;
                gpx->dop = gdop;
                gpx->diter = diter;
                gpx->sats[0] = gpx->gps.prn[i0]; gpx->sats[1] = gpx->gps.prn[i1]; gpx->sats[2] = gpx->gps.prn[i2]; gpx->sats[3] = gpx->gps.prn[i3];
                gdop0 = gdop;

                if (gpx->gps.opt_vel == 4) {
                    gpx->vH = vH;
                    gpx->vD = vD;
                    gpx->vU = vU;
                }
            }
        }

    }}}}
    }

    if (gpx->gps.opt_vergps == 8  ||  gpx->gps.opt_vergps == 2) {

        for (j = 0; j < N; j++) Sat_B[j] = gpx->gps.sat[gpx->gps.prn[j]];
        for (j = 0; j < N; j++) Sat_B1s[j] = gpx->gps.sat1s[gpx->gps.prn[j]];

        NAV_bancroft1(N, Sat_B, pos_ecef, &rx_cl_bias);
        ecef2elli(pos_ecef[0], pos_ecef[1], pos_ecef[2], &lat, &lon, &alt);
        gdop = -1;
        if (calc_DOPn(N, Sat_B, pos_ecef, DOP) == 0) {
            gdop = sqrt(DOP[0]+DOP[1]+DOP[2]+DOP[3]);
        }

        NAV_LinP(N, Sat_B, pos_ecef, rx_cl_bias, dpos_ecef, &rx_cl_bias);
        if (gpx->gps.opt_iter) {
            for (j = 0; j < 3; j++) pos_ecef[j] += dpos_ecef[j];
            ecef2elli(pos_ecef[0], pos_ecef[1], pos_ecef[2], &lat, &lon, &alt);
        }
        gpx->diter = dist(0, 0, 0, dpos_ecef[0], dpos_ecef[1], dpos_ecef[2]);

        // Sat mit schlechten Daten suchen
        if (gpx->diter > gpx->gps.d_err) {
            if (N > 5) {  // N > 4 kann auch funktionieren
                for (n = 0; n < N; n++) {
                    k = 0;
                    for (j = 0; j < N; j++) {
                        if (j != n) {
                            Sat_C[k] = Sat_B[j];
                            k++;
                        }
                    }
                    for (j = 0; j < 3; j++) pos0_ecef[j] = 0;
                    NAV_bancroft1(N-1, Sat_C, pos0_ecef, &rx_cl_bias);
                    NAV_LinP(N-1, Sat_C, pos0_ecef, rx_cl_bias, dpos_ecef, &rx_cl_bias);
                    diter = dist(0, 0, 0, dpos_ecef[0], dpos_ecef[1], dpos_ecef[2]);
                    ecef2elli(pos0_ecef[0], pos0_ecef[1], pos0_ecef[2], &lat0, &lon0, &alt0);
                    if (diter < gpx->diter) {
                        gpx->diter = diter;
                        for (j = 0; j < 3; j++) pos_ecef[j] = pos0_ecef[j];
                        lat = lat0;
                        lon = lon0;
                        alt = alt0;
                        exN = n;
                    }
                }
                if (exN >= 0) {
                    if (gpx->gps.prn[exN] == gpx->gps.prn32next) gpx->gps.prn32toggle ^= 0x1;
                    for (k = exN; k < N-1; k++) {
                        Sat_B[k] = Sat_B[k+1];
                        gpx->gps.prn[k] = gpx->gps.prn[k+1];
                        if (gpx->gps.opt_vel == 1) {
                            Sat_B1s[k] = Sat_B1s[k+1];
                        }
                    }
                    N = N-1;
                    if (calc_DOPn(N, Sat_B, pos_ecef, DOP) == 0) {
                        gdop = sqrt(DOP[0]+DOP[1]+DOP[2]+DOP[3]);
                    }
                }
            }
            /*
            if (exN < 0  &&  gpx->gps.prn32next > 0) {
                //prn32next used in pre-fix? prn32toggle ^= 0x1;
            }
            */
        }

        if (gpx->gps.opt_vel == 1) {
            NAV_bancroft1(N, Sat_B1s, pos1s_ecef, &rx_cl_bias);
            if (gpx->gps.opt_iter) {
                NAV_LinP(N, Sat_B1s, pos1s_ecef, rx_cl_bias, dpos_ecef, &rx_cl_bias);
                for (j = 0; j < 3; j++) pos1s_ecef[j] += dpos_ecef[j];
            }
            for (j = 0; j < 3; j++) vel_ecef[j] = pos_ecef[j] - pos1s_ecef[j];
            get_GPSvel(lat, lon, vel_ecef, &vH, &vD, &vU);
            ecef2elli(pos1s_ecef[0], pos1s_ecef[1], pos1s_ecef[2], &lat1s, &lon1s, &alt1s);
            if (gpx->gps.opt_vergps == 8) {
                fprintf(stdout, "\ndeltachips1s lat: %.6f , lon: %.6f , alt: %.2f ", lat1s, lon1s, alt1s);
                fprintf(stdout, " vH: %4.1f  D: %5.1f  vV: %3.1f ", vH, vD, vU);
                fprintf(stdout, "\n");
            }
        }
        if (gpx->gps.opt_vel >= 2) {
              //fprintf(stdout, "\nP(%.1f,%.1f,%.1f) \n", pos_ecef[0], pos_ecef[1], pos_ecef[2]);
            vel_ecef[0] = vel_ecef[1] = vel_ecef[2] = 0;
            NAV_LinV(N, Sat_B, pos_ecef, vel_ecef, 0.0, dvel_ecef, &rx_cl_bias);
            for (j=0; j<3; j++) vel_ecef[j] += dvel_ecef[j];
              //fprintf(stdout, " V(%.1f,%.1f,%.1f) ", vel_ecef[0], vel_ecef[1], vel_ecef[2]);
              //fprintf(stdout, " rx_vel_bias: %.1f \n", rx_cl_bias);
            /* 2. Iteration:
                NAV_LinV(N, Sat_B, pos_ecef, vel_ecef, rx_cl_bias, dvel_ecef, &rx_cl_bias);
                for (j=0; j<3; j++) vel_ecef[j] += dvel_ecef[j];
                //fprintf(stdout, " V(%.1f,%.1f,%.1f) ", vel_ecef[0], vel_ecef[1], vel_ecef[2]);
                //fprintf(stdout, " rx_vel_bias: %.1f \n", rx_cl_bias);
            */
            get_GPSvel(lat, lon, vel_ecef, &vH, &vD, &vU);
        }

        if (gpx->gps.opt_vergps == 8) {
            fprintf(stdout, "bancroft[%2d] lat: %.6f , lon: %.6f , alt: %.2f ", N, lat, lon, alt);
            fprintf(stdout, " (d:%.1f)", gpx->diter);
            if (gpx->gps.opt_vel) {
                fprintf(stdout, "  vH: %4.1f  D: %5.1f  vV: %3.1f ", vH, vD, vU);
            }
            fprintf(stdout, "  DOP[");
            for (j = 0; j < N; j++) {
                fprintf(stdout, "%d", gpx->gps.prn[j]);
                if (j < N-1) fprintf(stdout, ","); else fprintf(stdout, "] %.1f ", gdop);
            }
            fprintf(stdout, "\n");
        }

        if (gpx->gps.opt_vergps == 2) {
            gpx->lat = lat;
            gpx->lon = lon;
            gpx->alt = alt;
            gpx->dop = gdop;
            num = N;

            if (gpx->gps.opt_vel) {
                gpx->vH = vH;
                gpx->vD = vD;
                gpx->vU = vU;
            }
        }

    }

    return num;
}


/* ------------------------------------------------------------------------------------ */

#define rs_N 255
#define rs_R 24
#define rs_K (rs_N-rs_R)

static int rs92_ecc(gpx_t *gpx, int msglen) {

    int i, ret = 0;
    int errors;
    ui8_t cw[rs_N];
    ui8_t err_pos[rs_R], err_val[rs_R];

    memset(cw, 0, rs_N);

    if (msglen > FRAME_LEN) msglen = FRAME_LEN;
    for (i = msglen; i < FRAME_LEN; i++) gpx->frame[i] = 0;//xFF;


    for (i = 0; i < rs_R;            i++) cw[i]      = gpx->frame[cfg_rs92.parpos+i];
    for (i = 0; i < cfg_rs92.msglen; i++) cw[rs_R+i] = gpx->frame[cfg_rs92.msgpos+i];

    errors = rs_decode(&gpx->RS, cw, err_pos, err_val);

    //for (i = 0; i < cfg_rs92.hdrlen; i++) gpx->frame[i] = data[i];
    for (i = 0; i < rs_R;            i++) gpx->frame[cfg_rs92.parpos+i] = cw[i];
    for (i = 0; i < cfg_rs92.msglen; i++) gpx->frame[cfg_rs92.msgpos+i] = cw[rs_R+i];

    ret = errors;

    return ret;
}

/* ------------------------------------------------------------------------------------ */

static int print_position(gpx_t *gpx, int ec) {  // GPS-Hoehe ueber Ellipsoid
    int j, k, n = 0;
    int err1, err2, err3, err4;

    err1 = 0;
    err1 |= get_FrameNb(gpx);
    err1 |= get_SondeID(gpx);
    err2  = get_PTU(gpx);
    err3  = 0;
  //err3 |= get_GPSweek();
    err3 |= get_GPStime(gpx);
    err4  = get_Aux(gpx);

    if (!err3 && (gpx->gps.almanac || gpx->gps.ephem)) {
        k = get_pseudorange(gpx);
        if (k >= 4) {
            n = get_GPSkoord(gpx, k);
        }
    }

    if (!err1)
    {
        fprintf(stdout, "[%5d] ", gpx->frnr);
        fprintf(stdout, "(%s) ", gpx->id);

        if (!err3) {
            if (gpx->gps.almanac || gpx->gps.ephem)
            {
                Gps2Date(gpx);
                //fprintf(stdout, "(W %d) ", gpx->week);
                fprintf(stdout, "(%04d-%02d-%02d) ", gpx->jahr, gpx->monat, gpx->tag);
            }
            fprintf(stdout, "%s ", weekday[gpx->wday]);  // %04.1f: wenn sek >= 59.950, wird auf 60.0 gerundet
            fprintf(stdout, "%02d:%02d:%06.3f", gpx->std, gpx->min, gpx->sek);

            if (n > 0) {
                fprintf(stdout, " ");

                if (gpx->gps.almanac) fprintf(stdout, " lat: %.4f  lon: %.4f  alt: %.1f ", gpx->lat, gpx->lon, gpx->alt);
                else                  fprintf(stdout, " lat: %.5f  lon: %.5f  alt: %.1f ", gpx->lat, gpx->lon, gpx->alt);

                if (gpx->option.vbs  &&  gpx->gps.opt_vergps != 8) {
                    fprintf(stdout, " (d:%.1f)", gpx->diter);
                }
                if (gpx->gps.opt_vel  /*&&  gpx->gps.opt_vergps >= 2*/) {
                    fprintf(stdout,"  vH: %4.1f  D: %5.1f  vV: %3.1f ", gpx->vH, gpx->vD, gpx->vU);
                }
                if (gpx->option.vbs) {
                    if (gpx->gps.opt_vergps != 2) {
                        fprintf(stdout, " DOP[%02d,%02d,%02d,%02d] %.1f",
                                       gpx->sats[0], gpx->sats[1], gpx->sats[2], gpx->sats[3], gpx->dop);
                    }
                    else {  // wenn gpx->gps.opt_vergps=2, dann n=N=k(-1)
                        fprintf(stdout, " DOP[");
                        for (j = 0; j < n; j++) {
                            fprintf(stdout, "%d", gpx->gps.prn[j]);
                            if (j < n-1) fprintf(stdout, ","); else fprintf(stdout, "] %.1f ", gpx->dop);
                        }
                    }
                }
            }
        }
        if (!err2 && gpx->option.ptu) {
            fprintf(stdout, " ");
            if (gpx->T > -273.0f) fprintf(stdout, " T=%.1fC ", gpx->T);
            if (gpx->_RH > -0.5f) fprintf(stdout, " _RH=%.0f%% ", gpx->_RH);
            if (gpx->_P  >  0.0f) fprintf(stdout, " _P=%.1fhPa ", gpx->_P);
        }

        if (gpx->option.aux) {
            if (gpx->option.vbs != 4 && (gpx->crc & crc_AUX)==0 || !gpx->option.crc) {
                if (gpx->aux[0] != 0 || gpx->aux[1] != 0 || gpx->aux[2] != 0 || gpx->aux[3] != 0) {
                    fprintf(stdout, " # %04x %04x %04x %04x", gpx->aux[0], gpx->aux[1], gpx->aux[2], gpx->aux[3]);
                }
            }
        }

        fprintf(stdout, "  # ");
        fprintf(stdout, "[");
        for (j=0; j<4; j++) fprintf(stdout, "%d", (gpx->crc>>j)&1);
        fprintf(stdout, "]");
        if (gpx->option.ecc == 2) {
            if (ec > 0) fprintf(stdout, " (%d)", ec);
            if (ec < 0) fprintf(stdout, " (-)");
        }

        get_Cal(gpx);
        /*
        if (!err3) {
            if (gpx->gps.opt_vergps == 8)
            {
                fprintf(stdout, "\n");
                for (j = 0; j < 60; j++) { fprintf(stdout, "%d", prn_le[j]); if (j % 5 == 4) fprintf(stdout, " "); }
                fprintf(stdout, ": ");
                for (j = 0; j < 12; j++) fprintf(stdout, "%2d ", prns[j]);
                fprintf(stdout, "\n");
                fprintf(stdout, "                                                                  status: ");
                for (j = 0; j < 12; j++) fprintf(stdout, "%02X ", gpx->gps.sat_status[j]); //range[prns[j]].status
                fprintf(stdout, "\n");
            }
        }
        */

        if (gpx->option.jsn) {
            // Print out telemetry data as JSON  //even if we don't have a valid GPS lock
            if ((gpx->crc & (crc_FRAME | crc_GPS))==0 && (gpx->gps.almanac || gpx->gps.ephem)) //(!err1 && !err3)
            {   // eigentlich GPS, d.h. UTC = GPS - UTC_OFS (UTC_OFS=18sec ab 1.1.2017)
                char *ver_jsn = NULL;
                fprintf(stdout, "\n");
                fprintf(stdout, "{ \"type\": \"%s\"", "RS92");
                fprintf(stdout, ", \"frame\": %d, \"id\": \"%s\", \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f",
                               gpx->frnr, gpx->id, gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek, gpx->lat, gpx->lon, gpx->alt, gpx->vH, gpx->vD, gpx->vU);
                if (gpx->option.ptu && !err2) {
                    if (gpx->T > -273.0f) {
                        fprintf(stdout, ", \"temp\": %.1f",  gpx->T );
                    }
                    if (gpx->_RH > -0.5f) {
                        fprintf(stdout, ", \"humidity\": %.1f",  gpx->_RH );
                    }
                    if (gpx->_P > 0.0f) {
                        fprintf(stdout, ", \"pressure\": %.2f",  gpx->_P );
                    }
                }
                if ((gpx->crc & crc_AUX)==0 && (gpx->aux[0] != 0 || gpx->aux[1] != 0 || gpx->aux[2] != 0 || gpx->aux[3] != 0)) {
                    fprintf(stdout, ", \"aux\": \"%04x%04x%04x%04x\"", gpx->aux[0], gpx->aux[1], gpx->aux[2], gpx->aux[3]);
                }
                fprintf(stdout, ", \"subtype\": \"RS92-%s\"",  gpx->rs_type == RS92SGP ? "SGP" : "NGP" );
                if (gpx->jsn_freq > 0) {  // rs92-frequency: gpx->freq
                    int fq_kHz = gpx->jsn_freq;
                    //if (gpx->freq > 0) fq_kHz = gpx->freq; // L-band: option.ngp ?
                    fprintf(stdout, ", \"freq\": %d", fq_kHz );
                }

                // Include frequency derived from subframe information if available.
                if (gpx->freq > 0) {
                    fprintf(stdout, ", \"tx_frequency\": %d", gpx->freq );
                }

                // Reference time/position
                fprintf(stdout, ", \"ref_datetime\": \"%s\"", "GPS" ); // {"GPS", "UTC"} GPS-UTC=leap_sec
                fprintf(stdout, ", \"ref_position\": \"%s\"", "GPS" ); // {"GPS", "MSL"} GPS=ellipsoid , MSL=geoid

                #ifdef VER_JSN_STR
                    ver_jsn = VER_JSN_STR;
                #endif
                if (ver_jsn && *ver_jsn != '\0') fprintf(stdout, ", \"version\": \"%s\"", ver_jsn);
                fprintf(stdout, " }\n");
            }
        }

        fprintf(stdout, "\n");
        //if (gpx->gps.opt_vergps == 8) fprintf(stdout, "\n");
    }

    return err3;
}

static void print_frame(gpx_t *gpx, int len) {
    int i, ec = 0;
    ui8_t byte;

    gpx->crc = 0;

    if (gpx->option.ecc) {
        ec = rs92_ecc(gpx, len);
    }

    for (i = len; i < FRAME_LEN; i++) {
        gpx->frame[i] = 0;
    }

    if (gpx->option.raw) {
        for (i = 0; i < len; i++) {
            byte = gpx->frame[i];
            fprintf(stdout, "%02x", byte);
        }
        if (gpx->option.ecc && gpx->option.vbs) {
            fprintf(stdout, " ");
            if (ec >= 0) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
            if (ec > 0) fprintf(stdout, " (%d)", ec);
            if (ec < 0) fprintf(stdout, " (-)");
        }
        fprintf(stdout, "\n");
        // fprintf(stdout, "\n");
    }
    else print_position(gpx, ec);
}

/* -------------------------------------------------------------------------- */


int main(int argc, char *argv[]) {

    FILE *fp, *fp_alm = NULL, *fp_eph = NULL;
    char *fpname = NULL;

    int option_der = 0;    // linErr
    int option_min = 0;
    int option_iq = 0;
    int option_iqdc = 0;
    int option_lp = 0;
    int option_dc = 0;
    int option_noLUT = 0;
    int option_softin = 0;
    int option_pcmraw = 0;
    int sel_wavch = 0;     // audio channel: left
    int spike = 0;
    int fileloaded = 0;
    int rawhex = 0;
    int cfreq = -1;

    char bitbuf[BITS];
    int bitpos = 0,
        b8pos = 0,
        byte_count = FRAMESTART;
    int bit, byte;
    int bitQ;
    int herrs, herr1;
    int headerlen = 0;

    int k;

    int header_found = 0;

    float thres = 0.7;
    float _mv = 0.0;

    float set_lpIQbw = -1.0f;

    int symlen = 2;
    int bitofs = 2; // +0 .. +3
    int shift = 0;

    pcm_t pcm = {0};
    dsp_t dsp = {0};  //memset(&dsp, 0, sizeof(dsp));

    hdb_t hdb = {0};

    gpx_t gpx = {0};

    gpx.gps.prn32toggle = 0x1;
    gpx.gps.dop_limit = 9.9;
    gpx.gps.d_err = 10000;
    gpx.gps.exSat = -1;
    gpx.gps.WEEK1024epoch = 1; // SEM almanac, GPS epoch (1: 1999-2019)


#ifdef CYGWIN
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    setbuf(stdout, NULL);


    fpname = argv[0];
    ++argv;
    while ((*argv) && (!fileloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] <file>\n", fpname);
            fprintf(stderr, "  file: audio.wav or raw_data\n");
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       --vel; --vel1, --vel2 (-g2)\n");
            fprintf(stderr, "       -v, -vx, -vv\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       -i, --invert\n");
            fprintf(stderr, "       -e, --ephem    <ephemperisRinex>\n");
            fprintf(stderr, "       -a, --almanac  <almanacSEM>\n");
            fprintf(stderr, "           --gpsepoch <n> (2019-04-07: n=2)\n");
            fprintf(stderr, "       -g1          (verbose GPS:   4 sats)\n");
            fprintf(stderr, "       -g2          (verbose GPS: all sats)\n");
            fprintf(stderr, "       -gg          (vverbose GPS)\n");
            fprintf(stderr, "       --crc        (CRC check GPS)\n");
            fprintf(stderr, "       --ecc        (Reed-Solomon)\n");
            fprintf(stderr, "       --ths <x>    (peak threshold; default=%.1f)\n", thres);
            fprintf(stderr, "       --json       (JSON output)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "--vel") == 0) ) {
            gpx.gps.opt_vel = 4;
        }
        else if ( (strcmp(*argv, "--vel1") == 0) ) {
            gpx.gps.opt_vel = 1;
            if (gpx.gps.opt_vergps < 1) gpx.gps.opt_vergps = 2;
        }
        else if ( (strcmp(*argv, "--vel2") == 0) ) {
            gpx.gps.opt_vel = 2;
            if (gpx.gps.opt_vergps < 1) gpx.gps.opt_vergps = 2;
        }
        else if ( (strcmp(*argv, "--iter") == 0) ) {
            gpx.gps.opt_iter = 1;
        }
        else if ( (strcmp(*argv, "-v")  == 0) ) { gpx.option.vbs = 1; }
        else if ( (strcmp(*argv, "-vv") == 0) ) { gpx.option.vbs = 4; }
        else if ( (strcmp(*argv, "-vx") == 0) ) { gpx.option.aux = 1; }
        else if   (strcmp(*argv, "--crc") == 0) { gpx.option.crc = 1; }
        else if   (strcmp(*argv, "--ecc")  == 0) { gpx.option.ecc = 1; }
        else if   (strcmp(*argv, "--ecc2") == 0) { gpx.option.ecc = 2; }
        else if   (strcmp(*argv, "--ptu" ) == 0) { gpx.option.ptu = 1; }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            gpx.option.raw = 1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            gpx.option.inv = 1;
        }
        else if ( (strcmp(*argv, "-e") == 0) || (strncmp(*argv, "--ephem", 7) == 0) ) {
            ++argv;
            if (*argv) fp_eph = fopen(*argv, "rb"); // bin-mode
            else return -1;
            if (fp_eph == NULL) fprintf(stderr, "[rinex] %s konnte nicht geoeffnet werden\n", *argv);
        }
        else if ( (strcmp(*argv, "-a") == 0) || (strcmp(*argv, "--almanac") == 0) ) {
            ++argv;
            if (*argv) fp_alm = fopen(*argv, "r"); // txt-mode
            else return -1;
            if (fp_alm == NULL) fprintf(stderr, "[almanac] %s konnte nicht geoeffnet werden\n", *argv);
        }
        else if ( strcmp(*argv, "--gpsepoch") == 0 ) { // SEM almanac, GPS week: 10 bit
            ++argv;                                    // GPS epoch (default: 1)
            if (*argv) {                               // 2019-04-07: rollover 1 -> 2
                int gpsepoch = atoi(*argv);
                if (gpsepoch < 0  || gpsepoch > 4) gpsepoch = 1;
                gpx.gps.WEEK1024epoch = gpsepoch;
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "--dop") == 0) ) {
            ++argv;
            if (*argv) {
                gpx.gps.dop_limit = atof(*argv);
                if (gpx.gps.dop_limit <= 0  || gpx.gps.dop_limit >= 100)  gpx.gps.dop_limit = 9.9;
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "--der") == 0) ) {
            ++argv;
            if (*argv) {
                gpx.gps.d_err = atof(*argv);
                if (gpx.gps.d_err <= 0  || gpx.gps.d_err >= 100000)  gpx.gps.d_err = 10000;
                else option_der = 1;
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "--exsat") == 0) ) {
            ++argv;
            if (*argv) {
                gpx.gps.exSat = atoi(*argv);
                if (gpx.gps.exSat < 1  || gpx.gps.exSat > 32)  gpx.gps.exSat = -1;
            }
            else return -1;
        }
        else if   (strcmp(*argv, "-g1") == 0) { gpx.gps.opt_vergps = 1; }  //  verbose1 GPS
        else if   (strcmp(*argv, "-g2") == 0) { gpx.gps.opt_vergps = 2; }  //  verbose2 GPS (bancroft)
        else if   (strcmp(*argv, "-gg") == 0) { gpx.gps.opt_vergps = 8; }  // vverbose GPS
        else if   (strcmp(*argv, "--json") == 0) {
            gpx.option.jsn = 1;
            gpx.option.ecc = 2;
            gpx.option.crc = 1;
            gpx.gps.opt_vel = 4;
        }
        else if   (strcmp(*argv, "--jsn_cfq") == 0) {
            int frq = -1;  // center frequency / Hz
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
        }
        else if   (strcmp(*argv, "--spike") == 0) { spike = 1; }
        else if   (strcmp(*argv, "--ch2") == 0) { sel_wavch = 1; }  // right channel (default: 0=left)
        else if   (strcmp(*argv, "--softin") == 0)  { option_softin = 1; }  // float32 soft input
        else if   (strcmp(*argv, "--softinv") == 0) { option_softin = 2; }  // float32 inverted soft input
        else if   (strcmp(*argv, "--ths") == 0) {
            ++argv;
            if (*argv) {
                thres = atof(*argv);
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "-d") == 0) ) {
            ++argv;
            if (*argv) {
                shift = atoi(*argv);
                if (shift >  4) shift =  4;
                if (shift < -4) shift = -4;
            }
            else return -1;
        }
        else if   (strcmp(*argv, "--iq0") == 0) { option_iq = 1; }  // differential/FM-demod
        else if   (strcmp(*argv, "--iq2") == 0) { option_iq = 2; }
        else if   (strcmp(*argv, "--iq3") == 0) { option_iq = 3; }  // iq2==iq3
        else if   (strcmp(*argv, "--iqdc") == 0) { option_iqdc = 1; }  // iq-dc removal (iq0,2,3)
        else if   (strcmp(*argv, "--IQ") == 0) { // fq baseband -> IF (rotate from and decimate)
            double fq = 0.0;                     // --IQ <fq> , -0.5 < fq < 0.5
            ++argv;
            if (*argv) fq = atof(*argv);
            else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            dsp.xlt_fq = -fq; // S(t) -> S(t)*exp(-f*2pi*I*t)
            option_iq = 5;
        }
        else if   (strcmp(*argv, "--lpIQ") == 0) { option_lp |= LP_IQ; }  // IQ/IF lowpass
        else if   (strcmp(*argv, "--lpbw") == 0) {  // IQ lowpass BW / kHz
            double bw = 0.0;
            ++argv;
            if (*argv) bw = atof(*argv);
            else return -1;
            if (bw > 4.6 && bw < 48.0) set_lpIQbw = bw*1e3;
            option_lp |= LP_IQ;
        }
        else if   (strcmp(*argv, "--lpFM") == 0) { option_lp |= LP_FM; }  // FM lowpass
        else if   (strcmp(*argv, "--dc") == 0) { option_dc = 1; }
        else if   (strcmp(*argv, "--noLUT") == 0) { option_noLUT = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if   (strcmp(*argv, "--ngp") == 0) { gpx.option.ngp = 1; }  // RS92-NGP, RS92-D: 1680 MHz
        else if   (strcmp(*argv, "--dbg" ) == 0) { gpx.option.dbg = 1; }
        else if (strcmp(*argv, "--rawhex") == 0) { rawhex = 2; }  // raw hex input
        else if (strcmp(*argv, "-") == 0) {
            int sample_rate = 0, bits_sample = 0, channels = 0;
            ++argv;
            if (*argv) sample_rate = atoi(*argv); else return -1;
            ++argv;
            if (*argv) bits_sample = atoi(*argv); else return -1;
            channels = 2;
            if (sample_rate < 1 || (bits_sample != 8 && bits_sample != 16 && bits_sample != 32)) {
                fprintf(stderr, "- <sr> <bs>\n");
                return -1;
            }
            pcm.sr  = sample_rate;
            pcm.bps = bits_sample;
            pcm.nch = channels;
            option_pcmraw = 1;
        }
        else {
            fp = fopen(*argv, "rb");
            if (fp == NULL) {
                fprintf(stderr, "error: open %s\n", *argv);
                return -1;
            }
            fileloaded = 1;
        }
        ++argv;
    }
    if (!fileloaded) fp = stdin;

    if (fp_alm) {
        if (read_SEMalmanac(fp_alm, gpx.gps.alm) == 0) {
            gpx.gps.almanac = 1;
        }
        fclose(fp_alm);
        if (!option_der) gpx.gps.d_err = 4000;
    }
    if (fp_eph) {
        /* i = read_RNXephemeris(fp_eph, eph);
           if (i == 0) {
               gpx.gps.ephem = 1;
               gpx.gps.almanac = 0;
           }
           fclose(fp_eph); */
        gpx.gps.ephs = read_RNXpephs(fp_eph);
        if (gpx.gps.ephs) {
            gpx.gps.ephem = 1;
            gpx.gps.almanac = 0;
        }
        fclose(fp_eph);
        if (!option_der) gpx.gps.d_err = 1000;
    }

    if (option_iq == 5 && option_dc) option_lp |= LP_FM;

    // LUT faster for decM, however frequency correction after decimation
    // LUT recommonded if decM > 2
    //
    if (option_noLUT && option_iq == 5) dsp.opt_nolut = 1; else dsp.opt_nolut = 0;


    gpx.option.crc = 1;
    if (gpx.option.ecc < 2) gpx.option.ecc = 1;  // turn off for ber-measurement

    if (gpx.option.ecc) {
        rs_init_RS255(&gpx.RS);
    }

    gpx.rs_type = RS92SGP;
    if (gpx.option.ngp) gpx.rs_type = RS92NGP;

    // init gpx
    memcpy(gpx.frame, rs92_header_bytes, sizeof(rs92_header_bytes)); // 6 header bytes

    if (cfreq > 0) gpx.jsn_freq = (cfreq+500)/1000;


    #ifdef EXT_FSK
    if (!option_softin) {
        option_softin = 1;
        fprintf(stderr, "reading float32 soft symbols\n");
    }
    #endif

    if (!rawhex) {

        if (!option_softin) {

            if (option_iq == 0 && option_pcmraw) {
                fclose(fp);
                fprintf(stderr, "error: raw data not IQ\n");
                return -1;
            }
            if (option_iq) sel_wavch = 0;

            pcm.sel_ch = sel_wavch;
            if (option_pcmraw == 0) {
                k = read_wav_header(&pcm, fp);
                if ( k < 0 ) {
                    fclose(fp);
                    fprintf(stderr, "error: wav header\n");
                    return -1;
                }
            }

            if (cfreq > 0) {
                int fq_kHz = (cfreq - dsp.xlt_fq*pcm.sr + 500)/1e3;
                gpx.jsn_freq = fq_kHz;
            }

            // rs92-sgp: BT=0.5, h=1.0 ?
            symlen = 2;

            // init dsp
            //
            dsp.fp = fp;
            dsp.sr = pcm.sr;
            dsp.bps = pcm.bps;
            dsp.nch = pcm.nch;
            dsp.ch = pcm.sel_ch;
            dsp.br = (float)BAUD_RATE;
            dsp.sps = (float)dsp.sr/dsp.br;
            dsp.symlen = symlen;
            dsp.symhd  = symlen;
            dsp._spb = dsp.sps*symlen;
            dsp.hdr = rs92_rawheader;
            dsp.hdrlen = strlen(rs92_rawheader);
            dsp.BT = 0.5; // bw/time (ISI) // 0.3..0.5
            dsp.h = 0.8; // 1.0 modulation index abzgl. BT
            dsp.opt_iq = option_iq;
            dsp.opt_iqdc = option_iqdc;
            dsp.opt_lp = option_lp;
            dsp.lpIQ_bw = 8e3; // IF lowpass bandwidth
            dsp.lpFM_bw = 6e3; // FM audio lowpass
            dsp.opt_dc = option_dc;
            dsp.opt_IFmin = option_min;
            if (gpx.option.ngp) { // L-band rs92-ngp
                dsp.h = 3.8;        // RS92-NGP: 1680/400=4.2, 4.2*0.9=3.8=4.75*0.8
                dsp.lpIQ_bw = 32e3; // IF lowpass bandwidth // 32e3=4.2*7.6e3 // 28e3..32e3
            }
            if (set_lpIQbw > 0.0f) dsp.lpIQ_bw = set_lpIQbw;

            if ( dsp.sps < 8 ) {
                fprintf(stderr, "note: sample rate low (%.1f sps)\n", dsp.sps);
            }


            k = init_buffers(&dsp); // BT=0.5  (IQ-Int: BT > 0.5 ?)
            if ( k < 0 ) {
                fprintf(stderr, "error: init buffers\n");
                return -1;
            };

            bitofs += shift;
        }
        else {
            // init circular header bit buffer
            hdb.hdr = rs92_rawheader;
            hdb.len = strlen(rs92_rawheader);
            //hdb.thb = 1.0 - 3.1/(float)hdb.len; // 1.0-max_bit_errors/hdrlen
            hdb.bufpos = -1;
            hdb.buf = NULL;
            /*
            calloc(hdb.len, sizeof(char));
            if (hdb.buf == NULL) {
                fprintf(stderr, "error: malloc\n");
                return -1;
            }
            */
            // caution ths=0.7: -3 byte offset, false positive
            // 2A 2A 2A 2A 2A 10|65 10 ..
            // header sync could be extended into the frame
            hdb.ths = 0.8;
            hdb.sbuf = calloc(hdb.len, sizeof(float));
            if (hdb.sbuf == NULL) {
                fprintf(stderr, "error: malloc\n");
                return -1;
            }
        }


        while ( 1 )
        {
            if (option_softin) {
                for (k = 0; k < hdb.len; k++) hdb.sbuf[k] = 0.0;
                header_found = find_softbinhead(fp, &hdb, &_mv, option_softin == 2);
            }
            else {
                header_found = find_header(&dsp, thres, 3, bitofs, dsp.opt_dc);
                _mv = dsp.mv;
            }

            if (header_found == EOF) break;

            // mv == correlation score
            if (_mv *(0.5-gpx.option.inv) < 0) {
                if (gpx.option.aut == 0) header_found = 0;
                else gpx.option.inv ^= 0x1;
            }

            if (header_found) {

                byte_count = FRAMESTART;
                bitpos = 0;
                b8pos = 0;

                while ( byte_count < FRAME_LEN ) {

                    if (option_softin) {
                        float s1 = 0.0;
                        float s2 = 0.0;
                        float s = 0.0;
                        bitQ = f32soft_read(fp, &s1, option_softin == 2);
                        if (bitQ != EOF) {
                            bitQ = f32soft_read(fp, &s2, option_softin == 2);
                            if (bitQ != EOF) {
                                s = s2-s1; // integrate both symbols  // only 2nd Manchester symbol: s2
                                bit = (s>=0.0); // no soft decoding
                            }
                        }
                    }
                    else {
                        float bl = -1;
                        if (option_iq > 2) bl = 4.0;
                        bitQ = read_slbit(&dsp, &bit, 0, bitofs, bitpos, bl, spike); // symlen=2
                    }
                    if ( bitQ == EOF) break;

                    if (gpx.option.inv) bit ^= 1;

                    bitpos += 1;
                    bitbuf[b8pos] = bit;
                    b8pos++;
                    if (b8pos >= BITS) {
                        b8pos = 0;
                        byte = bits2byte(bitbuf);
                        gpx.frame[byte_count] = byte;
                        byte_count++;
                    }
                }
                header_found = 0;
                print_frame(&gpx, byte_count);
                byte_count = FRAMESTART;

            }

        }


        if (!option_softin) free_buffers(&dsp);
        else {
            if (hdb.buf) { free(hdb.buf); hdb.buf = NULL; }
        }
    }
    else //if (rawhex)
    {
        char buffer_rawhex[2*FRAME_LEN+12];
        char *pbuf = NULL, *buf_sp = NULL;
        ui8_t frmbyte;
        int frameofs = 0, len, i;

        while (1 > 0) {

            pbuf = fgets(buffer_rawhex, 2*FRAME_LEN+12, fp);
            if (pbuf == NULL) break;
            buffer_rawhex[2*FRAME_LEN] = '\0';
            buf_sp = strchr(buffer_rawhex, ' ');
            if (buf_sp != NULL && buf_sp-buffer_rawhex < 2*FRAME_LEN) {
                buffer_rawhex[buf_sp-buffer_rawhex] = '\0';
            }
            len = strlen(buffer_rawhex) / 2;
            if (len > posGPS_TOW+4) {
                for (i = 0; i < len; i++) { //%2x  SCNx8=%hhx(inttypes.h)
                    sscanf(buffer_rawhex+2*i, "%2hhx", &frmbyte);
                    // wenn ohne %hhx: sscanf(buffer_rawhex+rawhex*i, "%2x", &byte); frame[frameofs+i] = (ui8_t)byte;
                    gpx.frame[frameofs+i] = frmbyte;
                }
                print_frame(&gpx, frameofs+len);
            }
        }
    }

    if (gpx.gps.ephs) free(gpx.gps.ephs);

    fclose(fp);

    return 0;
}


