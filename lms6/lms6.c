
/*
   LMS6
   (403 MHz)
*/

#include <stdio.h>
#include <string.h>
#include <math.h>


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;


int option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 0,      // rohe Frames
    option_inv = 0,      // invertiert Signal
    option_res = 0,      // genauere Bitmessung
    wavloaded = 0;


/* -------------------------------------------------------------------------- */

#define BAUD_RATE   4800

int sample_rate = 0, bits_sample = 0, channels = 0;
float samples_per_bit = 0;

int findstr(char *buf, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buf[(pos+i)%4] != str[i]) break;
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

int read_signed_sample(FILE *fp) {  // int = i32_t
    int byte, i, ret;         //  EOF -> 0x1000000

    for (i = 0; i < channels; i++) {
                           // i = 0: links bzw. mono
        byte = fgetc(fp);
        if (byte == EOF) return EOF_INT;
        if (i == 0) ret = byte;
    
        if (bits_sample == 16) {
            byte = fgetc(fp);
            if (byte == EOF) return EOF_INT;
            if (i == 0) ret +=  byte << 8;
        }

    }

    if (bits_sample ==  8) return ret-128;   // 8bit: 00..FF, centerpoint 0x80=128
    if (bits_sample == 16) return (short)ret;

    return ret;
}

int par=1, par_alt=1;
unsigned long sample_count = 0;

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
        sample_count++;
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

    if (!option_inv) *bit = (1+par_alt)/2;  // oben 1, unten -1
    else             *bit = (1-par_alt)/2;  // sdr#<rev1381?, invers: unten 1, oben -1
// *bit = (1+inv*par_alt)/2; // ausser inv=0

    return 0;
}

/* -------------------------------------------------------------------------- */


#define BITS 8
#define HEADLEN (3*16)
#define HEADOFS  0

// (pp pp 24 54) 00                00                00                  (7A..: SondeID, GPS, ...)
char header[] = "0011101100100000""0000000000000000""0000000000000000";//"0010010011110001";


#define FRAMESTART 0

#define FRAME_LEN       (300)  // 4800baud, 16bits/byte
#define BITFRAME_LEN    (FRAME_LEN*BITS)
#define RAWBITFRAME_LEN (BITFRAME_LEN*2)

char buf[HEADLEN];
int bufpos = -1;

char  frame_rawbits[RAWBITFRAME_LEN+8];

#define K 8
char polyA[] = "10010101"; // 0xA9
char polyB[] = "00100010"; // 0x44

char  frame_bits[BITFRAME_LEN+K+4];  // init K-1 bits mit 0
ui8_t frame_bytes[FRAME_LEN]; // = { 0x7A, ... };


void inc_bufpos() {
  bufpos = (bufpos+1) % HEADLEN;
}

char cb_inv(char c) {
    if (c == '0') return '1';
    if (c == '1') return '0';
    return c;
}

int compare2() {
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

    i = 0;
    j = bufpos;
    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != cb_inv(header[HEADOFS+HEADLEN-1-i])) break;
        j--;
        i++;
    }
    if (i == HEADLEN) return -1;

    return 0;

}


int deconv(char* frame_rawbits, char *bits) {

    int j, n, bitA, bitB;
    char *p;
    int len;
    int errors = 0;

        len = strlen(frame_rawbits);
        for (j = 0; j < K-1; j++) bits[j] = '0';
        n = 0;
        while (2*n < len-2*K) {
            p = frame_rawbits+2*n;
            bitA = bitB = 0;
            for (j = 0; j < K-1; j++) {
                bitA ^= (bits[n+j]&1) & (polyA[j]&1);
                bitB ^= (bits[n+j]&1) & (polyB[j]&1);
            }
            if      ( (bitA^(p[0]&1))==(polyA[K-1]&1)  &&  (bitB^(p[1]&1))==(polyB[K-1]&1) ) bits[n+K-1] = '1';
            else if ( (bitA^(p[0]&1))==0               &&  (bitB^(p[1]&1))==0              ) bits[n+K-1] = '0';
            else { // error: no error correction...
                if ( (bitA^(p[0]&1))!=(polyA[K-1]&1) && (bitB^(p[1]&1))==(polyB[K-1]&1) ) bits[n+K-1] = 0x39;
                else bits[n+K-1] = 0x38;
                if (n < 256) errors++; // nur bis Ende GPS-vel; alternativ: return pos 1. error 
            }
            n += 1;
        }
        bits[n+K-1] = '\0';

    return errors;
}

