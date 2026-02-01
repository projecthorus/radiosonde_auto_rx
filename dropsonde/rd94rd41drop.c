
/*
  dropsonde RD94/RD41
  frames,position: 2Hz
  velocity(wind): 4Hz
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


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;
typedef signed   char  i8_t;

typedef struct {
    i8_t vbs;  // verbose output
    i8_t raw;  // raw frames
    i8_t ptu;  // PTU: temperature
    i8_t inv;
    i8_t aut;
    i8_t jsn;  // JSON output (auto_rx)
} option_t;


#define RD41 41
#define RD94 94

#define BITS (1+8+1)  // 8N1 = 10bit/byte

#define HEADLEN (40)
#define HEADOFS (40)

static char header[] =
"10100110010110101001"   // 0x1A = 0 01011000 1
"10010101011010010101"   // 0xCF = 0 11110011 1
"10101001010101010101"   // 0xFC = 0 00111111 1
"10011001010110101001";  // 0x1D = 0 10111000 1

#define       FRAME_LEN  120  // 240/sec -> 120/frame
#define    BITFRAME_LEN (FRAME_LEN*BITS)
#define RAWBITFRAME_LEN (FRAME_LEN*BITS*2)

typedef struct {
    int frnr;
    unsigned id;
    int week; int gpstow; int gpssec;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; int sek; int ms; int cs;
    double lat; double lon;
    double alt;
    //
    double   X; double   Y; double   Z;
    double pAcc;
    double vX1; double vY1; double vZ1;
    double sAcc1;
    int sats;
    double vN; double vE; double vU;
    double vH; double vD; double vV;
    //
    double vX2; double vY2; double vZ2;
    double sAcc2;
    int sats2;
    double alt2;
    double vH2; double vD2; double vV2;
    //
    double P; double T; double U1; double U2;
    double bat; double iT;

    ui8_t type;
    char frame_rawbits[RAWBITFRAME_LEN+8];
    char frame_bits[BITFRAME_LEN+4];
    ui8_t frame_bytes[FRAME_LEN+10];

    option_t option;
    int jsn_freq;   // freq/kHz (SDR)
    int auto_type;

} gpx_t;

/* ------------------------------------------------------------------------------------ */

static char buf[HEADLEN+1] = "x";
static int bufpos = -1;

#define BAUD_RATE 4800  //(4798.8) //4800

static int sample_rate = 0, bits_sample = 0, channels = 0;
static float samples_per_bit = 0;

static int findstr(char *buf, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buf[(pos+i)%4] != str[i]) break;
    }
    return i;
}

static int read_wav_header(FILE *fp) {
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

static unsigned long sample_count = 0;

static int read_signed_sample(FILE *fp) {  // int = i32_t
    int byte, i, sample, s=0;              //  EOF -> 0x1000000

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

    if (bits_sample ==  8) s = sample-128;   // 8bit: 00..FF, centerpoint 0x80=128
    if (bits_sample == 16) s = (short)sample;

    sample_count++;

    return s;
}

static int option_res = 0;
static int par=1, par_alt=1;

static int read_bits_fsk(gpx_t *gpx, FILE *fp, int *bit, int *len) {
    static int sample;
    int n, y0;
    float l, x1;
    static float x0;

    n = 0;
    do {
        y0 = sample;
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        //sample_count++; // in read_signed_sample()
        par_alt = par;
        par =  (sample >= 0) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
        n++;
    } while (par*par_alt > 0);

    if (!option_res) l = (float)n / samples_per_bit;
    else {                                 // genauere Bitlaengen-Messung
        x1 = sample/(float)(sample-y0);    // hilft bei niedriger sample rate
        l = (n+x0-x1) / samples_per_bit;   // meist mehr frames (nicht immer)
        x0 = x1;
    }

    *len = (int)(l+0.5);

    if (!gpx->option.inv) *bit = (1+par_alt)/2;  // oben 1, unten -1
    else                  *bit = (1-par_alt)/2;  // sdr#<rev1381?, invers: unten 1, oben -1
// *bit = (1+inv*par_alt)/2; // ausser inv=0

    return 0;
}

static double bitgrenze = 0;
static int bitstart = 0;
static int read_rawbit(gpx_t *gpx, FILE *fp, int *bit) {
    int sample;
    int n, sum;

    sum = 0;
    n = 0;

    if (bitstart) {
        n = 1;    // d.h. bitgrenze = sample_count-1 (?)
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
        n++;
    } while (sample_count < bitgrenze);  // n < samples_per_bit

    if (sum >= 0) *bit = 1;
    else          *bit = 0;

    if (gpx->option.inv) *bit ^= 1;

    return 0;
}

static int f32soft_read(FILE *fp, float *s, int inv) {
    unsigned int word = 0;
    short *b = (short*)&word;
    float *f = (float*)&word;
    int bps = 32;

    if (fread( &word, bps/8, 1, fp) != 1) return EOF;

    if (bps == 32) {
        *s = *f;
    }
    else {
        if (bps ==  8) { *b -= 128; }
        *s = *b/128.0;
        if (bps == 16) { *s /= 256.0; }
    }

    if (inv) *s = -*s;

    return 0;
}

/* ------------------------------------------------------------------------------------ */

static void inc_bufpos() {
  bufpos = (bufpos+1) % HEADLEN;
}

static int compare() {
    int i, j;

    i = 0;
    j = bufpos;
    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != header[HEADOFS+HEADLEN-1-i]) break;
        j--;
        i++;
    }
    if (i == HEADLEN) return 1;
/*
    i = 0;
    j = bufpos;
    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != cb_inv(header[HEADOFS+HEADLEN-1-i])) break;
        j--;
        i++;
    }
    if (i == HEADLEN) return -1;
*/
    return 0;

}

/* ------------------------------------------------------------------------------------ */

// manchester1 1->10,0->01: 1.bit
// manchester2 0->10,1->01: 2.bit
static void manchester2(char* frame_rawbits, char *frame_bits) {
    int i;
    char bit, bits[2];

    for (i = 0; i < BITFRAME_LEN; i++) {
        bits[0] = frame_rawbits[2*i];
        bits[1] = frame_rawbits[2*i+1];

        if ((bits[0] == '0') && (bits[1] == '1')) bit = '1';
        else
        if ((bits[0] == '1') && (bits[1] == '0')) bit = '0';
        else bit = 'x';
        frame_bits[i] = bit;
    }
}

static int bits2bytes(char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < FRAME_LEN) {

        byteval = 0;
        d = 1;
        for (i = 1; i < BITS-1; i++) {
            bit=*(bitstr+bitpos+i); /* little endian */
            //bit=*(bitstr+bitpos+BITS-1-i);  /* big endian */
            if         (bit == '1')     byteval += d;
            else /*if ((bit == '0') */  byteval += 0;
            d <<= 1;
        }
        bitpos += BITS;
        bytes[bytepos++] = byteval;

    }

    //while (bytepos < FRAME_LEN) bytes[bytepos++] = 0;

    return 0;
}


