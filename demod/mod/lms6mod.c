
/*
 *  LMS6
 *  (403 MHz)
 *
 *  sync header: correlation/matched filter
 *  files: lms6mod.c demod_mod.c demod_mod.h bch_ecc_mod.c bch_ecc_mod.h
 *  compile, either (a) or (b):
 *  (a)
 *      gcc -c demod_mod.c
 *      gcc -DINCLUDESTATIC lms6mod.c demod_mod.o -lm -o lms6mod
 *  (b)
 *      gcc -c demod_mod.c
 *      gcc -c bch_ecc_mod.c
 *      gcc lms6mod.c demod_mod.o bch_ecc_mod.o -lm -o lms6mod
 *
 *  usage:
 *      ./lms6mod --vit --ecc <audio.wav>
 *      ( --vit recommended)
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
    i8_t vit;
    i8_t jsn;  // JSON output (auto_rx)
} option_t;


/* -------------------------------------------------------------------------- */

#define BAUD_RATE   4800

#define BITS 8
#define HEADOFS  0 //16
#define HEADLEN ((4*16)-HEADOFS)

#define SYNC_LEN 5
#define FRM_LEN    (223)
#define PAR_LEN    (32)
#define FRMBUF_LEN (3*FRM_LEN)
#define BLOCKSTART (SYNC_LEN*BITS*2)
#define BLOCK_LEN  (FRM_LEN+PAR_LEN+SYNC_LEN)  // 255+5 = 260
#define RAWBITBLOCK_LEN ((BLOCK_LEN+1)*BITS*2) // (+1 tail)

#define FRAME_LEN       (300)  // 4800baud, 16bits/byte
#define BITFRAME_LEN    (FRAME_LEN*BITS)
#define RAWBITFRAME_LEN (BITFRAME_LEN*2)
#define OVERLAP 64
#define OFS 4


static char  rawheader[] = "0101011000001000""0001110010010111""0001101010100111""0011110100111110"; // (c0,inv(c1))
//                   (00)   58                f3                3f                b8
//     char  header[]    = "0000001101011101""0100100111000010""0100111111110010""0110100001101011"; // (c0,c1)
static ui8_t rs_sync[] = { 0x00, 0x58, 0xf3, 0x3f, 0xb8};
// 0x58f33fb8 little-endian <-> 0x1ACFFC1D big-endian bytes

//                            (00)               58                f3                3f                b8
static char  blk_syncbits[] = "0000000000000000""0000001101011101""0100100111000010""0100111111110010""0110100001101011";

static ui8_t frm_sync[] = { 0x24, 0x54, 0x00, 0x00};


#define L 7  // d_f=10
static char polyA[] = "1001111"; // 0x4f: x^6+x^3+x^2+x+1
static char polyB[] = "1101101"; // 0x6d: x^6+x^5+x^3+x^2+1
/*
// d_f=6
qA[] = "1110011";  // 0x73: x^6+x^5+x^4+x+1
qB[] = "0011110";  // 0x1e: x^4+x^3+x^2+x
pA[] = "10010101"; // 0x95: x^7+x^4+x^2+1 = (x+1)(x^6+x^5+x^4+x+1)        = (x+1)qA
pB[] = "00100010"; // 0x22: x^5+x         = (x+1)(x^4+x^3+x^2+x)=x(x+1)^3 = (x+1)qB
polyA = qA + x*qB
polyB = qA + qB
*/

#define N (1 << L)
#define M (1 << (L-1))

typedef struct {
    ui8_t bIn;
    ui8_t codeIn;
    ui8_t prevState;  // 0..M=64
    int w;  // > 255 : if (w>250): w=250 ?
    //float sw;
} states_t;

typedef struct {
    char rawbits[RAWBITFRAME_LEN+OVERLAP*BITS*2 +8];
    states_t state[RAWBITFRAME_LEN+OVERLAP +8][M];
    states_t d[N];
} VIT_t;

