
/*
 * radiosondes RS41-SG(P)
 * author: zilog80
 * usage:
 *     ./rs41ecc [options] audio.wav
 *       options:
 *            -v, -vx, -vv  (info, aux, info/conf)
 *            -r, --raw
 *            -i, --invert
 *            --crc        (check CRC)
 *            --avg        (moving average)
 *            -b           (alt. Demod.)
 *            --ecc        (Reed-Solomon)
 */
/* (uses fec-lib by KA9Q)
   ka9q-fec:
      gcc -c init_rs_char.c
      gcc -c decode_rs_char.c
   gcc init_rs_char.o decode_rs_char.o rs41ecc.c -lm -o rs41ecc
*/


#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef CYGWIN
  #include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif


#include "fec.h"

#define rs_N 255
#define rs_R 24
#define rs_K (rs_N-rs_R)

void *rs;
unsigned char codeword1[rs_N], codeword2[rs_N];

typedef struct {
    int typ;
    int msglen;
    int msgpos;
    int parpos;
    int hdrlen;
    int frmlen;
} rscfg_t;

rscfg_t cfg_rs41 = { 41, (320-56)/2, 56, 8, 8, 320};


typedef unsigned char ui8_t;
typedef unsigned int  ui32_t;
typedef short i16_t;
typedef int   i32_t;

typedef struct {
    int frnr;
    char id[9];
    int week; int gpssec;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double alt;
    double vN; double vE; double vU;
    double vH; double vD; double vD2;
} gpx_t;

gpx_t gpx;

int option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 0,      // rohe Frames
    option_inv = 0,      // invertiert Signal
    option_res = 0,      // genauere Bitmessung
    option_crc = 0,      // check CRC
    option_avg = 0,      // moving average
    option_b = 0,
    option_ecc = 0,      // Reed-Solomon ECC
    option_sat = 0,      // GPS sat data
    wavloaded = 0;


#define HEADOFS 24 // HEADOFS+HEADLEN <= 64
#define HEADLEN 32 // HEADOFS+HEADLEN mod 8 = 0
#define FRAMESTART ((HEADOFS+HEADLEN)/8)

/*               10      B6      CA      11      22      96      12      F8      */
char header[] = "0000100001101101010100111000100001000100011010010100100000011111";
char buf[HEADLEN+1] = "x";
int bufpos = -1;

#define NDATA_LEN 320
#define XDATA_LEN 198
#define FRAME_LEN (NDATA_LEN+XDATA_LEN)
ui8_t //xframe[FRAME_LEN] = { 0x10, 0xB6, 0xCA, 0x11, 0x22, 0x96, 0x12, 0xF8},    = xorbyte( frame)
         frame[FRAME_LEN] = { 0x86, 0x35, 0xf4, 0x40, 0x93, 0xdf, 0x1a, 0x60}; // = xorbyte(xframe)


#define MASK_LEN 64
ui8_t mask[MASK_LEN] = { 0x96, 0x83, 0x3E, 0x51, 0xB1, 0x49, 0x08, 0x98,
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

// option_b: exakte Baudrate wichtig!
// im Prinzip in sync-preamble/header ermittelbar
#define BAUD_RATE 4800

int sample_rate = 0, bits_sample = 0, channels = 0;
float samples_per_bit = 0;

int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}

int read_wav_header(FILE *fp) {
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;

    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "RIFF", 4)) return -1;
    if (fread(txt, 1, 4, fp) < 4) return -1;
    // pos_WAVE = 8L
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "WAVE", 4)) return -1;
    // pos_fmt = 12L
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "fmt ", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;
    if (fread(dat, 1, 2, fp) < 2) return -1;

    if (fread(dat, 1, 2, fp) < 2) return -1;
    channels = dat[0] + (dat[1] << 8);

    if (fread(dat, 1, 4, fp) < 4) return -1;
    memcpy(&sample_rate, dat, 4); //sample_rate = dat[0]|(dat[1]<<8)|(dat[2]<<16)|(dat[3]<<24);

    if (fread(dat, 1, 4, fp) < 4) return -1;
    if (fread(dat, 1, 2, fp) < 2) return -1;
    //byte = dat[0] + (dat[1] << 8);

    if (fread(dat, 1, 2, fp) < 2) return -1;
    bits_sample = dat[0] + (dat[1] << 8);

    // pos_dat = 36L + info
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "data", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;


    fprintf(stderr, "sample_rate: %d\n", sample_rate);
    fprintf(stderr, "bits       : %d\n", bits_sample);
    fprintf(stderr, "channels   : %d\n", channels);

    if ((bits_sample != 8) && (bits_sample != 16)) return -1;

    samples_per_bit = sample_rate/(float)BAUD_RATE;

    fprintf(stderr, "samples/bit: %.2f\n", samples_per_bit);

    return 0;
}


