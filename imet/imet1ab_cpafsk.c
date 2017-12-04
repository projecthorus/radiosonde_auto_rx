
/*
 * radiosonde iMet-1-AB (GPS: Trimble/ublox)
 * author: zilog80
 * usage:
 *     gcc imet1ab.c -lm -o imet1ab
 *     ./imet1ab [options] audio.wav
 *       options:
 *               -r, --raw
 *               -i, --invert
 *               -1  (trimble: TOW/s)
 *               -2  (ublox: TOW/ms)
 *
 *
 * AFSK 1200Hz/2400Hz, noncoherent correlation:
 * option -b
 *   gcc imet1ab_cpafsk.c -lm -o imet1ab_cpfsk
 *   ./imet1ab_cpfsk -b -v imet1ab.wav
 *
 *   waveform output:
 *   gcc -DMULTI imet1ab_cpafsk.c -lm -o imet1ab_multi
 *   ./imet1ab_multi -b imet1ab.wav > multi_imet.wav
 *   wenn leise und 8bit, z.B.:
 *   ./imet1ab_multi -b -g 100 imet1ab.wav > multi_imet.wav
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef CYGWIN
  #include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif


typedef unsigned char ui8_t;

typedef struct {
    int frnr;
    char id1[9]; char id2[9];
    int week; double gpssec;
    //int jahr; int monat; int tag;
    int wday;
    int std; int min; int sek; int ms;
    double lat; double lon; double alt;
    double vH; double vD; double vV;
    double vx; double vy; double vD2;
} gpx_t;

gpx_t gpx;

int option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 0,      // rohe Frames
    option_color = 0,    // Farbe
    option_inv = 0,      // invertiert Signal
    option_b = 0,
    option_gps = 0,
    wavloaded = 0;

/* -------------------------------------------------------------------------- */

#define BAUD_RATE  2400
// iMet: AFSK Baudrate 2400
/* 1200 Hz: out-in
   2400 Hz: lang  ->  Baudrate
   4800 Hz: kurz (2x)
*/

unsigned int sample_rate, channels, bytes_sec, bits_sample, blockalign, datblocksize, datsize8;
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
    if (fread(dat, 1, 4, fp) < 4) return -1;
    datsize8 = dat[0] | (dat[1] << 8) | (dat[2] << 16) | (dat[3] << 24);

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

    // bits_sample
    if (fread(dat, 1, 2, fp) < 2) return -1;
    bits_sample = dat[0] + (dat[1] << 8);

    // channels
    if (fread(dat, 1, 2, fp) < 2) return -1;
    channels = dat[0] + (dat[1] << 8);

    // sample_rate
    if (fread(dat, 1, 4, fp) < 4) return -1;
    sample_rate = dat[0] | (dat[1] << 8) | (dat[2] << 16) | (dat[3] << 24); //memcpy(&sr, dat, 4);

    // bytes/sec
    if (fread(dat, 1, 4, fp) < 4) return -1;
    bytes_sec = dat[0] | (dat[1] << 8) | (dat[2] << 16) | (dat[3] << 24);

    // block align
    if (fread(dat, 1, 2, fp) < 2) return -1;
    blockalign = dat[0] | (dat[1] << 8);

    // bits/sample
    if (fread(dat, 1, 2, fp) < 2) return -1;
    bits_sample = dat[0] + (dat[1] << 8);
    if ((bits_sample != 8) && (bits_sample != 16)) return -2;


    // pos_dat = 36L + info
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "data", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;
    datblocksize = dat[0] | (dat[1] << 8) | (dat[2] << 16) | (dat[3] << 24);


    fprintf(stderr, "sample_rate: %d\n", sample_rate);
    fprintf(stderr, "bits       : %d\n", bits_sample);
    fprintf(stderr, "channels   : %d\n", channels);

    if ((bits_sample != 8) && (bits_sample != 16)) return -1;

    samples_per_bit = sample_rate/(float)BAUD_RATE;

    fprintf(stderr, "samples/bit: %.2f\n", samples_per_bit);

    return 0;
}