/* ------------------------------------------------------------------------------------ */
/*
 * Convert GPS Week and Seconds to Modified Julian Day.
 * - Adapted from sci.astro FAQ.
 * - Ignores UTC leap seconds.
 */
//// in : week, gpssec
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


#define OFS             (0x02)        // HEADLEN/(2*BITS)
#define pos_FrameNb     (OFS+0x01)    // 2 byte
// PTUsensors
#define pos_sensorP     (OFS+0x05)    // 4 byte float32
#define pos_sensorT     (OFS+0x09)    // 4 byte float32
#define pos_sensorU1    (OFS+0x0D)    // 4 byte float32
#define pos_sensorU2    (OFS+0x11)    // 4 byte float32
// ublox5 NAV-SOL
#define pos_GPSTOW      (OFS+0x18)    // iTow 4 byte (+ fTOW? 4 byte)
#define pos_GPSweek     (OFS+0x20)    // 2 byte
#define pos_GPSecefX    (OFS+0x24)    // 4 byte
#define pos_GPSecefY    (OFS+0x28)    // 4 byte
#define pos_GPSecefZ    (OFS+0x2C)    // 4 byte
#define pos_GPSpAcc     (OFS+0x30)    // 4 byte
#define pos_GPSecefV1   (OFS+0x34)    // 3*4 byte
#define pos_GPSsAcc1    (OFS+0x40)    // 4 byte
#define pos_GPSsats1    (OFS+0x46)    // 1 byte
#define pos_GPSecefV2   (OFS+0x4A)    // 3*4 byte
#define pos_GPSsAcc2    (OFS+0x56)    // 4 byte
#define pos_GPSsats2    (OFS+0x5A)    // 1 byte
// internal
#define pos94_ID        (OFS+0x5D)    // 4 byte
#define pos94_rev       (OFS+0x61)    // 2 byte char? // e.g. "A5"
#define pos94_bat       (OFS+0x66)    // 2 byte
#define pos94_sensorTi  (OFS+0x68)    // 4 byte float32
// checksums
#define pos_chkFrNb     (pos_FrameNb-1   +  3)  // 16 bit
#define pos_chkPTU      (pos_sensorP     + 17)  // 16 bit
#define pos_chkGPS1     (pos_GPSTOW      + 47)  // 16 bit
#define pos_chkGPS2     (pos_GPSecefV2-1 + 18)  // 16 bit
#define pos_chkInt      (pos94_ID        + 21)  // 16 bit

//
#define pos_pckFrm      (OFS+0x00)    //  3 bytes
#define pos_pckPTU      (OFS+0x05)    // 16 bytes (RD41) / 17 bytes (RD94)
#define pos_CCC         (OFS+0x17)    // 17 bytes
#define pos_DDD         (OFS+0x2A)    // 12 bytes
#define pos_EEE         (OFS+0x38)    // 13 bytes
#define pos_FFF         (OFS+0x47)    // 27 bytes (0x0A0A0A0A...)
#define pos_pckIDint    (OFS+0x64)    // 14 bytes

// internal
#define pos41_ID        (pos_pckIDint    ) //(OFS+0x64)   // 4 byte
#define pos41_bat       (pos_pckIDint+0x6) //(OFS+0x6A)   // 2 byte
#define pos41_sensorTi  (pos_pckIDint+0x8) //(OFS+0x6C)   // 4 byte float32
#define pos41_rev       (pos_pckIDint+0xC) //(OFS+0x70)   // 2 byte char? // e.g. "A5"


static unsigned chksum16(ui8_t *bytes, int len) {
    unsigned sum1, sum2;
    int i;
    sum1 = sum2 = 0;
    for (i = 0; i < len; i++) {
        sum1 = (sum1 + bytes[i]) % 0x100;
        sum2 = (sum2 + sum1) % 0x100;
    }
    //return sum1 | (sum2<<8);
    return sum2 | (sum1<<8);
}

