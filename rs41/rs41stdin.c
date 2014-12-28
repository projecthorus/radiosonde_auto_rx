
/*
 * radiosonde RS41-SG
 * author: zilog80
 * usage:
 *     gcc rs41stdin.c -lm -o rs41stdin
 *     ./rs41stdin [options] audio.wav
 *       options:
 *               -v, --verbose
 *               -r, --raw
 *     ./rs41stdin audio.wav
 *     ./rs41stdin -r audio.wav | less -S
 *     ./rs41stdin -v audio.wav 1> /dev/null
 *     ./rs41stdin -v audio.wav 1> pos.txt
 *     ./rs41stdin -v audio.wav 2> cal.txt
 *     ./rs41stdin -v audio.wav 2>&1 >/dev/null | grep 0x00
 *     ./rs41stdin < audio.wav
 *     sox -t oss /dev/dsp -t wav - 2>/dev/null | ./rs41stdin
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

typedef unsigned char ui8_t;

typedef struct {
    int frnr;
    char id[9];
    int week; int gpssec;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; int sek;
    double lat; double lon; double h;
} gpx_t;

gpx_t gpx;

int option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 0,      // rohe Frames
    wavloaded = 0;


#define HEADOFS 24 // HEADOFS+HEADLEN <= 64
#define HEADLEN 32 // HEADOFS+HEADLEN mod 8 = 0
#define FRAMESTART ((HEADOFS+HEADLEN)/8)

/*               10      B6      CA      11      22      96      12      F8      */      
char header[] = "0000100001101101010100111000100001000100011010010100100000011111";
char buf[HEADLEN+1] = "x";
int bufpos = -1;

#define FRAME_LEN 320
ui8_t frame[FRAME_LEN] = { 0x10, 0xB6, 0xCA, 0x11, 0x22, 0x96, 0x12, 0xF8};


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
    char txt[5] = "\0\0\0\0";
    char buff[4];
    int byte, p;
    // long pos_fmt, pos_dat;
    char fmt_[5] = "fmt ";
    char data[5] = "data";

    //if (fseek(fp, 0L, SEEK_SET)) return -1;
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "RIFF", 4)) return -1;

    // pos_WAVE = 8L
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "WAVE", 4)) return -1;

    // pos_fmt = 12L
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        buff[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(buff, fmt_, p) == 4) break;
    }
    
    if (fread(buff, 1, 4, fp) < 4) return -1;
    //memcpy(&byte, buff, 4); fprintf(stderr, "fmt_length : %04x\n", byte);
    if (fread(buff, 1, 2, fp) < 2) return -1;
    //byte = buff[0] + (buff[1] << 8); fprintf(stderr, "fmt_tag    : %04x\n", byte & 0xFFFF);
    if (fread(buff, 1, 2, fp) < 2) return -1;
    channels = buff[0] + (buff[1] << 8);
    //fprintf(stderr, "channels   : %d\n", channels & 0xFFFF);
    if (fread(buff, 1, 4, fp) < 4) return -1;
    memcpy(&sample_rate, buff, 4);
    //fprintf(stderr, "samplerate : %d\n", sample_rate);
    if (fread(buff, 1, 4, fp) < 4) return -1;
    //memcpy(&byte, buff, 4); fprintf(stderr, "bytes/sec  : %d\n", byte);
    if (fread(buff, 1, 2, fp) < 2) return -1;
    byte = buff[0] + (buff[1] << 8);
    //fprintf(stderr, "block_align: %04x\n", byte & 0xFFFF);
    if (fread(buff, 1, 2, fp) < 2) return -1;
    bits_sample = buff[0] + (buff[1] << 8);
    //fprintf(stderr, "bits/sample: %d\n", bits_sample & 0xFFFF);

    // pos_dat = 36L + info
    //if (fread(txt, 1, 4, fp) < 4) return -1;
    //fprintf(stderr, "data: %s\n", txt);
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        buff[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(buff, data, p) == 4) break;
    }
    if (fread(buff, 1, 4, fp) < 4) return -1;


    fprintf(stderr, "sample_rate: %d\n", sample_rate);
    fprintf(stderr, "bits       : %d\n", bits_sample);
    fprintf(stderr, "channels   : %d\n", channels);

    if ((bits_sample != 8) && (bits_sample != 16)) return -1;

    samples_per_bit = sample_rate/(float)BAUD_RATE;

    fprintf(stderr, "samples/bit: %.2f\n", samples_per_bit);

    return 0;
}