int read_wavheader(FILE *fp, unsigned char chIn, unsigned char chOut, FILE *fout) {
    unsigned int size = 0;
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;

    fseek(fp, 0, SEEK_SET);

    if (fread(txt, 1, 4, fp) < 4) return -1;  fwrite(txt, 1, 4, fout);
    if (strncmp(txt, "RIFF", 4)) return -1;

    if (fread(dat, 1, 4, fp) < 4) return -1;
    size = dat[0] | (dat[1] << 8) | (dat[2] << 16) | (dat[3] << 24);
    size = ((size+8-44)*chOut)/chIn + 44-8;
    for (byte = 0; byte < 4; byte++) { dat[byte] = size & 0xFF; size >>= 8; }
    fwrite(dat, 1, 4, fout);

    // pos_WAVE = 8L
    if (fread(txt, 1, 4, fp) < 4) return -1;  fwrite(txt, 1, 4, fout);
    if (strncmp(txt, "WAVE", 4)) return -1;

    // pos_fmt = 12L
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;  fprintf(fout, "%c", byte & 0xFF);
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "fmt ", p) == 4) break;
    }

    if (fread(dat, 1, 4, fp) < 4) return -1;  fwrite(dat, 1, 4, fout);
    if (fread(dat, 1, 2, fp) < 2) return -1;  fwrite(dat, 1, 2, fout);

    // channels
    if (fread(dat, 1, 2, fp) < 2) return -1;
    dat[0] = chOut; fwrite(dat, 1, 2, fout);

    // sample_rate
    if (fread(dat, 1, 4, fp) < 4) return -1;  fwrite(dat, 1, 4, fout);

    // bytes/sec
    if (fread(dat, 1, 4, fp) < 4) return -1;
    size = dat[0] | (dat[1] << 8) | (dat[2] << 16) | (dat[3] << 24);
    size = (size*chOut)/chIn;
    for (byte = 0; byte < 4; byte++) { dat[byte] = size & 0xFF; size >>= 8; }
    fwrite(dat, 1, 4, fout);

    // block align
    if (fread(dat, 1, 2, fp) < 2) return -1;
    size = dat[0] | (dat[1] << 8);
    size = (size*chOut)/chIn;
    for (byte = 0; byte < 2; byte++) { dat[byte] = size & 0xFF; size >>= 8; }
    fwrite(dat, 1, 2, fout);

    // bits/sample
    if (fread(dat, 1, 2, fp) < 2) return -1;  fwrite(dat, 1, 2, fout);
    //bits_sample = dat[0] + (dat[1] << 8);
    //if ((bits_sample != 8) && (bits_sample != 16)) return -2;

    // pos_dat = 36L + info
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;  fprintf(fout, "%c", byte & 0xFF);
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "data", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;
    size = dat[0] | (dat[1] << 8) | (dat[2] << 16) | (dat[3] << 24);
    size = (size*chOut)/chIn;
    for (byte = 0; byte < 4; byte++) { dat[byte] = size & 0xFF; size >>= 8; }
    fwrite(dat, 1, 4, fout);

    return 0;
}

int f32read_sample(FILE *fp, double *s) {  // channels == 1
    int i;
    short b = 0;

    for (i = 0; i < channels; i++) {

        if (fread( &b, bits_sample/8, 1, fp) != 1) return EOF;

        if (bits_sample ==  8) { b -= 128; }

        if (i == 0) {  // i = 0: links bzw. mono
            *s = b/128.0;
            if (bits_sample == 16) { *s /= 256.0; }
        }
    }

    return 0;
}

int f32write_mults(FILE *fp, double *w, int ch) {
    int i;
    int b;
    double x;

    for (i = 0; i < ch; i++) {
        x = 128.0 * w[i];
        if (bits_sample ==  8) { x += 128.0; }
        if (bits_sample == 16) { x *= 256.0; }

        b = (int)x; // -> short
                                            // 16 bit  (short)  ->  (int)
        fwrite( &b, bits_sample/8, 1, fp);  // +  0000 .. 7FFF  ->  0000 0000 .. 0000 7FFF
                                            // -  8000 .. FFFF  ->  FFFF 8000 .. FFFF FFFF
    }

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

    if (bits_sample ==  8) return ret-128;
    if (bits_sample == 16) return (short)ret;

    return ret;
}


