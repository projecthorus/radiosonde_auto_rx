
/*
 *  (unknown (26702) 2021-02-19)
 *  radiosonde "MP3-H1" (WMO translit: "MRZ-N1")
 *  author: zilog80
 *
 *  compile:
 *          gcc mp3h1.c -lm -o mp3h1
 *  usage:
 *          ./mp3h1 -v [-b2] fm_audio.wav
 *          (inverse polarity: -i)
 *
 */

#include <stdio.h>
#include <string.h>

#include <math.h>
#include <stdlib.h>

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

#define TIMEOUT_JSN 60


typedef unsigned char ui8_t;
typedef unsigned int ui32_t;
typedef unsigned short ui16_t;
typedef short i16_t;
typedef int i32_t;


#define BITFRAME_LEN    ((51*16)/2)  // 52..53: AA AA (1..5) or 00 00 (6)
#define RAWBITFRAME_LEN (BITFRAME_LEN*2)
#define FRAMESTART      (HEADOFS+HEADLEN)

#define FRAME_LEN       (BITFRAME_LEN/8)

typedef struct {
    ui8_t subcnt1;
    ui8_t subcnt2;
    //int frnr;
    int yr; int mth; int day;
    int hrs; int min; int sec;
    double lat; double lon; double alt;
    double vH; double vD; double vV;
    ui8_t numSats;
    float calA;
    float calB;
    float calC;
    ui8_t frame[FRAME_LEN+16];
    ui32_t cfg[16];
    ui32_t snC;
    ui32_t snD;
    ui8_t crcOK;
    //
    int sec_day;
    int sec_day_prev;
    int gps_cnt;
    int gps_cnt_prev;
    int week;
    int jsn_freq;   // freq/kHz (SDR)
} gpx_t;


static int bits_ofs = 8;
#define HEADLEN 44
#define HEADOFS  0

//Preamble
//header[] = "10011001100110011001""10101010"; // 28, ofs=0
static char header[] = "100110011001100110011001100110011001""10101010";

// each frame 6x
// AA BF 35 ........ AA AA
// AA BF 35 ........ AA AA
// AA BF 35 ........ AA AA
// AA BF 35 ........ AA AA
// AA BF 35 ........ AA AA
// AA BF 35 ........ 00 00

static char buf[HEADLEN+1] = "xxxxxxxxxx\0";
static int bufpos = -1;

static char frame_rawbits[RAWBITFRAME_LEN+8];
static char frame_bits[BITFRAME_LEN+4];


static int option_verbose = 0,  // ausfuehrliche Anzeige
           option_raw = 0,      // rohe Frames
           option_inv = 0,      // invertiert Signal
           option_auto = 0,
           option_avg = 0,      // moving average
           option_b = 0,
           option_ecc = 0,
           option_ptu = 0,
           option_dbg = 0,
           option_jsn = 0,
           option_uniq = 0,
           wavloaded = 0;
static int wav_channel = 0;     // audio channel: left

static int ptu_out = 0;

static int start = 0;

/* -------------------------------------------------------------------------- */

static int MANCH = 1;

// option_b: exakte Baudrate wichtig!
// eventuell in header ermittelbar
#define BAUD_RATE   2400

static int sample_rate = 0, bits_sample = 0, channels = 0;
static float samples_per_bit = 0;

static int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}

static int read_wav_header(FILE *fp) {
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;

    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "RIFF", 4) && strncmp(txt, "RF64", 4)) return -1;

    if (fread(txt, 1, 4, fp) < 4) return -1;
    // pos_WAVE = 8L
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "WAVE", 4))  return -1;

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

    if (bits_sample != 8 && bits_sample != 16 && bits_sample != 32) return -1;

    samples_per_bit = sample_rate/(float)BAUD_RATE;

    fprintf(stderr, "samples/bit: %.2f\n", samples_per_bit);

    return 0;
}


static unsigned long sample_count = 0;

