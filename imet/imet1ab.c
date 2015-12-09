
/*
 * radiosonde iMet-1-AB (Trimble GPS)
 * author: zilog80
 * usage:
 *     gcc imet.c -o imet
 *     ./imet [options] audio.wav
 *       options:
 *               -r, --raw
 *               -i, --invert
 */

#include <stdio.h>
#include <string.h>

#ifdef CYGWIN
  #include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif


typedef unsigned char ui8_t;

typedef struct {
    int frnr;
    char id1[9]; char id2[9];
    int week; int gpssec;
    //int jahr; int monat; int tag;
    int wday;
    int std; int min; int sek;
    double lat; double lon; double h;
} gpx_t;

gpx_t gpx;

int option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 0,      // rohe Frames
    option_color = 0,    // Farbe
    option_inv = 0,      // invertiert Signal
    wavloaded = 0;

/* -------------------------------------------------------------------------- */

#define BAUD_RATE  2400
// iMet: AFSK Baudrate 2400
/* 1200 Hz: out-in
   2400 Hz: lang  ->  Baudrate
   4800 Hz: kurz (2x)
*/

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


int par=1, par_alt=1;
unsigned long sample_count = 0;

int read_afsk_bits(FILE *fp, int *len) {
    int n, sample;
    float l;
    int start;

    start = 0;
    n = 0;
    do{ // High
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        if (option_inv) sample = -sample;
        sample_count++;
        if (sample < 0 && !start) continue;
        start = 1;
        par_alt = par;
        par =  (sample >= 0) ? 1 : -1;
        n++;
    } while (par*par_alt > 0);

    do{ // Low
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        if (option_inv) sample = -sample;
        sample_count++;
        par_alt = par;
        par =  (sample >= 0) ? 1 : -1;
        n++;
    } while (par*par_alt > 0);

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

#define pos_RecordNo  0x08  // 2 byte
#define pos_SondeID1  0x12  // 5 byte
#define pos_SondeID2  0x2C  // 5 byte

#define pos_GPSTOW  0x8A  // 4 byte
#define pos_GPSlat  0x8E  // 4 byte
#define pos_GPSlon  0x92  // 4 byte
#define pos_GPSalt  0x96  // 4 byte

#define FRAMELEN 204
ui8_t frame[FRAMELEN+6];

double B60B60 = 0xB60B60;  // 2^32/360 = 0xB60B60.xxx

char weekday[7][3] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};

int gpsTOW(int gpstime) {
    int day;

    gpx.gpssec = gpstime;
    if (gpx.gpssec<0 || gpx.gpssec>7*24*60*60) return -1;  // 1 Woche = 604800 sek

    day = gpstime / (24 * 3600);
    gpstime %= (24*3600);

    if ((day < 0) || (day > 6)) {
        //gpx.wday = 0;
        return -1;
    }
    gpx.wday = day;
    gpx.std = gpstime / 3600;
    gpx.min = (gpstime % 3600) / 60;
    gpx.sek = gpstime % 60;

    return 0;
}

int gpsLat(int lat) {
    gpx.lat = lat / B60B60;
    if (gpx.lat < -90  ||  gpx.lat > 90) return -1;
    return 0;
}

int gpsLon(int lon) {
    gpx.lon = lon / B60B60;
    return 0;
}

int gpsAlt(int alt) {
    gpx.h = alt / 1000.0;
    if (gpx.h < -200  ||  gpx.h > 50000) return -1;
    return 0;
}

int get_GPS() {
    int i, tow, lat, lon, alt;
    int err = 0;

    tow = lat = lon = alt = 0;
    for (i = 0; i < 4; i++) {
        tow |= frame[pos_GPSTOW+i] << (8*i);
        lat |= frame[pos_GPSlat+i] << (8*i);
        lon |= frame[pos_GPSlon+i] << (8*i);
        alt |= frame[pos_GPSalt+i] << (8*i);
    }
    err = 0;
    err |= gpsTOW(tow);
    err |= gpsLat(lat);
    err |= gpsLon(lon);
    err |= gpsAlt(alt);

    return err;
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

void print_gps(FILE *fp) {
    fprintf(fp, "%s ",weekday[gpx.wday]);
    fprintf(fp, "%02d:%02d:%02d ", gpx.std, gpx.min, gpx.sek);
    fprintf(fp, " lat: %.6f ", gpx.lat);
    fprintf(fp, " lon: %.6f ", gpx.lon);
    fprintf(fp, " h: %.2f ", gpx.h);
}


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
        fprintf(stdout, "\n");
    }
    else {
        err1 = err2 = err3 = 0;
        err1 |= get_RecordNo();
        err2 |= get_SondeID();
        err3 |= get_GPS();
        if ( !err3 || err2!=0x3 ) {
            fprintf(stdout, "[%5d] ", gpx.frnr);
            if (err2!=0x3) fprintf(stdout, "(%s)  ", err2&0x1?gpx.id2:gpx.id1);
            if (!err3) print_gps(stdout);
            fprintf(stdout, "\n");
        }
    }
}

int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;
    int  i, len;
    char bitbuf[8];

int bitl1 = 0,
    bitl2 = 0,
    bitl4 = 0,
    bytepos = 0,
    bitpos = 0,
    head = 0,
    inout = 0,
    byteval = 0;

#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _setmode(_fileno(stdin), _O_BINARY);
    setbuf(stdout, NULL);
#endif

    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       -i, --invert\n");
            return 0;
        }
/*
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if   (strcmp(*argv, "-vv") == 0) { option_verbose = 2; }
*/
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-c") == 0) || (strcmp(*argv, "--color") == 0) ) {
            option_color = 1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
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


    bitl1 = 0;
    bitl2 = 0;
    bitl4 = 0;
    bytepos = 0;
    bitpos = 0;
    head = 0;

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
    printf("\n");


    fclose(fp);

    return 0;
}