#define EOF_INT  0x1000000

#define LEN_movAvg 3
int movAvg[LEN_movAvg];
unsigned long sample_count = 0;
double bitgrenze = 0;

int read_signed_sample(FILE *fp) {  // int = i32_t
    int byte, i, sample, s=0;       // EOF -> 0x1000000

    for (i = 0; i < channels; i++) {
                           // i = 0: links bzw. mono
        byte = fgetc(fp);
        if (byte == EOF) return EOF_INT;
        if (i == 0) sample = byte;

        if (bits_sample == 16) {
            byte = fgetc(fp);
            if (byte == EOF) return EOF_INT;
            if (i == 0) sample +=  byte << 8;
        }

    }

    if (bits_sample ==  8)  s = sample-128;   // 8bit: 00..FF, centerpoint 0x80=128
    if (bits_sample == 16)  s = (short)sample;

    if (option_avg) {
        movAvg[sample_count % LEN_movAvg] = s;
        s = 0;
        for (i = 0; i < LEN_movAvg; i++) s += movAvg[i];
        s = (s+0.5) / LEN_movAvg;
    }

    sample_count++;

    return s;
}

int par=1, par_alt=1;

int read_bits_fsk(FILE *fp, int *bit, int *len) {
    static int sample;
    int n, y0;
    float l, x1;
    static float x0;

    n = 0;
    do{
        y0 = sample;
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        //sample_count++; // in read_signed_sample()
        par_alt = par;
        par =  (sample >= 0) ? 1 : -1;  // 8bit: 0..127,128..255 (-128..-1,0..127)
        n++;
    } while (par*par_alt > 0);

    if (!option_res) l = (float)n / samples_per_bit;
    else {                                 // genauere Bitlaengen-Messung
        x1 = sample/(float)(sample-y0);    // hilft bei niedriger sample rate
        l = (n+x0-x1) / samples_per_bit;   // meist mehr frames (nicht immer)
        x0 = x1;
    }

    *len = (int)(l+0.5);

    if (!option_inv) *bit = (1+par_alt)/2;  // oben 1, unten -1
    else             *bit = (1-par_alt)/2;  // sdr#<rev1381?, invers: unten 1, oben -1

    /* Y-offset ? */

    return 0;
}

int bitstart = 0;
int read_rawbit(FILE *fp, int *bit) {
    int sample;
    int n, sum;
    int sample0, pars;

    sum = 0;
    n = 0;

    sample0 = 0;
    pars = 0;

    if (bitstart) {
        //n = 1;    // d.h. bitgrenze = sample_count-1 (?)
        bitgrenze = sample_count-1;
        bitstart = 0;
    }
    bitgrenze += samples_per_bit;

    do {
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        //sample_count++; // in read_signed_sample()
        //par =  (sample >= 0) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
        sum += sample;

        if (sample * sample0 < 0) pars++;   // wenn sample[0..n-1]=0 ...
        sample0 = sample;

        n++;
    } while (sample_count < bitgrenze);  // n < samples_per_bit

    if (sum >= 0) *bit = 1;
    else          *bit = 0;

    if (option_inv) *bit ^= 1;

    return pars;
}


/* ------------------------------------------------------------------------------------ */


int bits2byte(char bits[]) {
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


void inc_bufpos() {
  bufpos = (bufpos+1) % HEADLEN;
}

int compare() {
    int i=0, j = bufpos;

    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != header[HEADOFS+HEADLEN-1-i]) break;
        j--;
        i++;
    }
    return i;
}
/*
ui8_t xorbyte(int pos) {
    return  xframe[pos] ^ mask[pos % MASK_LEN];
}
*/
ui8_t framebyte(int pos) {
    return  frame[pos];
}


