
/*
   Sippican MkIIa
   LMS-6 (1680 MHz)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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


int option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 0,      // rohe Frames
    option_inv = 0,      // invertiert Signal
    option_crc = 0,      // check CRC
    option_res = 0,      // genauere Bitmessung
    option_b = 0,
    option_jsn = 0,      // JSON output (auto_rx)
    wavloaded = 0;


/* -------------------------------------------------------------------------- */

#define BAUD_RATE   9616  // 9616..9618

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
unsigned long sample_count = 0;

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

    sample_count++;

    if (bits_sample ==  8) return ret-128;   // 8bit: 00..FF, centerpoint 0x80=128
    if (bits_sample == 16) return (short)ret;

    return ret;
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
        //sample_count++;
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

    /* Y-offset ? */

    return 0;
}

int bitstart = 0;
double bitgrenze = 0;
unsigned long scount = 0;
int read_rawbit(FILE *fp, int *bit) {
    int sample;
    int sum;

    sum = 0;

    if (bitstart) {
        scount = 1;    // eigentlich scount = 1
        bitgrenze = 0; //   oder bitgrenze = -1
        bitstart = 0;
    }
    bitgrenze += samples_per_bit;

    do {
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        //sample_count++; // in read_signed_sample()
        //par =  (sample >= 0) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
        sum += sample;
        scount++;
    } while (scount < bitgrenze);  // n < samples_per_bit

    if (sum >= 0) *bit = 1;
    else          *bit = 0;

    if (option_inv) *bit ^= 1;

    return 0;
}

/* -------------------------------------------------------------------------- */


#define BITS (1+8+1)  // 8N1 = 10bit/byte

#define HEADLEN 40
#define HEADOFS 0
             //  CA          CA          CA          24              52
char header[] = "0010100111""0010100111""0010100111""0001001001"; //"0010010101";
// moeglicherweise auch anderes sync-byte als 0xCA moeglich
char sync[]   = "0010100111""0010100111""0010100111""0010100111"; // CA CA CA CA

#define FRAMESTART 0

#define FRAME_LEN       (960+2)   // max; min 36+3 GPS
#define BITFRAME_LEN    (FRAME_LEN*BITS)

char buf[HEADLEN];
int bufpos = -1;

ui8_t frame_bytes[FRAME_LEN] = { 0x24 }; // = { 0x24, 0x52, ... };
char  frame_bits[BITFRAME_LEN+4];


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

int findsync() {
    int i, j;

    i = 0;
    j = bufpos;
    // SYNCLEN=HEADLEN
    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != sync[HEADLEN-1-i]) break;
        j--;
        i++;
    }
    if (i == HEADLEN) return 1;

    return 0;
}


int bits2bytes(char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < FRAME_LEN) {

        byteval = 0;
        d = 1;
        for (i = 1; i < BITS-1; i++) {
            bit = *(bitstr+bitpos+i); /* little endian */
            //bit = *(bitstr+bitpos+BITS-1-i);  /* big endian */
            if (bit == '\0') goto frame_end;
            if         (bit == '1')     byteval += d;
            else /*if ((bit == '0') */  byteval += 0;
            d <<= 1;
        }
        bitpos += BITS;
        bytes[bytepos++] = byteval;

    }
frame_end:
    for (i = bytepos; i < FRAME_LEN; i++) bytes[i] = 0;

    return bytepos;
}

/* -------------------------------------------------------------------------- */

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

typedef struct {
    int frnr;
    int prev_frnr;
    ui32_t id;
    int week; int gpstow;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double alt;
    double vH; double vD; double vV;
    double vE; double vN; double vU;
    //int freq;
    int jsn_freq;   // freq/kHz (SDR)
} gpx_t;

gpx_t gpx;


