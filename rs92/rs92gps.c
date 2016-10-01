
/*
 *  radiosonde RS92
 *
 *
 *  broadcast ephemeris:
 *  http://cddis.gsfc.nasa.gov/Data_and_Derived_Products/GNSS/broadcast_ephemeris_data.html
 *  ftp://cddis.gsfc.nasa.gov/gnss/data/daily/YYYY/DDD/YYn/brdcDDD0.YYn.Z (updated)
 *  ftp://cddis.gsfc.nasa.gov/gnss/data/daily/YYYY/brdc/brdcDDD0.YYn.Z (final)
 *
 *  SEM almanac:
 *  https://celestrak.com/GPS/almanac/SEM/
 *
 *  GPS calendar:
 *  http://adn.agi.com/GNSSWeb/
 *
 *  GPS-Hoehe ueber Ellipsoid, Geoid-Hoehe:
 *  http://geographiclib.sourceforge.net/cgi-bin/GeoidEval
 */

/*
    gcc rs92gps.c -lm -o rs92gps
    (includes nav_gps.c)

    examples:

    sox -t oss /dev/dsp -t wav - lowpass 2600 2>/dev/null | ./rs92gps -e brdc3050.15n

    ./rs92gps -r 2015_11_01.wav > raw1.out
    sox -t oss /dev/dsp -t wav - lowpass 2600 2>/dev/null | stdbuf -oL ./rs92gps -r > raw2.out
    ./rs92gps --dop 5 -gg -e brdc3050.15n --rawin1 raw.out

    sox -t oss /dev/dsp -t wav - lowpass 2600 2>/dev/null | tee audio.wav | ./rs92gps -e brdc3050.15n
    ./rs92gps -g1 -e brdc3050.15n 2015_11_01-14.wav | tee out1.txt
    ./rs92gps -g2 -e brdc3050.15n 2015_11_01-14.wav | tee out2.txt
    sox 2015_11_01.wav -t wav - lowpass 2600 2>/dev/null | ./rs92gps -gg -e brdc3050.15n | tee out3.txt

    sox -t oss /dev/dsp -t wav - lowpass 2600 2>/dev/null | stdbuf -oL ./rs92gps -e brdc3050.15n > out1.txt
    sox -t oss /dev/dsp -t wav - lowpass 2600 2>/dev/null | stdbuf -oL ./rs92gps -e brdc3050.15n | tee out2.txt

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

typedef struct {
    int frnr;
    char id[11];
    int week; int gpssec;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double h;
    int sats[4];
    double dop;
    unsigned short aux[4];
} gpx_t;

gpx_t gpx;

int option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 0,      // rohe Frames
    option_inv = 0,      // invertiert Signal
    option_res = 0,      // genauere Bitmessung
    option_avg = 0,      // moving average
    option_b = 0,
    fileloaded = 0,
    option_vergps = 0,
    rawin = 0;
double dop_limit = 10.0;

int rollover = 0,
    err_gps = 0;

int almanac = 0,
    ephem = 0;

/* --- RS92-SGP: 8N1 manchester --- */
#define BITS (2*(1+8+1))  // 20
#define HEADOFS 40 //  HEADOFS+HEADLEN = 120  (bis 0x10)
#define HEADLEN 80 // (HEADOFS+HEADLEN) mod BITS = 0
/*
#define HEADOFS 0  // HEADOFS muss 0 wegen Wiederholung
#define HEADLEN 60 // HEADLEN < 100, (HEADOFS+HEADLEN) mod BITS = 0
*/
#define FRAMESTART ((HEADOFS+HEADLEN)/BITS)

/*               2A                  10*/      
char header[] = "10100110011001101001"
                "10100110011001101001"
                "10100110011001101001"
                "10100110011001101001"
                "1010011001100110100110101010100110101001";
char buf[HEADLEN+1] = "x";
int bufpos = -1;

#define FRAME_LEN 240
ui8_t frame[FRAME_LEN] = { 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x10};
/* --- RS92-SGP ------------------- */