static int f32read_sample(FILE *fp, float *s) {
    int i;
    unsigned int word = 0;
    short *b = (short*)&word;
    float *f = (float*)&word;

    for (i = 0; i < channels; i++) {

        if (fread( &word, bits_sample/8, 1, fp) != 1) return EOF;

        if (i == wav_channel) {  // i = 0: links bzw. mono
            //if (bits_sample ==  8)  sint = b-128;   // 8bit: 00..FF, centerpoint 0x80=128
            //if (bits_sample == 16)  sint = (short)b;

            if (bits_sample == 32) {
                *s = *f;
            }
            else {
                if (bits_sample ==  8) { *b -= 128; }
                *s = *b/128.0;
                if (bits_sample == 16) { *s /= 256.0; }
            }
        }
    }

    sample_count++;

    return 0;
}

static int par=1, par_alt=1;

static int read_bits_fsk(FILE *fp, int *bit, int *len) {
    static float sample;
    int n;
    float l;

    n = 0;
    do {
        if ( f32read_sample(fp, &sample) == EOF ) return EOF;

        par_alt = par;
        par =  (sample >= 0.0f) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
        n++;
    } while (par*par_alt > 0);

    l = (float)n / samples_per_bit;

    *len = (int)(l+0.5);

    if (!option_inv) *bit = (1+par_alt)/2;  // oben 1, unten -1
    else             *bit = (1-par_alt)/2;  // sdr#<rev1381?, invers: unten 1, oben -1
// *bit = (1+inv*par_alt)/2; // ausser inv=0

    /* Y-offset ? */

    return 0;
}

static int bitstart = 0;
static double bitgrenze = 0;
static unsigned long scount = 0;
static int read_rawbit(FILE *fp, int *bit) {
    float sample;
    float sum;

    sum = 0.0f;

    if (bitstart) {
        scount = 0;    // eigentlich scount = 1
        bitgrenze = 0; //   oder bitgrenze = -1
        bitstart = 0;
    }
    bitgrenze += samples_per_bit;

    do {
        if ( f32read_sample(fp, &sample) == EOF ) return EOF;
        //sample_count++; // in f32read_sample()
        //par =  (sample >= 0.0f) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
        sum += sample;
        scount++;
    } while (scount < bitgrenze);  // n < samples_per_bit

    if (sum >= 0.0f) *bit = 1;
    else             *bit = 0;

    if (option_inv) *bit ^= 1;

    return 0;
}

static int read_rawbit2(FILE *fp, int *bit) {
    float sample;
    float sum;

    sum = 0.0f;

    if (bitstart) {
        scount = 0;    // eigentlich scount = 1
        bitgrenze = 0; //   oder bitgrenze = -1
        bitstart = 0;
    }

    bitgrenze += samples_per_bit;
    do {
        if ( f32read_sample(fp, &sample) == EOF ) return EOF;
        //sample_count++; // in f32read_sample()
        //par =  (sample >= 0.0f) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
        sum += sample;
        scount++;
    } while (scount < bitgrenze);  // n < samples_per_bit

    bitgrenze += samples_per_bit;
    do {
        if ( f32read_sample(fp, &sample) == EOF ) return EOF;
        //sample_count++; // in f32read_sample()
        //par =  (sample >= 0.0f) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
        sum -= sample;
        scount++;
    } while (scount < bitgrenze);  // n < samples_per_bit

    if (sum >= 0) *bit = 1;
    else          *bit = 0;

    if (MANCH == 2) *bit ^= 1;

    if (option_inv) *bit ^= 1;

    return 0;
}


/* -------------------------------------------------------------------------- */


static void inc_bufpos() {
  bufpos = (bufpos+1) % HEADLEN;
}

static char cb_inv(char c) {
    if (c == '0') return '1';
    if (c == '1') return '0';
    return c;
}

// Gefahr bei Manchester-Codierung: inverser Header wird leicht fehl-erkannt
// da manchester1 und manchester2 nur um 1 bit verschoben
static int compare2() {
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

    if (option_auto) {
    i = 0;
    j = bufpos;
    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != cb_inv(header[HEADOFS+HEADLEN-1-i])) break;
        j--;
        i++;
    }
    if (i == HEADLEN) return -1;
    }

    return 0;

}