#define OFS 2  // (0x2452 ..)
#define pos_SondeID  (OFS+0x02)  // 2 byte (LSB)
#define pos_FrameNb  (OFS+0x04)  // 2 byte
//GPS Position
#define pos_GPSTOW   (OFS+0x08)  // 4 byte, subframe 0x(2452)54
#define pos_GPSlat   (OFS+0x10)  // 4 byte, subframe 0x(2452)54
#define pos_GPSlon   (OFS+0x14)  // 4 byte, subframe 0x(2452)54
#define pos_GPSalt   (OFS+0x18)  // 4 byte, subframe 0x(2452)54
//GPS Velocity East-North-Up (ENU)
#define pos_GPSvO    (OFS+0x1C)  // 3 byte, subframe 0x(2452)54
#define pos_GPSvN    (OFS+0x1F)  // 3 byte, subframe 0x(2452)54
#define pos_GPSvV    (OFS+0x22)  // 3 byte, subframe 0x(2452)54
// full 1680MHz-ID, config-subblock:sonde_id
#define pos_FullID   (OFS+0x30)  // 2+2 byte (LSB,MSB), subframe 0x(2452)4D


int check_CRC(int len) {
    ui32_t crclen = 0,
           crcdat = 0;
/*
    if      (frame_bytes[OFS] == 0x4D) crclen = 67;
    else if (frame_bytes[OFS] == 0x54) crclen = 172; // 172, 146? variable? Mk2a, LMS6-1680?
    else crclen = len;
*/
    crclen = len;
    crcdat = (frame_bytes[crclen]<<8) | frame_bytes[crclen+1];
    if ( crcdat != crc16_0(frame_bytes, crclen) ) {
        return 1;  // CRC NO
    }
    else return 0; // CRC OK
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


//char weekday[7][3] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

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
    gpx.alt = height;

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

    int i, crc_err = 0;
    int flen = len/BITS;

    for (i = len; i < BITFRAME_LEN; i++) frame_bits[i] = 0;  // oder: '0'
    bits2bytes(frame_bits, frame_bytes+1);

    while (flen > 2 && frame_bytes[flen-1] == 0xCA) flen--; // if crc != 0xYYCA ...

    crc_err = check_CRC(flen-2);
    if (crc_err) { // crc_bytes == sync_bytes?
        crc_err = check_CRC(flen-1);
        if (crc_err == 0) flen += 1;
        else {
            crc_err = check_CRC(flen);
            if (crc_err == 0) flen += 2;
        }
    }

    if (option_raw) {
        for (i = 0; i < flen; i++) printf("%02x ", frame_bytes[i]);
        if (option_crc) {
            if (crc_err==0) printf(" [OK]"); else printf(" [NO]");
        }
        printf("\n");
    }

    if (frame_bytes[OFS] == 0x4D  &&  len/BITS > pos_FullID+4) {
        if ( !crc_err ) {
            if (frame_bytes[pos_SondeID]   == frame_bytes[pos_FullID]  &&
                frame_bytes[pos_SondeID+1] == frame_bytes[pos_FullID+1]) {
                ui32_t __id =  (frame_bytes[pos_FullID+2]<<24) | (frame_bytes[pos_FullID+3]<<16)
                                | (frame_bytes[pos_FullID]  << 8) |  frame_bytes[pos_FullID+1];
                gpx.id = __id;
            }
        }
    }

    if (frame_bytes[OFS] == 0x54  &&  len/BITS > pos_GPSalt+4) {

        get_FrameNb();
        get_GPStime();
        get_GPSlat();
        get_GPSlon();
        get_GPSalt();

        if ( !crc_err ) {
            ui32_t _id = (frame_bytes[pos_SondeID]<<8) | frame_bytes[pos_SondeID+1];
            if ((gpx.id & 0xFFFF) != _id) gpx.id = _id;
        }
        if (option_verbose && !crc_err) {
            if (gpx.id & 0xFFFF0000) printf(" (%u)", gpx.id);
            else if (gpx.id) printf(" (0x%04X)", gpx.id);
        }

        printf(" [%5d] ", gpx.frnr);

        printf("%s ", weekday[gpx.wday]);
        printf("%02d:%02d:%06.3f ", gpx.std, gpx.min, gpx.sek); // falls Rundung auf 60s: Ueberlauf
        printf(" lat: %.5f ", gpx.lat);
        printf(" lon: %.5f ", gpx.lon);
        printf(" alt: %.2fm ", gpx.alt);

        get_GPSvel24();
        printf("  vH: %.1fm/s  D: %.1f  vV: %.1fm/s ", gpx.vH, gpx.vD, gpx.vV);
        //if (option_verbose == 2) printf("  (%.1f ,%.1f,%.1f) ", gpx.vE, gpx.vN, gpx.vU);

        if (option_crc) {
            if (crc_err==0) printf(" [OK]"); else printf(" [NO]");
        }

        printf("\n");

        if (option_jsn) {
            // Print JSON output required by auto_rx.
            if (crc_err==0 && (gpx.id & 0xFFFF0000)) { // CRC-OK and FullID
                if (gpx.prev_frnr != gpx.frnr) { //|| gpx.id != _id0
                    // UTC oder GPS?
                    char *ver_jsn = NULL;
                    printf("{ \"type\": \"%s\"", "LMS");
                    printf(", \"frame\": %d, \"id\": \"LMS6-%d\", \"datetime\": \"%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f",
                            gpx.frnr, gpx.id, gpx.std, gpx.min, gpx.sek, gpx.lat, gpx.lon, gpx.alt, gpx.vH, gpx.vD, gpx.vV );
                    printf(", \"subtype\": \"%s\"", "MK2A");
                    if (gpx.jsn_freq > 0) {
                        printf(", \"freq\": %d", gpx.jsn_freq);
                    }
                    #ifdef VER_JSN_STR
                        ver_jsn = VER_JSN_STR;
                    #endif
                    if (ver_jsn && *ver_jsn != '\0') printf(", \"version\": \"%s\"", ver_jsn);
                    printf(" }\n");
                    printf("\n");
                    gpx.prev_frnr = gpx.frnr;
                }
            }
        }

    }


}