int par=1,     // init_sample > 0
    par_alt=1;
unsigned long sample_count = 0;

int read_afsk_bits(FILE *fp, int *len) {
    int n, sample;
    float l;

    n = 0;
    do{ // High                               // par>0
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        if (option_inv) sample = -sample;
        sample_count++;
        par_alt = par;
        par =  (sample >= 0) ? 1 : -1;
        n++;
    } while (par*par_alt > 0);

    do{ // Low                                // par<0
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        if (option_inv) sample = -sample;
        sample_count++;
        par_alt = par;
        par =  (sample >= 0) ? 1 : -1;
        n++;
    } while (par*par_alt > 0);                // par>0

    l = (float)n / (samples_per_bit/2.0);
    *len = (int)(l+0.5); // round(l)

    return 0;
}

int read_afsk_bits1(FILE *fp, int *len) {
    int n; static int sample;
    float l;

    while (sample >= 0) {
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        if (option_inv) sample = -sample;
        sample_count++;
    }
    n = 0;
    while (sample < 0) {
        n++;
        par_alt = par;
        par =  (sample >= 0) ? 1 : -1;
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        if (option_inv) sample = -sample;
        sample_count++;
    }
    while (sample >= 0) {
        n++;
        par_alt = par;
        par =  (sample >= 0) ? 1 : -1;
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        if (option_inv) sample = -sample;
        sample_count++;
    }

    l = (float)n / (samples_per_bit/2.0);
    *len = (int)(l+0.5); // round(l)

    return 0;
}

/* -------------------------------------------------------------------------- */

/*
Beginn/Header:
69 69 69 69 69 10
Ende:
96 96 96 96 96 96 96 96 96
*/

#define pos_Start  0x05  // 2 byte

#define pos_RecordNo  0x08  // 2 byte
#define pos_SondeID1  0x12  // 5 byte
#define pos_SondeID2  0x2C  // 5 byte

#define pos_GPSTOW  0x8A  // 4 byte
#define pos_GPSlat  0x8E  // 4 byte
#define pos_GPSlon  0x92  // 4 byte
#define pos_GPSalt  0x96  // 4 byte
//Velocity East-North-Up (ENU)
#define pos_GPSvO  0x84  // 2 byte
#define pos_GPSvN  0x86  // 2 byte
#define pos_GPSvV  0x88  // 2 byte

#define pos_xcSum  0xC2  // 1 byte: xsumDLE(frame+pos_Start, 189)
                         //         189 = pos_xcSum-pos_Start

#define FRAMELEN 204
ui8_t frame[FRAMELEN+6];

double B60B60 = 0xB60B60;  // 2^32/360 = 0xB60B60.xxx

char weekday[7][3] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};

typedef struct {
    int cnt;
    int tow;
} gpstow_t;

gpstow_t tow0, tow1;

int gpsTOW(int gpstime) {
    int day;

    tow0 = tow1;
    tow1.tow = gpstime;
    tow1.cnt = gpx.frnr;
    if (!option_gps) {
        if (tow1.cnt-tow0.cnt == 1) {
            if (tow1.tow-tow0.tow > 998  &&  tow1.tow-tow0.tow < 1002)  option_gps = 2;
            if (tow1.tow-tow0.tow > 0    &&  tow1.tow-tow0.tow < 2   )  option_gps = 1;
        }
    }

    gpx.gpssec = gpstime;
    if (option_gps == 2) {
        gpx.ms = gpstime % 1000;
        gpx.gpssec /= 1000.0;
        gpstime /= 1000;
    }
    if (gpx.gpssec<0 || gpx.gpssec>7*24*60*60) return 1;  // 1 Woche = 604800 sek

    day = gpstime / (24 * 3600);
    gpstime %= (24*3600);

    if ((day < 0) || (day > 6)) {
        //gpx.wday = 0;
        return 1;
    }
    gpx.wday = day;
    gpx.std = gpstime / 3600;
    gpx.min = (gpstime % 3600) / 60;
    gpx.sek = gpstime % 60;

    return 0;
}