char buffer_rawin[3*FRAME_LEN+8]; //## rawin1: buffer_rawin[2*FRAME_LEN+4]; 
int frameofs = 0;

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

    if (option_inv) *bit ^= 1;

    return 0;
}

/* ------------------------------------------------------------------------------------ */

// manchester1 1->10,0->01: 1.bit
// manchester2 0->10,1->01: 2.bit
// RS92-SGP: 8N1 manchester2
char manch(char *mbits) {
   if      ((mbits[0] == 1) && (mbits[1] == 0)) return 0;
   else if ((mbits[0] == 0) && (mbits[1] == 1)) return 1;
   else return -1;
}
int bits2byte(char bits[]) {
    int i, byteval=0, d=1;
    int bit8[8];

    if (manch(bits+0) != 0) return 0x100;
    for (i = 0; i < 8; i++) {
        bit8[i] = manch(bits+2*(i+1));
    }
    if (manch(bits+(2*(8+1))) != 1) return 0x100;
    
    for (i = 0; i < 8; i++) {     // little endian
    /* for (i = 7; i >= 0; i--) { // big endian */
        if      (bit8[i] == 1)  byteval += d;
        else if (bit8[i] == 0)  byteval += 0;
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

ui8_t xorbyte(int pos) {
    return  frame[pos]; // ^ mask[pos % MASK_LEN];
}


/* ------------------------------------------------------------------------------------ */
/*
 * Convert GPS Week and Seconds to Modified Julian Day.
 * - Adapted from sci.astro FAQ.
 * - Ignores UTC leap seconds.
 */
/*
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
*/
/* ------------------------------------------------------------------------------------ */

#define pos_FrameNb   0x08  // 2 byte
#define pos_SondeID   0x0C  // 8 byte  // oder: 0x0A, 10 byte?
#define pos_CalData   0x17  // 1 byte, counter 0x00..0x1f
#define pos_Calfreq   0x1A  // 2 byte, calfr 0x00
#define pos_GPSTOW    0x48  // 4 byte
#define pos_AuxData   0xC8  // 8 byte

#define posGPS_PRN    0x4E  // 12*5 bit in 8 byte
#define posGPS_DATA   0x62  // 12*8 byte


int get_FrameNb() {
    int i;
    unsigned byte;
    ui8_t frnr_bytes[2];
    int frnr;

    for (i = 0; i < 2; i++) {
        byte = xorbyte(pos_FrameNb + i);
        frnr_bytes[i] = byte;
    }

    frnr = frnr_bytes[0] + (frnr_bytes[1] << 8);
    gpx.frnr = frnr;

    return 0;
}

int get_SondeID() {
    int i;
    unsigned byte;
    ui8_t sondeid_bytes[10];

    for (i = 0; i < 8; i++) {
        byte = xorbyte(pos_SondeID + i);
        if ((byte < 0x20) || (byte > 0x7E)) return -1;
        sondeid_bytes[i] = byte;
    }

    for (i = 0; i < 8; i++) {
        gpx.id[i] = sondeid_bytes[i];
    }
    gpx.id[8] = '\0';

    return 0;
}

char weekday[7][3] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};

int get_GPStime() {
    int i;
    unsigned byte;
    ui8_t gpstime_bytes[4];
    int gpstime = 0, // 32bit
        day;
    float ms;

    for (i = 0; i < 4; i++) {
        byte = xorbyte(pos_GPSTOW + i);
        gpstime_bytes[i] = byte;
    }

    memcpy(&gpstime, gpstime_bytes, 4);
    ms = gpstime % 1000;
    gpstime /= 1000;

    gpx.gpssec = gpstime;

    day = gpstime / (24 * 3600);
    gpstime %= (24*3600);

    if ((day < 0) || (day > 6)) return -1;
    gpx.wday = day;
    gpx.std = gpstime / 3600;
    gpx.min = (gpstime % 3600) / 60;
    gpx.sek = gpstime % 60 + ms/1000.0;

    return 0;
}

int get_Aux() {
    int i;
    unsigned short byte;

    for (i = 0; i < 4; i++) {
        byte = xorbyte(pos_AuxData+2*i)+(xorbyte(pos_AuxData+2*i+1)<<8);
        gpx.aux[i] = byte;
    }

    return 0;
}

int get_Cal() {
    int i;
    unsigned byte;
    ui8_t calfr = 0;
    //ui8_t burst = 0;
    int freq = 0;
    ui8_t freq_bytes[2];

    byte = xorbyte(pos_CalData);
    calfr = byte;

    if (option_verbose == 2) {
        fprintf(stderr, "[%5d] ", gpx.frnr);
        fprintf(stderr, "  0x%02x:", calfr);
    }
    for (i = 0; i < 16; i++) {
        byte = xorbyte(pos_CalData+1+i);
        if (option_verbose == 2) {
            fprintf(stderr, " %02x", byte);
        }
    }
    if (option_verbose) {
        get_Aux();
        if (option_verbose == 2) {
            fprintf(stderr, "  #  ");
            for (i = 0; i < 8; i++) {
                byte = xorbyte(pos_AuxData+i);
                fprintf(stderr, "%02x ", byte);
            }
        }
        else {
            if (gpx.aux[0] != 0 || gpx.aux[1] != 0 ||gpx.aux[2] != 0 || gpx.aux[3] != 0) {
                fprintf(stdout, " # %04x %04x %04x %04x", gpx.aux[0], gpx.aux[1], gpx.aux[2], gpx.aux[3]);
            }
        }   
    }

    if (calfr == 0x00) {
        for (i = 0; i < 2; i++) {
            byte = xorbyte(pos_Calfreq + i);
            freq_bytes[i] = byte;
        }
        byte = freq_bytes[0] + (freq_bytes[1] << 8);
        //printf(":%04x ", byte);
        freq = 400000 + 10*byte; // kHz;
        fprintf(stdout, " : freq %d kHz", freq);
    }

    if (option_verbose == 2) {
        fprintf(stderr, "\n");
    }

    return 0;
}


/* ---------------------------------------------------------------------------------------------------- */


#include "nav_gps.c"

EPHEM_t alm[33];
//EPHEM_t eph[33][24];
EPHEM_t *ephs = NULL;

SAT_t sat[33];


ui8_t prn_le[12*5];
/* le - little endian */
void prnbits_le(ui16_t byte16, ui8_t bits[15]) {
    int i; /* letztes bit wird nicht gelesen */
    for (i = 0; i < 15; i++) {
        bits[i] = byte16 & 1;
        byte16 >>= 1;
    }
}
ui8_t prns[12];
void prn12(ui8_t *prn_le, ui8_t prns[12]) {
    int i, j, d;
    for (i = 0; i < 12; i++) {
        prns[i] = 0;
        d = 1;
        for (j = 0; j < 5; j++) {
          if (prn_le[5*i+j]) prns[i] += d;
          d <<= 1;
        }
        // ?? if (prns[i] == 0) prns[i] = 32; ??  // 5 bit: 0..31
    }
}


int calc_satpos_alm(EPHEM_t alm[], double t, SAT_t *satp) {
    double X, Y, Z;
    int j;
    int week;
    double cl_corr;

    for (j = 1; j < 33; j++) {
        if (alm[j].prn > 0) {  // prn==j

            // Woche hat 604800 sec
            if      (t-alm[j].toa >  604800/2) rollover = +1;
            else if (t-alm[j].toa < -604800/2) rollover = -1;
            else rollover = 0;
            week = alm[j].week - rollover;

            GPS_SatellitePosition_Ephem(
                week, t, alm[j],
                &cl_corr, &X, &Y, &Z
            );

            satp[alm[j].prn].X = X;
            satp[alm[j].prn].Y = Y;
            satp[alm[j].prn].Z = Z;
            satp[alm[j].prn].clock_corr = cl_corr;

        }
    }

    return 0;
}

int calc_satpos_rnx(EPHEM_t eph[][24], double t, SAT_t *satp) {
    double X, Y, Z;
    int j, i, ti;
    int week;
    double cl_corr;
    double tdiff, td;

    for (j = 1; j < 33; j++) {

        // Woche hat 604800 sec
        tdiff = 604800;
        ti = 0;
        for (i = 0; i < 24; i++) {
            if (eph[j][i].prn > 0) {
                if      (t-eph[j][i].toe >  604800/2) rollover = +1;
                else if (t-eph[j][i].toe < -604800/2) rollover = -1;
                else rollover = 0;
                td = t-eph[j][i].toe - rollover*604800;
                if (td < 0) td *= -1;

                if ( td < tdiff ) {
                    tdiff = td;
                    ti = i;
                    week = eph[j][ti].week - rollover;
                }
            }
        }

        GPS_SatellitePosition_Ephem(
            week, t, eph[j][ti],
            &cl_corr, &X, &Y, &Z
        );

        satp[eph[j][ti].prn].X = X;
        satp[eph[j][ti].prn].Y = Y;
        satp[eph[j][ti].prn].Z = Z;
        satp[eph[j][ti].prn].clock_corr = cl_corr;

    }

    return 0;
}

int calc_satpos_rnx2(EPHEM_t *eph, double t, SAT_t *satp) {
    double X, Y, Z;
    int j;
    int week;
    double cl_corr;
    double tdiff, td;
    int count, count0;

    for (j = 1; j < 33; j++) {

        count = count0 = 0;

        // Woche hat 604800 sec
        tdiff = 604800;

        while (eph[count].prn > 0) {

            if (eph[count].prn == j) {

                if      (t - eph[count].toe >  604800/2) rollover = +1;
                else if (t - eph[count].toe < -604800/2) rollover = -1;
                else rollover = 0;
                td = fabs( t - eph[count].toe - rollover*604800);

                if ( td < tdiff ) {
                    tdiff = td;
                    week = eph[count].week - rollover;
                    count0 = count;
                }
            }
            count += 1;
        }

        GPS_SatellitePosition_Ephem(
            week, t, eph[count0],
            &cl_corr, &X, &Y, &Z
        );

        satp[j].X = X;
        satp[j].Y = Y;
        satp[j].Z = Z;
        satp[j].clock_corr = cl_corr;
        satp[j].ephtime = eph[count0].toe;

    }

    return 0;
}


typedef struct {
    int chips;
    ui32_t time;
    int ca;
} RANGE_t;
RANGE_t range[33];

int prn[12];


// pseudo.range = -df*pseudo.chips , df = lightspeed/(chips/sec)/2^10
const double df = 299792.458/1023.0/1024.0; //0.286183844 // c=299792458m/s, 1023000chips/s

int get_pseudorange() {
    ui32_t gpstime;
    ui8_t gpstime_bytes[4];
    ui8_t pseudobytes[4];
    unsigned byteval;
    int i, j, k;
    ui8_t bytes[4];
    ui16_t byte16;
    double  pr0, prj;

    for (i = 0; i < 4; i++) {
        gpstime_bytes[i] = xorbyte(pos_GPSTOW + i);
    }
    memcpy(&gpstime, gpstime_bytes, 4);  // GPS-TOW in ms

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 2; j++) {
            bytes[j] = frame[posGPS_PRN+2*i+j];
        }
        memcpy(&byte16, bytes, 2);
        prnbits_le(byte16, prn_le+15*i);
    }
    prn12(prn_le, prns);  // PRN-Nummern


    if (almanac) calc_satpos_alm(alm, gpstime/1000.0, sat);
    //if (ephem)   calc_satpos_rnx(eph, gpstime/1000.0, sat);
    if (ephem)   calc_satpos_rnx2(ephs, gpstime/1000.0, sat);

    k = 0;
    for (j = 0; j < 12; j++) {

        for (i = 0; i < 4; i++) { pseudobytes[i] = frame[posGPS_DATA+8*j+i]; }
        memcpy(&byteval, pseudobytes, 4);

        if ( byteval == 0x7FFFFFFF  ||  byteval == 0x55555555 ) {
             range[prns[j]].chips = 0;
             continue;
        }
        if (option_vergps != 8) {
        if ( byteval >  0x10000000  &&  byteval <  0xF0000000 ) {
             range[prns[j]].chips = 0;
             continue;
        }}

        if ( prns[j] == 0 )  prns[j] = 32;
        range[prns[j]].chips = byteval;
        range[prns[j]].time = gpstime;
/*
        for (i = 0; i < 4; i++) { pseudobytes[i] = frame[posGPS_DATA+8*j+4+i]; }
        memcpy(&byteval, pseudobytes, 4);
        range[prns[j]].ca = byteval & 0xFFFFFF;
        //range[prns[j]].dop = (byteval >> 24) & 0xFF;
        if (range[prns[j]].ca == 0x555555) {
            range[prns[j]].ca = 0;
            continue;
        }
*/
        if ( dist(sat[prns[j]].X, sat[prns[j]].Y, sat[prns[j]].Z, 0, 0, 0) > 6700000 )
        {
            for (i = 0; i < k; i++) { if (prn[i] == prns[j]) break; }
            if (i == k) {
                prn[k] = prns[j];
                k++;
            }
        }

    }


    for (j = 0; j < 12; j++) {    // 0x013FB0A4
        sat[prns[j]].pseudorange = /*0x01400000*/ - range[prns[j]].chips * df;
    }


    pr0 = (double)0x01400000;
    for (j = 0; j < k; j++) {
        prj = sat[prn[j]].pseudorange + sat[prn[j]].clock_corr;
        if (prj < pr0) pr0 = prj;
    }
    for (j = 0; j < k; j++) sat[prn[j]].PR = sat[prn[j]].pseudorange + sat[prn[j]].clock_corr - pr0 + 20e6;
    // es kann PRNs geben, die zeitweise stark abweichende PR liefern;
    // eventuell Standardabweichung ermitteln und fehlerhafte Sats weglassen

    return k;
}