int bits2bytes(char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < FRAME_LEN) {

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

    //while (bytepos < FRAME_LEN) bytes[bytepos++] = 0;

    return 0;
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


#define pos_SondeSN  0x00  // ?3 byte 7A....
#define pos_FrameNb  0x03  // 2 byte
//GPS Position
#define pos_GPSTOW   0x05  // 4 byte
#define pos_GPSlat   0x0D  // 4 byte
#define pos_GPSlon   0x11  // 4 byte
#define pos_GPSalt   0x15  // 4 byte
//#define pos_GPSweek   0x20  // 2 byte
//GPS Velocity East-North-Up (ENU)
#define pos_GPSvO  0x19  // 3 byte
#define pos_GPSvN  0x1C  // 3 byte
#define pos_GPSvV  0x1F  // 3 byte


int get_SondeSN() {
    unsigned byte;

    byte = (frame_bytes[pos_SondeSN]<<16) | (frame_bytes[pos_SondeSN+1]<<8) | frame_bytes[pos_SondeSN+2];
    gpx.sn = byte;

    return 0;
}

int get_FrameNb() {
    int i;
    unsigned byte;
    ui8_t frnr_bytes[2];
    int frnr;

    for (i = 0; i < 2; i++) {
        byte = frame_bytes[pos_FrameNb + i];
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
        byte = frame_bytes[pos_GPSTOW + i];
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

    if ((day < 0) || (day > 6)) return -1;
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
        byte = frame_bytes[pos_GPSlat + i];
        if (byte > 0xFF) return -1;
        gpslat_bytes[i] = byte;
    }

    gpslat = 0;
    for (i = 0; i < 4; i++) {
        gpslat |= gpslat_bytes[i] << (8*(3-i));
    }
    lat = gpslat / B60B60;
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
        byte = frame_bytes[pos_GPSlon + i];
        if (byte > 0xFF) return -1;
        gpslon_bytes[i] = byte;
    }

    gpslon = 0;
    for (i = 0; i < 4; i++) {
        gpslon |= gpslon_bytes[i] << (8*(3-i));
    }
    lon = gpslon / B60B60;
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
        byte = frame_bytes[pos_GPSalt + i];
        if (byte > 0xFF) return -1;
        gpsheight_bytes[i] = byte;
    }

    gpsheight = 0;
    for (i = 0; i < 4; i++) {
        gpsheight |= gpsheight_bytes[i] << (8*(3-i));
    }
    height = gpsheight / 1000.0;
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
        byte = frame_bytes[pos_GPSvO + i];
        if (byte > 0xFF) return -1;
        gpsVel_bytes[i] = byte;
    }
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vx = vel24 / 1e3; // ost

    for (i = 0; i < 3; i++) {
        byte = frame_bytes[pos_GPSvN + i];
        if (byte > 0xFF) return -1;
        gpsVel_bytes[i] = byte;
    }
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vy= vel24 / 1e3; // nord

    for (i = 0; i < 3; i++) {
        byte = frame_bytes[pos_GPSvV + i];
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


void print_frame(int len) {

    int i, err = 0;

    if (len > RAWBITFRAME_LEN) len = RAWBITFRAME_LEN;

    for (i = len; i < RAWBITFRAME_LEN; i++) frame_rawbits[i] = 0;  // oder: '0'
    err = deconv(frame_rawbits, frame_bits);
    bits2bytes(frame_bits+K-1, frame_bytes);


    if (option_raw) {
        if (option_raw == 1) {
            for (i = 0; i < len/(2*BITS); i++) printf("%02x ", frame_bytes[i]); printf("\n");
        }
        else {
            for (i = 0; i < len; i++) printf("%c", frame_rawbits[i]); printf("\n");
        }
    }
    else if (!err  &&  len > 8*2*(pos_GPSTOW+4)) {

        if ((frame_bytes[0] & 0xF0) == 0x70)  // ? beginnen alle SNs mit 0x7A.... bzw 80..... ?
        {
            get_FrameNb();
            get_GPStime();
            get_SondeSN();
            if (option_verbose) printf(" (%7d) ", gpx.sn);
            printf(" [%5d] ", gpx.frnr);
            printf("%s ", weekday[gpx.wday]);
            printf("(%02d:%02d:%06.3f) ", gpx.std, gpx.min, gpx.sek); // falls Rundung auf 60s: Ueberlauf

            get_GPSlat();
            get_GPSlon();
            err = get_GPSalt();
            if (!err) {
                printf(" lat: %.6f° ", gpx.lat);
                printf(" lon: %.6f° ", gpx.lon);
                printf(" alt: %.2fm ", gpx.h);
                //if (option_verbose)
                {
                    get_GPSvel24();
                    //if (option_verbose == 2) printf("  (%.1f ,%.1f,%.1f) ", gpx.vE, gpx.vN, gpx.vU);
                    printf("  vH: %.1fm/s  D: %.1f°  vV: %.1fm/s ", gpx.vH, gpx.vD, gpx.vV);
                }
            }

            printf("\n");
        }
    }

}


int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;
    int i, bit, len, rbit, rbit0;
    int pos;
    //int header_found = 0;


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
        //else if ( (strcmp(*argv, "-vv") == 0) ) option_verbose = 2;
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-R") == 0) || (strcmp(*argv, "--RAW") == 0) ) {
            option_raw = 2;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1; // unnoetig, NRZ-S...
        }
        else if   (strcmp(*argv, "--res") == 0) { option_res = 1; }
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


    pos = FRAMESTART;
    rbit0 = 0; //idle

    while (!read_bits_fsk(fp, &rbit, &len)) {

        if (len == 0) { // reset_frame();
          /*if (pos > 8*2*pos_GPSlon) {
                //for (i = pos; i < RAWBITFRAME_LEN; i++) frame_rawbits[i] = '0';
                print_frame(pos);
                //header_found = 0;
                pos = FRAMESTART;
            }*/
            //inc_bufpos();
            //buf[bufpos] = 'x';
            continue;   // ...
        }

        for (i = 0; i < len; i++) {

            inc_bufpos();
            bit = 0x30 + (rbit==rbit0);  // Ascii, NRZ-S
            buf[bufpos] = bit;
            rbit0 = rbit;

            if (pos < RAWBITFRAME_LEN) frame_rawbits[pos] = bit;
            pos++;
            if ( compare2() ) {  // GPS: (36+3)
                print_frame(pos);//FRAME_LEN
                //header_found = 0;
                pos = FRAMESTART;
            }
        }
    }

    printf("\n");

    fclose(fp);

    return 0;
}