int gpsLat(int lat) {
    gpx.lat = lat / B60B60;
    if (gpx.lat < -90  ||  gpx.lat > 90) return 1;
    return 0;
}

int gpsLon(int lon) {
    gpx.lon = lon / B60B60;
    if (gpx.lon < -180  ||  gpx.lon > 180) return 1;
    return 0;
}

int gpsAlt(int alt) {
    gpx.alt = alt / 1000.0;
    if (gpx.alt < -200  ||  gpx.alt > 50000) return 1;
    return 0;
}

int get_GPStow() {
    int i, tow;
    int err = 0;

    tow = 0;
    for (i = 0; i < 4; i++) {
        tow |= frame[pos_GPSTOW+i] << (8*i);
    }
    err = gpsTOW(tow);

    return err;
}

int get_GPSpos() {
    int i, lat, lon, alt;
    int err = 0;

    lat = lon = alt = 0;
    for (i = 0; i < 4; i++) {
        lat |= frame[pos_GPSlat+i] << (8*i);
        lon |= frame[pos_GPSlon+i] << (8*i);
        alt |= frame[pos_GPSalt+i] << (8*i);
    }
    err = 0;
    err |= gpsLat(lat) << 1;
    err |= gpsLon(lon) << 2;
    err |= gpsAlt(alt) << 3;

    return err;
}

int get_GPSvel() {
    int i;
    unsigned byte;
    ui8_t gpsVel_bytes[2];
    short vel16;
    double vx, vy, dir, alpha;
    const double ms2kn100 = 2e2;  // m/s -> knots: 1 m/s = 3.6/1.852 kn = 1.94 kn

    for (i = 0; i < 2; i++) {
        byte = frame[pos_GPSvO + i];
        if (byte > 0xFF) return -1;
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] | gpsVel_bytes[1] << 8;
    vx = vel16 / ms2kn100; // ost

    for (i = 0; i < 2; i++) {
        byte = frame[pos_GPSvN + i];
        if (byte > 0xFF) return -1;
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] | gpsVel_bytes[1] << 8;
    vy= vel16 / ms2kn100; // nord

    gpx.vx = vx;
    gpx.vy = vy;
    gpx.vH = sqrt(vx*vx+vy*vy);
///*
    alpha = atan2(vy, vx)*180/M_PI;  // ComplexPlane (von x-Achse nach links) - GeoMeteo (von y-Achse nach rechts)
    dir = 90-alpha;                  // z=x+iy= -> i*conj(z)=y+ix=re(i(pi/2-t)), Achsen und Drehsinn vertauscht
    if (dir < 0) dir += 360;         // atan2(y,x)=atan(y/x)=pi/2-atan(x/y) , atan(1/t) = pi/2 - atan(t)
    gpx.vD2 = dir;
//*/
    dir = atan2(vx, vy) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx.vD = dir;

    for (i = 0; i < 2; i++) {
        byte = frame[pos_GPSvV + i];
        if (byte > 0xFF) return -1;
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] | gpsVel_bytes[1] << 8;
    gpx.vV = vel16 / ms2kn100;

    return 0;
}

int get_RecordNo() {
    int i;
    unsigned byte;
    ui8_t frnr_bytes[2];
    int frnr;

    for (i = 0; i < 2; i++) {
        byte = frame[pos_RecordNo + i];
        frnr_bytes[i] = byte;
    }

    frnr = frnr_bytes[0] + (frnr_bytes[1] << 8);
    gpx.frnr = frnr;

    return 0;
}