double DOP[4];

int get_GPSkoord(int N) {
    double lat, lon, alt, rx_cl_bias;
    double pos_ecef[3];
    double gdop, gdop0 = 1000.0;
    double hdop, vdop, pdop;
    int i0, i1, i2, i3, j;
    int nav_ret = 0;
    int num = 0;
    SAT_t Sat_A[4];
    SAT_t Sat_B[12]; // N <= 12

    if (option_vergps == 8) {
        printf("  sats: ");
        for (j = 0; j < N; j++) fprintf(stdout, "%02d ", prn[j]);
        printf("\n");
    }

    gpx.lat = gpx.lon = gpx.h = 0;

    if (option_vergps != 2) {
    for (i0=0;i0<N;i0++) { for (i1=i0+1;i1<N;i1++) { for (i2=i1+1;i2<N;i2++) { for (i3=i2+1;i3<N;i3++) {

        Sat_A[0] = sat[prn[i0]];
        Sat_A[1] = sat[prn[i1]];
        Sat_A[2] = sat[prn[i2]];
        Sat_A[3] = sat[prn[i3]];
        nav_ret = NAV_ClosedFormSolution_FromPseudorange( Sat_A, &lat, &lon, &alt, &rx_cl_bias, pos_ecef );

        if (nav_ret == 0) {
            num += 1;
            if (calc_DOPn(4, Sat_A, pos_ecef, DOP) == 0) {
                gdop = sqrt(DOP[0]+DOP[1]+DOP[2]+DOP[3]);
                //printf(" DOP : %.1f ", gdop);
                if (option_vergps == 8) {
                    //gdop = sqrt(DOP[0]+DOP[1]+DOP[2]+DOP[3]);
                    hdop = sqrt(DOP[0]+DOP[1]);
                    vdop = sqrt(DOP[2]);
                    pdop = sqrt(DOP[0]+DOP[1]+DOP[2]);
                    if (gdop < dop_limit) {
                        printf("       ");
                        printf("lat: %.5f , lon: %.5f , alt: %.1f ", lat, lon, alt);
                        printf("  sats: ");
                        printf("%02d %02d %02d %02d  ", prn[i0], prn[i1], prn[i2], prn[i3]);
                        printf(" GDOP : %.1f  ", gdop);
                        printf(" HDOP: %.1f  VDOP: %.1f ", hdop, vdop);
                        printf(" PDOP: %.1f  ", pdop);
                        printf("\n");
                    }
                }
            }
            else gdop = -1;

            if (gdop > 0 && gdop < gdop0) {
                gpx.lat = lat;
                gpx.lon = lon;
                gpx.h   = alt;
                gpx.dop = gdop;
                gpx.sats[0] = prn[i0]; gpx.sats[1] = prn[i1]; gpx.sats[2] = prn[i2]; gpx.sats[3] = prn[i3];
                gdop0 = gdop;
            }
        }

    }}}}
    }

    if (option_vergps == 8  ||  option_vergps == 2) {

        for (j = 0; j < N; j++) Sat_B[j] = sat[prn[j]];

        if (option_vergps == 8) {
            NAV_bancroft1(N, Sat_B, pos_ecef, &rx_cl_bias);
            ecef2elli(pos_ecef[0], pos_ecef[1], pos_ecef[2], &lat, &lon, &alt);
            printf("bancroft1[%d] lat: %.6f , lon: %.6f , alt: %.2f ", N, lat, lon, alt);
            printf("\n");
        }

        NAV_bancroft2(N, Sat_B, pos_ecef, &rx_cl_bias);
        ecef2elli(pos_ecef[0], pos_ecef[1], pos_ecef[2], &lat, &lon, &alt);
        gdop = -1;
        if (calc_DOPn(N, Sat_B, pos_ecef, DOP) == 0) {
            gdop = sqrt(DOP[0]+DOP[1]+DOP[2]+DOP[3]);
        }

        if (option_vergps == 8) {
            printf("bancroft2[%d] lat: %.6f , lon: %.6f , alt: %.2f ", N, lat, lon, alt);
            printf(" GDOP[");
            for (j = 0; j < N; j++) {
                printf("%d", prn[j]);
                if (j < N-1) printf(","); else printf("] %.1f ", gdop);
            }
            printf("\n");
        }

        if (option_vergps == 2) {
            gpx.lat = lat;
            gpx.lon = lon;
            gpx.h   = alt;
            gpx.dop = gdop;
            num = N;
        }
    }


    return num;
}