int read_sample(FILE *fp) {  // int = i32_t
    int byte, i, ret;        // return ui16_t/ui8_t oder EOF

    for (i = 0; i < channels; i++) {
                          // i = 0: links bzw. mono
        byte = fgetc(fp);
        if (byte == EOF) return EOF;
        if (i == 0) ret = byte;
    
        if (bits_sample == 16) {
            byte = fgetc(fp);
            if (byte == EOF) return EOF;
            if (i == 0) ret +=  byte << 8;
        }

    }
                 // unsigned 8/16 bit sample >= 0
    return ret;  // EOF < 0
}

int sign(int sample) {
    int sgn = 0;
    if (bits_sample == 8) {                         // unsigned char:
        if (sample & 0x80) sgn = 1; else sgn = -1;  // 00..7F - , 80..FF: +
    }
    else if (bits_sample == 16) {
        if (sample & 0x8000) sgn = -1; else sgn = 1;
    }
    return sgn;
}

int par=1, par_alt=1;
unsigned long sample_count = 0;

int read_bits_fsk(FILE *fp, int *bit, int *len) {
    int n, sample;
    float l;

    n = 0;
    do{
        sample = read_sample(fp);  // unsigned sample;
        if (sample == EOF) return EOF; // usample >= 0
        par_alt = par;
        par = sign(sample);
        sample_count++;
        n++;
    } while (par*par_alt > 0);

    l = (float)n / samples_per_bit;  // abw = n % samples_per_bit;

    *len = (int)(l+0.5);
    *bit = (1-par_alt)/2;  // unten 1, oben -1
                           // inverse: *bit = (1+par_alt)/2;
    /* Y-offset ? */

    return 0;
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
        if (buf[j] != header[HEADLEN+HEADOFS-1-i]) break;
        j--;
        i++;
    }
    return i;
}

ui8_t xorbyte(int pos) {
    return  frame[pos] ^ mask[pos % MASK_LEN];
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

#define pos_FrameNb   0x03B  // 2 byte
#define pos_SondeID   0x03D  // 8 byte
#define pos_CalData   0x052  // 1 byte, counter 0x00..0x32
#define pos_GPSweek   0x095  // 2 byte
#define pos_GPSTOW    0x097  // 4 byte
#define pos_GPSecefX  0x114  // 4 byte
#define pos_GPSecefY  0x118  // 4 byte
#define pos_GPSecefZ  0x11C  // 4 byte


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
    ui8_t sondeid_bytes[8];

    for (i = 0; i < 8; i++) {
        byte = xorbyte(pos_SondeID + i);
        if ((byte < 0x20) || (byte > 0x7E)) return -1;
        sondeid_bytes[i] = byte;
    }

    for (i = 0; i < 8; i++) {
        gpx.id[i] = sondeid_bytes[i];
    }
    gpx.id[9] = '\0';

    return 0;
}

int get_GPSweek() {
    int i;
    unsigned byte;
    ui8_t gpsweek_bytes[2];
    int gpsweek;

    for (i = 0; i < 2; i++) {
        byte = xorbyte(pos_GPSweek + i);
        gpsweek_bytes[i] = byte;
    }

    gpsweek = gpsweek_bytes[0] + (gpsweek_bytes[1] << 8);
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

    for (i = 0; i < 4; i++) {
        byte = xorbyte(pos_GPSTOW + i);
        gpstime_bytes[i] = byte;
    }

    memcpy(&gpstime, gpstime_bytes, 4);
    //ms = gpstime % 1000;
    gpstime /= 1000;

    gpx.gpssec = gpstime;

    day = gpstime / (24 * 3600);
    gpstime %= (24*3600);

    if ((day < 0) || (day > 6)) return -1;
    gpx.wday = day;
    gpx.std = gpstime / 3600;
    gpx.min = (gpstime % 3600) / 60;
    gpx.sek = gpstime % 60;

    return 0;
}