int get_SondeID() {
    int i;
    unsigned byte;
    ui8_t sondeid_bytes[8]; // 5 bis 6 ascii + '\0'
    int IDlen = 6+1; // < 9
    int err = 0;

    for (i = 0; i < IDlen; i++) {
        byte = frame[pos_SondeID1 + i];
        if (byte == 0) IDlen = i+1;
        else
        if (byte < 0x20 || byte > 0x7E) err |= 0x1;
        sondeid_bytes[i] = byte;
    }
    for (i = 0; i < IDlen; i++) {
        gpx.id1[i] = sondeid_bytes[i];
    }

    IDlen = 6+1;
    for (i = 0; i < IDlen; i++) {
        byte = frame[pos_SondeID2 + i];
        if (byte == 0) IDlen = i+1;
        else
        if (byte < 0x20 || byte > 0x7E) err |= 0x2;
        sondeid_bytes[i] = byte;
    }
    for (i = 0; i < IDlen; i++) {
        gpx.id2[i] = sondeid_bytes[i];
    }

    return err;
}

/* -------------------------------------------------------------------------- */

// Frame: <DLE><id><data_bytes><DLE><ETX>,
//        <DLE>=0x10, <ETX>=0x03; <id>=0xb9
// (.. 69) 10 b9 01 .. .. 10 03 cs (96 ..)
// 8bit-xor-checksum:
//  xsumDLE(frame+pos_Start, pos_xcSum-pos_Start)
int xsumDLE(ui8_t bytes[], int len) {
    int i, xsum = 0;
    for (i = 0; i < len; i++) {  // TSIP-Protokoll: <DLE>=0x10
        // innnerhalb <DLE>, 0x10 doppelt, und 0x10^0x10=0x00
        if (bytes[i] != 0x10) xsum ^= bytes[i];
        // ausser <DLE> zu Beginn/Ende
    }
    return xsum & 0xFF;
}

/* -------------------------------------------------------------------------- */


int bits2byte(char *bits) {
    int i, d = 1, byte = 0;

    for (i = 0; i < 8; i++) {
        if      (bits[i] == 1)  byte += d;
        else if (bits[i] == 0)  byte += 0;
        d <<= 1;
    }
    return byte & 0xFF;
}


#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

void print_frame(int len) {
    FILE *fpo;
    int i;
    int err1, err2, err3;

    if (option_raw) {
        for (i = 0; i < len; i++) {
            if (option_color) {
                if (i >= pos_GPSTOW  &&  i < pos_GPSalt+4) fprintf(stdout, ANSI_COLOR_CYAN);
                else fprintf(stdout, ANSI_COLOR_RESET);
            }
            fprintf(stdout, "%02x ", frame[i]);
        }
        if (option_verbose) {                                                          // pos_xcSum-pos_Start=189
             fprintf(stdout, " [%02X # %02X]", frame[pos_xcSum], xsumDLE(frame+pos_Start, pos_xcSum-pos_Start));
        }
        fprintf(stdout, "\n");
    }
    else {
        fpo = stdout;
        get_RecordNo();
        err1 = get_SondeID();
        err2 = get_GPStow();
        err3 = get_GPSpos();

        if ( !err1 || !err2 || !err3 ) {
            fprintf(fpo, "[%5d] ", gpx.frnr);
            if ( err1!=0x3 ) {
                fprintf(fpo, "(%s)  ", err1&0x1?gpx.id2:gpx.id1);
            }
            if ( !err2 ) {
                fprintf(fpo, "%s ",weekday[gpx.wday]);
                fprintf(fpo, "%02d:%02d:%02d", gpx.std, gpx.min, gpx.sek);
                if (option_gps == 2) fprintf(fpo, ".%03d", gpx.ms);
                fprintf(fpo, " ");
            }
            if ( !err3 ) {
                fprintf(fpo, " lat: %.6f ", gpx.lat);
                fprintf(fpo, " lon: %.6f ", gpx.lon);
                fprintf(fpo, " alt: %.2f ", gpx.alt);
                if (option_verbose) {
                    err3 = get_GPSvel();
                    if (!err3) {
                        if (option_verbose == 2) fprintf(fpo, "  (%.1f , %.1f : %.1f°) ", gpx.vx, gpx.vy, gpx.vD2);
                        fprintf(fpo, "  vH: %.1f  D: %.1f°  vV: %.1f ", gpx.vH, gpx.vD, gpx.vV);
                    }
                }
            }
            fprintf(fpo, "\n");
        }
    }
}


