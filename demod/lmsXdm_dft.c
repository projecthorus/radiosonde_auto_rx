
/*
 *  LMSx
 *  (403 MHz)
 *
 *  sync header: correlation/matched filter
 *  files: lmsXdm_dft.c demod_dft.h demod_dft.c bch_ecc.c
 *  compile:
 *      gcc -c demod_dft.c
 *      gcc lmsXdm_dft.c demod_dft.o -lm -o lmsXdm_dft
 *  usage:
 *      ./lmsXdm_dft -v --vit --ecc <audio.wav>
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


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;

#include "demod_dft.h"

#include "bch_ecc.c"  // RS/ecc/


int option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 0,      // rohe Frames
    option_ecc = 0,
    option_vit = 0,
    option_inv = 0,      // invertiert Signal
    option_dc = 0,
    wavloaded = 0;
int wav_channel = 0;     // audio channel: left


/* -------------------------------------------------------------------------- */

#define BAUD_RATE   (4797.7)  // = 4800 / (48023/48000) ?

#define BITS 8
#define HEADOFS  0 //16
#define HEADLEN ((4*16)-HEADOFS)

char rawheader[] = "0101011000001000""0001110010010111""0001101010100111""0011110100111110"; // (c0,inv(c1))
//         (00)     58                f3                3f                b8
//char header[]  = "0000001101011101""0100100111000010""0100111111110010""0110100001101011"; // (c0,c1)
ui8_t rs_sync[] = { 0x00, 0x58, 0xf3, 0x3f, 0xb8};
// 0x58f33fb8 little-endian <-> 0x1ACFFC1D big-endian bytes

#define FRAME_LEN  (300)  // 4800baud, 16bits/byte
#define SYNC_LEN 5
#define FRM_LEN    (223)
#define PAR_LEN    (32)
#define FRMBUF_LEN (3*FRM_LEN)
#define BLOCKSTART (SYNC_LEN*BITS*2)
#define BLOCK_LEN  (FRM_LEN+PAR_LEN+SYNC_LEN)  // 255+5 = 260
//#define RAWBITBLOCK_LEN ((BLOCK_LEN+1)*BITS*2) // (+1 tail)
#define RAWBITBLOCK_LEN ((300)*BITS*2)

//                                                      (00)               58                f3                3f                b8
char  blk_rawbits[RAWBITBLOCK_LEN+SYNC_LEN*BITS*2 +8] = "0000000000000000""0000001101011101""0100100111000010""0100111111110010""0110100001101011";
//char  *block_rawbits = blk_rawbits+SYNC_LEN*BITS*2;

float  soft_rawbits[RAWBITBLOCK_LEN+SYNC_LEN*BITS*2 +8] =
 { -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0,
   -1.0, -1.0, -1.0, -1.0, -1.0, -1.0,  1.0,  1.0, -1.0,  1.0, -1.0,  1.0,  1.0,  1.0, -1.0,  1.0,
   -1.0,  1.0, -1.0, -1.0,  1.0, -1.0, -1.0,  1.0,  1.0,  1.0, -1.0, -1.0, -1.0, -1.0,  1.0, -1.0,
   -1.0,  1.0, -1.0, -1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0, -1.0, -1.0,  1.0, -1.0,
   -1.0,  1.0,  1.0, -1.0,  1.0, -1.0, -1.0, -1.0, -1.0,  1.0,  1.0, -1.0,  1.0, -1.0,  1.0,  1.0 };

ui8_t block_bytes[FRAME_LEN+8];  // BLOCK_LEN + 40


//ui8_t frm_sync[] = { 0x24, 0x54, 0x00, 0x00};
ui8_t frm_sync[] = { 0x24, 0x46, 0x05, 0x00};
ui8_t frame[FRM_LEN] = { 0x24, 0x54, 0x00, 0x00}; // dataheader

ui8_t *p_frame = frame;


#define BITFRAME_LEN    (FRAME_LEN*BITS)
#define RAWBITFRAME_LEN (BITFRAME_LEN*2)
#define OVERLAP 64
#define OFS 4


char  frame_bits[BITFRAME_LEN+OVERLAP*BITS +8];  // init L-1 bits mit 0