void ecef2elli(double X[], double *lat, double *lon, double *h) {
    double a = 6378137.0,
           b = 6356752.31424518,
           e, ee;
    double phi, lam, R, p, t;

    e  = sqrt( (a*a - b*b) / (a*a) );
    ee = sqrt( (a*a - b*b) / (b*b) );

    lam = atan2( X[1] , X[0] );

    p = sqrt( X[0]*X[0] + X[1]*X[1] );
    t = atan2( X[2]*a , p*b );
    
    phi = atan2( X[2] + ee*ee * b * sin(t)*sin(t)*sin(t) ,
                 p - e*e * a * cos(t)*cos(t)*cos(t) );

    R = a / sqrt( 1 - e*e*sin(phi)*sin(phi) );
    *h = p / cos(phi) - R;
    
    *lat = phi*180/M_PI;
    *lon = lam*180/M_PI;
}


int get_GPSkoord() {
    int i, k;
    unsigned byte;
    ui8_t XYZ_bytes[4];
    int XYZ; // 32bit
    double X[3], lat, lon, h;

    for (k = 0; k < 3; k++) {

        for (i = 0; i < 4; i++) {
            byte = xorbyte(pos_GPSecefX + 4*k + i);
            XYZ_bytes[i] = byte;
        }

        memcpy(&XYZ, XYZ_bytes, 4);
        X[k] = XYZ / 100.0;

    }

    ecef2elli(X, &lat, &lon, &h);
    gpx.lat = lat;
    gpx.lon = lon;
    gpx.h = h;

    return 0;
}

int get_Cal() {
    int i;
    unsigned byte;
    ui8_t calfr;

    byte = xorbyte(pos_CalData);
    calfr = byte;

    fprintf(stderr, "  0x%02x:", calfr);
    for (i = 0; i < 16; i++) {
        byte = xorbyte(pos_CalData+1+i);
        fprintf(stderr, " %02x", byte);
    }
    fprintf(stderr, "\n");

    return 0;
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
            if (option_verbose) fprintf(stdout, "(%s) ", gpx.id);
            fprintf(stdout, "%s ", weekday[gpx.wday]);
            fprintf(stdout, "%04d-%02d-%02d %02d:%02d:%02d", 
                    gpx.jahr, gpx.monat, gpx.tag, gpx.std, gpx.min, gpx.sek);
            if (option_verbose) fprintf(stdout, " (W %d)", gpx.week);
            fprintf(stdout, " ");
            fprintf(stdout, " lat: %.5f ", gpx.lat);
            fprintf(stdout, " lon: %.5f ", gpx.lon);
            fprintf(stdout, " h: %.2f ", gpx.h);

            if (option_verbose) get_Cal();

            fprintf(stdout, "\n");
        }

    return err;
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

    FILE *fp;
    char *fpname;
    char bitbuf[8];
    int bit_count = 0,
        byte_count = FRAMESTART,
        header_found = 0,
        byte, i;
    int bit, len;

    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v, --verbose\n");
            fprintf(stderr, "       -r, --raw\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
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


    i = read_wav_header(fp);
    if (i) {
        fclose(fp);
        return -1;
    }


    while (!read_bits_fsk(fp, &bit, &len)) {

        if (len == 0) { // reset_frame();
            if (byte_count > FRAME_LEN-20) print_frame(byte_count);
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
            
                if (bit_count == 8) {
                    bit_count = 0;
                    byte = bits2byte(bitbuf);
                    frame[byte_count] = byte;
                    byte_count++;
                    if (byte_count == FRAME_LEN) {
                        byte_count = FRAMESTART;
                        header_found = 0;
                        print_frame(FRAME_LEN);
                    }
                }
            }

        }

    }
    fclose(fp);

    return 0;
}