int demod_zeroX(FILE *fp) {

    int bitl1 = 0,
        bitl2 = 0,
        bitl4 = 0,
        bytepos = 0,
        bitpos = 0,
        head = 0,
        inout = 0,
        byteval = 0;

    int  i, len;
    char bitbuf[8];


    while (!read_afsk_bits(fp, &len)) {

        if (len == 0) continue;

        if (len == 1) {
            bitl1++;
            if (bitl1 < 2) continue;
        }
        if (len == 2) bitl2++;
        if (len == 4) {
            bitl4++;
            inout = 1;
            bitl1 = 0;
            bitl2 = 0;
        }

        if (len == 3) {
            if (bitl1 == 1 && bitpos < 7) {
                bitl1 = 0; bitbuf[bitpos++] = 1;
                bitl2++;
                len = 2;
            }
        }

        if (len > 0 && len < 3) {
            bitl4 = 0;
            inout = 0;
            if (head > 0) {
                head = 0;
                if (bytepos > pos_GPSalt+4) print_frame(FRAMELEN);
                bitpos = 0;
                bytepos = 0;
                for (i=0; i<FRAMELEN; i++) frame[i] = 0;
            }
        }

        if (bitl1 == 2) { bitl1 = 0; bitbuf[bitpos++] = 1; }
        if (bitl2 == 1) { bitl2 = 0; bitbuf[bitpos++] = 0; }

        if (bitpos > 7 || inout) {
            if (bitpos > 2) {
                if (bytepos < FRAMELEN) {
                    byteval = bits2byte(bitbuf);
                    if (byteval == 0x10  &&  frame[bytepos-1] == 0x10) frame[bytepos-1] = 0x10;
                    else {                                          // woher die doppelte 0x10?
                        frame[bytepos] = byteval & 0xFF;            // koennte vom TSIP-Protokoll kommen:
                        bytepos++;                                  // <DLE><id><data_bytes><DLE><ETX>,
                    }                                               // wobei <DLE>=0x10, <ETX>=0x03.
                }                                                   // wenn 0x10 in data, dann doppelt.
            }
            bitpos = 0;
        }

        if (bitl4 > 2) { head = 1; }

    }

    return 0;
}


/*
 * noncoherent demod/correlation
 *

N = sample_rate/2400

f0 = 1/N
f1 = 2/N = 2*f0

unbekannte Phase phi des Signals A_k * cos(2*PI*f_k * t + phi)
correlator exp(i * 2*PI*f_j * t)

sum_{t=0}^{N-1} A_k * cos(2*PI*f_k * t + phi) * exp(i * 2*PI*f_j * t)
= A_k*N/2 * exp(-i*phi) , falls j=k
(sonst 0, wenn f_j-f_k=m/N, m=+-1,+-2,...)

insbesondere bei WFM sind Amplituden A_f0, A_f1 unterschiedlich
-> gainBit0, gainBit1, gainBit_ anpassen

*/

// 1200 Hz
#define   COSf_(i)  cosf[   i % (2*N)]
#define   SINf_(i)  cosf[(  i +2*N-N/2) % (2*N)]

// 2400 Hz
#define   COSf0(i)  cosf[(2*i) % (2*N)]
#define   SINf0(i)  cosf[(2*i + 2*N-N/2) %( 2*N)]

// 4800 Hz
#define   COSf1(i)  cosf[(4*i) % (2*N)]
#define   SINf1(i)  cosf[(4*i + 2*N-N/2) % (2*N)]

#define   CH_OUT  5

