
/*
 *  iMet-1-RS / iMet-4
 *  Bell202 8N1
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
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


typedef  unsigned char  ui8_t;

int option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 0,      // rohe Frames
    option_rawbits = 0,
    option_b = 1,
    option_json = 0,
    wavloaded = 0;

// Bell202, 1200 baud (1200Hz/2200Hz), 8N1
#define BAUD_RATE 1200


typedef struct {
    // GPS
    int hour;
    int min;
    int sec;
    float lat;
    float lon;
    int alt;
    int sats;
    // PTU
    int frame;
    float temp;
    float pressure;
    float humidity;
    float batt;
    //
    int gps_valid;
    int ptu_valid;
    //
    int jsn_freq;   // freq/kHz (SDR)
} gpx_t;

gpx_t gpx;

/* ------------------------------------------------------------------------------------ */

int sample_rate = 0, bits_sample = 0, channels = 0;
//float samples_per_bit = 0;

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

    if ((bits_sample != 8) && (bits_sample != 16) && (bits_sample != 32)) return -1;

    //samples_per_bit = sample_rate/(float)BAUD_RATE;
    //fprintf(stderr, "samples/bit: %.2f\n", samples_per_bit);

    return 0;
}

int f32read_sample(FILE *fp, float *s) {
    int i;
    unsigned int word = 0;
    short *b = (short*)&word;
    float *f = (float*)&word;

    for (i = 0; i < channels; i++) {

        if (fread( &word, bits_sample/8, 1, fp) != 1) return EOF;

        if (i == 0) {  // i = 0: links bzw. mono
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

    return 0;
}

/* ------------------------------------------------------------------------------------ */


#define BITS (10)
#define LEN_BITFRAME  BAUD_RATE
#define LEN_BYTEFRAME  (LEN_BITFRAME/BITS)
#define HEADLEN 30

char header[] = "1111111111111111111""10""10000000""1";
char buf[HEADLEN+1] = "x";
int bufpos = -1;

int    bitpos;
ui8_t  bitframe[LEN_BITFRAME+1] = { 0, 1, 0, 0, 0, 0, 0, 0, 0, 1};
ui8_t  byteframe[LEN_BYTEFRAME+1];

int    N, ptr;
float *buffer = NULL;


/* ------------------------------------------------------------------------------------ */


void inc_bufpos() {
  bufpos = (bufpos+1) % HEADLEN;
}

int compare() {
    int i=0, j = bufpos;

    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != header[HEADLEN-1-i]) break;
        j--;
        i++;
    }
    return i;
}

int bits2byte(ui8_t *bits) {
    int i, d = 1, byte = 0;

    if ( bits[0]+bits[1]+bits[2]+bits[3]+bits[4] // 1 11111111 1 (sync)
        +bits[5]+bits[6]+bits[7]+bits[8]+bits[9] == 10 ) return 0xFFFF;

    for (i = 1; i < BITS-1; i++) {  // little endian
        if      (bits[i] == 1)  byte += d;
        else if (bits[i] == 0)  byte += 0;
        d <<= 1;
    }
    return byte & 0xFF;
}


int bits2bytes(ui8_t *bits, ui8_t *bytes, int len) {
    int i;
    int byte;
    for (i = 0; i < len; i++) {
        byte = bits2byte(bits+BITS*i);
        bytes[i] = byte & 0xFF;
        if (byte == 0xFFFF) break;
    }
    return i;
}

void print_rawbits(int len) {
    int i;
    for (i = 0; i < len; i++) {
        if ((i % BITS == 1) || (i % BITS == BITS-1)) fprintf(stdout, " ");
        fprintf(stdout, "%d", bitframe[i]);
    }
    fprintf(stdout, "\n");
}


/* -------------------------------------------------------------------------- */