#define L 7  // d_f=10
    char polyA[] = "1001111"; // 0x4f: x^6+x^3+x^2+x+1
    char polyB[] = "1101101"; // 0x6d: x^6+x^5+x^3+x^2+1
/*
// d_f=6
qA[] = "1110011";  // 0x73: x^6+x^5+x^4+x+1
qB[] = "0011110";  // 0x1e: x^4+x^3+x^2+x
pA[] = "10010101"; // 0x95: x^7+x^4+x^2+1 = (x+1)(x^6+x^5+x^4+x+1)        = (x+1)qA
pB[] = "00100010"; // 0x22: x^5+x         = (x+1)(x^4+x^3+x^2+x)=x(x+1)^3 = (x+1)qB
polyA = qA + x*qB
polyB = qA + qB
*/

char vit_rawbits[RAWBITFRAME_LEN+OVERLAP*BITS*2 +8];
char vits_rawbits[RAWBITFRAME_LEN+OVERLAP*BITS*2 +8];
char vits_bits[BITFRAME_LEN+OVERLAP*BITS +8];

#define N (1 << L)
#define M (1 << (L-1))

typedef struct {
    ui8_t bIn;
    ui8_t codeIn;
    int w;
    int prevState;
    float sw;
} states_t;

states_t vit_state[RAWBITFRAME_LEN+OVERLAP +8][M];

states_t vit_d[N];

ui8_t vit_code[N];


int vit_initCodes() {
    int cA, cB;
    int i, bits;

    for (bits = 0; bits < N; bits++) {
        cA = 0;
        cB = 0;
        for (i = 0; i < L; i++) {
            cA ^= (polyA[L-1-i]&1) & ((bits >> i)&1);
            cB ^= (polyB[L-1-i]&1) & ((bits >> i)&1);
        }
        vit_code[bits] = (cA<<1) | cB;
    }

    return 0;
}

int vit_dist(int c, char *rc) {
    return (((c>>1)^rc[0])&1) + ((c^rc[1])&1);
}

int vit_start(char *rc) {
    int t, m, j, c, d;

    t = L-1;
    m = M;
    while ( t > 0 ) {  // t=0..L-2: nextState<M
        for (j = 0; j < m; j++) {
            vit_state[t][j].prevState = j/2;
        }
        t--;
        m /= 2;
    }

    m = 2;
    for (t = 1; t < L; t++) {
        for (j = 0; j < m; j++) {
            c = vit_code[j];
            vit_state[t][j].bIn = j % 2;
            vit_state[t][j].codeIn = c;
            d = vit_dist( c, rc+2*(t-1) );
            vit_state[t][j].w = vit_state[t-1][vit_state[t][j].prevState].w + d;
        }
        m *= 2;
    }

    return t;
}

int vit_next(int t, char *rc) {
    int b, nstate;
    int j, index;

    for (j = 0; j < M; j++) {
        for (b = 0; b < 2; b++) {
            nstate = j*2 + b;
            vit_d[nstate].bIn = b;
            vit_d[nstate].codeIn = vit_code[nstate];
            vit_d[nstate].prevState = j;
            vit_d[nstate].w = vit_state[t][j].w + vit_dist( vit_d[nstate].codeIn, rc );
        }
     }

    for (j = 0; j < M; j++) {

        if ( vit_d[j].w <= vit_d[j+M].w ) index = j; else index = j+M;

        vit_state[t+1][j] = vit_d[index];
    }

    return 0;
}

int vit_path(int j, int t) {
    int c;

    vit_rawbits[2*t] = '\0';
    while (t > 0) {
        c = vit_state[t][j].codeIn;
        vit_rawbits[2*t -2] = 0x30 + ((c>>1) & 1);
        vit_rawbits[2*t -1] = 0x30 + (c & 1);
        j = vit_state[t][j].prevState;
        t--;
    }

    return 0;
}

int viterbi(char *rc) {
    int t, tmax;
    int j, j_min, w_min;

    vit_start(rc);

    tmax = strlen(rc)/2;

    for (t = L-1; t < tmax; t++)
    {
        vit_next(t, rc+2*t);
    }

    w_min = -1;
    for (j = 0; j < M; j++) {
        if (w_min < 0) {
            w_min = vit_state[tmax][j].w;
            j_min = j;
        }
        if (vit_state[tmax][j].w < w_min) {
            w_min = vit_state[tmax][j].w;
            j_min = j;
        }
    }
    vit_path(j_min, tmax);

    return 0;
}


