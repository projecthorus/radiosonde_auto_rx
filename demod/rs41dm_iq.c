
/*
 *  rs41
 *  sync header: correlation/matched filter
 *  files: rs41dm_iq.c bch_ecc.c demod_iq.c demod_iq.h
 *  compile:
 *      gcc -c demod_iq.c
 *      gcc rs41dm_iq.c demod_iq.o -lm -o rs41dm_iq
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


#include "demod_iq.h"

#include "bch_ecc.c"  // RS/ecc/


typedef struct {
    i8_t vbs;  // verbose output
    i8_t raw;  // raw frames
    i8_t crc;  // CRC check output
    i8_t ecc;  // Reed-Solomon ECC
    i8_t sat;  // GPS sat data
    i8_t ptu;  // PTU: temperature
} option_t;

typedef struct {
    int typ;
    int msglen;
    int msgpos;
    int parpos;
    int hdrlen;
    int frmlen;
} rscfg_t;

rscfg_t cfg_rs41 = { 41, (320-56)/2, 56, 8, 8, 320}; // const: msgpos, parpos


#define NDATA_LEN 320                    // std framelen 320
#define XDATA_LEN 198
#define FRAME_LEN (NDATA_LEN+XDATA_LEN)  // max framelen 518
/*
ui8_t //xframe[FRAME_LEN] = { 0x10, 0xB6, 0xCA, 0x11, 0x22, 0x96, 0x12, 0xF8},    = xorbyte( frame)
         frame[FRAME_LEN] = { 0x86, 0x35, 0xf4, 0x40, 0x93, 0xdf, 0x1a, 0x60}; // = xorbyte(xframe)
*/
typedef struct {
    int out;
    int frnr;
    char id[9];
    ui8_t numSV;
    int week; int gpssec;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double alt;
    double vN; double vE; double vU;
    double vH; double vD; double vD2;
    float T;
    ui32_t crc;
    ui8_t frame[FRAME_LEN];
    ui8_t calibytes[51*16];
    ui8_t calfrchk[51];
    float ptu_Rf1;      // ref-resistor f1 (750 Ohm)
    float ptu_Rf2;      // ref-resistor f2 (1100 Ohm)
    float ptu_co1[3];   // { -243.911 , 0.187654 , 8.2e-06 }
    float ptu_calT1[3]; // calibration T1
    float ptu_co2[3];   // { -243.911 , 0.187654 , 8.2e-06 }
    float ptu_calT2[3]; // calibration T2-Hum
    ui16_t conf_fw; // firmware
    ui16_t conf_kt; // kill timer (sec)
    ui16_t conf_bt; // burst timer (sec)
    ui8_t  conf_bk; // burst kill
    ui32_t freq;    // freq/kHz
    char rstyp[9];  // RS41-SG, RS41-SGP
    int  aux;
    char xdata[XDATA_LEN+16]; // xdata: aux_str1#aux_str2 ...
    option_t option;
} gpx_t;


#define BITS    8
#define HEADLEN 64
#define FRAMESTART ((HEADLEN)/BITS)

/*                      10      B6      CA      11      22      96      12      F8      */
static char header[] = "0000100001101101010100111000100001000100011010010100100000011111";

static ui8_t header_bytes[8] = { 0x86, 0x35, 0xf4, 0x40, 0x93, 0xdf, 0x1a, 0x60};

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
/*
    frame[pos] = xframe[pos] ^ mask[pos % MASK_LEN];
*/

/* ------------------------------------------------------------------------------------ */

#define BAUD_RATE 4800

/* ------------------------------------------------------------------------------------ */
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

static int bits2byte(char bits[]) {
    int i, byteval=0, d=1;
    for (i = 0; i < 8; i++) {     // little endian
    /* for (i = 7; i >= 0; i--) { // big endian */
        if      (bits[i] == 1)  byteval += d;
        else if (bits[i] == 0)  byteval += 0;
        else return 0x100;
        d <<= 1;
    }
    return byteval;
}

/* ------------------------------------------------------------------------------------ */

static ui32_t u4(ui8_t *bytes) {  // 32bit unsigned int
    ui32_t val = 0;
    memcpy(&val, bytes, 4);
    return val;
}

static ui32_t u3(ui8_t *bytes) {  // 24bit unsigned int
    int val24 = 0;
    val24 = bytes[0] | (bytes[1]<<8) | (bytes[2]<<16);
    // = memcpy(&val, bytes, 3), val &= 0x00FFFFFF;
    return val24;
}

static int i3(ui8_t *bytes) {  // 24bit signed int
    int val = 0,
        val24 = 0;
    val = bytes[0] | (bytes[1]<<8) | (bytes[2]<<16);
    val24 = val & 0xFFFFFF; if (val24 & 0x800000) val24 = val24 - 0x1000000;
    return val24;
}

static ui32_t u2(ui8_t *bytes) {  // 16bit unsigned int
    return  bytes[0] | (bytes[1]<<8);
}

/*
double r8(ui8_t *bytes) {
    double val = 0;
    memcpy(&val, bytes, 8);
    return val;
}

float r4(ui8_t *bytes) {
    float val = 0;
    memcpy(&val, bytes, 4);
    return val;
}
*/