typedef struct {
    int frnr;
    int sn;
    int week; int gpstow;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double alt;
    double vH; double vD; double vV;
    double vE; double vN; double vU;
    char  blk_rawbits[RAWBITBLOCK_LEN+SYNC_LEN*BITS*2 +8];
    ui8_t frame[FRM_LEN];  // = { 0x24, 0x54, 0x00, 0x00}; // dataheader
    int frm_pos;     // ecc_blk <-> frm_blk
    int sf;
    option_t option;
    RS_t RS;
    VIT_t *vit;
} gpx_t;


// ------------------------------------------------------------------------

static ui8_t vit_code[N];
static vitCodes_init = 0;

static int vit_initCodes(gpx_t *gpx) {
    int cA, cB;
    int i, bits;

    VIT_t *pv = calloc(1, sizeof(VIT_t));
    if (pv == NULL) return -1;
    gpx->vit = pv;

    if ( vitCodes_init == 0 ) {
        for (bits = 0; bits < N; bits++) {
            cA = 0;
            cB = 0;
            for (i = 0; i < L; i++) {
                cA ^= (polyA[L-1-i]&1) & ((bits >> i)&1);
                cB ^= (polyB[L-1-i]&1) & ((bits >> i)&1);
            }
            vit_code[bits] = (cA<<1) | cB;
        }
        vitCodes_init = 1;
    }

    return 0;
}

static int vit_dist(int c, char *rc) {
    return (((c>>1)^rc[0])&1) + ((c^rc[1])&1);
}

static int vit_start(VIT_t *vit, char *rc) {
    int t, m, j, c, d;

    t = L-1;
    m = M;
    while ( t > 0 ) {  // t=0..L-2: nextState<M
        for (j = 0; j < m; j++) {
            vit->state[t][j].prevState = j/2;
        }
        t--;
        m /= 2;
    }

    m = 2;
    for (t = 1; t < L; t++) {
        for (j = 0; j < m; j++) {
            c = vit_code[j];
            vit->state[t][j].bIn = j % 2;
            vit->state[t][j].codeIn = c;
            d = vit_dist( c, rc+2*(t-1) );
            vit->state[t][j].w = vit->state[t-1][vit->state[t][j].prevState].w + d;
        }
        m *= 2;
    }

    return t;
}

static int vit_next(VIT_t *vit, int t, char *rc) {
    int b, nstate;
    int j, index;

    for (j = 0; j < M; j++) {
        for (b = 0; b < 2; b++) {
            nstate = j*2 + b;
            vit->d[nstate].bIn = b;
            vit->d[nstate].codeIn = vit_code[nstate];
            vit->d[nstate].prevState = j;
            vit->d[nstate].w = vit->state[t][j].w + vit_dist( vit->d[nstate].codeIn, rc );
        }
     }

    for (j = 0; j < M; j++) {

        if ( vit->d[j].w <= vit->d[j+M].w ) index = j; else index = j+M;

        vit->state[t+1][j] = vit->d[index];
    }

    return 0;
}

static int vit_path(VIT_t *vit, int j, int t) {
    int c;

    vit->rawbits[2*t] = '\0';
    while (t > 0) {
        c = vit->state[t][j].codeIn;
        vit->rawbits[2*t -2] = 0x30 + ((c>>1) & 1);
        vit->rawbits[2*t -1] = 0x30 + (c & 1);
        j = vit->state[t][j].prevState;
        t--;
    }

    return 0;
}

static int viterbi(VIT_t *vit, char *rc) {
    int t, tmax;
    int j, j_min, w_min;

    vit_start(vit, rc);

    tmax = strlen(rc)/2;

    for (t = L-1; t < tmax; t++)
    {
        vit_next(vit, t, rc+2*t);
    }

    w_min = -1;
    for (j = 0; j < M; j++) {
        if (w_min < 0) {
            w_min = vit->state[tmax][j].w;
            j_min = j;
        }
        if (vit->state[tmax][j].w < w_min) {
            w_min = vit->state[tmax][j].w;
            j_min = j;
        }
    }
    vit_path(vit, j_min, tmax);

    return 0;
}