/* ------------------------------------------------------------------------------------ */
/*
 * Convert GPS Week and Seconds to Modified Julian Day.
 * - Adapted from sci.astro FAQ.
 * - Ignores UTC leap seconds.
 */
void Gps2Date(long GpsWeek, long GpsSeconds, int *Year, int *Month, int *Day) {

    long GpsDays, Mjd;
    long J, C, Y, M;

    GpsDays = GpsWeek * 7 + (GpsSeconds / 86400);
    Mjd = 44244 + GpsDays;

    J = Mjd + 2468570;
    C = 4 * J / 146097;
    J = J - (146097 * C + 3) / 4;
    Y = 4000 * (J + 1) / 1461001;
    J = J - 1461 * Y / 4 + 31;
    M = 80 * J / 2447;
    *Day = J - 2447 * M / 80;
    J = M / 11;
    *Month = M + 2 - (12 * J);
    *Year = 100 * (C - 49) + Y + J;
}
/* ------------------------------------------------------------------------------------ */

ui32_t u4(ui8_t *bytes) {  // 32bit unsigned int
    ui32_t val = 0;
    memcpy(&val, bytes, 4);
    return val;
}

int i3(ui8_t *bytes) {  // 24bit signed int
    int val = 0,
        val24 = 0;
    val = bytes[0] | (bytes[1]<<8) | (bytes[2]<<16);
    val24 = val & 0xFFFFFF; if (val24 & 0x800000) val24 = val24 - 0x1000000;
    return val24;
}