static unsigned crc16(ui8_t *bytes, int len) {
    int crc16poly = 0x1021;
    int rem = 0x0000, i, j;
    int byte;

    for (i = 0; i < len; i++) {
        byte = bytes[i];
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

static int get_ID(gpx_t *gpx) {
    unsigned byte;
    int i;
    int pos_ID = (gpx->type == RD41) ? pos41_ID : pos94_ID;

    byte = 0;
    for (i = 0; i < 4; i++) {              // big endian
        byte |= gpx->frame_bytes[pos_ID + i] << (24-8*i);
    }
    gpx->id = byte;

    return 0;
}

static int get_FrameNb(gpx_t *gpx) {
    int i;
    int frnr;
    ui8_t frnr_bytes[2];

    for (i = 0; i < 2; i++) {
        frnr_bytes[i] = gpx->frame_bytes[pos_FrameNb + i];
    }
    frnr = (gpx->type == RD41) ? frnr_bytes[1] + (frnr_bytes[0] << 8)
                               : frnr_bytes[0] + (frnr_bytes[1] << 8);
    gpx->frnr = frnr;

    return 0;
}

static int get_GPSweek(gpx_t *gpx) {
    int i;
    int gpsweek;
    ui8_t gpsweek_bytes[2];

    for (i = 0; i < 2; i++) {
        gpsweek_bytes[i] = gpx->frame_bytes[pos_GPSweek + i];
    }

    gpsweek = gpsweek_bytes[0] + (gpsweek_bytes[1] << 8);
    if (gpsweek < 0) { gpx->week = -1; return 0x0300; }
    gpx->week = gpsweek;

    return 0;
}

static char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static int get_GPStime_rd94(gpx_t *gpx) {
    int i;
    ui8_t gpstime_bytes[4];
    int gpstime = 0, // 32bit
        day;

    for (i = 0; i < 4; i++) {
        gpstime_bytes[i] = gpx->frame_bytes[pos_GPSTOW + i];
    }

    memcpy(&gpstime, gpstime_bytes, 4);
    gpx->gpstow = gpstime;
    gpx->ms = gpstime % 1000;
    gpstime /= 1000;

    gpx->gpssec = gpx->gpstow / 1000;
    gpx->cs = gpx->ms / 10;

    day = gpstime / (24 * 3600);
    gpstime %= (24*3600);

    if (day < 0 || day > 6) return 0x0100;
    gpx->wday = day;
    gpx->std = gpstime / 3600;
    gpx->min = (gpstime % 3600) / 60;
    gpx->sek = gpstime % 60;

    return 0;
}

#define EARTH_a  6378137.0
#define EARTH_b  6356752.31424518
#define EARTH_a2_b2  (EARTH_a*EARTH_a - EARTH_b*EARTH_b)

static double a = EARTH_a,
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

static int get_GPSkoord_rd94(gpx_t *gpx) {
    int i, k;
    ui8_t XYZ_bytes[4];
    int XYZ; // 32bit
    double X[3], lat, lon, alt;

    for (k = 0; k < 3; k++) {
        for (i = 0; i < 4; i++) {
            XYZ_bytes[i] = gpx->frame_bytes[pos_GPSecefX + 4*k + i];
        }
        memcpy(&XYZ, XYZ_bytes, 4);
        X[k] = XYZ / 100.0;
    }

    // ECEF-Position
    ecef2elli(X, &lat, &lon, &alt);
    gpx->lat = lat;
    gpx->lon = lon;
    gpx->alt = alt;

    for (i = 0; i < 4; i++) {
        XYZ_bytes[i] = gpx->frame_bytes[pos_GPSpAcc + i];
    }
    memcpy(&XYZ, XYZ_bytes, 4);
    gpx->pAcc = XYZ / 100.0;
    gpx->X = X[0];
    gpx->Y = X[1];
    gpx->Z = X[2];

    if ((alt < -1000) || (alt > 80000)) return 0x0200;

    return 0;
}

static int get_GPSvel_rd94(gpx_t *gpx) {
    int i, k;
    ui8_t XYZ_bytes[4];
    int XYZ; // 32bit
    double V[3];
    double phi, lam, dir;

    for (k = 0; k < 3; k++) {
        for (i = 0; i < 4; i++) {
            XYZ_bytes[i] = gpx->frame_bytes[pos_GPSecefV1 + 4*k + i];
        }
        memcpy(&XYZ, XYZ_bytes, 4);
        V[k] = XYZ / 100.0;
    }
    gpx->vX1 = V[0];
    gpx->vY1 = V[1];
    gpx->vZ1 = V[2];
    gpx->sats = gpx->frame_bytes[pos_GPSsats1];

    phi = gpx->lat*M_PI/180.0;
    lam = gpx->lon*M_PI/180.0;
    gpx->vN = -V[0]*sin(phi)*cos(lam) - V[1]*sin(phi)*sin(lam) + V[2]*cos(phi);
    gpx->vE = -V[0]*sin(lam) + V[1]*cos(lam);
    gpx->vU =  V[0]*cos(phi)*cos(lam) + V[1]*cos(phi)*sin(lam) + V[2]*sin(phi);

    gpx->vH = sqrt(gpx->vN*gpx->vN+gpx->vE*gpx->vE);
    dir = atan2(gpx->vE, gpx->vN) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx->vD = dir;
    gpx->vV = gpx->vU;

    for (i = 0; i < 4; i++) {
        XYZ_bytes[i] = gpx->frame_bytes[pos_GPSsAcc1 + i];
    }
    memcpy(&XYZ, XYZ_bytes, 4);
    gpx->sAcc1 = XYZ / 100.0;


    for (k = 0; k < 3; k++) {
        for (i = 0; i < 4; i++) {
            XYZ_bytes[i] = gpx->frame_bytes[pos_GPSecefV2 + 4*k + i];
        }
        memcpy(&XYZ, XYZ_bytes, 4);
        V[k] = XYZ / 100.0;
    }
    gpx->vX2 = V[0];
    gpx->vY2 = V[1];
    gpx->vZ2 = V[2];
    gpx->sats2 = gpx->frame_bytes[pos_GPSsats2];

    for (i = 0; i < 4; i++) {
        XYZ_bytes[i] = gpx->frame_bytes[pos_GPSsAcc2 + i];
    }
    memcpy(&XYZ, XYZ_bytes, 4);
    gpx->sAcc2 = XYZ / 100.0;

    return 0;
}

static int get_GPStime_rd41(gpx_t *gpx) {

    gpx->std = gpx->frame_bytes[pos_CCC+ 9] & 0x1F;
    gpx->min = gpx->frame_bytes[pos_CCC+10];
    gpx->sek = gpx->frame_bytes[pos_CCC+11];
    gpx->cs  = gpx->frame_bytes[pos_CCC+12];
    gpx->ms  = gpx->cs*10;

    return 0;
}

static int get_GPSlatlon_rd41(gpx_t *gpx) {
    double lat, lon;
    int lat_i4, lon_i4;

    lat_i4 = (gpx->frame_bytes[pos_DDD+0+ 0]<<24) | (gpx->frame_bytes[pos_DDD+0+ 1]<<16) |
             (gpx->frame_bytes[pos_DDD+0+ 2]<< 8) |  gpx->frame_bytes[pos_DDD+0+ 3];
    lat = lat_i4 / 1e7;

    lon_i4 = (gpx->frame_bytes[pos_DDD+4+ 0]<<24) | (gpx->frame_bytes[pos_DDD+4+ 1]<<16) |
             (gpx->frame_bytes[pos_DDD+4+ 2]<< 8) |  gpx->frame_bytes[pos_DDD+4+ 3];
    lon = lon_i4 / 1e7;

    gpx->lat = lat;
    gpx->lon = lon;

    return 0;
}

static int get_GPSvelalt_rd41(gpx_t *gpx) {
    short vH1, _D1, _V1; // 16bit
    short vH2, _D2, _V2; // 16bit
    int alt1_i3; // 24bit
    int alt2_i3; // 24bit
    double dir;

    // vel1
    vH1 = (gpx->frame_bytes[pos_CCC + 0] << 8) | gpx->frame_bytes[pos_CCC + 1];
    _D1 = (gpx->frame_bytes[pos_CCC + 2] << 8) | gpx->frame_bytes[pos_CCC + 3];
    _V1 = (gpx->frame_bytes[pos_CCC + 4] << 8) | gpx->frame_bytes[pos_CCC + 5];

    // alt1
    alt1_i3 = (gpx->frame_bytes[pos_CCC+6+ 0]<<16) | (gpx->frame_bytes[pos_CCC+6+ 1]<<8) |
               gpx->frame_bytes[pos_CCC+6+ 2];
    if (alt1_i3 & 0x800000) alt1_i3 -= 0x1000000;

    // pos_CCC+9 .. posCCC+12: time

    gpx->sats = gpx->frame_bytes[pos_CCC+13];

    //vel2
    vH2 = (gpx->frame_bytes[pos_EEE + 0] << 8) | gpx->frame_bytes[pos_EEE + 1];
    _D2 = (gpx->frame_bytes[pos_EEE + 2] << 8) | gpx->frame_bytes[pos_EEE + 3];
    _V2 = (gpx->frame_bytes[pos_EEE + 4] << 8) | gpx->frame_bytes[pos_EEE + 5];

    // alt2
    alt2_i3 = (gpx->frame_bytes[pos_EEE+6+ 0]<<16) | (gpx->frame_bytes[pos_EEE+6+ 1]<<8) |
               gpx->frame_bytes[pos_EEE+6+ 2];
    if (alt2_i3 & 0x800000) alt2_i3 -= 0x1000000;

    gpx->sats2 = gpx->frame_bytes[pos_EEE+9];

    gpx->vH = vH1/100.0;
    gpx->vD = _D1/100.0;
    gpx->vV = -_V1/100.0;
    gpx->alt = alt1_i3/100.0;

    gpx->vH2 = vH2/100.0;
    gpx->vD2 = _D2/100.0;
    gpx->vV2 = -_V2/100.0;
    gpx->alt2 = alt2_i3/100.0;

    return 0;
}

static float float32_rd94(ui8_t *b) {
    int i;
    unsigned num, val;
    float f;
    // double e, s, m;

    num = 0;
    for (i=0;i<4;i++) { num |= b[i] << (24-8*i); }
/*
    val = 0;
    for (i=31;i>=24;i--) { val |= ((num>>i)&1)<<(i-24); }
    e = (double)val-127;  // exponent

    val = 0;
    for (i=22;i>= 0;i--) { val |= ((num>>i)&1)<<i; }
    m = (double)val/(1<<23);  // mantissa

    s = (num>>23)&1 ? -1.0 : +1.0 ;  // sign

    f = s*(1+m)*pow(2,e);
*/
    val  = (num     &   0x800000)<<8;  // sign
    val |= (num>>1) & 0x7F800000;      // exponent
    val |=  num     &   0x7FFFFF;      // mantissa

    memcpy(&f, &val, 4);

    return f;
}

static float float32(ui8_t *b) {
    int i;
    unsigned num, val;
    float f;

    num = 0;
    for (i=0;i<4;i++) { num |= b[i] << (8*i); }

    memcpy(&f, &num, 4);

    return f;
}

static int get_Sensors1(gpx_t *gpx) {
    float (*f32p)(ui8_t *) = (gpx->type == RD41) ? float32 : float32_rd94;

    gpx->P  = f32p(gpx->frame_bytes+pos_sensorP);
    gpx->T  = f32p(gpx->frame_bytes+pos_sensorT);
    gpx->U1 = f32p(gpx->frame_bytes+pos_sensorU1);
    gpx->U2 = f32p(gpx->frame_bytes+pos_sensorU2);

    return 0;
}

static int get_Sensors2(gpx_t *gpx) {
    int val;
    int pos_bat = (gpx->type == RD41) ? pos41_bat : pos94_bat;
    int pos_sensorTi = (gpx->type == RD41) ? pos41_sensorTi : pos94_sensorTi;
    float (*f32p)(ui8_t *) = (gpx->type == RD41) ? float32 : float32_rd94;

    gpx->iT = f32p(gpx->frame_bytes+pos_sensorTi);

    if (gpx->type == RD41) val = (gpx->frame_bytes[pos_bat]<<8) | gpx->frame_bytes[pos_bat+1];
    else                   val = gpx->frame_bytes[pos_bat] | (gpx->frame_bytes[pos_bat+1]<<8);

    gpx->bat = val/1e3;

    return 0;
}

static int getBlock_FrNb(gpx_t *gpx) {  // block 0: frame counter
    unsigned bytes;
    unsigned check;
    int err = 0;
    unsigned (*fchkp)(ui8_t *, int) = (gpx->type == RD41) ? crc16 : chksum16;

     // header (next frame)
    if ( gpx->frame_bytes[OFS+116] != 0x1A ) {
        err |= (0x1 << 7);
    }
    if ( gpx->frame_bytes[OFS+117] != 0xCF ) {
        err |= (0x1 << 8);
    }

    check = fchkp(gpx->frame_bytes+pos_pckFrm, 3);
    bytes = (gpx->frame_bytes[pos_pckFrm+3]<<8) | gpx->frame_bytes[pos_pckFrm+4];
    if (bytes != check)  err |= (0x1 << 0);

    get_FrameNb(gpx);

    return err;
}

static int getBlock_PTU(gpx_t *gpx) {  // block 1: sensors P, T, U1, U2
    unsigned bytes;
    unsigned check;
    int err = 0;
    unsigned (*fchkp)(ui8_t *, int) = (gpx->type == RD41) ? crc16 : chksum16;
    int chk_len = (gpx->type == RD41) ? 16 : 17;

    check = fchkp(gpx->frame_bytes+pos_pckPTU, chk_len);
    bytes = (gpx->frame_bytes[pos_pckPTU+chk_len]<<8) | gpx->frame_bytes[pos_pckPTU+chk_len+1];
    if (bytes != check)  err |= (0x1 << 1);

    get_Sensors1(gpx);

    return err;
}

static int getBlock_GPS_rd94(gpx_t *gpx) {  // block 2,3: GPS pos+vel1, vel2
    unsigned bytes;
    unsigned check;
    int err = 0, err_gps = 0;

    check = chksum16(gpx->frame_bytes+pos_chkGPS1-47, 47);
    bytes = (gpx->frame_bytes[pos_chkGPS1]<<8) | gpx->frame_bytes[pos_chkGPS1+1];
    if (bytes != check)  err |= (0x1 << 2);

    check = chksum16(gpx->frame_bytes+pos_chkGPS2-18, 18);
    bytes = (gpx->frame_bytes[pos_chkGPS2]<<8) | gpx->frame_bytes[pos_chkGPS2+1];
    if (bytes != check)  err |= (0x1 << 3);

    err_gps |= get_GPSweek(gpx);
    err_gps |= get_GPStime_rd94(gpx);
    err_gps |= get_GPSkoord_rd94(gpx);
    err_gps |= get_GPSvel_rd94(gpx);

    return err | (err_gps<<8);
}

static int getBlock_GPS_rd41(gpx_t *gpx) {  // block 2,3,4: GPS vel1+alt1+time, pos, vel2+alt2
    unsigned bytes;
    unsigned check;
    int err = 0, err_gps = 0;

    check = crc16(gpx->frame_bytes+pos_CCC, 17);
    bytes = (gpx->frame_bytes[pos_CCC+17]<<8) | gpx->frame_bytes[pos_CCC+18];
    if (bytes != check)  err |= (0x1 << 2);

    check = crc16(gpx->frame_bytes+pos_DDD, 12);
    bytes = (gpx->frame_bytes[pos_DDD+12]<<8) | gpx->frame_bytes[pos_DDD+13];
    if (bytes != check)  err |= (0x1 << 3);

    check = crc16(gpx->frame_bytes+pos_EEE, 13);
    bytes = (gpx->frame_bytes[pos_EEE+13]<<8) | gpx->frame_bytes[pos_EEE+14];
    if (bytes != check)  err |= (0x1 << 4);

    check = crc16(gpx->frame_bytes+pos_FFF, 27);
    bytes = (gpx->frame_bytes[pos_FFF+27]<<8) | gpx->frame_bytes[pos_FFF+28];
    if (bytes != check)  err |= (0x1 << 5);

    err_gps |= get_GPStime_rd41(gpx);
    err_gps |= get_GPSlatlon_rd41(gpx);
    err_gps |= get_GPSvelalt_rd41(gpx);

    return err;  // | (err<<8);
}

static int getBlock_GPS(gpx_t *gpx) {  // block 2,3: GPS pos+vel1, vel2
    int ret = (gpx->type == RD41) ? getBlock_GPS_rd41(gpx) : getBlock_GPS_rd94(gpx);
    return ret;
}

static int getBlock_Int(gpx_t *gpx) {  // block 4/6: SondeID, internalTemp, battery
    unsigned bytes;
    unsigned check;
    int err = 0;
    unsigned (*fchkp)(ui8_t *, int) = (gpx->type == RD41) ? crc16 : chksum16;
    int pos = (gpx->type == RD41) ? pos_pckIDint : pos_chkInt-21;  // pos_chkInt-21 = pos94_ID
    int len = (gpx->type == RD41) ? 14 : 21;

    check = fchkp(gpx->frame_bytes+pos, len);
    bytes = (gpx->frame_bytes[pos+len]<<8) | gpx->frame_bytes[pos+len+1];
    if (bytes != check) {
        int bl = (gpx->type == RD41) ? 6 : 4;
        err |= (0x1 << bl);
    }

    get_ID(gpx);

    //if (gpx->option.vbs)
    get_Sensors2(gpx);

    return err;
}

static unsigned geterr_rd94(gpx_t * gpx)
{
    unsigned bytes;
    unsigned check;
    unsigned err = 0;

    check = chksum16(gpx->frame_bytes+pos_chkFrNb-3, 3);
    bytes = (gpx->frame_bytes[pos_chkFrNb]<<8) | gpx->frame_bytes[pos_chkFrNb+1];
    if (bytes != check)  err |= (0x1 << 0);

    check = chksum16(gpx->frame_bytes+pos_chkPTU-17, 17);
    bytes = (gpx->frame_bytes[pos_chkPTU]<<8) | gpx->frame_bytes[pos_chkPTU+1];
    if (bytes != check)  err |= (0x1 << 1);

    check = chksum16(gpx->frame_bytes+pos_chkGPS1-47, 47);
    bytes = (gpx->frame_bytes[pos_chkGPS1]<<8) | gpx->frame_bytes[pos_chkGPS1+1];
    if (bytes != check)  err |= (0x1 << 2);

    check = chksum16(gpx->frame_bytes+pos_chkGPS2-18, 18);
    bytes = (gpx->frame_bytes[pos_chkGPS2]<<8) | gpx->frame_bytes[pos_chkGPS2+1];
    if (bytes != check)  err |= (0x1 << 3);

    check = chksum16(gpx->frame_bytes+pos_chkInt-21, 21);
    bytes = (gpx->frame_bytes[pos_chkInt]<<8) | gpx->frame_bytes[pos_chkInt+1];
    if (bytes != check)  err |= (0x1 << 4);

    return err;
}

static unsigned geterr_rd41(gpx_t * gpx)
{
    unsigned bytes;
    unsigned check;
    unsigned err = 0;

    check = crc16(gpx->frame_bytes+pos_pckFrm, 3);
    bytes = (gpx->frame_bytes[pos_pckFrm+3]<<8) | gpx->frame_bytes[pos_pckFrm+4];
    if (bytes != check)  err |= (0x1 << 0);

    check = crc16(gpx->frame_bytes+pos_pckPTU, 16);
    bytes = (gpx->frame_bytes[pos_pckPTU+16]<<8) | gpx->frame_bytes[pos_pckPTU+17];
    if (bytes != check)  err |= (0x1 << 1);

    check = crc16(gpx->frame_bytes+pos_CCC, 17);
    bytes = (gpx->frame_bytes[pos_CCC+17]<<8) | gpx->frame_bytes[pos_CCC+18];
    if (bytes != check)  err |= (0x1 << 2);

    check = crc16(gpx->frame_bytes+pos_DDD, 12);
    bytes = (gpx->frame_bytes[pos_DDD+12]<<8) | gpx->frame_bytes[pos_DDD+13];
    if (bytes != check)  err |= (0x1 << 3);

    check = crc16(gpx->frame_bytes+pos_EEE, 13);
    bytes = (gpx->frame_bytes[pos_EEE+13]<<8) | gpx->frame_bytes[pos_EEE+14];
    if (bytes != check)  err |= (0x1 << 4);

    check = crc16(gpx->frame_bytes+pos_FFF, 27);
    bytes = (gpx->frame_bytes[pos_FFF+27]<<8) | gpx->frame_bytes[pos_FFF+28];
    if (bytes != check)  err |= (0x1 << 5);

    check = crc16(gpx->frame_bytes+pos_pckIDint, 14);
    bytes = (gpx->frame_bytes[pos_pckIDint+14]<<8) | gpx->frame_bytes[pos_pckIDint+15];
    if (bytes != check)  err |= (0x1 << 6);

    return err;
}

static void print_frame(gpx_t *gpx) {
    int i;
    int frm_ok = 0;

    unsigned err_rd94 = -1;
    unsigned err_rd41 = -1;

    if (gpx->type == RD94 || gpx->auto_type) {
        err_rd94 = geterr_rd94(gpx);
    }

    if (gpx->type == RD41 || gpx->auto_type) {
        err_rd41 = geterr_rd41(gpx);
    }

    if (gpx->auto_type) {
        int num_errs41 = 0;
        gpx->type = RD41;
        for (i = 0; i < 7; i++) num_errs41 += (err_rd41>>i)&1;
        if (num_errs41 > 2) {
            int num_errs94 = 0;
            for (i = 0; i < 5; i++) num_errs41 += (err_rd94>>i)&1;
            if (num_errs94 < 3) {
                gpx->type = RD94;
            }
        }
    }
    //DBG:
    //fprintf(stderr, "rd94:%04X # rd41:%04X ## RD%d\n", err_rd94, err_rd41, gpx->type);

    if (gpx->option.raw) {
        for (i = 0; i < FRAME_LEN; i++) {
            fprintf(stdout, "%02x", gpx->frame_bytes[i]);
            if (gpx->option.raw == 2) {
                if (gpx->type == RD94) // RD94
                {
                    if ( i==OFS-1
                        || i==OFS+0  || i==OFS+2                            // frame-counter
                        || i==OFS+4  || i==OFS+8 || i==OFS+12 || i==OFS+16  // sensors (P,T,U1,U2)
                        || i==OFS+20 || i==OFS+21
                        || i==OFS+23 || i==OFS+27                           // TOW
                        || i==OFS+31 || i==OFS+33                           // week
                        || i==OFS+35 || i==OFS+39 || i==OFS+43              // ECEF-pos
                        || i==OFS+47
                        || i==OFS+51 || i==OFS+55 || i==OFS+59              // ECEF-vel1
                        || i==OFS+63 || i==OFS+67
                        || i==OFS+69 || i==OFS+70                           // sats-1
                        || i==OFS+72
                        || i==OFS+73 || i==OFS+77 || i==OFS+81              // ECEF-vel2
                        || i==OFS+85
                        || i==OFS+89 || i==OFS+90                           // sats-2
                        || i==OFS+92 || i==OFS+96 || i==OFS+98              // SondeID, Rev?
                        || i==OFS+101                                       // bat
                        || i==OFS+103 || i==OFS+107                         // internT
                        || i==OFS+113 || i==OFS+115
                        ) fprintf(stdout, " ");
                    if ( i==pos_chkFrNb -4  )  fprintf(stdout, " ");
                    if ( i==pos_chkFrNb +1  )  fprintf(stdout, "[%04X] ", chksum16(gpx->frame_bytes+pos_chkFrNb-3, 3));
                    if ( i==pos_chkPTU  -18 )  fprintf(stdout, " ");
                    if ( i==pos_chkPTU  +1  )  fprintf(stdout, "[%04X] ", chksum16(gpx->frame_bytes+pos_chkPTU-17, 17));
                    if ( i==pos_chkGPS1 -48 )  fprintf(stdout, " ");
                    if ( i==pos_chkGPS1 +1  )  fprintf(stdout, "[%04X] ", chksum16(gpx->frame_bytes+pos_chkGPS1-47, 47));
                    if ( i==pos_chkGPS2 -19 )  fprintf(stdout, " ");
                    if ( i==pos_chkGPS2 +1  )  fprintf(stdout, "[%04X] ", chksum16(gpx->frame_bytes+pos_chkGPS2-18, 18));
                    if ( i==pos_chkInt  -22 )  fprintf(stdout, " ");
                    if ( i==pos_chkInt  +1  )  fprintf(stdout, "[%04X] ", chksum16(gpx->frame_bytes+pos_chkInt-21, 21));
                    if ( i==pos_chkInt  +1  )  fprintf(stdout, " ");
                }
                else
                if (gpx->type == RD41)
                {
                    if ( i==OFS-1 ) fprintf(stdout, "  ");
                    if ( i==pos_pckFrm +3-1)  fprintf(stdout, " ");
                    if ( i==pos_pckFrm +3+1)  fprintf(stdout, " [%04X]  ", crc16(gpx->frame_bytes+pos_pckFrm, 3));
                    if ( i==pos_pckPTU +16-1) fprintf(stdout, " ");
                    if ( i==pos_pckPTU +16+1) fprintf(stdout, " [%04X]  ", crc16(gpx->frame_bytes+pos_pckPTU, 16));
                    if ( i==pos_CCC +17-1) fprintf(stdout, " ");
                    if ( i==pos_CCC +17+1) fprintf(stdout, " [%04X]  ", crc16(gpx->frame_bytes+pos_CCC, 17));
                    if ( i==pos_DDD +12-1) fprintf(stdout, " ");
                    if ( i==pos_DDD +12+1) fprintf(stdout, " [%04X]  ", crc16(gpx->frame_bytes+pos_DDD, 12));
                    if ( i==pos_EEE +13-1) fprintf(stdout, " ");
                    if ( i==pos_EEE +13+1) fprintf(stdout, " [%04X]  ", crc16(gpx->frame_bytes+pos_EEE, 13));
                    if ( i==pos_FFF +27-1) fprintf(stdout, " ");
                    if ( i==pos_FFF +27+1) fprintf(stdout, " [%04X]  ", crc16(gpx->frame_bytes+pos_FFF, 27));
                    if ( i==pos_pckIDint +14-1) fprintf(stdout, " ");
                    if ( i==pos_pckIDint +14+1) fprintf(stdout, " [%04X]  ", crc16(gpx->frame_bytes+pos_pckIDint, 14));
                }
            }
        }

        if (gpx->option.raw == 2) {
            fprintf(stdout, "  # chk: ");
            if (gpx->type == RD94) {  // blocks: 0=F, 1=S, 2=G1, 3=G2, 4=I
                for (i = 0; i < 5; i++) fprintf(stdout, "%d", (err_rd94>>i)&1);
            }
            else
            if (gpx->type == RD41) {  // blocks: 0=F, 1=S, 2=G1, 3=G2, 4=G3, 5=A, 6=I
                for (i = 0; i < 7; i++) fprintf(stdout, "%d", (err_rd41>>i)&1);
            }
        }

        fprintf(stdout, "\n");
    }
    else {

        int err_frn = getBlock_FrNb(gpx);
        int err_ptu = getBlock_PTU(gpx);
        int err_gps = getBlock_GPS(gpx);
        int err_int = getBlock_Int(gpx);

        if (gpx->type == RD94 && !(err_rd94 & 0x17) )
        {
            Gps2Date(gpx);
            fprintf(stdout, "[%5d] ", gpx->frnr);
            fprintf(stdout, "%s", weekday[gpx->wday]);
            fprintf(stdout, " %04d-%02d-%02d", gpx->jahr, gpx->monat, gpx->tag);
            fprintf(stdout, " %02d:%02d:%02d.%03d",  gpx->std, gpx->min, gpx->sek, gpx->ms);
            if (gpx->option.vbs) fprintf(stdout, " (W %d)", gpx->week);
            fprintf(stdout, "  ");
            fprintf(stdout, " lat: %.5f° ", gpx->lat);
            fprintf(stdout, " lon: %.5f° ", gpx->lon);
            fprintf(stdout, " alt: %.2fm ", gpx->alt);
            if (gpx->option.vbs == 2) {
                //fprintf(stdout," (%7.2f,%7.2f,%7.2f) ", gpx->X, gpx->Y, gpx->Z);
                fprintf(stdout, " (E:%.2fm) ", gpx->pAcc);
            }
            if (gpx->option.vbs) {
                fprintf(stdout, " sats: %2d ", gpx->sats);
            }
            if (gpx->option.vbs == 2) {
                fprintf(stdout," V1: (%5.2f,%5.2f,%5.2f) ", gpx->vX1, gpx->vY1, gpx->vZ1);
                fprintf(stdout, "(E:%.2fm/s) ", gpx->sAcc1);
            }
            fprintf(stdout, " vH: %.2fm/s  D: %.1f°  vV: %.2fm/s ", gpx->vH, gpx->vD, gpx->vV);
            if (gpx->option.vbs == 2 && !(err_rd94 & 0x08)) {
                fprintf(stdout, " ENU=(%.2f,%.2f,%.2f) ", gpx->vE, gpx->vN, gpx->vU);
                fprintf(stdout, " V2: (%5.2f,%5.2f,%5.2f) ", gpx->vX2, gpx->vY2, gpx->vZ2);
                fprintf(stdout, "(E:%.2fm/s) ", gpx->sAcc2);
                fprintf(stdout, " sats2: %2d ", gpx->sats2);
            }

            fprintf(stdout, "  ");
            fprintf(stdout, " P=%.2fhPa ", gpx->P);
            fprintf(stdout, " T=%.2f°C ",  gpx->T);
            fprintf(stdout, " H1=%.2f%% ", gpx->U1);
            fprintf(stdout, " H2=%.2f%% ", gpx->U2);
            fprintf(stdout, " ");
            fprintf(stdout, " (%09d) ", gpx->id);

            if (gpx->option.vbs == 2) {
                fprintf(stdout, " ");
                fprintf(stdout, " Ti=%.2f°C ", gpx->iT);
                fprintf(stdout, " Bat=%.2fV ", gpx->bat);
            }

            fprintf(stdout, "  # chk: ");  // blocks: 0=F, 1=S, 2=G1, 3=G2, 4=I
            for (i = 0; i < 5; i++) fprintf(stdout, "%d", (err_rd94>>i)&1);

            fprintf(stdout, "\n");  // fflush(stdout);
        }
        else if (gpx->type == RD41 && !(err_rd41 & 0x4F) )
        {
            fprintf(stdout, "[%5d] ", gpx->frnr);
            //fprintf(stdout, " %04d-%02d-%02d", gpx->jahr, gpx->monat, gpx->tag);
            fprintf(stdout, " %02d:%02d:%02d.%02d",  gpx->std, gpx->min, gpx->sek, gpx->cs);
            fprintf(stdout, "  ");
            fprintf(stdout, " lat: %.5f° ", gpx->lat);
            fprintf(stdout, " lon: %.5f° ", gpx->lon);
            fprintf(stdout, " alt: %.2fm ", gpx->alt);
            fprintf(stdout, " vH: %.2fm/s  D: %.1f°  vV: %.2fm/s ", gpx->vH, gpx->vD, gpx->vV);
            if (gpx->option.vbs) {
                fprintf(stdout, " sats: %2d ", gpx->sats);
            }
            if (gpx->option.vbs && !(err_rd41 & 0x10) ) {
                fprintf(stdout, " alt2: %.2fm ", gpx->alt2);
                fprintf(stdout, " vH2: %.2fm/s  D2: %.1f°  vV2: %.2fm/s ", gpx->vH2, gpx->vD2, gpx->vV2);
                fprintf(stdout, " sats2: %2d ", gpx->sats2);
            }

            fprintf(stdout, "  ");
            fprintf(stdout, " P=%.2fhPa ", gpx->P);
            fprintf(stdout, " T=%.2f°C ",  gpx->T);
            fprintf(stdout, " H1=%.2f%% ", gpx->U1);
            fprintf(stdout, " H2=%.2f%% ", gpx->U2);
            fprintf(stdout, " ");
            fprintf(stdout, " (%09d) ", gpx->id);

            if (gpx->option.vbs == 2) {
                fprintf(stdout, " ");
                fprintf(stdout, " Ti=%.2f°C ", gpx->iT);
                fprintf(stdout, " Bat=%.2fV ", gpx->bat);
            }

            fprintf(stdout, "  # chk: ");  // blocks: 0=F, 1=S, 2=G1, 3=G2, 4=G3, 5=A, 6=I
            for (i = 0; i < 7; i++) fprintf(stdout, "%d", (err_rd41>>i)&1);

            fprintf(stdout, "\n");  // fflush(stdout);
        }

        // all blocks ok
        frm_ok = (gpx->type == RD41) ? (err_rd41 & 0x7F) == 0 : (err_rd94 & 0x1F) == 0;

        // JSON output // raw output ?
        if (gpx->option.jsn && frm_ok && !gpx->option.raw) {
            char *ver_jsn = NULL;
            char id_str[10+1] = {0};
            //if (gpx->id > 0)  // ? 0 -> "000000000"
            {
                sprintf(id_str, "%09d", gpx->id);
            }
            fprintf(stdout, "{ \"type\": \"%s\"", (gpx->type == RD94) ? "RD94" : "RD41");
            fprintf(stdout, ", \"frame\": %d, \"id\": \"%s\"", gpx->frnr, id_str );
            if (gpx->type == RD94) fprintf(stdout, ", \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ\"", gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek, gpx->ms);
            else fprintf(stdout, ", \"datetime\": \"%02d:%02d:%02d.%02dZ\"", gpx->std, gpx->min, gpx->sek, gpx->cs);
            fprintf(stdout, ", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f, \"sats\": %d", gpx->lat, gpx->lon, gpx->alt, gpx->vH, gpx->vD, gpx->vV, gpx->sats );
            //if (!err_ptu)
            {
                if (gpx->T  > -273.0) fprintf(stdout, ", \"temp\": %.1f", gpx->T );
                if (gpx->U1 > -0.5)   fprintf(stdout, ", \"humidity\": %.1f", gpx->U1 ); // gpx->U1 <-> gpx->U2
                if (gpx->P  > 0.0)    fprintf(stdout, ", \"pressure\": %.2f", gpx->P );
            }
            //fprintf(stdout, ", \"subtype\": \"%s\"", rs_typ_str);
            if (gpx->jsn_freq > 0) {
                fprintf(stdout, ", \"freq\": %d", gpx->jsn_freq);
            }

            // Reference time/position
            fprintf(stdout, ", \"ref_datetime\": \"%s\"", (gpx->type == RD94) ? "GPS" : "UTC" ); // {"GPS", "UTC"} GPS-UTC=leap_sec
            fprintf(stdout, ", \"ref_position\": \"%s\"", (gpx->type == RD94) ? "GPS" : "MSL" ); // {"GPS", "MSL"} GPS=ellipsoid , MSL=geoid

            #ifdef VER_JSN_STR
                ver_jsn = VER_JSN_STR;
            #endif
            if (ver_jsn && *ver_jsn != '\0') fprintf(stdout, ", \"version\": \"%s\"", ver_jsn);
            fprintf(stdout, " }\n");
            fprintf(stdout, "\n");
        }
    }
}

static void print_bitframe(gpx_t *gpx, int len) {
    int i;
    for (i = len; i < RAWBITFRAME_LEN; i++) gpx->frame_rawbits[i] = '0';
    manchester2(gpx->frame_rawbits, gpx->frame_bits);
    bits2bytes(gpx->frame_bits, gpx->frame_bytes);
    print_frame(gpx);
}


int main(int argc, char **argv) {

    FILE *fp = NULL;
    char *fpname;
    char *pbuf = NULL;
    int header_found = 0;
    int i, pos, bit, len;
    int fileloaded = 0,
        rawin = 0,
        option_b = 0,
        option_softin = 0;
    int cfreq = -1;
    float baudrate = -1;

    gpx_t gpx = {0};

    fpname = argv[0];
    ++argv;
    while ((*argv) && (!fileloaded)) {
        if ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] <file>\n", fpname);
            fprintf(stderr, "  file: audio.wav or raw_data\n");
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v,        (verbose)\n");
            fprintf(stderr, "       -r,        (output: rawbytes)\n");
            fprintf(stderr, "       -R,        (output: raw_bytes)\n");
            fprintf(stderr, "       -i         (invert polarity)\n");
            fprintf(stderr, "       --rawhex   (input: bytes)\n");
            return 0;
        }
        else if (strcmp(*argv, "-v") == 0) {
            gpx.option.vbs = 1;
        }
        else if (strcmp(*argv, "-vv") == 0) {
            gpx.option.vbs = 2;
        }
        else if (strcmp(*argv, "-r") == 0) {
            gpx.option.raw = 1;
        }
        else if (strcmp(*argv, "-R") == 0) {
            gpx.option.raw = 2;
        }
        else if (strcmp(*argv, "-i") == 0) {
            gpx.option.inv = 1;
        }
        else if (strcmp(*argv, "--rawhex") == 0) { rawin = 2; } //--inbytes // rawbytes input
        else if (strcmp(*argv, "--rd41") == 0) { gpx.type = RD41; }
        else if (strcmp(*argv, "--rd94") == 0) { gpx.type = RD94; }
        else if (strcmp(*argv, "--json") == 0) { gpx.option.jsn = 1; }
        else if (strcmp(*argv, "--jsn_cfq") == 0) {
            int frq = -1;  // center frequency / Hz
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
        }
        else if (strcmp(*argv, "-b" ) == 0) { option_b = 1; }
        //else if (strcmp(*argv, "-b2") == 0) { option_b = 2; }
        else if (strcmp(*argv, "--br") == 0) {
            ++argv;
            if (*argv) {
                baudrate = atof(*argv);                // default: 4800
                if (baudrate < 4700 || baudrate > 4900) baudrate = 4800;
            }
            else return -1;
        }
        else if   (strcmp(*argv, "--softin") == 0)  { option_softin = 1; }  // float32 soft input
        else if   (strcmp(*argv, "--softinv") == 0) { option_softin = 2; }  // float32 inverted soft input
        else {
            if (!rawin) fp = fopen(*argv, "rb");
            else        fp = fopen(*argv, "r");
            if (fp == NULL) {
                fprintf(stderr, "error open %s\n", *argv);
                return -1;
            }
            fileloaded = 1;
        }
        ++argv;
    }
    if (!fileloaded) fp = stdin;

    if (gpx.type) gpx.auto_type = 0;
    else {
        gpx.type = RD41;
        gpx.auto_type = 1;
    }

    if (cfreq > 0) gpx.jsn_freq = (cfreq+500)/1000; // kHz

    if (!rawin) {

        for (pos = 0; pos < HEADLEN; pos++) {
            gpx.frame_rawbits[pos] = header[HEADOFS+pos];
        }

        if (option_softin) {
            float s = 0.0;
            int bitQ = 0;
            while (bitQ != EOF)
            {
                bitQ = f32soft_read(fp, &s, option_softin == 2);
                if (bitQ == EOF) break;

                if (gpx.option.inv) s = -s;
                bit = (s>=0.0);

                inc_bufpos();
                buf[bufpos] = 0x30+bit;

                if (!header_found) {
                    header_found = compare();
                }
                else {
                    gpx.frame_rawbits[pos] = 0x30+bit;
                    //printf("%d", bit);
                    pos++;
                    if (pos == RAWBITFRAME_LEN) {
                        //frames++;
                        print_bitframe(&gpx, pos);
                        header_found = 0;
                        pos = HEADLEN;
                    }
                }
            }
        }
        else {

            i = read_wav_header(fp);
            if (i) {
                fclose(fp);
                return -1;
            }
            if (baudrate > 0) {
                samples_per_bit = sample_rate/baudrate;
                fprintf(stderr, "corr: %.4f\n", samples_per_bit);
            }

            while (!read_bits_fsk(&gpx, fp, &bit, &len)) {

                if (len == 0) { // reset_frame();
                /*  if (pos > 2*BITS*pos_GPSV) {
                        print_bitframe(&gpx, pos);
                        pos = HEADLEN;
                        header_found = 0;
                    } */
                    continue;   // ...
                }

                for (i = 0; i < len; i++) {

                    inc_bufpos();
                    buf[bufpos] = 0x30+bit;

                    if (!header_found) {
                        header_found = compare();
                    }
                    else {
                        gpx.frame_rawbits[pos] = 0x30+bit;
                        //printf("%d", bit);
                        pos++;
                        if (pos == RAWBITFRAME_LEN) {
                            //frames++;
                            print_bitframe(&gpx, pos);
                            header_found = 0;
                            pos = HEADLEN;
                        }
                    }
                }
                if (header_found && option_b) {
                    bitstart = 1;
                    while ( pos < RAWBITFRAME_LEN ) {
                        if (read_rawbit(&gpx, fp, &bit) == EOF) break;
                        gpx.frame_rawbits[pos] = 0x30+bit;
                        //printf("%d", bit);
                        pos++;

                        inc_bufpos();
                        buf[bufpos] = 0x30+bit;
                    }
                    //frames++;
                    print_bitframe(&gpx, pos);
                    header_found = 0;
                    pos = HEADLEN;
                }
            }
        }
    }
    else {  // input: hexraw bytes
        while (1 > 0) {              // rawin=2: 2chars->1byte
            pbuf = fgets(gpx.frame_rawbits, rawin*FRAME_LEN+4, fp);
            if (pbuf == NULL) break;
            gpx.frame_rawbits[rawin*FRAME_LEN+1] = '\0';
            len = strlen(gpx.frame_rawbits) / rawin;
            for (i = 0; i < len; i++) { //%2x  SCNx8=%hhx(inttypes.h)
                sscanf(gpx.frame_rawbits+rawin*i, "%2hhx", gpx.frame_bytes+i);
            }
            for (i = len; i < FRAME_LEN; i++) gpx.frame_bytes[i] = 0x00;
            if (gpx.frame_bytes[0] == 0xFC  &&  gpx.frame_bytes[1] == 0x1D) { // Header: 1A CF FC 1D
                print_frame(&gpx);
            }
        }
    }

    fclose(fp);

    return 0;
}