// ------------------------------------------------------------------------

static int deconv(char* rawbits, char *bits) {

    int j, n, bitA, bitB;
    char *p;
    int len;
    int errors = 0;
    int m = L-1;

    len = strlen(rawbits);
    for (j = 0; j < m; j++) bits[j] = '0';
    n = 0;
    while ( 2*(m+n) < len ) {
        p = rawbits+2*(m+n);
        bitA = bitB = 0;
        for (j = 0; j < m; j++) {
            bitA ^= (bits[n+j]&1) & (polyA[j]&1);
            bitB ^= (bits[n+j]&1) & (polyB[j]&1);
        }
        if      ( (bitA^(p[0]&1))==(polyA[m]&1)  &&  (bitB^(p[1]&1))==(polyB[m]&1) ) bits[n+m] = '1';
        else if ( (bitA^(p[0]&1))==0             &&  (bitB^(p[1]&1))==0            ) bits[n+m] = '0';
        else {
            if ( (bitA^(p[0]&1))!=(polyA[m]&1) && (bitB^(p[1]&1))==(polyB[m]&1) ) bits[n+m] = 0x39;
            else bits[n+m] = 0x38;
            errors = n;
            break;
        }
        n += 1;
    }
    bits[n+m] = '\0';

    return errors;
}

// ------------------------------------------------------------------------