ui32_t u2(ui8_t *bytes) {  // 16bit unsigned int
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

/*
int crc16x(int start, int len) {
    int crc16poly = 0x1021;
    int rem = 0xFFFF, i, j;
    int xbyte;

    if (start+len >= FRAME_LEN) return -1;

    for (i = 0; i < len; i++) {
        xbyte = xorbyte(start+i);
        rem = rem ^ (xbyte << 8);
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
*/
int crc16(int start, int len) {
    int crc16poly = 0x1021;
    int rem = 0xFFFF, i, j;
    int byte;

    if (start+len >= FRAME_LEN) return -1;

    for (i = 0; i < len; i++) {
        byte = framebyte(start+i);
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

int check_CRC(ui32_t pos, ui32_t pck) {
    ui32_t crclen = 0,
           crcdat = 0;
    if (((pck>>8) & 0xFF) != frame[pos]) return -1;
    crclen = frame[pos+1];
    crcdat = u2(frame+pos+2+crclen);
    if ( crcdat == crc16(pos+2, crclen) ) {
        return 1;  // CRC OK
    }
    else return 0;
}


/*
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

#define pck_PTU      0x7A2A  // PTU
#define pos_PTU       0x065

#define xor_GPS1     0x9667  // ^0xEA79=0x7C1E
#define pck_GPS1     0x7C1E  // RXM-RAW (0x02 0x10)
#define pos_GPS1      0x093
#define pos_GPSweek   0x095  // 2 byte
#define pos_GPSiTOW   0x097  // 4 byte
#define pos_satsN     0x09B  // 12x2 byte

#define pck_GPS2     0x7D59  // RXM-RAW (0x02 0x10)
#define pos_GPS2      0x0B5
#define pos_minPR     0x0B7  //        4 byte
#define pos_FF        0x0BB  //        1 byte
#define pos_dataSats  0x0BC  // 12x(4+3) byte (4: pseudorange, 3: doppler)

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

#define pck_AUX      0x7E00  // LEN variable
#define pos_AUX       0x12B


double c = 299.792458e6;
double L1 = 1575.42e6;

int get_SatData() {
    int i, n;
    int sv;
    ui32_t minPR;
    int Nfix;
    double pDOP, sAcc;

    fprintf(stdout, "[%d]\n", u2(frame+pos_FrameNb));

    fprintf(stdout, "iTOW: 0x%08X", u4(frame+pos_GPSiTOW));
    fprintf(stdout, "  week: 0x%04X", u2(frame+pos_GPSweek));
    fprintf(stdout, "\n");
    minPR = u4(frame+pos_minPR);
    fprintf(stdout, "minPR: %d", minPR);
    fprintf(stdout, "\n");

    for (i = 0; i < 12; i++) {
        n = i*7;
        sv = frame[pos_satsN+2*i];
        if (sv == 0xFF) break;
        fprintf(stdout, "    SV: %2d #  ", sv);
        fprintf(stdout, "prMes: %.1f", u4(frame+pos_dataSats+n)/100.0 + minPR);
        fprintf(stdout, "  ");
        fprintf(stdout, "doMes: %.1f", -i3(frame+pos_dataSats+n+4)/100.0*L1/c);
        fprintf(stdout, "\n");
    }

    fprintf(stdout, "ECEF-POS: (%d,%d,%d)\n",
                     (i32_t)u4(frame+pos_GPSecefX),
                     (i32_t)u4(frame+pos_GPSecefY),
                     (i32_t)u4(frame+pos_GPSecefZ));
    fprintf(stdout, "ECEF-VEL: (%d,%d,%d)\n",
                     (i16_t)u2(frame+pos_GPSecefV+0),
                     (i16_t)u2(frame+pos_GPSecefV+2),
                     (i16_t)u2(frame+pos_GPSecefV+4));

    Nfix = frame[pos_numSats];
    sAcc = frame[pos_sAcc]/10.0;
    pDOP = frame[pos_pDOP]/10.0;
    fprintf(stdout, "numSatsFix: %2d  Acc: %.1f  pDOP: %.1f\n", Nfix, sAcc, pDOP);


    fprintf(stdout, "CRC: ");
    fprintf(stdout, " %04X", pck_GPS1);
    if (check_CRC(pos_GPS1, pck_GPS1)>0) fprintf(stdout, "[OK]"); else fprintf(stdout, "[NO]");
    //fprintf(stdout, "[%+d]", check_CRC(pos_GPS1, pck_GPS1));
    fprintf(stdout, " %04X", pck_GPS2);
    if (check_CRC(pos_GPS2, pck_GPS2)>0) fprintf(stdout, "[OK]"); else fprintf(stdout, "[NO]");
    //fprintf(stdout, "[%+d]", check_CRC(pos_GPS2, pck_GPS2));
    fprintf(stdout, " %04X", pck_GPS3);
    if (check_CRC(pos_GPS3, pck_GPS3)>0) fprintf(stdout, "[OK]"); else fprintf(stdout, "[NO]");
    //fprintf(stdout, "[%+d]", check_CRC(pos_GPS3, pck_GPS3));

    fprintf(stdout, "\n");
    fprintf(stdout, "\n");

    return 0;
}


int get_FrameNb() {
    int i;
    unsigned byte;
    ui8_t frnr_bytes[2];
    int frnr;

    for (i = 0; i < 2; i++) {
        byte = framebyte(pos_FrameNb + i);
        frnr_bytes[i] = byte;
    }

    frnr = frnr_bytes[0] + (frnr_bytes[1] << 8);
    gpx.frnr = frnr;

    return 0;
}

int get_SondeID() {
    int i;
    unsigned byte;
    ui8_t sondeid_bytes[8];

    for (i = 0; i < 8; i++) {
        byte = framebyte(pos_SondeID + i);
        if ((byte < 0x20) || (byte > 0x7E)) return -1;
        sondeid_bytes[i] = byte;
    }

    for (i = 0; i < 8; i++) {
        gpx.id[i] = sondeid_bytes[i];
    }
    gpx.id[8] = '\0';

    return 0;
}

int get_GPSweek() {
    int i;
    unsigned byte;
    ui8_t gpsweek_bytes[2];
    int gpsweek;

    for (i = 0; i < 2; i++) {
        byte = framebyte(pos_GPSweek + i);
        gpsweek_bytes[i] = byte;
    }

    gpsweek = gpsweek_bytes[0] + (gpsweek_bytes[1] << 8);
    //if (gpsweek < 0) { gpx.week = -1; return -1; } // (short int)
    gpx.week = gpsweek;

    return 0;
}

char weekday[7][3] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};

int get_GPStime() {
    int i;
    unsigned byte;
    ui8_t gpstime_bytes[4];
    int gpstime = 0, // 32bit
        day;
    int ms;
    int crclen;
    int crcdat;
    int crcpos = pos_GPS1;

    // xorbyte(crcpos) == (pck_GPS1>>8) & 0xFF ?
    if ( option_crc ) {
        crclen = framebyte(crcpos+1);
        crcdat = framebyte(crcpos+2+crclen) | (framebyte(crcpos+2+crclen+1)<<8);
        if ( crcdat != crc16(crcpos+2, crclen) ) {
            return -2; // CRC error
        }
    }


    for (i = 0; i < 4; i++) {
        byte = framebyte(pos_GPSiTOW + i);
        gpstime_bytes[i] = byte;
    }

    memcpy(&gpstime, gpstime_bytes, 4);
    ms = gpstime % 1000;
    gpstime /= 1000;

    gpx.gpssec = gpstime;

    day = (gpstime / (24 * 3600)) % 7;
    //if ((day < 0) || (day > 6)) return -1;  // besser CRC-check

    gpstime %= (24*3600);

    gpx.wday = day;
    gpx.std = gpstime / 3600;
    gpx.min = (gpstime % 3600) / 60;
    gpx.sek = gpstime % 60 + ms/1000.0;

    return 0;
}


#define EARTH_a  6378137.0
#define EARTH_b  6356752.31424518
#define EARTH_a2_b2  (EARTH_a*EARTH_a - EARTH_b*EARTH_b)

double a = EARTH_a,
       b = EARTH_b,
       a_b = EARTH_a2_b2,
       e2  = EARTH_a2_b2 / (EARTH_a*EARTH_a),
       ee2 = EARTH_a2_b2 / (EARTH_b*EARTH_b);

void ecef2elli(double X[], double *lat, double *lon, double *alt) {
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

int get_GPSkoord() {
    int i, k;
    unsigned byte;
    ui8_t XYZ_bytes[4];
    int XYZ; // 32bit
    double X[3], lat, lon, alt;
    ui8_t gpsVel_bytes[2];
    short vel16; // 16bit
    double V[3], phi, lam, alpha, dir;

    int crclen;
    int crcdat;
    int crcpos = pos_GPS3;

    // xorbyte(crcpos) == (pck_GPS3>>8) & 0xFF ?
    if ( option_crc ) {
        crclen = framebyte(crcpos+1);
        crcdat = framebyte(crcpos+2+crclen) | (framebyte(crcpos+2+crclen+1)<<8);
        if ( crcdat != crc16(crcpos+2, crclen) ) {
            return -2; // CRC error
        }
    }

    for (k = 0; k < 3; k++) {

        for (i = 0; i < 4; i++) {
            byte = frame[pos_GPSecefX + 4*k + i];
            XYZ_bytes[i] = byte;
        }
        memcpy(&XYZ, XYZ_bytes, 4);
        X[k] = XYZ / 100.0;

        for (i = 0; i < 2; i++) {
            byte = frame[pos_GPSecefV + 2*k + i];
            gpsVel_bytes[i] = byte;
        }
        vel16 = gpsVel_bytes[0] | gpsVel_bytes[1] << 8;
        V[k] = vel16 / 100.0;

    }


    // ECEF-Position
    ecef2elli(X, &lat, &lon, &alt);
    gpx.lat = lat;
    gpx.lon = lon;
    gpx.alt = alt;
    if ((alt < -1000) || (alt > 80000)) return -1;


    // ECEF-Velocities
    // ECEF-Vel -> NorthEastUp
    phi = lat*M_PI/180.0;
    lam = lon*M_PI/180.0;
    gpx.vN = -V[0]*sin(phi)*cos(lam) - V[1]*sin(phi)*sin(lam) + V[2]*cos(phi);
    gpx.vE = -V[0]*sin(lam) + V[1]*cos(lam);
    gpx.vU =  V[0]*cos(phi)*cos(lam) + V[1]*cos(phi)*sin(lam) + V[2]*sin(phi);

    // NEU -> HorDirVer
    gpx.vH = sqrt(gpx.vN*gpx.vN+gpx.vE*gpx.vE);
///*
    alpha = atan2(gpx.vN, gpx.vE)*180/M_PI;  // ComplexPlane (von x-Achse nach links) - GeoMeteo (von y-Achse nach rechts)
    dir = 90-alpha;                          // z=x+iy= -> i*conj(z)=y+ix=re(i(pi/2-t)), Achsen und Drehsinn vertauscht
    if (dir < 0) dir += 360;                 // atan2(y,x)=atan(y/x)=pi/2-atan(x/y) , atan(1/t) = pi/2 - atan(t)
    gpx.vD2 = dir;
//*/
    dir = atan2(gpx.vE, gpx.vN) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx.vD = dir;

    return 0;
}

int get_Aux() {
//
// "Ozone Sounding with Vaisala Radiosonde RS41" user's guide
//
    int i, auxlen, auxcrc, count7E, pos7E;

    count7E = 0;
    pos7E = pos_AUX;

    // 7Exx: xdata
    while ( pos7E < FRAME_LEN  &&  framebyte(pos7E) == 0x7E ) {

        auxlen = framebyte(pos_AUX+1);
        auxcrc = framebyte(pos_AUX+2+auxlen) | (framebyte(pos_AUX+2+auxlen+1)<<8);

        if (count7E == 0) fprintf(stdout, "\n # xdata = ");
        else              fprintf(stdout, " # ");

        if ( auxcrc == crc16(pos_AUX+2, auxlen) ) {
            //fprintf(stdout, " # %02x : ", framebyte(pos_AUX+2));
            for (i = 1; i < auxlen; i++) {
                fprintf(stdout, "%c", framebyte(pos_AUX+2+i));
            }
            count7E++;
            pos7E += 2+auxlen+2;
        }
        else pos7E = FRAME_LEN;
    }

    return count7E;
}

int get_Cal() {
    int i;
    unsigned byte;
    ui8_t calfr = 0;
    ui8_t burst = 0;
    int freq = 0, f0 = 0, f1 = 0;
    char sondetyp[9];

    byte = framebyte(pos_CalData);
    calfr = byte;

    if (option_verbose == 3) {
        fprintf(stdout, "\n");  // fflush(stdout);
        fprintf(stdout, "[%5d] ", gpx.frnr);
        fprintf(stdout, " 0x%02x: ", calfr);
        for (i = 0; i < 16; i++) {
            byte = framebyte(pos_CalData+1+i);
            fprintf(stdout, "%02x ", byte);
        }
    }

    if (calfr == 0x02  &&  option_verbose /*== 2*/) {
        byte = framebyte(pos_Calburst);
        burst = byte;
        fprintf(stdout, ": BK %02X ", burst);
    }

    if (calfr == 0x00  &&  option_verbose) {
        byte = framebyte(pos_Calfreq) & 0xC0;  // erstmal nur oberste beiden bits
        f0 = (byte * 10) / 64;  // 0x80 -> 1/2, 0x40 -> 1/4 ; dann mal 40
        byte = framebyte(pos_Calfreq+1);
        f1 = 40 * byte;
        freq = 400000 + f1+f0; // kHz;
        fprintf(stdout, ": fq %d ", freq);
    }

    if (calfr == 0x21  &&  option_verbose /*== 2*/) {  // eventuell noch zwei bytes in 0x22
        for (i = 0; i < 9; i++) sondetyp[i] = 0;
        for (i = 0; i < 8; i++) {
            byte = framebyte(pos_CalRSTyp + i);
            if ((byte >= 0x20) && (byte < 0x7F)) sondetyp[i] = byte;
            else if (byte == 0x00) sondetyp[i] = '\0';
        }
        fprintf(stdout, ": %s ", sondetyp);
    }

    return 0;
}

/* ------------------------------------------------------------------------------------ */

int rs41_ecc(int msglen) {

    int i, ret = 0;
    int errors1, errors2,
        errpos1[rs_R], errpos2[rs_R];

    if (msglen > FRAME_LEN) msglen = FRAME_LEN;
    cfg_rs41.frmlen = msglen;
    cfg_rs41.msglen = (msglen-56)/2; // msgpos=56;

    for (i = msglen; i < FRAME_LEN; i++) frame[i] = 0;


    for (i = 0; i < cfg_rs41.msglen; i++) {
        codeword1[rs_K-1-i] = frame[cfg_rs41.msgpos+  2*i];
        codeword2[rs_K-1-i] = frame[cfg_rs41.msgpos+1+2*i];
    }

    for (i = 0; i < rs_R; i++) {
        codeword1[rs_N-1-i] = frame[cfg_rs41.parpos+     i];
        codeword2[rs_N-1-i] = frame[cfg_rs41.parpos+rs_R+i];
    }

    errors1 = decode_rs_char(rs, codeword1, errpos1, 0);
    errors2 = decode_rs_char(rs, codeword2, errpos2, 0);

/*
                printf("codeword1, ");
                printf("errors: %d\n", errors1);
                if (errors1 > 0) {
                    printf("pos: ");
                    for (i = 0; i < errors1; i++) printf(" %d", errpos1[i]);
                    printf("\n");
                }

                printf("codeword2, ");
                printf("errors: %d\n", errors2);
                if (errors2 > 0) {
                    printf("pos: ");
                    for (i = 0; i < errors2; i++) printf(" %d", errpos2[i]);
                    printf("\n");
                }
*/

    //for (i = 0; i < cfg_rs41.hdrlen; i++) frame[i] = data[i];
    for (i = 0; i < rs_R; i++) {
        frame[cfg_rs41.parpos+     i] = codeword1[rs_N-1-i];
        frame[cfg_rs41.parpos+rs_R+i] = codeword2[rs_N-1-i];
    }
    for (i = 0; i < cfg_rs41.msglen; i++) {
        frame[cfg_rs41.msgpos+  2*i] = codeword1[rs_K-1-i];
        frame[cfg_rs41.msgpos+1+2*i] = codeword2[rs_K-1-i];
    }

    if (errors1 > 0 || errors2 > 0) ret = 1;
    if (errors1 < 0 || errors2 < 0) ret = -1;
    return ret;
}

/* ------------------------------------------------------------------------------------ */


int print_position() {
    int err;

        err = 0;
        err |= get_FrameNb();
        err |= get_SondeID();
        err |= get_GPSweek();
        err |= get_GPStime();
        err |= get_GPSkoord();

        if (!err) {
            Gps2Date(gpx.week, gpx.gpssec, &gpx.jahr, &gpx.monat, &gpx.tag);
            fprintf(stdout, "[%5d] ", gpx.frnr);
            fprintf(stdout, "(%s) ", gpx.id);
            fprintf(stdout, "%s ", weekday[gpx.wday]);
            fprintf(stdout, "%04d-%02d-%02d %02d:%02d:%06.3f",
                    gpx.jahr, gpx.monat, gpx.tag, gpx.std, gpx.min, gpx.sek);
            if (option_verbose == 3) fprintf(stdout, " (W %d)", gpx.week);
            fprintf(stdout, " ");
            fprintf(stdout, " lat: %.5f ", gpx.lat);
            fprintf(stdout, " lon: %.5f ", gpx.lon);
            fprintf(stdout, " alt: %.2f ", gpx.alt);
            //if (option_verbose)
            {
                //fprintf(stdout, "  (%.1f %.1f %.1f) ", gpx.vN, gpx.vE, gpx.vU);
                fprintf(stdout,"  vH: %4.1f  D: %5.1f°  vV: %3.1f ", gpx.vH, gpx.vD, gpx.vU);
            }
            get_Cal();
            if (option_verbose > 1) get_Aux();
            fprintf(stdout, "\n");  // fflush(stdout);
        }

    return err;
}

void print_frame(int len) {
    int i, ret = 0;

    if (option_ecc) {
        ret = rs41_ecc(len);
    }

    for (i = len; i < FRAME_LEN; i++) {
        //xframe[i] = 0;
        frame[i] = 0;
    }

    if (option_raw) {
/*
        for (i = 0; i < len; i++) {
            byte = framebyte(i);
            fprintf(stdout, "%02x", byte);
        }
        fprintf(stdout, "\n");
*/
        for (i = 0; i < len; i++) {
            fprintf(stdout, "%02x", frame[i]);
        }
        if (option_ecc) {
            if (ret >= 0) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
        }
        fprintf(stdout, "\n");
//        fprintf(stdout, "\n");
    }
    else if (option_sat) {
        get_SatData();
    }
    else {
        print_position();
    }
}

int main(int argc, char *argv[]) {

    FILE *fp;
    char *fpname;
    char bitbuf[8];
    int bit_count = 0,
        byte_count = FRAMESTART,
        header_found = 0,
        byte, i;
    int bit, len;

    int sumQ, bitQ;
    double ratioQ, ratioQ0;

#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _fileno(stdin)
#endif
    setbuf(stdout, NULL);

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
            fprintf(stderr, "       --avg        (moving average)\n");
            fprintf(stderr, "       -b           (alt. Demod.)\n");
            fprintf(stderr, "       --ecc        (Reed-Solomon)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if   (strcmp(*argv, "-vx") == 0) { option_verbose = 2; }
        else if   (strcmp(*argv, "-vv") == 0) { option_verbose = 3; }
        else if   (strcmp(*argv, "--crc") == 0) { option_crc = 1; }
        else if   (strcmp(*argv, "--res") == 0) { option_res = 1; }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
        }
        else if ( (strcmp(*argv, "--avg") == 0) ) {
            option_avg = 1;
        }
        else if   (strcmp(*argv, "-b") == 0) { option_b = 1; }
        else if   (strcmp(*argv, "--ecc") == 0) { option_ecc = 1; }
        else if   (strcmp(*argv, "--sat") == 0) { option_sat = 1; }
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


    i = read_wav_header(fp);
    if (i) {
        fclose(fp);
        return -1;
    }


    if (option_ecc) {
        rs = init_rs_char( 8, 0x11d, 0, 1, rs_R, 0);
    }

    while (!read_bits_fsk(fp, &bit, &len)) {

        if (len == 0) { // reset_frame();
            if (byte_count > pos_AUX) {
                print_frame(byte_count);
                bit_count = 0;
                byte_count = FRAMESTART;
                header_found = 0;
            }
            //inc_bufpos();
            //buf[bufpos] = 'x';
            continue;   // ...
        }

        for (i = 0; i < len; i++) {

            inc_bufpos();
            buf[bufpos] = 0x30 + bit;  // Ascii

            if (!header_found) {
                if (compare() >= HEADLEN) header_found = 1;
            }
            else {
                bitbuf[bit_count] = bit;
                bit_count++;

                if (bit_count == 8) {
                    bit_count = 0;
                    byte = bits2byte(bitbuf);
                    //xframe[byte_count] = byte;
                    frame[byte_count] = byte ^ mask[byte_count % MASK_LEN];
                    byte_count++;
                    if (byte_count == FRAME_LEN) {
                        byte_count = FRAMESTART;
                        header_found = 0;
                        print_frame(FRAME_LEN);
                    }
                }
            }

        }
        if (header_found && option_b) {
            bitstart = 1;
            sumQ = 0;
            ratioQ0 = 0;

            while ( byte_count < FRAME_LEN ) {
                bitQ = read_rawbit(fp, &bit);
                if ( bitQ == EOF) break;
                sumQ += bitQ;
                bitbuf[bit_count] = bit;
                bit_count++;
                if (bit_count == 8) {
                    bit_count = 0;
                    byte = bits2byte(bitbuf);
                    //xframe[byte_count] = byte;
                    frame[byte_count] = byte ^ mask[byte_count % MASK_LEN];
                    byte_count++;

                    ratioQ0 = ratioQ;
                    ratioQ = sumQ/samples_per_bit;
                    //printf("# %3d  sumQ: %d  sumQ/(samples/bit): %.1f\n", byte_count-1, sumQ, sumQ/samples_per_bit);
                    if (byte_count > NDATA_LEN) {
                        if (ratioQ > 0.7  && ratioQ0 > 0.7) {
                            byte_count -= 2;
                            break;
                        }
                    }
                    sumQ = 0;
                }
            }
            header_found = 0;
            print_frame(byte_count);
            byte_count = FRAMESTART;

        }

    }

    fclose(fp);

    return 0;
}


