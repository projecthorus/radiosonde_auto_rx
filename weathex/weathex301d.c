
/*
    Weathex WxR-301D (64kHz wide)
    UAII2022 Lindenberg: w/ PN9, 5000 baud
    Malaysia: w/o PN9, 4800 baud
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// optional JSON "version"
//  (a) set global
//      gcc -DVERSION_JSN [-I<inc_dir>] ...
#ifdef VERSION_JSN
  #include "version_jsn.h"
#endif
// or
//  (b) set local compiler option, e.g.
//      gcc -DVER_JSN_STR=\"0.0.2\" ...


typedef unsigned char ui8_t;
typedef short i16_t;
typedef unsigned int ui32_t;


int option_verbose = 0,
    option_raw = 0,
    option_inv = 0,
    option_b = 0,
    option_json = 0,
    option_timestamp = 0,
    option_softin = 0,
    wavloaded = 0;
int wav_channel = 0;     // audio channel: left

int option_pn9 = 0;

#define BAUD_RATE      4800.0
#define BAUD_RATE_PN9  5000.0 //(4997.2) // 5000

#define FRAMELEN    69 //64
#define BITFRAMELEN (8*FRAMELEN)

#define HEADLEN 40
#define HEADOFS 0
char header_pn9[] = "10101010""10101010""10101010"//"10101010"      // AA AA AA  (preamble)
                    "11000001""10010100"; //"11000001""11000110";   // C1 94 (C1 C6)

char header[] = "10101010""10101010""10101010"       // AA AA AA (preamble)
                "00101101""11010100"; //"10101010";  // 2D D4 (55/AA)

char buf[HEADLEN+1] = "xxxxxxxxxx\0";
int bufpos = 0;

char frame_bits[BITFRAMELEN+1];
ui8_t frame_bytes[FRAMELEN+1];
ui8_t xframe[FRAMELEN+1];

float baudrate = BAUD_RATE;

/* ------------------------------------------------------------------------------------ */

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
    if (strncmp(txt, "RIFF", 4) && strncmp(txt, "RF64", 4)) return -1;
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

    if (bits_sample != 8 && bits_sample != 16 && bits_sample != 32) return -1;

    if (sample_rate == 900001) sample_rate -= 1;

    samples_per_bit = sample_rate/(float)baudrate;

    fprintf(stderr, "samples/bit: %.2f\n", samples_per_bit);

    return 0;
}

unsigned long sample_count = 0;
double bitgrenze = 0;