static int crc16_0(ui8_t frame[], int len) {
    int crc16poly = 0x1021;
    int rem = 0x0, i, j;
    int byte;

    for (i = 0; i < len; i++) {
        byte = frame[i];
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

static int check_CRC(ui8_t frame[]) {
    ui32_t crclen = 0,
           crcdat = 0;

    crclen = 221;
    crcdat = (frame[crclen]<<8) | frame[crclen+1];
    if ( crcdat != crc16_0(frame, crclen) ) {
        return 1;  // CRC NO
    }
    else return 0; // CRC OK
}

// ------------------------------------------------------------------------

static int bits2bytes(char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int len = strlen(bitstr)/8;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < len) {

        byteval = 0;
        d = 1;
        for (i = 0; i < BITS; i++) {
            bit=*(bitstr+bitpos+i);   /* little endian */
            //bit=*(bitstr+bitpos+7-i);  /* big endian */
            if        ((bit == '1') || (bit == '9'))    byteval += d;
            else /*if ((bit == '0') || (bit == '8'))*/  byteval += 0;
            d <<= 1;
        }
        bitpos += BITS;
        bytes[bytepos++] = byteval & 0xFF;
    }

    //while (bytepos < FRAME_LEN+OVERLAP) bytes[bytepos++] = 0;

    return bytepos;
}

/* -------------------------------------------------------------------------- */


#define pos_SondeSN  (OFS+0x00)  // ?4 byte 00 7A....
#define pos_FrameNb  (OFS+0x04)  // 2 byte
//GPS Position
#define pos_GPSTOW   (OFS+0x06)  // 4 byte
#define pos_GPSlat   (OFS+0x0E)  // 4 byte
#define pos_GPSlon   (OFS+0x12)  // 4 byte
#define pos_GPSalt   (OFS+0x16)  // 4 byte
//GPS Velocity East-North-Up (ENU)
#define pos_GPSvO    (OFS+0x1A)  // 3 byte
#define pos_GPSvN    (OFS+0x1D)  // 3 byte
#define pos_GPSvV    (OFS+0x20)  // 3 byte


static int get_SondeSN(gpx_t *gpx) {
    unsigned byte;

    byte =  (gpx->frame[pos_SondeSN]<<24) | (gpx->frame[pos_SondeSN+1]<<16)
          | (gpx->frame[pos_SondeSN+2]<<8) | gpx->frame[pos_SondeSN+3];
    gpx->sn = byte & 0xFFFFFF;

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

    frnr = (frnr_bytes[0] << 8) + frnr_bytes[1] ;
    gpx->frnr = frnr;

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
    float ms;

    for (i = 0; i < 4; i++) {
        byte = gpx->frame[pos_GPSTOW + i];
        gpstime_bytes[i] = byte;
    }
    gpstime = 0;
    for (i = 0; i < 4; i++) {
        gpstime |= gpstime_bytes[i] << (8*(3-i));
    }

    gpx->gpstow = gpstime;

    ms = gpstime % 1000;
    gpstime /= 1000;

    day = gpstime / (24 * 3600);
    gpstime %= (24*3600);

    if ((day < 0) || (day > 6)) return -1;

    gpx->wday = day;
    gpx->std = gpstime / 3600;
    gpx->min = (gpstime % 3600) / 60;
    gpx->sek = gpstime % 60 + ms/1000.0;

    return 0;
}

static double B60B60 = (1<<30)/90.0; // 2^32/360 = 2^30/90 = 0xB60B60.711x

static int get_GPSlat(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t gpslat_bytes[4];
    int gpslat;
    double lat;

    for (i = 0; i < 4; i++) {
        byte = gpx->frame[pos_GPSlat + i];
        gpslat_bytes[i] = byte;
    }

    gpslat = 0;
    for (i = 0; i < 4; i++) {
        gpslat |= gpslat_bytes[i] << (8*(3-i));
    }
    lat = gpslat / B60B60;
    gpx->lat = lat;

    return 0;
}

static int get_GPSlon(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t gpslon_bytes[4];
    int gpslon;
    double lon;

    for (i = 0; i < 4; i++) {
        byte = gpx->frame[pos_GPSlon + i];
        gpslon_bytes[i] = byte;
    }

    gpslon = 0;
    for (i = 0; i < 4; i++) {
        gpslon |= gpslon_bytes[i] << (8*(3-i));
    }
    lon = gpslon / B60B60;
    gpx->lon = lon;

    return 0;
}

static int get_GPSalt(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t gpsheight_bytes[4];
    int gpsheight;
    double height;

    for (i = 0; i < 4; i++) {
        byte = gpx->frame[pos_GPSalt + i];
        gpsheight_bytes[i] = byte;
    }

    gpsheight = 0;
    for (i = 0; i < 4; i++) {
        gpsheight |= gpsheight_bytes[i] << (8*(3-i));
    }
    height = gpsheight / 1000.0;
    gpx->alt = height;

    if (height < -200 || height > 60000) return -1;
    return 0;
}

static int get_GPSvel24(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t gpsVel_bytes[3];
    int vel24;
    double vx, vy, vz, dir; //, alpha;

    for (i = 0; i < 3; i++) {
        byte = gpx->frame[pos_GPSvO + i];
        gpsVel_bytes[i] = byte;
    }
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vx = vel24 / 1e3; // ost

    for (i = 0; i < 3; i++) {
        byte = gpx->frame[pos_GPSvN + i];
        gpsVel_bytes[i] = byte;
    }
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vy= vel24 / 1e3; // nord

    for (i = 0; i < 3; i++) {
        byte = gpx->frame[pos_GPSvV + i];
        gpsVel_bytes[i] = byte;
    }
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vz = vel24 / 1e3; // hoch

    gpx->vE = vx;
    gpx->vN = vy;
    gpx->vU = vz;


    gpx->vH = sqrt(vx*vx+vy*vy);
/*
    alpha = atan2(vy, vx)*180/M_PI;  // ComplexPlane (von x-Achse nach links) - GeoMeteo (von y-Achse nach rechts)
    dir = 90-alpha;                  // z=x+iy= -> i*conj(z)=y+ix=re(i(pi/2-t)), Achsen und Drehsinn vertauscht
    if (dir < 0) dir += 360;         // atan2(y,x)=atan(y/x)=pi/2-atan(x/y) , atan(1/t) = pi/2 - atan(t)
    gpx->vD2 = dir;
*/
    dir = atan2(vx, vy) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx->vD = dir;

    gpx->vV = vz;

    return 0;
}


// RS(255,223)-CCSDS
#define rs_N 255
#define rs_K 223
#define rs_R (rs_N-rs_K) // 32

static int lms6_ecc(gpx_t *gpx, ui8_t *cw) {
    int errors;
    ui8_t err_pos[rs_R],
          err_val[rs_R];

    errors = rs_decode(&gpx->RS, cw, err_pos, err_val);

    return errors;
}

static void print_frame(gpx_t *gpx, int crc_err, int len) {
    int err=0;

    if (gpx->frame[0] != 0)
    {
        //if ((gpx->frame[pos_SondeSN+1] & 0xF0) == 0x70)  // ? beginnen alle SNs mit 0x7A.... bzw 80..... ?
        if ( gpx->frame[pos_SondeSN+1] )
        {
            get_SondeSN(gpx);
            get_FrameNb(gpx);
            printf(" (%7d) ", gpx->sn);
            printf(" [%5d] ", gpx->frnr);
            err = get_GPStime(gpx);
            if (!err) printf("%s ", weekday[gpx->wday]);
            printf("%02d:%02d:%06.3f ", gpx->std, gpx->min, gpx->sek); // falls Rundung auf 60s: Ueberlauf

            get_GPSlat(gpx);
            get_GPSlon(gpx);
            err = get_GPSalt(gpx);
            if (!err) {
                printf(" lat: %.5f ", gpx->lat);
                printf(" lon: %.5f ", gpx->lon);
                printf(" alt: %.2fm ", gpx->alt);
                get_GPSvel24(gpx);
                //if (gpx->option.vbs == 2) printf("  (%.1f ,%.1f,%.1f) ", gpx->vE, gpx->vN, gpx->vU);
                printf("  vH: %.1fm/s  D: %.1f  vV: %.1fm/s ", gpx->vH, gpx->vD, gpx->vV);
            }

            if (crc_err==0) printf(" [OK]"); else printf(" [NO]");

            printf("\n");


            if (gpx->option.jsn) {
                // Print JSON output required by auto_rx.
                if (crc_err==0) { // CRC-OK
                    // UTC oder GPS?
                    printf("{ \"frame\": %d, \"id\": \"%d\", \"time\": \"%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f }\n",
                           gpx->frnr, gpx->sn, gpx->std, gpx->min, gpx->sek, gpx->lat, gpx->lon, gpx->alt, gpx->vH, gpx->vD, gpx->vV );
                    printf("\n");
                }
            }

        }
    }
}


static void proc_frame(gpx_t *gpx, int len) {
    int blk_pos = SYNC_LEN;
    ui8_t block_bytes[BLOCK_LEN+8];
    ui8_t rs_cw[rs_N];
    char  frame_bits[BITFRAME_LEN+OVERLAP*BITS +8];  // init L-1 bits mit 0
    char *rawbits = NULL;
    int i, j;
    int err = 0;
    int errs = 0;
    int crc_err = 0;
    int flen, blen;


    if ((len % 8) > 4) {
        while (len % 8) gpx->blk_rawbits[len++] = '0';
    }
    gpx->blk_rawbits[len] = '\0';

    flen = len / (2*BITS);

    if (gpx->option.vit == 1) {
        viterbi(gpx->vit, gpx->blk_rawbits);
        rawbits = gpx->vit->rawbits;
    }
    else rawbits = gpx->blk_rawbits;

    err = deconv(rawbits, frame_bits);

    if (err) { for (i=err; i < RAWBITBLOCK_LEN/2; i++) frame_bits[i] = 0; }


    blen = bits2bytes(frame_bits, block_bytes);
    for (j = blen; j < BLOCK_LEN+8; j++) block_bytes[j] = 0;


    if (gpx->option.ecc) {
        for (j = 0; j < rs_N; j++) rs_cw[rs_N-1-j] = block_bytes[SYNC_LEN+j];
        errs = lms6_ecc(gpx, rs_cw);
        for (j = 0; j < rs_N; j++) block_bytes[SYNC_LEN+j] = rs_cw[rs_N-1-j];
    }

    if (gpx->option.raw == 2) {
        for (i = 0; i < flen; i++) printf("%02x ", block_bytes[i]);
        if (gpx->option.ecc) printf("(%d)", errs);
        printf("\n");
    }
    else if (gpx->option.raw == 4  &&  gpx->option.ecc) {
        for (i = 0; i < rs_N; i++) printf("%02x", block_bytes[SYNC_LEN+i]);
        printf(" (%d)", errs);
        printf("\n");
    }
    else if (gpx->option.raw == 8) {
        if (gpx->option.vit == 1) {
            for (i = 0; i < len; i++) printf("%c", gpx->vit->rawbits[i]); printf("\n");
        }
        else {
            for (i = 0; i < len; i++) printf("%c", gpx->blk_rawbits[i]); printf("\n");
        }
    }

    blk_pos = SYNC_LEN;

    while ( blk_pos-SYNC_LEN < FRM_LEN ) {

        if (gpx->sf == 0) {
            while ( blk_pos-SYNC_LEN < FRM_LEN ) {
                gpx->sf = 0;
                for (j = 0; j < 4; j++) gpx->sf += (block_bytes[blk_pos+j] == frm_sync[j]);
                if (gpx->sf == 4)  {
                    gpx->frm_pos = 0;
                    break;
                }
                blk_pos++;
            }
        }

        if ( gpx->sf  &&  gpx->frm_pos < FRM_LEN ) {
            gpx->frame[gpx->frm_pos] = block_bytes[blk_pos];
            gpx->frm_pos++;
            blk_pos++;
        }

        if (gpx->frm_pos == FRM_LEN) {

            crc_err = check_CRC(gpx->frame);

            if (gpx->option.raw == 1) {
                for (i = 0; i < FRM_LEN; i++) printf("%02x ", gpx->frame[i]);
                if (crc_err==0) printf(" [OK]"); else printf(" [NO]");
                printf("\n");
            }

            if (gpx->option.raw == 0) print_frame(gpx, crc_err, len);

            gpx->frm_pos = 0;
            gpx->sf = 0;
        }

    }

}


int main(int argc, char **argv) {

    int option_inv = 0;    // invertiert Signal
    int option_iq = 0;
    int option_dc = 0;
    int wavloaded = 0;
    int sel_wavch = 0;     // audio channel: left

    FILE *fp = NULL;
    char *fpname = NULL;

    int k;

    int bit, rbit;
    int bitpos = 0;
    int bitQ;
    int pos;
    //int headerlen = 0;

    int header_found = 0;

    float thres = 0.76;
    float _mv = 0.0;

    int symlen = 1;
    int bitofs = 1; // +1 .. +2
    int shift = 0;

    unsigned int bc = 0;

    pcm_t pcm = {0};
    dsp_t dsp = {0};  //memset(&dsp, 0, sizeof(dsp));
/*
    // gpx_t _gpx = {0}; gpx_t *gpx = &_gpx;  // stack size ...
    gpx_t *gpx = NULL;
    gpx = calloc(1, sizeof(gpx_t));
    //memset(gpx, 0, sizeof(gpx_t));
*/
    gpx_t _gpx = {0}; gpx_t *gpx = &_gpx;


#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _setmode(_fileno(stdin), _O_BINARY);
#endif
    setbuf(stdout, NULL);


    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v, --verbose\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       --vit        (Viterbi)\n");
            fprintf(stderr, "       --ecc        (Reed-Solomon)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            gpx->option.vbs = 1;
        }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            gpx->option.raw = 1; // bytes - rs_ecc_codewords
        }
        else if ( (strcmp(*argv, "-r0") == 0) || (strcmp(*argv, "--raw0") == 0) ) {
            gpx->option.raw = 2; // bytes: sync + codewords
        }
        else if ( (strcmp(*argv, "-rc") == 0) || (strcmp(*argv, "--rawecc") == 0) ) {
            gpx->option.raw = 4; // rs_ecc_codewords
        }
        else if ( (strcmp(*argv, "-R") == 0) || (strcmp(*argv, "--RAW") == 0) ) {
            gpx->option.raw = 8; // rawbits
        }
        else if   (strcmp(*argv, "--ecc" ) == 0) { gpx->option.ecc = 1; } // RS-ECC
        else if   (strcmp(*argv, "--vit" ) == 0) { gpx->option.vit = 1; } // viterbi
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;  // nicht noetig
        }
        else if ( (strcmp(*argv, "--dc") == 0) ) {
            option_dc = 1;
        }
        else if ( (strcmp(*argv, "--ch2") == 0) ) { sel_wavch = 1; }  // right channel (default: 0=left)
        else if ( (strcmp(*argv, "--ths") == 0) ) {
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
        else if   (strcmp(*argv, "--json") == 0) {
            gpx->option.jsn = 1;
            gpx->option.ecc = 1;
            gpx->option.vit = 1;
        }
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


    if (gpx->option.raw == 4) gpx->option.ecc = 1;

    // init gpx
    memcpy(gpx->blk_rawbits, blk_syncbits, sizeof(blk_syncbits));
    memcpy(gpx->frame, frm_sync, sizeof(frm_sync));
    gpx->frm_pos = 0;     // ecc_blk <-> frm_blk
    gpx->sf = 0;

    gpx->option.inv = option_inv; // irrelevant

    if (option_iq) sel_wavch = 0;

    pcm.sel_ch = sel_wavch;
    k = read_wav_header(&pcm, fp);
    if ( k < 0 ) {
        fclose(fp);
        fprintf(stderr, "error: wav header\n");
        return -1;
    }

    symlen = 1;

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
    dsp.symhd = 1;
    dsp._spb = dsp.sps*symlen;
    dsp.hdr = rawheader;
    dsp.hdrlen = strlen(rawheader);
    dsp.BT = 1.5; // bw/time (ISI) // 1.0..2.0
    dsp.h = 0.9;  // 1.0 modulation index
    dsp.opt_iq = option_iq;

    if ( dsp.sps < 8 ) {
        fprintf(stderr, "note: sample rate low (%.1f sps)\n", dsp.sps);
    }

    //headerlen = dsp.hdrlen;

    k = init_buffers(&dsp);
    if ( k < 0 ) {
        fprintf(stderr, "error: init buffers\n");
        return -1;
    };


    if (gpx->option.vit) {
        k = vit_initCodes(gpx);
        if (k < 0) return -1;
    }
    if (gpx->option.ecc) {
        rs_init_RS255ccsds(&gpx->RS); // bch_ecc.c
    }


    bitofs += shift;


        while ( 1 )
        {

                header_found = find_header(&dsp, thres, 3, bitofs, option_dc);
                _mv = dsp.mv;

            if (header_found == EOF) break;

            // mv == correlation score
            if (_mv*(0.5-gpx->option.inv) < 0) {
                gpx->option.inv ^= 0x1;  // LMS6: irrelevant
            }

                if (header_found) {

                    bitpos = 0;
                    pos = BLOCKSTART;

                    if (_mv > 0) bc = 0; else bc = 1;

                    while ( pos < RAWBITBLOCK_LEN ) {

                        bitQ = read_slbit(&dsp, &rbit, 0, bitofs, bitpos, -1, 0); // symlen=1

                        if (bitQ == EOF) { break; }

                        bit = rbit ^ (bc%2);  // (c0,inv(c1))
                        gpx->blk_rawbits[pos] = 0x30 + bit;

                        bc++;
                        pos++;
                        bitpos += 1;
                    }

                    gpx->blk_rawbits[pos] = '\0';

                    proc_frame(gpx, pos);

                    if (pos < RAWBITBLOCK_LEN) break;

                    pos = BLOCKSTART;
                    header_found = 0;
                }

        }


    free_buffers(&dsp);
    if (gpx->vit) { free(gpx->vit); gpx->vit = NULL; }

    fclose(fp);

    return 0;
}