/* ------------------------------------------------------------------------------------ */


int print_position() {  // GPS-Hoehe ueber Ellipsoid
    int j, k;
    int err1, err2;

    err1 = 0;
    if (!option_verbose) err1 = err_gps;
    err1 |= get_FrameNb();
    err1 |= get_SondeID();

    err2  = err1 | err_gps;
  //err2 |= get_GPSweek();
    err2 |= get_GPStime();

    if (!err1) {
        //Gps2Date(gpx.week, gpx.gpssec, &gpx.jahr, &gpx.monat, &gpx.tag);
        fprintf(stdout, "[%5d] ", gpx.frnr);
        fprintf(stdout, "(%s) ", gpx.id);
    }

    if (!err2) {
        fprintf(stdout, "%s ", weekday[gpx.wday]);
        fprintf(stdout, "%02d:%02d:%04.1f", gpx.std, gpx.min, gpx.sek);
        /*
        fprintf(stdout, "%04d-%02d-%02d %02d:%02d:%02d", 
                gpx.jahr, gpx.monat, gpx.tag, gpx.std, gpx.min, gpx.sek);
        if (option_verbose) fprintf(stdout, " (W %d)", gpx.week);
        */

        if (almanac || ephem) {
            k = get_pseudorange();
            if (k >= 4) {
                if (get_GPSkoord(k) > 0) {
                    fprintf(stdout, " ");
                    if (almanac) fprintf(stdout, " lat: %.4f  lon: %.4f  alt: %.1f ", gpx.lat, gpx.lon, gpx.h);
                    else         fprintf(stdout, " lat: %.5f  lon: %.5f  alt: %.1f ", gpx.lat, gpx.lon, gpx.h);
                    if (option_vergps) {
                        if (option_vergps != 2) {
                            fprintf(stdout, " GDOP[%02d,%02d,%02d,%02d] %.1f",
                                           gpx.sats[0], gpx.sats[1], gpx.sats[2], gpx.sats[3], gpx.dop);
                        }
                        else {
                            fprintf(stdout, " GDOP[");
                            for (j = 0; j < k; j++) {
                                printf("%d", prn[j]);
                                if (j < k-1) printf(","); else printf("] %.1f ", gpx.dop);
                            }
                        }
                    }
                }
            }
        }

        get_Cal();

    }

    if (!err1) {
        fprintf(stdout, "\n");
        if (option_vergps == 8) printf("\n");
    }

    return err2;
}