static int crc16(gpx_t *gpx, int start, int len) {
    int crc16poly = 0x1021;
    int rem = 0xFFFF, i, j;
    int byte;

    if (start+len+2 > FRAME_LEN) return -1;

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

static int check_CRC(gpx_t *gpx, ui32_t pos, ui32_t pck) {
    ui32_t crclen = 0,
           crcdat = 0;
    if (((pck>>8) & 0xFF) != gpx->frame[pos]) return -1;
    crclen = gpx->frame[pos+1];
    if (pos + crclen + 4 > FRAME_LEN) return -1;
    crcdat = u2(gpx->frame+pos+2+crclen);
    if ( crcdat != crc16(gpx, pos+2, crclen) ) {
        return 1;  // CRC NO
    }
    else return 0; // CRC OK
}


/*
GPS chip: ublox UBX-G6010-ST

  Pos: SubHeader, 1+1 byte (ID+LEN)
0x039: 7928   FrameNumber+SondeID
              +(0x050: 0732  CalFrames 0x00..0x32)
0x065: 7A2A   PTU
0x093: 7C1E   GPS1: RXM-RAW (0x02 0x10) Week, TOW, Sats
0x0B5: 7D59   GPS2: RXM-RAW (0x02 0x10) pseudorange, doppler
0x112: 7B15   GPS3: NAV-SOL (0x01 0x06) ECEF-POS, ECEF-VEL
0x12B: 7611   00
0x12B: 7Exx   AUX-xdata
*/

#define crc_FRAME    (1<<0)
#define xor_FRAME    0x1713  // ^0x6E3B=0x7928
#define pck_FRAME    0x7928
#define pos_FRAME     0x039
#define pos_FrameNb   0x03B  // 2 byte
#define pos_SondeID   0x03D  // 8 byte
#define pos_CalData   0x052  // 1 byte, counter 0x00..0x32
#define pos_Calfreq   0x055  // 2 byte, calfr 0x00
#define pos_Calburst  0x05E  // 1 byte, calfr 0x02
// ? #define pos_Caltimer  0x05A  // 2 byte, calfr 0x02 ?
#define pos_CalRSTyp  0x05B  // 8 byte, calfr 0x21 (+2 byte in 0x22?)
        // weitere chars in calfr 0x22/0x23; weitere ID

#define crc_PTU      (1<<1)
#define xor_PTU      0xE388  // ^0x99A2=0x0x7A2A
#define pck_PTU      0x7A2A  // PTU
#define pos_PTU       0x065

#define crc_GPS1     (1<<2)
#define xor_GPS1     0x9667  // ^0xEA79=0x7C1E
#define pck_GPS1     0x7C1E  // RXM-RAW (0x02 0x10)
#define pos_GPS1      0x093
#define pos_GPSweek   0x095  // 2 byte
#define pos_GPSiTOW   0x097  // 4 byte
#define pos_satsN     0x09B  // 12x2 byte (1: SV, 1: quality,strength)

#define crc_GPS2     (1<<3)
#define xor_GPS2     0xD7AD  // ^0xAAF4=0x7D59
#define pck_GPS2     0x7D59  // RXM-RAW (0x02 0x10)
#define pos_GPS2      0x0B5
#define pos_minPR     0x0B7  //        4 byte
#define pos_FF        0x0BB  //        1 byte
#define pos_dataSats  0x0BC  // 12x(4+3) byte (4: pseudorange, 3: doppler)

#define crc_GPS3     (1<<4)
#define xor_GPS3     0xB9FF  // ^0xC2EA=0x7B15
#define pck_GPS3     0x7B15  // NAV-SOL (0x01 0x06)
#define pos_GPS3      0x112
#define pos_GPSecefX  0x114  //   4 byte
#define pos_GPSecefY  0x118  //   4 byte
#define pos_GPSecefZ  0x11C  //   4 byte
#define pos_GPSecefV  0x120  // 3*2 byte
#define pos_numSats   0x126  //   1 byte
#define pos_sAcc      0x127  //   1 byte
#define pos_pDOP      0x128  //   1 byte

#define crc_AUX      (1<<5)
#define pck_AUX      0x7E00  // LEN variable
#define pos_AUX       0x12B

#define crc_ZERO     (1<<6)  // LEN variable
#define pck_ZERO     0x7600
#define pck_ZEROstd  0x7611  // NDATA std-frm, no aux
#define pos_ZEROstd   0x12B  // pos_AUX(0)


/*
  frame[pos_FRAME-1] == 0x0F: len == NDATA_LEN(320)
  frame[pos_FRAME-1] == 0xF0: len == FRAME_LEN(518)
*/
static int frametype(gpx_t *gpx) { // -4..+4: 0xF0 -> -4 , 0x0F -> +4
    int i;
    ui8_t b = gpx->frame[pos_FRAME-1];
    int ft = 0;
    for (i = 0; i < 4; i++) {
        ft += ((b>>i)&1) - ((b>>(i+4))&1);
    }
    return ft;
}


const double c = 299.792458e6;
const double L1 = 1575.42e6;

static int get_SatData(gpx_t *gpx) {
    int i, n;
    int sv;
    ui32_t minPR;
    int numSV;
    double pDOP, sAcc;

    fprintf(stdout, "[%d]\n", u2(gpx->frame+pos_FrameNb));

    fprintf(stdout, "iTOW: 0x%08X", u4(gpx->frame+pos_GPSiTOW));
    fprintf(stdout, "  week: 0x%04X", u2(gpx->frame+pos_GPSweek));
    fprintf(stdout, "\n");
    minPR = u4(gpx->frame+pos_minPR);
    fprintf(stdout, "minPR: %d", minPR);
    fprintf(stdout, "\n");

    for (i = 0; i < 12; i++) {
        n = i*7;
        sv = gpx->frame[pos_satsN+2*i];
        if (sv == 0xFF) break;
        fprintf(stdout, "    SV: %2d ", sv);
        //fprintf(stdout, " (%02x) ", gpx->frame[pos_satsN+2*i+1]);
        fprintf(stdout, "#  ");
        fprintf(stdout, "prMes: %.1f", u4(gpx->frame+pos_dataSats+n)/100.0 + minPR);
        fprintf(stdout, "  ");
        fprintf(stdout, "doMes: %.1f", -i3(gpx->frame+pos_dataSats+n+4)/100.0*L1/c);
        fprintf(stdout, "\n");
    }

    fprintf(stdout, "ECEF-POS: (%d,%d,%d)\n",
                     (i32_t)u4(gpx->frame+pos_GPSecefX),
                     (i32_t)u4(gpx->frame+pos_GPSecefY),
                     (i32_t)u4(gpx->frame+pos_GPSecefZ));
    fprintf(stdout, "ECEF-VEL: (%d,%d,%d)\n",
                     (i16_t)u2(gpx->frame+pos_GPSecefV+0),
                     (i16_t)u2(gpx->frame+pos_GPSecefV+2),
                     (i16_t)u2(gpx->frame+pos_GPSecefV+4));

    numSV = gpx->frame[pos_numSats];
    sAcc = gpx->frame[pos_sAcc]/10.0; if (gpx->frame[pos_sAcc] == 0xFF) sAcc = -1.0;
    pDOP = gpx->frame[pos_pDOP]/10.0; if (gpx->frame[pos_pDOP] == 0xFF) pDOP = -1.0;
    fprintf(stdout, "numSatsFix: %2d  sAcc: %.1f  pDOP: %.1f\n", numSV, sAcc, pDOP);


    fprintf(stdout, "CRC: ");
    fprintf(stdout, " %04X", pck_GPS1);
    if (check_CRC(gpx, pos_GPS1, pck_GPS1)==0) fprintf(stdout, "[OK]"); else fprintf(stdout, "[NO]");
    //fprintf(stdout, "[%+d]", check_CRC(gpx, pos_GPS1, pck_GPS1));
    fprintf(stdout, " %04X", pck_GPS2);
    if (check_CRC(gpx, pos_GPS2, pck_GPS2)==0) fprintf(stdout, "[OK]"); else fprintf(stdout, "[NO]");
    //fprintf(stdout, "[%+d]", check_CRC(gpx, pos_GPS2, pck_GPS2));
    fprintf(stdout, " %04X", pck_GPS3);
    if (check_CRC(gpx, pos_GPS3, pck_GPS3)==0) fprintf(stdout, "[OK]"); else fprintf(stdout, "[NO]");
    //fprintf(stdout, "[%+d]", check_CRC(gpx, pos_GPS3, pck_GPS3));

    fprintf(stdout, "\n");
    fprintf(stdout, "\n");

    return 0;
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

static int get_SondeID(gpx_t *gpx, int crc) {
    int i;
    unsigned byte;
    char sondeid_bytes[9];

    if (crc == 0) {
        for (i = 0; i < 8; i++) {
            byte = gpx->frame[pos_SondeID + i];
            //if ((byte < 0x20) || (byte > 0x7E)) return -1;
            sondeid_bytes[i] = byte;
        }
        sondeid_bytes[8] = '\0';
        if ( strncmp(gpx->id, sondeid_bytes, 8) != 0 ) {
            //for (i = 0; i < 51; i++) gpx->calfrchk[i] = 0;
            memset(gpx->calfrchk, 0, 51);
            memcpy(gpx->id, sondeid_bytes, 8);
            gpx->id[8] = '\0';
            // conf data
            gpx->conf_fw = 0;
            gpx->conf_kt = -1;
            gpx->conf_bt = 0;
            gpx->conf_bk = 0;
            gpx->freq = 0;
            memset(gpx->rstyp, 0, 9);
        }
    }

    return 0;
}

static int get_FrameConf(gpx_t *gpx) {
    int crc, err;
    ui8_t calfr;
    int i;

    crc = check_CRC(gpx, pos_FRAME, pck_FRAME);
    if (crc) gpx->crc |= crc_FRAME;

    err = crc;
    err |= get_FrameNb(gpx);
    err |= get_SondeID(gpx, crc);

    if (crc == 0) {
        calfr = gpx->frame[pos_CalData];
        if (gpx->calfrchk[calfr] == 0) // const?
        {                              // 0x32 not constant
            for (i = 0; i < 16; i++) {
                gpx->calibytes[calfr*16 + i] = gpx->frame[pos_CalData+1+i];
            }
            gpx->calfrchk[calfr] = 1;
        }
    }

    return err;
}

static int get_CalData(gpx_t *gpx) {

    memcpy(&(gpx->ptu_Rf1), gpx->calibytes+61, 4);  // 0x03*0x10+13
    memcpy(&(gpx->ptu_Rf2), gpx->calibytes+65, 4);  // 0x04*0x10+ 1

    memcpy(gpx->ptu_co1+0, gpx->calibytes+77, 4);  // 0x04*0x10+13
    memcpy(gpx->ptu_co1+1, gpx->calibytes+81, 4);  // 0x05*0x10+ 1
    memcpy(gpx->ptu_co1+2, gpx->calibytes+85, 4);  // 0x05*0x10+ 5

    memcpy(gpx->ptu_calT1+0, gpx->calibytes+89, 4);  // 0x05*0x10+ 9
    memcpy(gpx->ptu_calT1+1, gpx->calibytes+93, 4);  // 0x05*0x10+13
    memcpy(gpx->ptu_calT1+2, gpx->calibytes+97, 4);  // 0x06*0x10+ 1

    memcpy(gpx->ptu_co2+0, gpx->calibytes+293, 4);  // 0x12*0x10+ 5
    memcpy(gpx->ptu_co2+1, gpx->calibytes+297, 4);  // 0x12*0x10+ 9
    memcpy(gpx->ptu_co2+2, gpx->calibytes+301, 4);  // 0x12*0x10+13

    memcpy(gpx->ptu_calT2+0, gpx->calibytes+305, 4);  // 0x13*0x10+ 1
    memcpy(gpx->ptu_calT2+1, gpx->calibytes+309, 4);  // 0x13*0x10+ 5
    memcpy(gpx->ptu_calT2+2, gpx->calibytes+313, 4);  // 0x13*0x10+ 9

    return 0;
}

static float get_Tc0(gpx_t *gpx, ui32_t f, ui32_t f1, ui32_t f2) {
    // y  = (f - f1) / (f2 - f1);
    // y1 = (f - f1) / f2; // = (1 - f1/f2)*y
    float a =  3.9083e-3, // Pt1000 platinum resistance
          b = -5.775e-7,
          c = -4.183e-12; // below 0C, else C=0
    float *cal = gpx->ptu_calT1;
    float Rb = (f1*gpx->ptu_Rf2-f2*gpx->ptu_Rf1)/(f2-f1), // ofs
          Ra = f * (gpx->ptu_Rf2-gpx->ptu_Rf1)/(f2-f1) - Rb,
          raw = Ra/1000.0,
          g_r = 0.8024*cal[0] + 0.0176,  // empirisch
          r_o = 0.0705*cal[1] + 0.0011,  // empirisch
          r = raw * g_r + r_o,
          t = (-a + sqrt(a*a + 4*b*(r-1)))/(2*b); // t>0: c=0
    // R/R0 = 1 + at + bt^2 + c(t-100)t^3 , R0 = 1000 Ohm, t/Celsius
    return t;
}
static float get_Tc(gpx_t *gpx, ui32_t f, ui32_t f1, ui32_t f2) {
    float *p = gpx->ptu_co1;
    float *c  = gpx->ptu_calT1;
    float  g = (float)(f2-f1)/(gpx->ptu_Rf2-gpx->ptu_Rf1),       // gain
          Rb = (f1*gpx->ptu_Rf2-f2*gpx->ptu_Rf1)/(float)(f2-f1), // ofs
          Rc = f/g - Rb,
          //R = (Rc + c[1]) * c[0],
          //T = p[0] + p[1]*R + p[2]*R*R;
          R = Rc * c[0],
          T = (p[0] + p[1]*R + p[2]*R*R + c[1])*(1.0 + c[2]);
    return T;
}

static int get_PTU(gpx_t *gpx) {
    int err=0, i;
    int bR, bc1, bT1,
            bc2, bT2;
    ui32_t meas[12];
    float Tc = -273.15;
    float Tc0 = -273.15;

    get_CalData(gpx);

    err = check_CRC(gpx, pos_PTU, pck_PTU);
    if (err) gpx->crc |= crc_PTU;

    if (err == 0)
    {

        for (i = 0; i < 12; i++) {
            meas[i] = u3(gpx->frame+pos_PTU+2+3*i);
        }

        bR  = gpx->calfrchk[0x03] && gpx->calfrchk[0x04];
        bc1 = gpx->calfrchk[0x04] && gpx->calfrchk[0x05];
        bT1 = gpx->calfrchk[0x05] && gpx->calfrchk[0x06];
        bc2 = gpx->calfrchk[0x12] && gpx->calfrchk[0x13];
        bT2 = gpx->calfrchk[0x13];

        if (bR && bc1 && bT1) {
            Tc = get_Tc(gpx, meas[0], meas[1], meas[2]);
            Tc0 = get_Tc0(gpx, meas[0], meas[1], meas[2]);
        }
        gpx->T = Tc;

        if (gpx->option.vbs == 4)
        {
            printf("  h: %8.2f   # ", gpx->alt); // crc_GPS3 ?

            printf("1: %8d %8d %8d", meas[0], meas[1], meas[2]);
            printf("   #   ");
            printf("2: %8d %8d %8d", meas[3], meas[4], meas[5]);
            printf("   #   ");
            printf("3: %8d %8d %8d", meas[6], meas[7], meas[8]);
            printf("   #   ");
            if (Tc > -273.0) {
                printf("  T: %8.4f , T0: %8.4f ", Tc, Tc0);
            }
            printf("\n");

            if (gpx->alt > -100.0) {
                printf("    %9.2f ; %6.1f ; %6.1f ", gpx->alt, gpx->ptu_Rf1, gpx->ptu_Rf2);
                printf("; %10.6f ; %10.6f ; %10.6f ;", gpx->ptu_calT1[0], gpx->ptu_calT1[1], gpx->ptu_calT1[2]);
                printf("  %8d ; %8d ; %8d ", meas[0], meas[1], meas[2]);
                printf("; %10.6f ; %10.6f ; %10.6f ;", gpx->ptu_calT2[0], gpx->ptu_calT2[1], gpx->ptu_calT2[2]);
                printf("  %8d ; %8d ; %8d" , meas[6], meas[7], meas[8]);
                printf("\n");
            }
        }

    }

    return err;
}

static int get_GPSweek(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t gpsweek_bytes[2];
    int gpsweek;

    for (i = 0; i < 2; i++) {
        byte = gpx->frame[pos_GPSweek + i];
        gpsweek_bytes[i] = byte;
    }

    gpsweek = gpsweek_bytes[0] + (gpsweek_bytes[1] << 8);
    //if (gpsweek < 0) { gpx->week = -1; return -1; } // (short int)
    gpx->week = gpsweek;

    return 0;
}

//char weekday[7][3] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
static char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static int get_GPStime(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t gpstime_bytes[4];
    int gpstime = 0, // 32bit
        day;
    int ms;

    for (i = 0; i < 4; i++) {
        byte = gpx->frame[pos_GPSiTOW + i];
        gpstime_bytes[i] = byte;
    }

    memcpy(&gpstime, gpstime_bytes, 4);
    ms = gpstime % 1000;
    gpstime /= 1000;

    gpx->gpssec = gpstime;

    day = (gpstime / (24 * 3600)) % 7;
    //if ((day < 0) || (day > 6)) return -1;  // besser CRC-check

    gpstime %= (24*3600);

    gpx->wday = day;
    gpx->std = gpstime / 3600;
    gpx->min = (gpstime % 3600) / 60;
    gpx->sek = gpstime % 60 + ms/1000.0;

    return 0;
}

static int get_GPS1(gpx_t *gpx) {
    int err=0;

    // ((gpx->frame[pos_GPS1]<<8) | gpx->frame[pos_GPS1+1]) != pck_GPS1 ?
    if ( gpx->frame[pos_GPS1] != ((pck_GPS1>>8) & 0xFF) ) {
        gpx->crc |= crc_GPS1;
        return -1;
    }

    err = check_CRC(gpx, pos_GPS1, pck_GPS1);
    if (err) gpx->crc |= crc_GPS1;

    err |= get_GPSweek(gpx); // no plausibility-check
    err |= get_GPStime(gpx); // no plausibility-check

    return err;
}

static int get_GPS2(gpx_t *gpx) {
    int err=0;

    err = check_CRC(gpx, pos_GPS2, pck_GPS2);
    if (err) gpx->crc |= crc_GPS2;

    return err;
}

#define EARTH_a  6378137.0
#define EARTH_b  6356752.31424518
#define EARTH_a2_b2  (EARTH_a*EARTH_a - EARTH_b*EARTH_b)

const
double a = EARTH_a,
       b = EARTH_b,
       a_b = EARTH_a2_b2,
       e2  = EARTH_a2_b2 / (EARTH_a*EARTH_a),
       ee2 = EARTH_a2_b2 / (EARTH_b*EARTH_b);

static void ecef2elli(double X[], double *lat, double *lon, double *alt) {
    double phi, lam, R, p, t;

    lam = atan2( X[1] , X[0] );

    p = sqrt( X[0]*X[0] + X[1]*X[1] );
    t = atan2( X[2]*a , p*b );

    phi = atan2( X[2] + ee2 * b * sin(t)*sin(t)*sin(t) ,
                 p - e2 * a * cos(t)*cos(t)*cos(t) );

    R = a / sqrt( 1 - e2*sin(phi)*sin(phi) );
    *alt = p / cos(phi) - R;

    *lat = phi*180/M_PI;
    *lon = lam*180/M_PI;
}

static int get_GPSkoord(gpx_t *gpx) {
    int i, k;
    unsigned byte;
    ui8_t XYZ_bytes[4];
    int XYZ; // 32bit
    double X[3], lat, lon, alt;
    ui8_t gpsVel_bytes[2];
    short vel16; // 16bit
    double V[3], phi, lam, dir;


    for (k = 0; k < 3; k++) {

        for (i = 0; i < 4; i++) {
            byte = gpx->frame[pos_GPSecefX + 4*k + i];
            XYZ_bytes[i] = byte;
        }
        memcpy(&XYZ, XYZ_bytes, 4);
        X[k] = XYZ / 100.0;

        for (i = 0; i < 2; i++) {
            byte = gpx->frame[pos_GPSecefV + 2*k + i];
            gpsVel_bytes[i] = byte;
        }
        vel16 = gpsVel_bytes[0] | gpsVel_bytes[1] << 8;
        V[k] = vel16 / 100.0;

    }


    // ECEF-Position
    ecef2elli(X, &lat, &lon, &alt);
    gpx->lat = lat;
    gpx->lon = lon;
    gpx->alt = alt;
    if ((alt < -1000) || (alt > 80000)) return -3; // plausibility-check: altitude, if ecef=(0,0,0)


    // ECEF-Velocities
    // ECEF-Vel -> NorthEastUp
    phi = lat*M_PI/180.0;
    lam = lon*M_PI/180.0;
    gpx->vN = -V[0]*sin(phi)*cos(lam) - V[1]*sin(phi)*sin(lam) + V[2]*cos(phi);
    gpx->vE = -V[0]*sin(lam) + V[1]*cos(lam);
    gpx->vU =  V[0]*cos(phi)*cos(lam) + V[1]*cos(phi)*sin(lam) + V[2]*sin(phi);

    // NEU -> HorDirVer
    gpx->vH = sqrt(gpx->vN*gpx->vN+gpx->vE*gpx->vE);
/*
    double alpha;
    alpha = atan2(gpx->vN, gpx->vE)*180/M_PI;  // ComplexPlane (von x-Achse nach links) - GeoMeteo (von y-Achse nach rechts)
    dir = 90-alpha;                            // z=x+iy= -> i*conj(z)=y+ix=re(i(pi/2-t)), Achsen und Drehsinn vertauscht
    if (dir < 0) dir += 360;                   // atan2(y,x)=atan(y/x)=pi/2-atan(x/y) , atan(1/t) = pi/2 - atan(t)
    gpx->vD2 = dir;
*/
    dir = atan2(gpx->vE, gpx->vN) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx->vD = dir;

    gpx->numSV = gpx->frame[pos_numSats];

    return 0;
}

static int get_GPS3(gpx_t *gpx) {
    int err=0;

    // ((gpx->frame[pos_GPS3]<<8) | gpx->frame[pos_GPS3+1]) != pck_GPS3 ?
    if ( gpx->frame[pos_GPS3] != ((pck_GPS3>>8) & 0xFF) ) {
        gpx->crc |= crc_GPS3;
        return -1;
    }

    err = check_CRC(gpx, pos_GPS3, pck_GPS3);
    if (err) gpx->crc |= crc_GPS3;

    err |= get_GPSkoord(gpx); // plausibility-check: altitude, if ecef=(0,0,0)

    return err;
}

static int get_Aux(gpx_t *gpx) {
//
// "Ozone Sounding with Vaisala Radiosonde RS41" user's guide
//
    int auxlen, auxcrc, count7E, pos7E;
    int i, n;

    n = 0;
    count7E = 0;
    pos7E = pos_AUX;
    gpx->xdata[0] = '\0';

    if (frametype(gpx) <= 0) // pos7E == pos7611, 0x7E^0x76=0x08 ...
    {
        // 7Exx: xdata
        while ( pos7E < FRAME_LEN  &&  gpx->frame[pos7E] == 0x7E ) {

            auxlen = gpx->frame[pos7E+1];
            auxcrc = gpx->frame[pos7E+2+auxlen] | (gpx->frame[pos7E+2+auxlen+1]<<8);

            if ( auxcrc == crc16(gpx, pos7E+2, auxlen) ) {
                if (count7E == 0) fprintf(stdout, "\n # xdata = ");
                else { fprintf(stdout, " # "); gpx->xdata[n++] = '#'; }

                //fprintf(stdout, " # %02x : ", gpx->frame[pos7E+2]);
                for (i = 1; i < auxlen; i++) {
                    ui8_t c = gpx->frame[pos7E+2+i]; // (char) or better < 0x7F
                    if (c > 0x1E && c < 0x7F) {      // ASCII-only
                        fprintf(stdout, "%c", c);
                        gpx->xdata[n++] = c;
                    }
                }
                count7E++;
                pos7E += 2+auxlen+2;
            }
            else {
                pos7E = FRAME_LEN;
                gpx->crc |= crc_AUX;
            }
        }
    }
    gpx->xdata[n] = '\0';

    i = check_CRC(gpx, pos7E, 0x7600);  // 0x76xx: 00-padding block
    if (i) gpx->crc |= crc_ZERO;

    return count7E;
}

static int get_Calconf(gpx_t *gpx, int out) {
    int i;
    unsigned byte;
    ui8_t calfr = 0;
    ui16_t fw = 0;
    int freq = 0, f0 = 0, f1 = 0;
    char sondetyp[9];
    int err = 0;

    byte = gpx->frame[pos_CalData];
    calfr = byte;
    err = check_CRC(gpx, pos_FRAME, pck_FRAME);

    if (gpx->option.vbs == 3) {
        fprintf(stdout, "\n");  // fflush(stdout);
        fprintf(stdout, "[%5d] ", gpx->frnr);
        fprintf(stdout, " 0x%02x: ", calfr);
        for (i = 0; i < 16; i++) {
            byte = gpx->frame[pos_CalData+1+i];
            fprintf(stdout, "%02x ", byte);
        }
        if (err == 0) fprintf(stdout, "[OK]");
        else          fprintf(stdout, "[NO]");
        fprintf(stdout, " ");
    }

    if (err == 0)
    {
        if (calfr == 0x01) {
            fw = gpx->frame[pos_CalData+6] | (gpx->frame[pos_CalData+7]<<8);
            if (out && gpx->option.vbs) fprintf(stdout, ": fw 0x%04x ", fw);
            gpx->conf_fw = fw;
        }

        if (calfr == 0x02) {
            ui8_t  bk = gpx->frame[pos_Calburst];  // fw >= 0x4ef5, burst-killtimer in 0x31 relevant
            ui16_t kt = gpx->frame[0x5A] + (gpx->frame[0x5B] << 8); // killtimer (short?)
            if (out && gpx->option.vbs) fprintf(stdout, ": BK %02X ", bk);
            if (out && gpx->option.vbs && kt != 0xFFFF ) fprintf(stdout, ": kt %.1fmin ", kt/60.0);
            gpx->conf_bk = bk;
            gpx->conf_kt = kt;
        }

        if (calfr == 0x00) {
            byte = gpx->frame[pos_Calfreq] & 0xC0;  // erstmal nur oberste beiden bits
            f0 = (byte * 10) / 64;  // 0x80 -> 1/2, 0x40 -> 1/4 ; dann mal 40
            byte = gpx->frame[pos_Calfreq+1];
            f1 = 40 * byte;
            freq = 400000 + f1+f0; // kHz;
            if (out && gpx->option.vbs) fprintf(stdout, ": fq %d ", freq);
            gpx->freq = freq;
        }

        if (calfr == 0x31) {
            ui16_t bt = gpx->frame[0x59] + (gpx->frame[0x5A] << 8); // burst timer (short?)
            // fw >= 0x4ef5: default=[88 77]=0x7788sec=510min
            if (out && gpx->option.vbs && bt != 0x0000 && gpx->conf_bk) fprintf(stdout, ": bt %.1fmin ", bt/60.0);
            gpx->conf_bt = bt;
        }

        if (calfr == 0x21) {  // ... eventuell noch 2 bytes in 0x22
            for (i = 0; i < 9; i++) sondetyp[i] = 0;
            for (i = 0; i < 8; i++) {
                byte = gpx->frame[pos_CalRSTyp + i];
                if ((byte >= 0x20) && (byte < 0x7F)) sondetyp[i] = byte;
                else if (byte == 0x00) sondetyp[i] = '\0';
            }
            if (out && gpx->option.vbs) fprintf(stdout, ": %s ", sondetyp);
            strcpy(gpx->rstyp, sondetyp);
        }
    }

    return 0;
}

/* ------------------------------------------------------------------------------------ */

#define rs_N 255
#define rs_R 24
#define rs_K (rs_N-rs_R)

static int rs41_ecc(gpx_t *gpx, int frmlen) {
// richtige framelen wichtig fuer 0-padding

    int i, leak, ret = 0;
    int errors1, errors2;
    ui8_t cw1[rs_N], cw2[rs_N];
    ui8_t err_pos1[rs_R], err_pos2[rs_R],
          err_val1[rs_R], err_val2[rs_R];

    memset(cw1, 0, rs_N);
    memset(cw2, 0, rs_N);

    if (frmlen > FRAME_LEN) frmlen = FRAME_LEN;
    //cfg_rs41.frmlen = frmlen;
    //cfg_rs41.msglen = (frmlen-56)/2; // msgpos=56;
    leak = frmlen % 2;

    for (i = frmlen; i < FRAME_LEN; i++) gpx->frame[i] = 0;  // FRAME_LEN-HDR = 510 = 2*255


    for (i = 0; i < rs_R; i++) cw1[i] = gpx->frame[cfg_rs41.parpos+i     ];
    for (i = 0; i < rs_R; i++) cw2[i] = gpx->frame[cfg_rs41.parpos+i+rs_R];
    for (i = 0; i < rs_K; i++) cw1[rs_R+i] = gpx->frame[cfg_rs41.msgpos+2*i  ];
    for (i = 0; i < rs_K; i++) cw2[rs_R+i] = gpx->frame[cfg_rs41.msgpos+2*i+1];

    errors1 = rs_decode(cw1, err_pos1, err_val1);
    errors2 = rs_decode(cw2, err_pos2, err_val2);


    if (gpx->option.ecc == 2 && (errors1 < 0 || errors2 < 0))
    {   // 2nd pass
        gpx->frame[pos_FRAME] = (pck_FRAME>>8)&0xFF; gpx->frame[pos_FRAME+1] = pck_FRAME&0xFF;
        gpx->frame[pos_PTU]   = (pck_PTU  >>8)&0xFF; gpx->frame[pos_PTU  +1] = pck_PTU  &0xFF;
        gpx->frame[pos_GPS1]  = (pck_GPS1 >>8)&0xFF; gpx->frame[pos_GPS1 +1] = pck_GPS1 &0xFF;
        gpx->frame[pos_GPS2]  = (pck_GPS2 >>8)&0xFF; gpx->frame[pos_GPS2 +1] = pck_GPS2 &0xFF;
        gpx->frame[pos_GPS3]  = (pck_GPS3 >>8)&0xFF; gpx->frame[pos_GPS3 +1] = pck_GPS3 &0xFF;
        // AUX-frames mit vielen Fehlern besser mit 00 auffuellen
        // std-O3-AUX-frame: NDATA+7
        if (frametype(gpx) < -2) {  // ft >= 0: NDATA_LEN , ft < 0: FRAME_LEN
            for (i = NDATA_LEN + 7; i < FRAME_LEN-2; i++) gpx->frame[i] = 0;
        }
        else { // std-frm (len=320): std_ZERO-frame (7611 00..00 ECC7)
            for (i = NDATA_LEN; i < FRAME_LEN; i++) gpx->frame[i] = 0;
            gpx->frame[pos_ZEROstd  ] = 0x76;  // pck_ZEROstd
            gpx->frame[pos_ZEROstd+1] = 0x11;  // pck_ZEROstd
            for (i = pos_ZEROstd+2; i < NDATA_LEN-2; i++) gpx->frame[i] = 0;
            gpx->frame[NDATA_LEN-2] = 0xEC;    // crc(pck_ZEROstd)
            gpx->frame[NDATA_LEN-1] = 0xC7;    // crc(pck_ZEROstd)
        }
        for (i = 0; i < rs_K; i++) cw1[rs_R+i] = gpx->frame[cfg_rs41.msgpos+2*i  ];
        for (i = 0; i < rs_K; i++) cw2[rs_R+i] = gpx->frame[cfg_rs41.msgpos+2*i+1];
        errors1 = rs_decode(cw1, err_pos1, err_val1);
        errors2 = rs_decode(cw2, err_pos2, err_val2);
    }


    // Wenn Fehler im 00-padding korrigiert wurden,
    // war entweder der frame zu kurz, oder
    // Fehler wurden falsch korrigiert;
    // allerdings ist bei t=12 die Wahrscheinlichkeit,
    // dass falsch korrigiert wurde mit 1/t! sehr gering.

    // check CRC32
    // CRC32 OK:
    //for (i = 0; i < cfg_rs41.hdrlen; i++) frame[i] = data[i];
    for (i = 0; i < rs_R; i++) {
        gpx->frame[cfg_rs41.parpos+     i] = cw1[i];
        gpx->frame[cfg_rs41.parpos+rs_R+i] = cw2[i];
    }
    for (i = 0; i < rs_K; i++) { // cfg_rs41.msglen <= rs_K
        gpx->frame[cfg_rs41.msgpos+  2*i] = cw1[rs_R+i];
        gpx->frame[cfg_rs41.msgpos+1+2*i] = cw2[rs_R+i];
    }
    if (leak) {
        gpx->frame[cfg_rs41.msgpos+2*i] = cw1[rs_R+i];
    }


    ret = errors1 + errors2;
    if (errors1 < 0 || errors2 < 0) {
        ret = 0;
        if (errors1 < 0) ret |= 0x1;
        if (errors2 < 0) ret |= 0x2;
        ret = -ret;
    }

    return ret;
}

/* ------------------------------------------------------------------------------------ */


static int print_position(gpx_t *gpx, int ec) {
    int i;
    int err, err0, err1, err2, err3;
    int output, out_mask;

    gpx->out = 0;
    gpx->aux = 0;

    err = get_FrameConf(gpx);

    err1 = get_GPS1(gpx);
    err2 = get_GPS2(gpx);
    err3 = get_GPS3(gpx);

    err0 = get_PTU(gpx);

    out_mask = crc_FRAME|crc_GPS1|crc_GPS3;
    output = ((gpx->crc & out_mask) != out_mask);  // (!err || !err1 || !err3);

    if (output) {

        gpx->out = 1; // cf. gpx->crc

        if (!err) {
            fprintf(stdout, "[%5d] ", gpx->frnr);
            fprintf(stdout, "(%s) ", gpx->id);
        }
        if (!err1) {
            Gps2Date(gpx);
            fprintf(stdout, "%s ", weekday[gpx->wday]);
            fprintf(stdout, "%04d-%02d-%02d %02d:%02d:%06.3f",
                    gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek);
            if (gpx->option.vbs == 3) fprintf(stdout, " (W %d)", gpx->week);
        }
        if (!err3) {
            fprintf(stdout, " ");
            fprintf(stdout, " lat: %.5f ", gpx->lat);
            fprintf(stdout, " lon: %.5f ", gpx->lon);
            fprintf(stdout, " alt: %.2f ", gpx->alt);
            //if (gpx->option.vbs)
            {
                //fprintf(stdout, "  (%.1f %.1f %.1f) ", gpx->vN, gpx->vE, gpx->vU);
                fprintf(stdout,"  vH: %4.1f  D: %5.1f°  vV: %3.1f ", gpx->vH, gpx->vD, gpx->vU);
                if (gpx->option.vbs == 3) fprintf(stdout," sats: %02d ", gpx->numSV);
            }
        }
        if (gpx->option.ptu && !err0) {
            if (gpx->T > -273.0) printf("  T=%.1fC ", gpx->T);
        }


        if (gpx->option.crc) {
            fprintf(stdout, " # ");
            if (gpx->option.ecc && ec >= 0 && (gpx->crc & 0x1F) != 0) {
                int pos, blk, len, crc;   // unexpected blocks
                int flen = NDATA_LEN;
                if (frametype(gpx) < 0) flen += XDATA_LEN;
                pos = pos_FRAME;
                while (pos < flen-1) {
                    blk = gpx->frame[pos];     // 0x80XX: encrypted block
                    len = gpx->frame[pos+1];   // 0x76XX: 00-padding block
                    crc = check_CRC(gpx, pos, blk<<8);
                    fprintf(stdout, " %02X%02X", gpx->frame[pos], gpx->frame[pos+1]);
                    fprintf(stdout, "[%d]", crc&1);
                    pos = pos+2+len+2;
                }
            }
            else {
                fprintf(stdout, "[");
                for (i=0; i<5; i++) fprintf(stdout, "%d", (gpx->crc>>i)&1);
                fprintf(stdout, "]");
            }
            if (gpx->option.ecc == 2) {
                if (ec > 0) fprintf(stdout, " (%d)", ec);
                if (ec < 0) {
                    if      (ec == -1)  fprintf(stdout, " (-+)");
                    else if (ec == -2)  fprintf(stdout, " (+-)");
                    else   /*ec == -3*/ fprintf(stdout, " (--)");
                }
            }
        }

        get_Calconf(gpx, output);

        if (gpx->option.vbs > 1) gpx->aux = get_Aux(gpx);

        fprintf(stdout, "\n");  // fflush(stdout);

    }

    err |=  err1 | err3;

    return  err;
}

static void print_frame(gpx_t *gpx, int len) {
    int i, ec = 0, ft;

    gpx->crc = 0;

    // len < NDATA_LEN: EOF
    if (len < pos_GPS1) { // else: try prev.frame
        for (i = len; i < FRAME_LEN; i++) gpx->frame[i] = 0;
    }

    //frame[pos_FRAME-1] == 0x0F: len == NDATA_LEN(320)
    //frame[pos_FRAME-1] == 0xF0: len == FRAME_LEN(518)
    ft = frametype(gpx);
    if (ft >= 0) len = NDATA_LEN;  // ft >= 0: NDATA_LEN (default)
    else         len = FRAME_LEN;  // ft <  0: FRAME_LEN (aux)

    if (gpx->option.ecc) {
        ec = rs41_ecc(gpx, len);
    }


    if (gpx->option.raw) {
        for (i = 0; i < len; i++) {
            fprintf(stdout, "%02x", gpx->frame[i]);
        }
        if (gpx->option.ecc) {
            if (ec >= 0) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
            if (gpx->option.ecc == 2) {
                if (ec > 0) fprintf(stdout, " (%d)", ec);
                if (ec < 0) {
                    if      (ec == -1)  fprintf(stdout, " (-+)");
                    else if (ec == -2)  fprintf(stdout, " (+-)");
                    else   /*ec == -3*/ fprintf(stdout, " (--)");
                }
            }
        }
        fprintf(stdout, "\n");
    }
    else if (gpx->option.sat) {
        get_SatData(gpx);
    }
    else {
        print_position(gpx, ec);
    }
}


int main(int argc, char *argv[]) {

    int option_inv = 0;    // invertiert Signal
    int option_iq = 0;
    int option_ofs = 0;
    int wavloaded = 0;
    int sel_wavch = 0;     // audio channel: left
    int rawhex = 0, xorhex = 0;

    FILE *fp;
    char *fpname = NULL;
    char bitbuf[8];
    int bit_count = 0,
        bitpos = 0,
        byte_count = FRAMESTART,
        header_found = 0;
    int bit, byte;
    int herrs;
    int bitQ;

    int k, K;
    float mv;
    ui32_t mv_pos, mv0_pos;
    int mp = 0;

    float thres = 0.7;

    int symlen = 1;
    int bitofs = 2;
    int shift = 0;

    pcm_t pcm = {0};
    dsp_t dsp = {0};

    gpx_t gpx = {0};


#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _fileno(stdin)
#endif
    setbuf(stdout, NULL);


    // init gpx
    memcpy(gpx.frame, header_bytes, sizeof(header_bytes)); // 8 header bytes


    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v, -vx, -vv  (info, aux, info/conf)\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       -i, --invert\n");
            fprintf(stderr, "       --crc        (check CRC)\n");
            fprintf(stderr, "       --ecc2       (Reed-Solomon 2-pass)\n");
            fprintf(stderr, "       --ths <x>    (peak threshold; default=%.1f)\n", thres);
            fprintf(stderr, "       --iq0,2,3    (IQ data)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            gpx.option.vbs = 1;
        }
        else if   (strcmp(*argv, "-vx") == 0) { gpx.option.vbs = 2; }
        else if   (strcmp(*argv, "-vv") == 0) { gpx.option.vbs = 3; }
        else if   (strcmp(*argv, "-vvv") == 0) { gpx.option.vbs = 4; }
        else if   (strcmp(*argv, "--crc") == 0) { gpx.option.crc = 1; }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            gpx.option.raw = 1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
        }
        else if   (strcmp(*argv, "--ecc" ) == 0) { gpx.option.ecc = 1; }
        else if   (strcmp(*argv, "--ecc2") == 0) { gpx.option.ecc = 2; }
        else if   (strcmp(*argv, "--sat") == 0) { gpx.option.sat = 1; }
        else if   (strcmp(*argv, "--ptu") == 0) { gpx.option.ptu = 1; }
        else if   (strcmp(*argv, "--ch2") == 0) { sel_wavch = 1; }  // right channel (default: 0=left)
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
        else if   (strcmp(*argv, "--ofs") == 0) { option_ofs = 1; }
        else if   (strcmp(*argv, "--rawhex") == 0) { rawhex = 2; }  // raw hex input
        else if   (strcmp(*argv, "--xorhex") == 0) { rawhex = 2; xorhex = 1; }  // raw xor input
        else {
            fp = fopen(*argv, "rb");
            if (fp == NULL) {
                fprintf(stderr, "%s konnte nicht geoeffnet werden\n", *argv);
                return -1;
            }
            wavloaded = 1;
        }
        ++argv;
    }
    if (!wavloaded) fp = stdin;


    if (gpx.option.ecc < 2) gpx.option.ecc = 1;

    if (gpx.option.ecc) {
        rs_init_RS255(); // ... rs_init_RS255(&RS);
    }



    if (!rawhex) {

        if (option_iq) sel_wavch = 0;

        pcm.sel_ch = sel_wavch;
        k = read_wav_header(&pcm, fp);
        if ( k < 0 ) {
            fclose(fp);
            fprintf(stderr, "error: wav header\n");
            return -1;
        }

        // rs41: BT=0.5, h=0.8,1.0 ?
        symlen = 1;

        // init dsp
        //
        //memset(&dsp, 0, sizeof(dsp));
        dsp.fp = fp;
        dsp.sr = pcm.sr;
        dsp.bps = pcm.bps;
        dsp.nch = pcm.nch;
        dsp.ch = pcm.sel_ch;
        dsp.br = (float)BAUD_RATE;
        dsp.sps = (float)dsp.sr/dsp.br;
        dsp.symlen = symlen;
        dsp._spb = dsp.sps*symlen;
        dsp.hdr = header;
        dsp.hdrlen = strlen(header);
        dsp.BT = 0.5; // bw/time (ISI) // 0.3..0.5
        dsp.h = 1.0;  // 0.8..0.9? modulation index
        dsp.opt_iq = option_iq;

        if ( dsp.sps < 8 ) {
            fprintf(stderr, "note: sample rate low (%.1f sps)\n", dsp.sps);
        }


        K = init_buffers(&dsp); // BT=0.5  (IQ-Int: BT > 0.5 ?)
        if ( K < 0 ) {
            fprintf(stderr, "error: init buffers\n");
            return -1;
        };

        //if (option_iq >= 2) bitofs += 1;
        bitofs += shift; // +0 .. +3    // FM: +1 , IQ: +2

        k = 0;
        mv = 0.0;
        mv_pos = 0;

        while ( f32buf_sample(&dsp, option_inv) != EOF ) {

            k += 1;
            if (k >= dsp.K-4) {
                mv0_pos = mv_pos;
                mp = getCorrDFT(&dsp, 0, 0, &mv, &mv_pos);
                k = 0;
            }
            else {
                mv = 0.0;
                continue;
            }

            if (mv > thres && mp > 0) {
                if (mv_pos > mv0_pos) {

                    header_found = 0;
                    herrs = headcmp(&dsp, symlen, mv_pos, mv<0, 0); // symlen=1
                    if (herrs <= 3) header_found = 1; // herrs <= 3 bitfehler in header

                    if (header_found) {

                        if (/*preamble &&*/ option_ofs /*option_iq*/)
                        {
                            float freq = 0.0;
                            float snr = 0.0;
                            int er = get_fqofs_rs41(&dsp, mv_pos, &freq, &snr);
                        }

                        byte_count = FRAMESTART;
                        bit_count = 0; // byte_count*8-HEADLEN
                        bitpos = 0;

                        while ( byte_count < FRAME_LEN ) {
                            if (option_iq >= 2) {
                                bitQ = read_slbit(&dsp, symlen, &bit, option_inv, bitofs, bit_count, 1.0);
                            }
                            else {
                                bitQ = read_slbit(&dsp, symlen, &bit, option_inv, bitofs, bit_count, -1);
                            }
                            if ( bitQ == EOF) break;
                            bit_count += 1;
                            bitbuf[bitpos] = bit;
                            bitpos++;
                            if (bitpos == BITS) {
                                bitpos = 0;
                                byte = bits2byte(bitbuf);
                                gpx.frame[byte_count] = byte ^ mask[byte_count % MASK_LEN];
                                byte_count++;
                            }
                        }

                        print_frame(&gpx, byte_count);
                        byte_count = FRAMESTART;
                        header_found = 0;
                    }
                }
            }

        }

        free_buffers(&dsp);
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
            if (len > pos_SondeID+10) {
                for (i = 0; i < len; i++) { //%2x  SCNx8=%hhx(inttypes.h)
                    sscanf(buffer_rawhex+2*i, "%2hhx", &frmbyte);
                    // wenn ohne %hhx: sscanf(buffer_rawhex+rawhex*i, "%2x", &byte); frame[frameofs+i] = (ui8_t)byte;
                    if (xorhex) frmbyte ^= mask[(frameofs+i) % MASK_LEN];
                    gpx.frame[frameofs+i] = frmbyte;
                }
                print_frame(&gpx, frameofs+len);
            }
        }
    }


    fclose(fp);

    return 0;
}