int f32read_signed_sample(FILE *fp, float *s) {
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

int par=1, par_alt=1;

int read_bits_fsk(FILE *fp, int *bit, int *len) {
    int n;
    float s;
    float l;

    n = 0;
    do {
        if (f32read_signed_sample(fp, &s) == EOF) return EOF;
        //sample_count++; // in f32read_signed_sample()
        par_alt = par;
        par =  (s >= 0) ? 1 : -1;
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

int bitstart = 0;
unsigned long scount = 0;
int read_rawbit(FILE *fp, int *bit) {
    float s;
    float sum;

    sum = 0;

    if (bitstart) {
        scount = 0;    // eigentlich scount = 1
        bitgrenze = 0; //   oder bitgrenze = -1
        bitstart = 0;
    }
    bitgrenze += samples_per_bit;

    do {
        if (f32read_signed_sample(fp, &s) == EOF) return EOF;
        //sample_count++; // in f32read_signed_sample()
        sum += s;
        scount++;
    } while (scount < bitgrenze);  // n < samples_per_bit

    if (sum >= 0) *bit = 1;
    else          *bit = 0;

    if (option_inv) *bit ^= 1;

    return 0;
}


int f32soft_read(FILE *fp, float *s) {
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

    return 0;
}


int compare(char *hdr) {
    int i=0;
    while ((i < HEADLEN) && (buf[(bufpos+i) % HEADLEN] == hdr[HEADLEN+HEADOFS-1-i])) {
        i++;
    }
    return i;
}

char inv(char c) {
    if (c == '0') return '1';
    if (c == '1') return '0';
    return c;
}

int compare2(char *hdr) {
    int i=0;
    while ((i < HEADLEN) && (buf[(bufpos+i) % HEADLEN] == inv(hdr[HEADLEN+HEADOFS-1-i]))) {
        i++;
    }
    return i;
}

int bits2bytes(char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < FRAMELEN) {

        byteval = 0;
        d = 1;
        for (i = 0; i < 8; i++) {
            //bit = bitstr[bitpos+i]; /* little endian */
            bit = bitstr[bitpos+7-i];  /* big endian */
            if         (bit == '1')     byteval += d;
            else /*if ((bit == '0') */  byteval += 0;
            d <<= 1;
        }
        bitpos += 8;
        bytes[bytepos++] = byteval;

    }

    //while (bytepos < FRAME_LEN) bytes[bytepos++] = 0;

    return 0;
}


// PN9 Data Whitening
// cf. https://www.ti.com/lit/an/swra322/swra322.pdf
//     https://destevez.net/2019/07/lucky-7-decoded/
//
// counter low byte: frame[ofs+4] XOR 0xCC
// zero bytes, frame[ofs+30]: 0C CA C9 FB 49 37 E5 A8
//
ui8_t  PN9b[64] = { 0xFF, 0x87, 0xB8, 0x59, 0xB7, 0xA1, 0xCC, 0x24,
                    0x57, 0x5E, 0x4B, 0x9C, 0x0E, 0xE9, 0xEA, 0x50,
                    0x2A, 0xBE, 0xB4, 0x1B, 0xB6, 0xB0, 0x5D, 0xF1,
                    0xE6, 0x9A, 0xE3, 0x45, 0xFD, 0x2C, 0x53, 0x18,
                    0x0C, 0xCA, 0xC9, 0xFB, 0x49, 0x37, 0xE5, 0xA8,
                    0x51, 0x3B, 0x2F, 0x61, 0xAA, 0x72, 0x18, 0x84,
                    0x02, 0x23, 0x23, 0xAB, 0x63, 0x89, 0x51, 0xB3,
                    0xE7, 0x8B, 0x72, 0x90, 0x4C, 0xE8, 0xFb, 0xC1};


ui32_t xor8sum(ui8_t bytes[], int len) {
    int j;
    ui8_t xor8 = 0;
    ui8_t sum8 = 0;

    for (j = 0; j < len; j++) {
        xor8 ^= bytes[j];
        sum8 += bytes[j];
    }
    //sum8 &= 0xFF;

    return  (xor8 << 8) | sum8;
}


typedef struct {
    ui32_t sn1;
    ui32_t cnt1;
    int chk1ok;
    //
    ui32_t sn2;
    ui32_t cnt2;
    int chk2ok; // GPS subframe
    ui8_t hrs;
    ui8_t min;
    ui8_t sec;
    float lat;
    float lon;
    float alt;
    //
    int jsn_freq;   // freq/kHz (SDR)
} gpx_t;

gpx_t gpx;


// xPN9: OFS=8, 5000 baud ; w/o PN9: OFS=6, 4800 baud
#define OFS      6
#define OFS_PN9  8
int ofs = OFS;

int print_frame() {
    int j;
    int chkdat, chkval, chk_ok;

    bits2bytes(frame_bits, frame_bytes);

    for (j = 0; j < FRAMELEN; j++) {
        ui8_t b = frame_bytes[j];
        if (option_pn9) {
            if (j >= 6) b ^= PN9b[(j-6)%64];
        }
        xframe[j] = b;
    }

    chkval = xor8sum(xframe+ofs, 53);
    chkdat = (xframe[ofs+53]<<8) | xframe[ofs+53+1];
    chk_ok = (chkdat == chkval);

    if (option_raw) {
        if (option_raw == 1) {
            for (j = 0; j < FRAMELEN; j++) {
                //printf("%02X ", frame_bytes[j]);
                printf("%02X ", xframe[j]);
            }
            printf(" #  %s", chk_ok ? "[OK]" : "[NO]");
            if (option_verbose) printf(" # [%04X:%04X]", chkdat, chkval);
        }
        else {
            for (j = 0; j < BITFRAMELEN; j++) {
                printf("%c", frame_bits[j]);
                //if (j % 8 == 7) printf(" ");
            }
        }
        printf("\n");
    }
    else {

        ui32_t sn;
        ui32_t cnt;
        int val;

        // SN
        sn = xframe[ofs] | (xframe[ofs+1]<<8) | (xframe[ofs+2]<<16) | (xframe[ofs+3]<<24);

        // counter
        cnt = xframe[ofs+4] | (xframe[ofs+5]<<8);

        ui8_t frid = xframe[ofs+6];

        if (frid == 1)
        {
            gpx.chk1ok = chk_ok;
            gpx.sn1    = sn;
            gpx.cnt1   = cnt;

            if (option_verbose) {

                printf(" (%u) ", sn);  //printf(" (0x%08X) ", sn);
                printf(" [%5d] ", cnt);

                printf("  %s", chk_ok ? "[OK]" : "[NO]");
                if (option_verbose) printf(" # [%04X:%04X]", chkdat, chkval);

                printf("\n");
            }
        }
        else if (frid == 2)
        {
            gpx.chk2ok = chk_ok;
            gpx.sn2    = sn;
            gpx.cnt2   = cnt;

            // SN
            printf(" (%u) ", sn);  //printf(" (0x%08X) ", sn);

            // counter
            printf(" [%5d] ", cnt);

            // time/UTC
            int hms;
            hms = xframe[ofs+7] | (xframe[ofs+8]<<8) | (xframe[ofs+9]<<16);
            hms &= 0x3FFFF;
            //printf(" (%6d) ", hms);
            ui8_t h =  hms / 10000;
            ui8_t m = (hms % 10000) / 100;
            ui8_t s =  hms % 100;
            printf(" %02d:%02d:%02d ", h, m, s);  // UTC
            gpx.hrs = h;
            gpx.min = m;
            gpx.sec = s;

            // alt
            val = xframe[ofs+13] | (xframe[ofs+14]<<8) | (xframe[ofs+15]<<16);
            val >>= 4;
            val &= 0x7FFFF; // int19 ?
            //if (val & 0x40000) val -= 0x80000; ?? or sign bit ?
            float alt = val / 10.0f;
            printf(" alt: %.1f ", alt);  // MSL
            gpx.alt = alt;
            int val_alt = val;

            // lat
            val = xframe[ofs+15] | (xframe[ofs+16]<<8) | (xframe[ofs+17]<<16) | (xframe[ofs+18]<<24);
            val >>= 7;
            val &= 0x1FFFFFF; // int25 ?  ?? sign NMEA N/S ?
            //if (val & 0x1000000) val -= 0x2000000; // sign bit ?  (or 90 -> -90 wrap ?)
            float lat = val / 1e5f;
            printf(" lat: %.4f ", lat);
            gpx.lat = lat;
            int val_lat = val;

            // lon
            val = xframe[ofs+19] | (xframe[ofs+20]<<8) | (xframe[ofs+21]<<16)| (xframe[ofs+22]<<24);
            val &= 0x3FFFFFF; // int26 ?  ?? sign NMEA E/W ?
            //if (val & 0x2000000) val -= 0x4000000; // or sign bit ?  (or 180 -> -180 wrap ?)
            float lon = val / 1e5f;
            printf(" lon: %.4f ", lon);
            gpx.lon = lon;
            int val_lon = val;

            int zero_pos = val_alt == 0 && val_lat == 0 && val_lon == 0;

            // checksum
            printf("  %s", chk_ok ? "[OK]" : "[NO]");
            if (option_verbose) printf(" # [%04X:%04X]", chkdat, chkval);

            printf("\n");

            // JSON
            if (option_json && gpx.chk2ok && !zero_pos) {
                if (gpx.chk1ok && gpx.sn2 == gpx.sn1 && gpx.cnt2 == gpx.cnt1) // double check, unreliable checksums
                {
                    char *ver_jsn = NULL;
                    fprintf(stdout, "{ \"type\": \"%s\"", "WXR301");
                    fprintf(stdout, ", \"frame\": %u", gpx.cnt2);
                    fprintf(stdout, ", \"id\": \"WXR-%u\"", gpx.sn2);
                    fprintf(stdout, ", \"datetime\": \"%02d:%02d:%02dZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.2f",
                                    gpx.hrs, gpx.min, gpx.sec, gpx.lat, gpx.lon, gpx.alt);

                    // if data from subframe1,
                    // check  gpx.chk1ok && gpx.sn1==gpx.sn2 && gpx.cnt1==gpx.cnt2

                    if (option_pn9) {
                        fprintf(stdout, ", \"subtype\": \"WXR_PN9\"");
                    }

                    if (gpx.jsn_freq > 0) {
                        fprintf(stdout, ", \"freq\": %d", gpx.jsn_freq );
                    }

                    // Reference time/position
                    // (WxR-301D PN9)
                    fprintf(stdout, ", \"ref_datetime\": \"%s\"", "UTC" ); // {"GPS", "UTC"} GPS-UTC=leap_sec
                    fprintf(stdout, ", \"ref_position\": \"%s\"", "MSL" ); // {"GPS", "MSL"} GPS=ellipsoid , MSL=geoid

                    #ifdef VER_JSN_STR
                        ver_jsn = VER_JSN_STR;
                    #endif
                    if (ver_jsn && *ver_jsn != '\0') fprintf(stdout, ", \"version\": \"%s\"", ver_jsn);
                    fprintf(stdout, " }\n");
                    fprintf(stdout, "\n");
                }
            }

        }
    }

    return 0;
}

int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;

    int i, j, h, bit, len;
    int bit_count, frames;
    int header_found = 0;
    int cfreq = -1;

    char *hdr = header;

    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -i\n");
            fprintf(stderr, "       -b\n");
            return 0;
        }
        else if   (strcmp(*argv, "--pn9") == 0) { option_pn9 = 1; }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if   (strcmp(*argv, "--softin") == 0) { option_softin = 1; }  // float32 soft input
        else if   (strcmp(*argv, "-b" ) == 0) { option_b = 1; }
        else if   (strcmp(*argv, "-t" ) == 0) { option_timestamp = 1; }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-R") == 0) || (strcmp(*argv, "--RAW") == 0) ) {
            option_raw = 2;
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

    if (option_pn9) {
        baudrate = BAUD_RATE_PN9;
        hdr = header_pn9;
        ofs = OFS_PN9;
    }

    if ( !option_softin ) {
        i = read_wav_header(fp);
        if (i) return -1;
    }


    if (cfreq > 0) gpx.jsn_freq = (cfreq+500)/1000;


    bit_count = 0;
    frames = 0;

    if (option_softin)
    {
        float s = 0.0f;
        int bit = 0;
        sample_rate = baudrate;
        sample_count = 0;

        while (!f32soft_read(fp, &s)) {

            bit = option_inv ? (s<=0.0f) : (s>=0.0f);  // softbit s: bit=0 <=> s<0 , bit=1 <=> s>=0

            bufpos--;
            if (bufpos < 0) bufpos = HEADLEN-1;
            buf[bufpos] = 0x30 + bit;

            if (!header_found)
            {
                h = compare(hdr); //h2 = compare2(hdr);
                if ((h >= HEADLEN)) {
                    header_found = 1;
                    fflush(stdout);
                    if (option_timestamp) printf("<%8.3f> ", sample_count/(double)sample_rate);
                    strncpy(frame_bits, hdr, HEADLEN);
                    bit_count += HEADLEN;
                    frames++;
                }
            }
            else
            {
                frame_bits[bit_count] = 0x30 + bit;
                bit_count += 1;
            }

            if (bit_count >= BITFRAMELEN) {
                bit_count = 0;
                header_found = 0;

                print_frame();
            }
            sample_count += 1;
        }
    }
    else
    {
        while (!read_bits_fsk(fp, &bit, &len)) {

            if (len == 0) {
                bufpos--;
                if (bufpos < 0) bufpos = HEADLEN-1;
                buf[bufpos] = 'x';
                continue;
            }


            for (j = 0; j < len; j++) {
                bufpos--;
                if (bufpos < 0) bufpos = HEADLEN-1;
                buf[bufpos] = 0x30 + bit;

                if (!header_found)
                {
                    h = compare(hdr); //h2 = compare2(hdr);
                    if ((h >= HEADLEN)) {
                        header_found = 1;
                        fflush(stdout);
                        if (option_timestamp) printf("<%8.3f> ", sample_count/(double)sample_rate);
                        strncpy(frame_bits, hdr, HEADLEN);
                        bit_count += HEADLEN;
                        frames++;
                    }
                }
                else
                {
                    frame_bits[bit_count] = 0x30 + bit;
                    bit_count += 1;
                }

                if (bit_count >= BITFRAMELEN) {
                    bit_count = 0;
                    header_found = 0;

                    print_frame();
                }

            }
            if (header_found && option_b) {
                bitstart = 1;

                while ( bit_count < BITFRAMELEN ) {
                    if (read_rawbit(fp, &bit) == EOF) break;
                    frame_bits[bit_count] = 0x30 + bit;
                    bit_count += 1;
                }

                bit_count = 0;
                header_found = 0;

                print_frame();
            }
        }
    }

    printf("\n");

    fclose(fp);

    return 0;
}