// manchester1 1->10,0->01: 1.bit
// manchester2 0->10,1->01: 2.bit
static void manchester1(char* frame_rawbits, char *frame_bits, int pos) {
    int i, c, out, buf;
    char bit, bits[2];
    c = 0;

    for (i = 0; i < pos/2; i++) {  // -16
        bits[0] = frame_rawbits[2*i];
        bits[1] = frame_rawbits[2*i+1];

        if ((bits[0] == '0') && (bits[1] == '1')) { bit = '0'; out = 1; }
        else
        if ((bits[0] == '1') && (bits[1] == '0')) { bit = '1'; out = 1; }
        else { //
            if (buf == 0) { c = !c; out = 0; buf = 1; }
            else { bit = 'x'; out = 1; buf = 0; }
        }
        if (out) frame_bits[i] = bit;
    }
}
static void manchester2(char* frame_rawbits, char *frame_bits, int pos) {
    int i, c, out, buf;
    char bit, bits[2];
    c = 0;

    for (i = 0; i < pos/2; i++) {  // -16
        bits[0] = frame_rawbits[2*i];
        bits[1] = frame_rawbits[2*i+1];

        if ((bits[0] == '0') && (bits[1] == '1')) { bit = '1'; out = 1; }
        else
        if ((bits[0] == '1') && (bits[1] == '0')) { bit = '0'; out = 1; }
        else { //
            if (buf == 0) { c = !c; out = 0; buf = 1; }
            else { bit = 'x'; out = 1; buf = 0; }
        }
        if (out) frame_bits[i] = bit;
    }
}
static void manchester(char* frame_rawbits, char *frame_bits, int pos) {
    if (MANCH == 1) {
        manchester1(frame_rawbits, frame_bits, pos);
    }
    else {
        manchester2(frame_rawbits, frame_bits, pos);
    }
}

static int bits2bytes(char *bitstr, ui8_t *bytes, int len) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < len) {

        byteval = 0;
        d = 1;
        for (i = 0; i < 8; i++) {
            //bit = *(bitstr+bitpos+i); /* little endian */
            bit = *(bitstr+bitpos+7-i);  /* big endian */
            if (bit == '\0') goto frame_end;
            if         (bit == '1')     byteval += d;
            else /*if ((bit == '0') */  byteval += 0;
            d <<= 1;
        }
        bitpos += 8;
        bytes[bytepos++] = byteval;

    }
frame_end:
    for (i = bytepos; i < FRAME_LEN; i++) bytes[i] = 0;

    return bytepos;
}

/* ------------------------------------------------------------------------------------ */
static int datetime2GPSweek(int yy, int mm, int dd,
                            int hr, int min, int sec,
                            int *week, int *tow) {
    int ww = 0;
    int tt = 0;
    int gpsDays = 0;

    if ( mm < 3 ) { yy -= 1; mm += 12; }

    gpsDays = (int)(365.25*yy) + (int)(30.6001*(mm+1.0)) + dd - 723263; // 1980-01-06

    ww = gpsDays / 7;
    tt = gpsDays % 7;
    tt = tt*86400 + hr*3600 + min*60 + sec;

    *week = ww;
    *tow  = tt;

    return 0;
}
/* ------------------------------------------------------------------------------------ */

static ui32_t u4(ui8_t *bytes) {  // 32bit unsigned int
    ui32_t val = 0; // le: p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24)
    memcpy(&val, bytes, 4);
    return val;
}