int demod_cpafsk(FILE *fp, double gainOut) {

    FILE *fout = NULL;
    int  i, N;
    unsigned int sample, frame_sync, sync, framesample, bitsample;
    unsigned char chIn = 0, chOut = 0;
    double out[CH_OUT];
    double dc_ofs = 0.0; //0.001;

    double s, si,
           *bufs = NULL,
           *buf0 = NULL,
           *buf1 = NULL;
    char sbit, *bufsbit = NULL;

    double *cosf = NULL;

    double gainBit0 = 0.5625, // 0.65,
           gainBit1 = 1.0625, // 0.80,
           gainBit_ = 1.00;

    double sum1, sum2,
           bit0, bit1, bit_;
    double delay_s, delay_0, delay_1, delay__;

    char bitbuf[8];
    int bytepos = 0,
        bitpos = 0,
        byteval = 0,
        mbit = 0;

        bytepos = FRAMELEN+1;
        frame_sync = 0;
        sync = 0;


    fout = NULL;
#ifdef MULTI
    fout = stdout;
#endif

    if (sample_rate % 48000) {
        fprintf(stderr, "wav: sample_rate not 48k or 96k\n");
        return -1;
    }

    N = sample_rate / BAUD_RATE;  // 2400 Hz;

    chIn  = channels;
    chOut = CH_OUT;

    if (fout) {
        i = read_wavheader(fp, chIn, chOut, fout);
        if (i != 0) {
            fprintf(stderr, "error: wav header\n");
            return -1;
        }
    }


    bufsbit = (char *)calloc( N+1, sizeof(char)); if (bufsbit == NULL) return -1;

    bufs = (double *)calloc( 2*(N+1), sizeof(double)); if (bufs == NULL) return -1;
    buf0 = (double *)calloc( 2*(N+1), sizeof(double)); if (buf0 == NULL) return -1;
    buf1 = (double *)calloc( 2*(N+1), sizeof(double)); if (buf1 == NULL) return -1;

    cosf = (double *)calloc( 2*(N+1), sizeof(double)); if (cosf == NULL) return -1;

    for (i = 0; i < 2*N; i++) {
        cosf[i] = cos(M_PI*i/N);
    }


    sample = 0;

    while (f32read_sample(fp, &s) != EOF) {

        s += dc_ofs;

        bufs[sample % (2*N)] = s;

        sum1 = 0;
        sum2 = 0;
        for (i = 0; i < N; i++) {
            si = bufs[(sample+2*N-i) % (2*N)];
            sum1 += si*COSf0(i);
            sum2 += si*SINf0(i);
        }
        bit0 = 4*(sum1*sum1 + sum2*sum2)/(double)(N*N); // A_0*A_0 (betont Flanken)
        bit0 *= gainBit0;

        sum1 = 0;
        sum2 = 0;
        for (i = 0; i < N; i++) {
            si = bufs[(sample+2*N-i) % (2*N)];
            sum1 += si*COSf1(i);
            sum2 += si*SINf1(i);
        }
        bit1 = 4*(sum1*sum1 + sum2*sum2)/(double)(N*N); // A_1*A_1 (betont Flanken)
        bit1 *= gainBit1;

        sum1 = 0;
        sum2 = 0;
        for (i = 0; i < 2*N; i++) {
            si = bufs[(sample+2*N-i) % (2*N)];
            sum1 += si*COSf_(i);
            sum2 += si*SINf_(i);
        }
        bit_ = (sum1*sum1 + sum2*sum2)/(double)(N*N); // A__*A__ (betont Flanken)
        bit_ *= gainBit_;


        buf0[sample % (2*N)] = bit0;
        buf1[sample % (2*N)] = bit1;

        delay_s = bufs[(sample+N) % (2*N)];        // sample - N
        delay_1 = buf1[(sample+2*N-N/2) % (2*N)];  // sample - N/2
        delay_0 = buf0[(sample+2*N-N/2) % (2*N)];  // sample - N/2
        delay__ = bit_;                            // sample


        sbit = (delay_1 > delay_0) ? 1 : -1;
        if (bit_ > delay_1 && bit_ > delay_0) sbit = 0;

        bufsbit[sample % N] = sbit;


        if (fout) {
            out[0] = delay_s;
            out[1] = delay_1 * gainOut;
            out[2] = delay_0 * gainOut;
            out[3] = delay__ * gainOut;
            out[4] = sbit * 0.4;
            f32write_mults(fout, out, chOut);
        }
        else {

            if (sbit != 0 && sync > 4*N) { // TODO: accurate frame-sync
                frame_sync = 1;
            }
            if (sbit == 0) sync += 1;
            else           sync  = 0;

            if (frame_sync) {
                bytepos = 0;
                bitpos = 0;
                framesample = 0;
                bitsample = 0;
                frame_sync = 0;
            }

            if (bytepos < FRAMELEN) {

                bitsample = framesample % N;

                if (bitsample == N-1) {
                    if (bitpos < 8) {
                        mbit = 0;
                        for (i = -N/4; i < N/4; i++) {
                            mbit += bufsbit[(sample + N/2 + i) % N];
                        }
                        bitbuf[bitpos] =  (mbit > 0) ? 1 : 0;
                        bitpos++;
                    }

                    if (bitpos == 8) {
                        byteval = bits2byte(bitbuf);
                        if (byteval == 0x10  &&  frame[bytepos-1] == 0x10) frame[bytepos-1] = 0x10;
                        else {                                          // woher die doppelte 0x10?
                            frame[bytepos] = byteval & 0xFF;            // koennte vom TSIP-Protokoll kommen:
                            bytepos++;                                  // <DLE><id><data_bytes><DLE><ETX>,
                        }                                               // wobei <DLE>=0x10, <ETX>=0x03.
                        bitpos++;                                       // wenn 0x10 in data, dann doppelt.
                    }
                    //
                    // TODO: optional byte-sync
                }

                framesample++;
                if (framesample % (10*N) == 0) bitpos = 0;
            }

            if (bytepos == FRAMELEN) {
                print_frame(FRAMELEN);
                bitpos = 0;
                bytepos++;
                for (i=0; i<FRAMELEN; i++) frame[i] = 0;
            }

        }

        sample++;
    }


    if (cosf) { free(cosf); cosf = NULL; }
    if (bufs) { free(bufs); bufs = NULL; }
    if (buf0) { free(buf0); buf0 = NULL; }
    if (buf1) { free(buf1); buf1 = NULL; }
    if (bufsbit) { free(bufsbit); bufsbit = NULL; }

    return 0;
}