float vits_dist(int c, float *rc) {
    int bit0 = ((c>>1)&1) * 2 - 1;
    int bit1 = (c&1) * 2 - 1;
    return sqrt( (bit0-rc[0])*(bit0-rc[0]) + (bit1-rc[1])*(bit1-rc[1]) );
}

int vits_start(float *rc) {
    int t, m, j, c;
    float d;

    t = L-1;
    m = M;
    while ( t > 0 ) {  // t=0..L-2: nextState<M
        for (j = 0; j < m; j++) {
            vit_state[t][j].prevState = j/2;
        }
        t--;
        m /= 2;
    }

    m = 2;
    for (t = 1; t < L; t++) {
        for (j = 0; j < m; j++) {
            c = vit_code[j];
            vit_state[t][j].bIn = j % 2;
            vit_state[t][j].codeIn = c;
            d = vits_dist( c, rc+2*(t-1) );
            vit_state[t][j].sw = vit_state[t-1][vit_state[t][j].prevState].sw + d;
        }
        m *= 2;
    }

    return t;
}

int vits_next(int t, float *rc) {
    int b, nstate;
    int j, index;

    for (j = 0; j < M; j++) {
        for (b = 0; b < 2; b++) {
            nstate = j*2 + b;
            vit_d[nstate].bIn = b;
            vit_d[nstate].codeIn = vit_code[nstate];
            vit_d[nstate].prevState = j;
            vit_d[nstate].sw = vit_state[t][j].sw + vits_dist( vit_d[nstate].codeIn, rc );
        }
     }

    for (j = 0; j < M; j++) {

        if ( vit_d[j].sw <= vit_d[j+M].sw ) index = j; else index = j+M;

        vit_state[t+1][j] = vit_d[index];
    }

    return 0;
}

int vits_path(int j, int t) {
    int c;
    int dec;

    vits_rawbits[2*t] = '\0';
    vits_bits[t] = '\0';
    while (t > 0) {
        dec = vit_state[t][j].bIn;
        vits_bits[t-1] = 0x30 + dec;
        c = vit_state[t][j].codeIn;
        vits_rawbits[2*t -2] = 0x30 + ((c>>1) & 1);
        vits_rawbits[2*t -1] = 0x30 + (c & 1);
        j = vit_state[t][j].prevState;
        t--;
    }

    return 0;
}

int viterbi_soft(float *rc, int len) {
    int t, tmax;
    int j, j_min;
    float sw_min;

    vits_start(rc);

    tmax = len/2;

    for (t = L-1; t < tmax; t++)
    {
        vits_next(t, rc+2*t);
    }

    sw_min = -1.0;
    for (j = 0; j < M; j++) {
        if (sw_min < 0.0) {
            sw_min = vit_state[tmax][j].sw;
            j_min = j;
        }
        if (vit_state[tmax][j].sw < sw_min) {
            sw_min = vit_state[tmax][j].sw;
            j_min = j;
        }
    }
    vits_path(j_min, tmax);

    return 0;
}

// ------------------------------------------------------------------------