static i32_t i4(ui8_t *bytes) {  // 32bit signed int
    i32_t val = 0; // le: p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24)
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

static ui16_t u2(ui8_t *bytes) {  // 16bit unsigned int
    return  bytes[0] | (bytes[1]<<8);
}

static i16_t i2(ui8_t *bytes) { // 16bit signed int
    //return (i16_t)u2(bytes);
    int val = bytes[0] | (bytes[1]<<8);
    if (val & 0x8000) val -= 0x10000;
    return val;
}

// -----------------------------------------------------------------------------

// AA BF 35 .... crc AA AA
// "BF" header/subtype?
// "35" frame length?

#define OFS 0
#define pos_CNT1        (OFS+ 3)  //   1 nibble (0x80..0x8F ?)
#define pos_TIME        (OFS+ 4)  // 3*1 byte
#define pos_GPSecefX    (OFS+ 8)  //   4 byte
#define pos_GPSecefY    (OFS+12)  //   4 byte
#define pos_GPSecefZ    (OFS+16)  //   4 byte
#define pos_GPSecefV    (OFS+20)  // 3*2 byte
#define pos_GPSnSats    (OFS+26)  //   1 byte (num Sats ?)
#define pos_CNT2        (OFS+43)  //   1 byte   (0x01..0x10 ?)
#define pos_CFG         (OFS+44)  // 2/4 byte
#define pos_CRC         (OFS+48)  //   2 byte

// -----------------------------------------------------------------------------

static int crc16rev(gpx_t *gpx, int start, int len) {
    int crc16poly = 0xA001; // rev 0x8005
    int rem = 0xFFFF, i, j;
    int byte;

    if (start+len+2 > FRAME_LEN) return -1;

    for (i = 0; i < len; i++) {
        byte = gpx->frame[start+i];
        rem ^= byte;
        for (j = 0; j < 8; j++) {
            if (rem & 0x0001) {
                rem = (rem >> 1) ^ crc16poly;
            }
            else {
                rem = (rem >> 1);
            }
            rem &= 0xFFFF;
        }
    }
    return rem;
}
static int check_CRC(gpx_t *gpx) {
    ui32_t crclen = 45;
    ui32_t crcdat = 0;
    crcdat = u2(gpx->frame+pos_CRC);
    if ( crcdat != crc16rev(gpx, pos_CNT1, crclen) ) {
        return 1;  // CRC NO
    }
    else return 0; // CRC OK
}

// -----------------------------------------------------------------------------
// WGS84/GRS80 Ellipsoid
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
    ui8_t *gpsVel;
    short vel16; // 16bit
    double V[3];
    double phi, lam, dir;
    double vN; double vE; double vU;


    for (k = 0; k < 3; k++)
    {
        memcpy(&XYZ, gpx->frame+(pos_GPSecefX+4*k), 4);
        X[k] = XYZ / 100.0;

        gpsVel = gpx->frame+(pos_GPSecefV+2*k);
        vel16 = gpsVel[0] | gpsVel[1] << 8;
        V[k] = vel16 / 100.0;
    }


    // ECEF-Position
    ecef2elli(X, &lat, &lon, &alt);
    gpx->lat = lat;
    gpx->lon = lon;
    gpx->alt = alt;
    if ((alt < -1000.0) || (alt > 80000.0)) return -3; // plausibility-check: altitude, if ecef=(0,0,0)


    // ECEF-Velocities
    // ECEF-Vel -> NorthEastUp
    phi = lat*M_PI/180.0;
    lam = lon*M_PI/180.0;
    vN = -V[0]*sin(phi)*cos(lam) - V[1]*sin(phi)*sin(lam) + V[2]*cos(phi);
    vE = -V[0]*sin(lam) + V[1]*cos(lam);
    vU =  V[0]*cos(phi)*cos(lam) + V[1]*cos(phi)*sin(lam) + V[2]*sin(phi);

    // NEU -> HorDirVer
    gpx->vH = sqrt(vN*vN+vE*vE);

    dir = atan2(vE, vN) * 180.0 / M_PI;
    if (dir < 0) dir += 360.0;
    gpx->vD = dir;

    gpx->vV = vU;

    // num Sats solution ? GLONASS + GPS ?
    gpx->numSats = gpx->frame[pos_GPSnSats];

    return 0;
}

static int reset_time(gpx_t *gpx) {

    gpx->gps_cnt = 0;
    gpx->yr = 0;
    gpx->week = 0;

    return 0;
}