int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;
    int i, bit, len;
    int pos;
    int header_found = 0;
    int cfreq = -1;


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
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
        }
        else if   (strcmp(*argv, "-b" ) == 0) { option_b = 1; }
        else if   (strcmp(*argv, "--crc") == 0) { option_crc = 1; }
        else if   (strcmp(*argv, "--res") == 0) { option_res = 1; }
        else if   (strcmp(*argv, "--json") == 0) {
            option_jsn = 1;
            option_crc = 1;
            option_verbose = 1;
        }
        else if   (strcmp(*argv, "--jsn_cfq") == 0) {
            int frq = -1;  // center frequency / Hz
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
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

    gpx.jsn_freq = 0;
    if (cfreq > 0) gpx.jsn_freq = (cfreq+500)/1000;

    i = read_wav_header(fp);
    if (i) {
        fclose(fp);
        return -1;
    }


    pos = FRAMESTART;

    while (!read_bits_fsk(fp, &bit, &len)) {

        if (len == 0) { // reset_frame();
            if (pos > BITS*(pos_GPSalt+4)) {
                print_frame(pos);
                header_found = 0;
                pos = FRAMESTART;
            }
            //inc_bufpos();
            //buf[bufpos] = 'x';
            continue;   // ...
        }

        for (i = 0; i < len; i++) {

            inc_bufpos();
            buf[bufpos] = bit + 0x30;

            if (!header_found) {
                header_found = compare2();
            }
            else {
                frame_bits[pos] = bit + 0x30;
                pos++;
                if ( pos == BITFRAME_LEN  ||  findsync() ) {
                    print_frame(pos);//FRAME_LEN
                    header_found = 0;
                    pos = FRAMESTART;
                }
            }
        }
        if (header_found && option_b==1) {
            bitstart = 1;

            while ( pos < BITFRAME_LEN && !findsync()) {
                if (read_rawbit(fp, &bit) == EOF) break;
                inc_bufpos();
                buf[bufpos] = bit + 0x30;
                frame_bits[pos] = bit + 0x30;
                pos++;
            }
            frame_bits[pos] = '\0';
            print_frame(pos);//FRAME_LEN

            header_found = 0;
            pos = FRAMESTART;
        }

    }

    printf("\n");

    fclose(fp);

    return 0;
}