int deconv(char* rawbits, char *bits) {

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

int crc16_0(ui8_t frame[], int len) {
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

int check_CRC(ui8_t frame[]) {
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

int bits2bytes(char *bitstr, ui8_t *bytes) {
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


typedef struct {
    int frnr;
    int sn;
    int week; int gpstow;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double h;
    double vH; double vD; double vV;
    double vE; double vN; double vU;
    //int freq;
} gpx_t;

gpx_t gpx;

gpx_t gpx0 = { 0 };


#define pos_SondeSN  (OFS+0x00)  // ?4 byte 00 7A....
#define pos_FrameNb  (OFS+0x04)  // 2 byte
//GPS Position
#define pos_GPSTOW   (OFS+0x06)  // 4 byte
#define pos_GPSlat   (OFS+0x0E)  // 4 byte
#define pos_GPSlon   (OFS+0x12)  // 4 byte
#define pos_GPSalt   (OFS+0x16)  // 4 byte
//GPS Velocity East-North-Up (ENU)
#define pos_GPSvO    (OFS+0x1A)  // 2 byte
#define pos_GPSvN    (OFS+0x1C)  // 2 byte
#define pos_GPSvV    (OFS+0x1E)  // 2 byte


int get_SondeSN() {
    unsigned byte;

    byte =  (p_frame[pos_SondeSN]<<24) | (p_frame[pos_SondeSN+1]<<16)
          | (p_frame[pos_SondeSN+2]<<8) | p_frame[pos_SondeSN+3];
    gpx.sn = byte & 0xFFFFFF;

    return 0;
}

int get_FrameNb() {
    int i;
    unsigned byte;
    ui8_t frnr_bytes[2];
    int frnr;

    gpx = gpx0;

    for (i = 0; i < 2; i++) {
        byte = p_frame[pos_FrameNb + i];
        frnr_bytes[i] = byte;
    }

    frnr = (frnr_bytes[0] << 8) + frnr_bytes[1] ;
    gpx.frnr = frnr;

    return 0;
}


char weekday[7][3] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
//char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

int get_GPStime() {
    int i;
    unsigned byte;
    ui8_t gpstime_bytes[4];
    int gpstime = 0, // 32bit
        day;
    float ms;

    for (i = 0; i < 4; i++) {
        byte = p_frame[pos_GPSTOW + i];
        gpstime_bytes[i] = byte;
    }
    gpstime = 0;
    for (i = 0; i < 4; i++) {
        gpstime |= gpstime_bytes[i] << (8*(3-i));
    }

    gpx.gpstow = gpstime;

    ms = gpstime % 1000;
    gpstime /= 1000;

    day = gpstime / (24 * 3600);
    gpstime %= (24*3600);

    //if ((day < 0) || (day > 6)) return -1;

    gpx.wday = day;
    gpx.std = gpstime / 3600;
    gpx.min = (gpstime % 3600) / 60;
    gpx.sek = gpstime % 60 + ms/1000.0;

    return 0;
}

double B60B60 = 0xB60B60;  // 2^32/360 = 0xB60B60.xxx

int get_GPSlat() {
    int i;
    unsigned byte;
    ui8_t gpslat_bytes[4];
    int gpslat;
    double lat;

    for (i = 0; i < 4; i++) {
        byte = p_frame[pos_GPSlat + i];
        if (byte > 0xFF) return -1;
        gpslat_bytes[i] = byte;
    }

    gpslat = 0;
    for (i = 0; i < 4; i++) {
        gpslat |= gpslat_bytes[i] << (8*(3-i));
    }
    lat = gpslat / 1e7; //  / B60B60;
    gpx.lat = lat;

    return 0;
}

int get_GPSlon() {
    int i;
    unsigned byte;
    ui8_t gpslon_bytes[4];
    int gpslon;
    double lon;

    for (i = 0; i < 4; i++) {
        byte = p_frame[pos_GPSlon + i];
        if (byte > 0xFF) return -1;
        gpslon_bytes[i] = byte;
    }

    gpslon = 0;
    for (i = 0; i < 4; i++) {
        gpslon |= gpslon_bytes[i] << (8*(3-i));
    }
    lon = gpslon / 1e7; //   B60B60;
    gpx.lon = lon;

    return 0;
}

int get_GPSalt() {
    int i;
    unsigned byte;
    ui8_t gpsheight_bytes[4];
    int gpsheight;
    double height;

    for (i = 0; i < 4; i++) {
        byte = p_frame[pos_GPSalt + i];
        if (byte > 0xFF) return -1;
        gpsheight_bytes[i] = byte;
    }

    gpsheight = 0;
    for (i = 0; i < 4; i++) {
        gpsheight |= gpsheight_bytes[i] << (8*(3-i));
    }
    height = gpsheight / 100.0;
    gpx.h = height;

    if (height < -100 || height > 60000) return -1;
    return 0;
}

int get_GPSvel24() {
    int i;
    unsigned byte;
    ui8_t gpsVel_bytes[3];
    int vel24;
    double vx, vy, vz, dir; //, alpha;

    for (i = 0; i < 3; i++) {
        byte = p_frame[pos_GPSvO + i];
        if (byte > 0xFF) return -1;
        gpsVel_bytes[i] = byte;
    }
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vx = vel24 / 1e3; // ost

    for (i = 0; i < 3; i++) {
        byte = p_frame[pos_GPSvN + i];
        if (byte > 0xFF) return -1;
        gpsVel_bytes[i] = byte;
    }
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vy= vel24 / 1e3; // nord

    for (i = 0; i < 3; i++) {
        byte = p_frame[pos_GPSvV + i];
        if (byte > 0xFF) return -1;
        gpsVel_bytes[i] = byte;
    }
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vz = vel24 / 1e3; // hoch

    gpx.vE = vx;
    gpx.vN = vy;
    gpx.vU = vz;


    gpx.vH = sqrt(vx*vx+vy*vy);
/*
    alpha = atan2(vy, vx)*180/M_PI;  // ComplexPlane (von x-Achse nach links) - GeoMeteo (von y-Achse nach rechts)
    dir = 90-alpha;                  // z=x+iy= -> i*conj(z)=y+ix=re(i(pi/2-t)), Achsen und Drehsinn vertauscht
    if (dir < 0) dir += 360;         // atan2(y,x)=atan(y/x)=pi/2-atan(x/y) , atan(1/t) = pi/2 - atan(t)
    gpx.vD2 = dir;
*/
    dir = atan2(vx, vy) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx.vD = dir;

    gpx.vV = vz;

    return 0;
}

int get_GPSvel16() {
    int i;
    unsigned byte;
    ui8_t gpsVel_bytes[2];
    short vel16;
    double vx, vy, vz, dir; //, alpha;

    for (i = 0; i < 2; i++) {
        byte = p_frame[pos_GPSvO + i];
        if (byte > 0xFF) return -1;
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    vx = vel16 / 1e2; // ost

    for (i = 0; i < 2; i++) {
        byte = p_frame[pos_GPSvN + i];
        if (byte > 0xFF) return -1;
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    vy= vel16 / 1e2; // nord

    for (i = 0; i < 2; i++) {
        byte = p_frame[pos_GPSvV + i];
        if (byte > 0xFF) return -1;
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    vz = vel16 / 1e2; // hoch

    gpx.vH = vx;
    gpx.vD = vy;
    gpx.vV = vz;

    return 0;
}


// RS(255,223)-CCSDS
#define rs_N 255
#define rs_K 223
#define rs_R (rs_N-rs_K) // 32
ui8_t rs_cw[rs_N];

int lms6_ecc(ui8_t *cw) {
    int errors;
    ui8_t err_pos[rs_R],
          err_val[rs_R];

    errors = rs_decode(cw, err_pos, err_val);

    return errors;
}

void print_frame(int crc_err, int len) {
    int err=0;

    if (p_frame[0] != 0)
    {
        //if ((p_frame[pos_SondeSN+1] & 0xF0) == 0x70)  // ? beginnen alle SNs mit 0x7A.... bzw 80..... ?
        if ( p_frame[pos_SondeSN+1] )
        {
            get_FrameNb();
            get_GPStime();
            get_SondeSN();
            if (option_verbose) printf(" (%7d) ", gpx.sn);
            printf(" [%5d] ", gpx.frnr);
            //
            get_GPSlat();
            get_GPSlon();
            err = get_GPSalt();
            if (!err) {
                printf(" lat: %.6f° ", gpx.lat);
                printf(" lon: %.6f° ", gpx.lon);
                printf(" alt: %.2fm ", gpx.h);
                //if (option_verbose)
                {
                    get_GPSvel16();
                    printf("  vH: %.1fm/s  D: %.1f°  vV: %.1fm/s ", gpx.vH, gpx.vD, gpx.vV);
                }
            }
            if (crc_err==0) printf(" [OK]"); else printf(" [NO]");

            printf("\n");
        }
    }
}

int blk_pos = SYNC_LEN;
int frm_pos = 0;
int sf = 0;

void proc_frame(int len) {

    char *rawbits = NULL;
    int i, j;
    int err = 0;
    int errs = 0;
    int crc_err = 0;
    int flen, blen;


    if ((len % 8) > 4) {
        while (len % 8) blk_rawbits[len++] = '0';
    }
    //if (len > RAWBITFRAME_LEN+OVERLAP*BITS*2) len = RAWBITFRAME_LEN+OVERLAP*BITS*2;
    //for (i = len; i < RAWBITFRAME_LEN+OVERLAP*BITS*2; i++) frame_rawbits[i] = 0;  // oder: '0'
    blk_rawbits[len] = '\0';

    flen = len / (2*BITS);

    if (option_vit == 1) {
        viterbi(blk_rawbits);
        rawbits = vit_rawbits;
    }
    else if (option_vit == 2) {
        viterbi_soft(soft_rawbits, len);
        rawbits = vits_rawbits;
    }
    else rawbits = blk_rawbits;

    err = deconv(rawbits, frame_bits);

    if (err) { for (i=err; i < RAWBITBLOCK_LEN/2; i++) frame_bits[i] = 0; }


    blen = bits2bytes(frame_bits, block_bytes);
    for (j = blen; j < flen; j++) block_bytes[j] = 0;

    sf = 0;
    blk_pos = SYNC_LEN;
    for (j = 0; j < 4; j++) sf += (block_bytes[SYNC_LEN+j] == frm_sync[j]);
    if (sf < 4) { // scan 1..40 ?
        sf = 0;
        for (j = 0; j < 4; j++) sf += (block_bytes[SYNC_LEN+35+j] == frm_sync[j]);
        if (sf == 4)  blk_pos = SYNC_LEN+35;
        else {
            sf = 0;
            for (j = 0; j < 4; j++) sf += (block_bytes[SYNC_LEN+40+j] == frm_sync[j]);
            if (sf == 4)  blk_pos = SYNC_LEN+40; // 300-260
        }
    }

    if (blen > 100 && option_ecc) {
        for (j = 0; j < rs_N; j++) rs_cw[rs_N-1-j] = block_bytes[blk_pos+j];
        errs = lms6_ecc(rs_cw);
        for (j = 0; j < rs_N; j++) block_bytes[blk_pos+j] = rs_cw[rs_N-1-j];
    }

    if (option_raw == 2) {
        for (i = 0; i < flen; i++) printf("%02x ", block_bytes[i]);
        if (blen > 100 && option_ecc) printf("(%d)", errs);
        printf("\n");
    }
    else if (option_raw == 4  &&  option_ecc && blen > 100) {
        for (i = 0; i < rs_N; i++) printf("%02x", block_bytes[blk_pos+i]);
        printf(" (%d)", errs);
        printf("\n");
    }
    else if (option_raw == 8) {
        if (option_vit) {
            for (i = 0; i < len; i++) printf("%c", vit_rawbits[i]); printf("\n");
        }
        else {
            for (i = 0; i < len; i++) printf("%c", blk_rawbits[i]); printf("\n");
        }
    }

    for (j = 0; j < rs_K; j++) frame[j] = block_bytes[blk_pos+j];

    crc_err = check_CRC(p_frame);

    if (option_raw == 1) {
        for (i = 0; i < FRM_LEN; i++) printf("%02x ", p_frame[i]);
        if (crc_err==0) printf(" [OK]"); else printf(" [NO]");
        printf("\n");
    }

    if (option_raw == 0) print_frame(crc_err, len);

}


int main(int argc, char **argv) {

    FILE *fp = NULL;
    char *fpname = NULL;
    float spb = 0.0;
    int header_found = 0;

    int bit, rbit;
    int bitpos = 0;
    int bitQ;
    int pos;
    int herrs, herr1;
    int headerlen = 0;

    int k,K;
    float mv;
    unsigned int mv_pos, mv0_pos;
    int mp = 0;

    float thres = 0.76;

    int symlen = 1;
    int bitofs = 1; // +1 .. +2
    int shift = 0;

    unsigned int bc = 0;

    float sb = 0.0;
    float sbit = 0.0;
    float level = -1.0, ll = -1.0;


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
            fprintf(stderr, "       --vit,--vit2 (Viterbi/soft)\n");
            fprintf(stderr, "       --ecc        (Reed-Solomon)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1; // bytes - rs_ecc_codewords
        }
        else if ( (strcmp(*argv, "-r0") == 0) || (strcmp(*argv, "--raw0") == 0) ) {
            option_raw = 2; // bytes: sync + codewords
        }
        else if ( (strcmp(*argv, "-rc") == 0) || (strcmp(*argv, "--rawecc") == 0) ) {
            option_raw = 4; // rs_ecc_codewords
        }
        else if ( (strcmp(*argv, "-R") == 0) || (strcmp(*argv, "--RAW") == 0) ) {
            option_raw = 8; // rawbits
        }
        else if   (strcmp(*argv, "--ecc" ) == 0) { option_ecc = 1; } // RS-ECC
        else if   (strcmp(*argv, "--vit" ) == 0) { option_vit = 1; } // viterbi-hard
        else if   (strcmp(*argv, "--vit2") == 0) { option_vit = 2; } // viterbi-soft
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
        }
        else if ( (strcmp(*argv, "--dc") == 0) ) {
            option_dc = 1;
        }
        else if ( (strcmp(*argv, "--ch2") == 0) ) { wav_channel = 1; }  // right channel (default: 0=left)
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
        else if ( (strcmp(*argv, "--level") == 0) ) {
            ++argv;
            if (*argv) {
                ll = atof(*argv);
            }
            else return -1;
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


    spb = read_wav_header(fp, (float)BAUD_RATE, wav_channel);
    if ( spb < 0 ) {
        fclose(fp);
        fprintf(stderr, "error: wav header\n");
        return -1;
    }
    if ( spb < 8 ) {
        fprintf(stderr, "note: sample rate low\n");
    }


    if (option_raw == 4) option_ecc = 1;

    if (option_vit) {
        vit_initCodes();
    }
    if (option_ecc) {
        rs_init_RS255ccsds(); // bch_ecc.c
    }


    symlen = 1;
    bitofs += shift;

    headerlen = strlen(rawheader);
    K = init_buffers(rawheader, headerlen, 2); // shape=2 (alt. shape=1)
    if ( K < 0 ) {
        fprintf(stderr, "error: init buffers\n");
        return -1;
    };

    level = ll;
    k = 0;
    mv = 0;
    mv_pos = 0;

    while ( f32buf_sample(fp, option_inv) != EOF ) {

        k += 1;
        if (k >= K-4) {
            mv0_pos = mv_pos;
            mp = getCorrDFT(K, 0, &mv, &mv_pos);
            k = 0;
        }
        else {
            mv = 0.0;
            continue;
        }

        if (mp > 0 && (mv > thres || mv < -thres)) {
            if (mv_pos > mv0_pos) {

                header_found = 0;
                herrs = headcmp(symlen, rawheader, headerlen, mv_pos, mv<0, option_dc); // (symlen=1)
                herr1 = 0;

                if (herrs <= 3 && herrs > 0) {
                    herr1 = headcmp(symlen, rawheader, headerlen, mv_pos+1, mv<0, option_dc);
                    if (herr1 < herrs) {
                        herrs = herr1;
                        herr1 = 1;
                    }
                }
                if (herrs <= 3) header_found = 1; // herrs <= 3 bitfehler in header

                if (header_found) {

                    if (ll <= 0) level = header_level(rawheader, headerlen, mv_pos, mv<0) * 0.6;

                    bitpos = 0;
                    pos = BLOCKSTART;

                    if (mv > 0) bc = 0; else bc = 1;

                    while ( pos < RAWBITBLOCK_LEN ) {
                        header_found = !(pos>=RAWBITBLOCK_LEN-10);
                        //bitQ = read_sbit(fp, symlen, &rbit, option_inv, bitofs, bitpos==0); // symlen=1
                        bitQ = read_softbit(fp, symlen, &rbit, &sb, level, option_inv, bitofs, bitpos==0); // symlen=1
                        if (bitQ == EOF) { break; }

                        bit = rbit ^ (bc%2);  // (c0,inv(c1))
                        blk_rawbits[pos] = 0x30 + bit;

                        sbit = sb * (-(int)(bc%2)*2+1);
                        soft_rawbits[pos] = sbit;

                        bc++;
                        pos++;
                        bitpos += 1;
                    }

                    blk_rawbits[pos] = '\0';
                    soft_rawbits[pos] = 0;

                    proc_frame(pos);

                    if (pos < RAWBITBLOCK_LEN) break;

                    pos = BLOCKSTART;
                    header_found = 0;
                }
            }
        }

    }


    free_buffers();

    fclose(fp);

    return 0;
}