void print_frame(int len) {
    int i;
    ui8_t byte;

    if (option_raw) {
        for (i = 0; i < len; i++) {
            //byte = frame[i];
            byte = xorbyte(i);
            fprintf(stdout, "%02x", byte);
        }
        fprintf(stdout, "\n");
    }
    else print_position();
}


int main(int argc, char *argv[]) {

    FILE *fp, *fp_alm = NULL, *fp_eph = NULL;
    char *fpname;
    char bitbuf[BITS];
    int bit_count = 0,
        byte_count = FRAMESTART,
        header_found = 0,
        byte, i;
    int bit, len;
    char *pbuf = NULL;

#ifdef CYGWIN
    _setmode(_fileno(stdin), _O_BINARY);
    setbuf(stdout, NULL);
#endif

    fpname = argv[0];
    ++argv;
    while ((*argv) && (!fileloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] <file>\n", fpname);
            fprintf(stderr, "  file: audio.wav or raw_data\n");
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v, --verbose\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       -i, --invert\n");
            fprintf(stderr, "       -a, --almanac  <almanacSEM>\n");
            fprintf(stderr, "       -e, --ephem    <ephemperisRinex>\n");
            fprintf(stderr, "       -g1         (verbose GPS:   4 sats)\n");
            fprintf(stderr, "       -g2         (verbose GPS: all sats)\n");
            fprintf(stderr, "       -gg         (vverbose GPS)\n");
            fprintf(stderr, "       --rawin1,2  (raw_data file)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "-vv") == 0) ) {
            option_verbose = 2;
        }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
        }
        else if ( (strcmp(*argv, "-a") == 0) || (strcmp(*argv, "--almanac") == 0) ) {
            ++argv;
            if (*argv) fp_alm = fopen(*argv, "r"); // txt-mode
            else return -1;
        }
        else if ( (strcmp(*argv, "-e") == 0) || (strncmp(*argv, "--ephem", 7) == 0) ) {
            ++argv;
            if (*argv) fp_eph = fopen(*argv, "r"); // txt-mode
            else return -1;
        }
        else if ( (strcmp(*argv, "--dop") == 0) ) {
            ++argv;
            if (*argv) {
                dop_limit = atof(*argv);
                if (dop_limit <= 0  || dop_limit >= 100)  dop_limit = 10;
            }
            else return -1;
        }
        else if (strcmp(*argv, "-g1") == 0) { option_vergps = 1; }  //  verbose1 GPS
        else if (strcmp(*argv, "-g2") == 0) { option_vergps = 2; }  //  verbose2 GPS (bancroft)
        else if (strcmp(*argv, "-gg") == 0) { option_vergps = 8; }  // vverbose GPS
        else if (strcmp(*argv, "--rawin1") == 0) { rawin = 2; }     // raw_txt input1
        else if (strcmp(*argv, "--rawin2") == 0) { rawin = 3; }     // raw_txt input2 (SM)
        else if ( (strcmp(*argv, "--avg") == 0) ) {
            option_avg = 1;
        }
        else if   (strcmp(*argv, "-b") == 0) { option_b = 1; }
        else {
            if (!rawin) fp = fopen(*argv, "rb");
            else        fp = fopen(*argv, "r");
            if (fp == NULL) {
                fprintf(stderr, "%s konnte nicht geoeffnet werden\n", *argv);
                return -1;
            }
            fileloaded = 1;
        }
        ++argv;
    }
    if (!fileloaded) fp = stdin;

    if (fp_alm) {
        i = read_SEMalmanac(fp_alm, alm);
        if (i == 0) {
            almanac = 1;
        }
        fclose(fp_alm);
    }
    if (fp_eph) {
        /* i = read_RNXephemeris(fp_eph, eph);
           if (i == 0) {
               ephem = 1;
               almanac = 0;
           }
           fclose(fp_eph); */
        ephs = read_RNXpephs(fp_eph);
        if (ephs) {
            ephem = 1;
            almanac = 0;
        }
        fclose(fp_eph);
    }


    if (!rawin) {

        i = read_wav_header(fp);
        if (i) {
            fclose(fp);
            return -1;
        }

        while (!read_bits_fsk(fp, &bit, &len)) {

            if (len == 0) { // reset_frame();
                if (byte_count > pos_SondeID+8) {
                    if (byte_count < FRAME_LEN-20) err_gps = 1;
                    print_frame(byte_count);
                    err_gps = 0;
                }
                bit_count = 0;
                byte_count = FRAMESTART;
                header_found = 0;
                inc_bufpos();
                buf[bufpos] = 'x';
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
            
                    if (bit_count == BITS) {
                        bit_count = 0;
                        byte = bits2byte(bitbuf);
                        frame[byte_count] = byte;
                        byte_count++;
                        if (byte_count == FRAME_LEN) {
                            byte_count = FRAMESTART;
                            header_found = 0;
                            //inc_bufpos();
                            //buf[bufpos] = 'x';
                            print_frame(FRAME_LEN);
                        }
                    }
                }
            }
            if (header_found && option_b) {
                bitstart = 1;

                while ( byte_count < FRAME_LEN ) {
                    if (read_rawbit(fp, &bit) == EOF) break;
                    bitbuf[bit_count] = bit;
                    bit_count++;
                    if (bit_count == BITS) {
                        bit_count = 0;
                        byte = bits2byte(bitbuf);
                        frame[byte_count] = byte;
                        byte_count++;
                    }
                }
                byte_count = FRAMESTART;
                header_found = 0;
                print_frame(FRAME_LEN);

            }
        }

    }
   else //if (rawin)
   {
        if (rawin == 3) frameofs = 5;

        while (1 > 0) {

            pbuf = fgets(buffer_rawin, rawin*FRAME_LEN+4, fp);
            if (pbuf == NULL) break;
            buffer_rawin[rawin*FRAME_LEN+1] = '\0';
            len = strlen(buffer_rawin) / rawin;
            if (len > pos_SondeID+8) {
                for (i = 0; i < len-frameofs; i++) { //%2x  SCNx8=%hhx(inttypes.h)
                    sscanf(buffer_rawin+rawin*i, "%2hhx", frame+frameofs+i);
                    // wenn ohne %hhx: sscanf(buffer_rawin+rawin*i, "%2x", &byte); frame[frameofs+i] = (ui8_t)byte;
                }
                if (len < FRAME_LEN-20) err_gps = 1;
                print_frame(len);
                err_gps = 0;
            }
        }
    }

    free(ephs);
    fclose(fp);

    return 0;
}