int main(int argc, char **argv) {

    FILE *fp = NULL;
    char *fpname = NULL;
    double gainOut = 1.0;

    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -i, --invert\n");
            fprintf(stderr, "       -b\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       -v, --verbose\n");
            fprintf(stderr, "       -1  (v1: TOW/s)\n");
            fprintf(stderr, "       -2  (v2: TOW/ms)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if   (strcmp(*argv, "-vv") == 0) { option_verbose = 2; }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-c") == 0) || (strcmp(*argv, "--color") == 0) ) {
            option_color = 1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
        }
        else if ( (strcmp(*argv, "-1") == 0) ) { option_gps = 1; }  // Trimble
        else if ( (strcmp(*argv, "-2") == 0) ) { option_gps = 2; }  // ublox
        else if   (strcmp(*argv, "-b") == 0) { option_b = 1; }
        else if ( (strcmp(*argv, "-g") == 0) ) {
            ++argv;
            if (*argv) {
                gainOut = atof(*argv);
                if (gainOut <= 0)  gainOut = 1.0;
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

    if ( read_wav_header(fp) != 0 ) {
        fclose(fp);
        fprintf(stderr, "error: wav header\n");
        return -1;
    }

#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _setmode(_fileno(stdin), _O_BINARY);
#endif
    setbuf(stdout, NULL);


    if (!option_b) {
        demod_zeroX(fp);
    }
    else {
        demod_cpafsk(fp, gainOut);
    }


    fclose(fp);

    return 0;
}