static int get_time(gpx_t *gpx) {

    gpx->hrs = gpx->frame[pos_TIME];
    gpx->min = gpx->frame[pos_TIME+1];
    gpx->sec = gpx->frame[pos_TIME+2];


    if (gpx->crcOK)
    {
        int week = 0;
        int tow = 0;
        int sec_gps = 0;

        gpx->gps_cnt_prev = gpx->gps_cnt;
        gpx->sec_day_prev = gpx->sec_day;

        gpx->sec_day = gpx->hrs*60*60 + gpx->min*60 + gpx->sec;


        // JSON frame counter: seconds since GPS (ignoring leap seconds)
        //
        if (gpx->yr == 0) { // 1980-01-06
            week = 0;
            tow  = gpx->sec_day; // yr=mth=day=0
        }
        else {
            datetime2GPSweek(gpx->yr, gpx->mth, gpx->day, gpx->hrs, gpx->min, (int)(gpx->sec+0.5), &week, &tow);
        }
        sec_gps = week*604800 + tow; // SECONDS_IN_WEEK=7*86400=604800
        gpx->week = week;

        if (sec_gps > gpx->gps_cnt_prev) { // skip day roll-over until date update
            gpx->gps_cnt = sec_gps;
        }
    }

    return 0;
}

static int get_cfg(gpx_t *gpx) {

    gpx->subcnt1 = (gpx->frame[pos_CNT1] & 0xF);
    gpx->subcnt2 =  gpx->frame[pos_CNT2] ; // ? subcnt2 == subcnt1 + 1 ?

    if (gpx->crcOK)
    {
        ui32_t cfg32 = u4(gpx->frame+pos_CFG);
        gpx->cfg[gpx->subcnt1] = cfg32;

        switch (gpx->subcnt1) { // or use subcnt2 ?
            case 0x0: //sub2=0x01:
                        memcpy(&gpx->calA, &cfg32, 4);
                    break;
            case 0x1: //sub2=0x02:
                        memcpy(&gpx->calB, &cfg32, 4);
                    break;
            case 0x2: //sub2=0x03:
                        memcpy(&gpx->calC, &cfg32, 4);
                    break;
            case 0xC: //sub2=0x0D: SN GLONASS/GPS ?
                        if (cfg32 != gpx->snC && gpx->snC > 0) {
                            //reset_cfg
                            gpx->snD = 0;
                            reset_time(gpx);
                        }
                        gpx->snC = cfg32; // 16 or 32 bit ?
                    break;
            case 0xD: //sub2=0x0E: SN sensor boom ?
                        if (cfg32 != gpx->snD && gpx->snD > 0) {
                            //reset_cfg
                            gpx->snC = 0;
                            reset_time(gpx);
                        }
                        gpx->snD = cfg32; // 16 or 32 bit ?
                    break;
            case 0xE: //sub2=0x0F: calib date ?
                    break;
            case 0xF: //sub2=0x10: date
                        gpx->yr = cfg32 % 100;
                        gpx->yr += 2000;
                        cfg32 /= 100;
                        gpx->mth = cfg32 % 100;
                        cfg32 /= 100;
                        gpx->day = cfg32 % 100;
                    break;
            default:
                    break;
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------


static void print_gpx(gpx_t *gpx, int crcOK) {
    int i, j;

    //printf(" :%6.1f: ", sample_count/(double)sample_rate);
    //

    gpx->crcOK = crcOK;

    get_cfg(gpx);
    get_time(gpx);
    get_GPSkoord(gpx);

    if (gpx->sec_day != gpx->sec_day_prev || !option_uniq)
    {
        printf(" [%2d] ", gpx->subcnt1);

        printf(" (%02d:%02d:%02d) ", gpx->hrs, gpx->min, gpx->sec);

        printf(" lat: %.5f ", gpx->lat);
        printf(" lon: %.5f ", gpx->lon);
        printf(" alt: %.2f ", gpx->alt);
        printf("  vH: %4.1f  D: %5.1f  vV: %3.1f ", gpx->vH, gpx->vD, gpx->vV);
        printf("  sats: %d ", gpx->numSats);

        printf("  %s", gpx->crcOK ? "[OK]" : "[NO]");


        if (gpx->crcOK)
        {
            if (option_verbose)
            {
                printf("  (<%2d>", gpx->subcnt2);
                // subcnt2 == subcnt1 + 1 ?
                switch (gpx->subcnt1) {
                    case 0x0: printf(" calA: %.5f", gpx->calA); break;
                    case 0x1: printf(" calB: %.2f", gpx->calB); break;
                    case 0x2: printf(" calC: %.3f", gpx->calC); break;
                    case 0xC: printf(" snC: %d", gpx->snC); break;
                    case 0xD: printf(" snD: %d", gpx->snD); break;
                    case 0xE: printf(" calDate: %06d", gpx->cfg[gpx->subcnt1]); break;
                    case 0xF: printf(" %04d-%02d-%02d", gpx->yr, gpx->mth, gpx->day); break;
                    default:  break;
                }
                printf(")");
            }

            if (option_dbg)
            {
                printf("    : ");
                printf(" [0x%X:0x%02X]", gpx->subcnt1, gpx->subcnt2);
                printf("  0x%08X =", gpx->cfg[gpx->subcnt1]);
                if (gpx->subcnt1 > 0x8) printf(" %u ", gpx->cfg[gpx->subcnt1]); // 0x9,0xA not const
                else {
                    float *f = (float*)(gpx->cfg+gpx->subcnt1);
                    printf(" %.4f ", *f);
                }
            }
        }

        printf("\n");
    }


    if (option_jsn && gpx->crcOK) {
        // sonde SN change remains undetected until next SN update
        if (gpx->week > 0 && gpx->gps_cnt > gpx->gps_cnt_prev && gpx->snC > 0 && gpx->snD > 0)
        {
            if (gpx->gps_cnt - gpx->gps_cnt_prev > TIMEOUT_JSN && gpx->gps_cnt_prev > gpx->sec_day_prev) {
                // reset SN after TIMEOUT_JSN sec gap;
                // if new signal replaces old one within timeout limit,
                // new positions might still be transmitted with old SN
                //reset_cfg
                gpx->snC = 0;
                gpx->snD = 0;
                reset_time(gpx);
            }
            else {
                char *ver_jsn = NULL;
                printf("{ \"type\": \"%s\"", "MRZ");
                printf(", \"frame\": %lu, ", (unsigned long)gpx->gps_cnt); // sec_gps0+0.5
                printf("\"id\": \"MRZ-%d-%d\", \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%02dZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f, \"sats\": %d",
                        gpx->snC, gpx->snD, gpx->yr, gpx->mth, gpx->day, gpx->hrs, gpx->min, gpx->sec, gpx->lat, gpx->lon, gpx->alt, gpx->vH, gpx->vD, gpx->vV, gpx->numSats);
                if (gpx->jsn_freq > 0) {
                    printf(", \"freq\": %d", gpx->jsn_freq);
                }
                #ifdef VER_JSN_STR
                    ver_jsn = VER_JSN_STR;
                #endif
                if (ver_jsn && *ver_jsn != '\0') printf(", \"version\": \"%s\"", ver_jsn);
                printf(" }\n");
            }
        }
    }

}

static void print_frame(gpx_t *gpx, int pos) {
    int j;
    int crcOK = 0;

    static int frame_count = 0;

    if (option_b < 2) {
        if (pos > RAWBITFRAME_LEN) pos = RAWBITFRAME_LEN;
        manchester(frame_rawbits, frame_bits, pos);
        pos /= 2;
    }


    if (option_raw == 2) {
        //printf(" :%6.1f: ", sample_count/(double)sample_rate);
        //
        for (j = 0; j < pos; j++) {
            printf("%c", frame_bits[j]);
        }
        //if (frame_count % 3 == 2)
        {
            printf("\n");
        }
    }
    else {
        int frmlen = (pos-bits_ofs)/8;
        bits2bytes(frame_bits+bits_ofs, gpx->frame, frmlen);

        crcOK = (check_CRC(gpx) == 0);

        if (option_raw == 1) {
            //printf(" :%6.1f: ", sample_count/(double)sample_rate);
            //
            for (j = 0; j < frmlen; j++) {
                printf("%02X ", gpx->frame[j]);
            }
            printf(" %s", crcOK ? "[OK]" : "[NO]");
            printf("\n");
        }
        else {

            //if (frame_count % 3 == 0)
            {
                if (pos/8 > pos_GPSecefV+6) print_gpx(gpx, crcOK);
            }
        }
    }

    frame_count++;
}

/* -------------------------------------------------------------------------- */


int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;
    int pos, i, j, bit, len;
    int header_found = 0;
    int cfreq = -1;

    gpx_t gpx = {0};


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
            fprintf(stderr, "       -i, --invert\n");
            fprintf(stderr, "       -b           (alt. Demod.)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "--ofs") == 0) ) {
            ++argv;
            if (*argv) {
                bits_ofs = atoi(*argv);
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "--dbg" ) == 0) ) { option_dbg = 1; }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-R") == 0) || (strcmp(*argv, "--RAW") == 0) ) {
            option_raw = 2;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 0x1;
        }
        else if ( (strcmp(*argv, "--auto") == 0) ) {
            option_auto = 1;
        }
        else if ( (strcmp(*argv, "--uniq") == 0) ) {
            option_uniq = 1;
        }
        else if (strcmp(*argv, "-b" ) == 0) { option_b = 1; }
        else if (strcmp(*argv, "-b2") == 0) { option_b = 2; }
        else if (strcmp(*argv, "--ecc" ) == 0) { option_ecc = 1; }
        else if (strcmp(*argv, "--ptu") == 0) {
            option_ptu = 1;
        }
        else if (strcmp(*argv, "--ch2") == 0) { wav_channel = 1; }  // right channel (default: 0=left)
        else if (strcmp(*argv, "--json") == 0) {
            option_jsn = 1;
        }
        else if ( (strcmp(*argv, "--jsn_cfq") == 0) ) {
            int frq = -1;  // center frequency / Hz
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
        }
        else {
            fp = fopen(*argv, "rb");
            if (fp == NULL) {
                fprintf(stderr, "error open %s\n", *argv);
                return -1;
            }
            wavloaded = 1;
        }
        ++argv;
    }
    if (!wavloaded) fp = stdin;

    gpx.jsn_freq = 0;
    if (cfreq > 0) gpx.jsn_freq = (cfreq+500)/1000;

    i = read_wav_header(fp);
    if (i) {
        fclose(fp);
        return -1;
    }

    memcpy(frame_rawbits, header, HEADLEN);

    pos = FRAMESTART;

    while (!read_bits_fsk(fp, &bit, &len)) {

        if (len == 0) { // reset_frame();
            if (pos > RAWBITFRAME_LEN-10) {
                print_frame(&gpx, pos);
                //header_found = 0;
                //pos = FRAMESTART;
            }
            header_found = 0;
            pos = FRAMESTART;
            inc_bufpos();
            buf[bufpos] = 'x';
            continue;   // ...
        }

        for (i = 0; i < len; i++) {

            inc_bufpos();
            buf[bufpos] = 0x30 + bit;  // Ascii

            if (!header_found) {
                header_found = compare2();
                if (header_found < 0) option_inv ^= 0x1;
            }
            else {
                frame_rawbits[pos] = 0x30 + bit;  // Ascii
                pos++;

                if (pos == RAWBITFRAME_LEN) {
                    frame_rawbits[pos] = '\0';
                    print_frame(&gpx, pos);//FRAME_LEN
                    header_found = 0;
                    pos = FRAMESTART;
                }
            }

        }
        if (header_found && option_b==1) {
            bitstart = 1;

            while ( pos < RAWBITFRAME_LEN ) {
                if (read_rawbit(fp, &bit) == EOF) break;
                frame_rawbits[pos] = 0x30 + bit;
                pos++;
            }
            frame_rawbits[pos] = '\0';
            print_frame(&gpx, pos);//FRAME_LEN

            header_found = 0;
            pos = FRAMESTART;
        }
        if (header_found && option_b>=2) {
            bitstart = 1;

            if (pos%2) {
                if (read_rawbit(fp, &bit) == EOF) break;
                    frame_rawbits[pos] = 0x30 + bit;
                    pos++;
            }

            manchester(frame_rawbits, frame_bits, pos);
            pos /= 2;

            while ( pos < BITFRAME_LEN ) {

                if (read_rawbit2(fp, &bit) == EOF) break;

                frame_bits[pos] = 0x30 + bit;
                pos++;
            }
            frame_bits[pos] = '\0';
            print_frame(&gpx, pos);//FRAME_LEN

            header_found = 0;
            pos = FRAMESTART;
        }
    }


    fclose(fp);

    return 0;
}