int crc16poly = 0x1021; // CRC16-CCITT
int crc16(ui8_t bytes[], int len) {
    int rem = 0x1D0F;   // initial value
    int i, j;
    for (i = 0; i < len; i++) {
        rem = rem ^ (bytes[i] << 8);
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

/* -------------------------------------------------------------------------- */

#define LEN_GPSePTU   (18+20)


/*
GPS Data Packet
offset bytes description
 0     1     SOH = 0x01
 1     1     PKT_ID = 0x02
 2     4     Latitude, +/- deg (float)
 6     4     Longitude, +/- deg (float)
10     2     Altitude, meters (Alt = n-5000)
12     1     nSat (0 - 12)
13     3     Time (hr,min,sec)
16     2     CRC (16-bit)
packet size = 18 bytes
*/
#define pos_GPSlat  0x02  // 4 byte float
#define pos_GPSlon  0x06  // 4 byte float
#define pos_GPSalt  0x0A  // 2 byte int
#define pos_GPSsats 0x0C  // 1 byte
#define pos_GPStim  0x0D  // 3 byte
#define pos_GPScrc  0x10  // 2 byte

int print_GPS(int pos) {
    float lat, lon;
    int alt, sats;
    int std, min, sek;
    int crc_val, crc;

    crc_val = ((byteframe+pos)[pos_GPScrc] << 8) | (byteframe+pos)[pos_GPScrc+1];
    crc = crc16(byteframe+pos, pos_GPScrc); // len=pos

    //lat = *(float*)(byteframe+pos+pos_GPSlat);
    //lon = *(float*)(byteframe+pos+pos_GPSlon);
    // //raspi: copy into (aligned) float
    memcpy(&lat, byteframe+pos+pos_GPSlat, 4);
    memcpy(&lon, byteframe+pos+pos_GPSlon, 4);

    alt = ((byteframe+pos)[pos_GPSalt+1]<<8)+(byteframe+pos)[pos_GPSalt] - 5000;
    sats = (byteframe+pos)[pos_GPSsats];
    std = (byteframe+pos)[pos_GPStim+0];
    min = (byteframe+pos)[pos_GPStim+1];
    sek = (byteframe+pos)[pos_GPStim+2];

    fprintf(stdout, "(%02d:%02d:%02d) ", std, min, sek);
    fprintf(stdout, " lat: %.6f° ", lat);
    fprintf(stdout, " lon: %.6f° ", lon);
    fprintf(stdout, " alt: %dm ", alt);
    fprintf(stdout, " sats: %d ", sats);

    fprintf(stdout, " # ");
    fprintf(stdout, " CRC: %04X ", crc_val);
    fprintf(stdout, "- %04X ", crc);
    if (crc_val == crc) {
        fprintf(stdout, "[OK]");
        gpx.gps_valid = 1;
        gpx.lat = lat;
        gpx.lon = lon;
        gpx.alt = alt;
        gpx.sats = sats;
        gpx.hour = std;
        gpx.min = min;
        gpx.sec = sek;
    }
    else {
        fprintf(stdout, "[NO]");
        gpx.gps_valid = 0;
    }

    return (crc_val != crc);
}


/*
PTU (enhanced) Data Packet
offset bytes description
 0     1     SOH = 0x01
 1     1     PKT_ID = 0x04
 2     2     PKT = packet number
 4     3     P, mbs (P = n/100)
 7     2     T, °C (T = n/100)
 9     2     U, % (U = n/100)
11     1     Vbat, V (V = n/10)
12     2     Tint, °C (Tint = n/100)
14     2     Tpr, °C (Tpr = n/100)
16     2     Tu, °C (Tu = n/100)
18     2     CRC (16-bit)
packet size = 20 bytes
*/
#define pos_PCKnum  0x02  // 2 byte
#define pos_PTUprs  0x04  // 3 byte
#define pos_PTUtem  0x07  // 2 byte int
#define pos_PTUhum  0x09  // 2 byte
#define pos_PTUbat  0x0B  // 1 byte
#define pos_PTUcrc  0x12  // 2 byte

int print_ePTU(int pos) {
    int P, U;
    short T;
    int bat, pcknum;
    int crc_val, crc;

    crc_val = ((byteframe+pos)[pos_PTUcrc] << 8) | (byteframe+pos)[pos_PTUcrc+1];
    crc = crc16(byteframe+pos, pos_PTUcrc); // len=pos

    P   = (byteframe+pos)[pos_PTUprs] | ((byteframe+pos)[pos_PTUprs+1]<<8) | ((byteframe+pos)[pos_PTUprs+2]<<16);
    T   = (byteframe+pos)[pos_PTUtem] | ((byteframe+pos)[pos_PTUtem+1]<<8);
    U   = (byteframe+pos)[pos_PTUhum] | ((byteframe+pos)[pos_PTUhum+1]<<8);
    bat = (byteframe+pos)[pos_PTUbat];

    pcknum = (byteframe+pos)[pos_PCKnum] | ((byteframe+pos)[pos_PCKnum+1]<<8);
    fprintf(stdout, "[%d] ", pcknum);

    fprintf(stdout, " P:%.2fmb ", P/100.0);
    fprintf(stdout, " T:%.2f°C ", T/100.0);
    fprintf(stdout, " U:%.2f%% ", U/100.0);
    fprintf(stdout, " bat:%.1fV ", bat/10.0);

    fprintf(stdout, " # ");
    fprintf(stdout, " CRC: %04X ", crc_val);
    fprintf(stdout, "- %04X ", crc);
    if (crc_val == crc) {
        fprintf(stdout, "[OK]");
        gpx.ptu_valid = 1;
        gpx.frame = pcknum;
        gpx.pressure = P/100.0;
        gpx.temp = T/100.0;
        gpx.humidity = U/100.0;
        gpx.batt = bat/10.0;
    }
    else {
        fprintf(stdout, "[NO]");
        gpx.ptu_valid = 0;
    }

    return (crc_val != crc);
}

/* -------------------------------------------------------------------------- */

int print_frame(int len) {
    int i;
    int framelen;
    int crc_err1 = 0,
        crc_err2 = 0;
    int out = 0;

    if ( len < 2 || len > LEN_BYTEFRAME) return -1;
    for (i = len; i < LEN_BYTEFRAME; i++) byteframe[i] = 0;

    gpx.gps_valid = 0;
    gpx.ptu_valid = 0;

    if (option_rawbits)
    {
        print_rawbits((LEN_GPSePTU+2)*BITS);
    }
    else
    {
        framelen = bits2bytes(bitframe, byteframe, len);

        if (option_raw) {
            for (i = 0; i < framelen; i++) { // LEN_GPSePTU
                fprintf(stdout, "%02X ", byteframe[i]);
            }
            fprintf(stdout, "\n");
            out |= 4;
        }
        //else
        {
            if ((byteframe[0] == 0x01) && (byteframe[1] == 0x02)) { // GPS Data Packet
                crc_err1 = print_GPS(0x00);  // packet offset in byteframe
                fprintf(stdout, "\n");
                out |= 1;
            }
            if ((byteframe[pos_GPScrc+2+0] == 0x01) && (byteframe[pos_GPScrc+2+1] == 0x04)) { // PTU Data Packet
                crc_err2 = print_ePTU(pos_GPScrc+2);  // packet offset in byteframe
                fprintf(stdout, "\n");
                out |= 2;
            }
/*
            if ((byteframe[0] == 0x01) && (byteframe[1] == 0x04)) { // PTU Data Packet
                print_ePTU(0x00);  // packet offset in byteframe
                fprintf(stdout, "\n");
            }
*/
//          // if (crc_err1==0 && crc_err2==0) { }

            if (option_json) {
                if (gpx.gps_valid && gpx.ptu_valid) // frameNb part of PTU-pck
                {
                    char *ver_jsn = NULL;
                    fprintf(stdout, "{ \"type\": \"%s\"", "IMET");
                    fprintf(stdout, ", \"frame\": %d, \"id\": \"iMet\", \"datetime\": \"%02d:%02d:%02dZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %d, \"sats\": %d, \"temp\": %.2f, \"humidity\": %.2f, \"pressure\": %.2f, \"batt\": %.1f",
                            gpx.frame, gpx.hour, gpx.min, gpx.sec, gpx.lat, gpx.lon, gpx.alt, gpx.sats, gpx.temp, gpx.humidity, gpx.pressure, gpx.batt);
                    if (gpx.jsn_freq > 0) {
                        fprintf(stdout, ", \"freq\": %d", gpx.jsn_freq);
                    }
                    #ifdef VER_JSN_STR
                        ver_jsn = VER_JSN_STR;
                    #endif
                    if (ver_jsn && *ver_jsn != '\0') fprintf(stdout, ", \"version\": \"%s\"", ver_jsn);
                    fprintf(stdout, " }\n");
                }
            }

            if (out) fprintf(stdout, "\n");
            fflush(stdout);
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------- */

double complex F1sum = 0;
double complex F2sum = 0;

int main(int argc, char *argv[]) {

    FILE *fp;
    char *fpname;
    unsigned int sample_count;
    int i;
    int bit = 8, bit0 = 8;
    int pos = 0, pos0 = 0;
    double pos_bit = 0;
    int header_found = 0;
    double bitlen; // sample_rate/BAUD_RATE
    int len;
    double f1, f2;

    int n;
    double t  = 0.0;
    double tn = 0.0;
    double x  = 0.0;
    double x0 = 0.0;

    double complex X0 = 0;
    double complex X  = 0;

    double xbit = 0.0;
    float s = 0.0;

    int bitbuf[3];

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
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "--rawbits") == 0) ) {
            option_rawbits = 1;
        }
        else if ( (strcmp(*argv, "-b") == 0) ) {
            option_b = 1;
        }
        else if ( (strcmp(*argv, "--json") == 0) ) {
            option_json = 1;
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


    bitlen = sample_rate/(double)BAUD_RATE;

    f1 = 2200.0;  // bit0: 2200Hz
    f2 = 1200.0;  // bit1: 1200Hz

    N = 2*bitlen + 0.5;
    buffer = calloc( N+1, sizeof(float)); if (buffer == NULL) return -1;

    ptr = -1; sample_count = -1;

    while (f32read_sample(fp, &s) != EOF) {

        ptr++; sample_count++;
        if (ptr == N) ptr = 0;
        buffer[ptr] = s;

        n = bitlen;
        t = sample_count / (double)sample_rate;
        tn = (sample_count-n) / (double)sample_rate;

        x = buffer[sample_count % N];
        x0 = buffer[(sample_count - n + N) % N];

        // f1
        X0 = x0 * cexp(-tn*2*M_PI*f1*I); // alt
        X  = x  * cexp(-t *2*M_PI*f1*I); // neu
        F1sum +=  X - X0;

        // f2
        X0 = x0 * cexp(-tn*2*M_PI*f2*I); // alt
        X  = x  * cexp(-t *2*M_PI*f2*I); // neu
        F2sum +=  X - X0;

        xbit = cabs(F2sum) - cabs(F1sum);

        s = xbit / bitlen;


        if ( s < 0 ) bit = 0;  // 2200Hz
        else         bit = 1;  // 1200Hz

        bitbuf[sample_count % 3] = bit;

        if (header_found && option_b)
        {
            if (sample_count - pos_bit > bitlen+bitlen/5 + 3)
            {
                int bitsum = bitbuf[0]+bitbuf[1]+bitbuf[2];
                if (bitsum > 1.5) bit = 1; else bit = 0;

                bitframe[bitpos] = bit;
                bitpos++;
                if (bitpos >= LEN_BITFRAME-200) {  // LEN_GPSePTU*BITS+40

                    print_frame(bitpos/BITS);

                    bitpos = 0;
                    header_found = 0;
                }
                pos_bit += bitlen;
            }
        }
        else
        {
            if (bit != bit0) {

                pos0 = pos;
                pos = sample_count;  //sample_count-(N-1)/2

                len =  (pos-pos0)/bitlen + 0.5;
                for (i = 0; i < len; i++) {
                    inc_bufpos();
                    buf[bufpos] = 0x30 + bit0;

                    if (!header_found) {
                        if (compare() >= HEADLEN) {
                            header_found = 1;
                            bitpos = 10;
                            pos_bit = pos;
                            if (option_b) {
                                bitframe[bitpos] = bit;
                                bitpos++;
                            }
                        }
                    }
                    else {
                        bitframe[bitpos] = bit0;
                        bitpos++;
                        if (bitpos >= LEN_BITFRAME-200) {  // LEN_GPSePTU*BITS+40

                            print_frame(bitpos/BITS);

                            bitpos = 0;
                            header_found = 0;
                        }
                    }
                }
                bit0 = bit;
            }
        }
    }
    fprintf(stdout, "\n");

    if (buffer) { free(buffer); buffer = NULL; }

    fclose(fp);

    return 0;
}

