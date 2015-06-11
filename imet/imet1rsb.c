
/*
 * radiosonde iMet-1-RSB
 * Bell202 8N1
 * open packet format
 */

#include <stdio.h>
#include <string.h>

#ifdef CYGWIN
  #include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif


typedef unsigned char ui8_t;
typedef unsigned int ui32_t;


int //option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 1,      // rohe Frames
    option_inv = 0,      // invertiert Signal
    wavloaded = 0;

/* -------------------------------------------------------------------------- */

#define BAUD 1200
// iMet-1-RSB: BAUD 1200
/* 1200 Hz: lang ->         1 Schwingung   pro bit 1
   2200 Hz: kurz -> 2200/1200 Schwingungen pro bit 0
*/
#define BIT01 0.71
#define SYNC 128


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
    int byte, p=0;
    char fmt_[5] = "fmt ";
    char data[5] = "data";

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
    if (fread(buff, 1, 2, fp) < 2) return -1;
    if (fread(buff, 1, 2, fp) < 2) return -1;
    channels = buff[0] + (buff[1] << 8);
    if (fread(buff, 1, 4, fp) < 4) return -1;
    memcpy(&sample_rate, buff, 4);
    if (fread(buff, 1, 4, fp) < 4) return -1;
    if (fread(buff, 1, 2, fp) < 2) return -1;
    byte = buff[0] + (buff[1] << 8);
    if (fread(buff, 1, 2, fp) < 2) return -1;
    bits_sample = buff[0] + (buff[1] << 8);

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

    samples_per_bit = sample_rate/(float)BAUD;

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


int par=-1, par_alt=-1;
unsigned long sample_count = 0;

int read_bits(FILE *fp, char *Bit, float *l) {
    int n; static int sample;

    n = 0;
    do{
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        if (option_inv) sample = -sample;
        sample_count++;
        par_alt = par;
        par =  (sample >= 0) ? 1 : -1;
        n++;
    } while (par*par_alt > 0);

    *l = (float)n / (samples_per_bit/2.0);
//fprintf(stderr, " %.1f\n", *l);

    if (*l >= BIT01) {
        if (par_alt < 0) *Bit = 'D';
        else             *Bit = 'U';
    }
    else if (*l > 0.1) {
        if (par_alt < 0) *Bit = 'd';
        else             *Bit = 'u';
    }
    else *l = 0;
    
    //*len = (int)(l+0.5); // round(l)

    return 0;
}

/* -------------------------------------------------------------------------- */

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
#define pos_GPStim  0x0D  // 3 byte

#define FRAMELEN 150
ui8_t frame[FRAMELEN+6];
ui8_t bitframe[BAUD+10];


void print_gps(int pos) {
    float lat, lon;
    int alt;
    int std, min, sek;

    lat = *(float*)(frame+pos+pos_GPSlat);
    lon = *(float*)(frame+pos+pos_GPSlon);
    alt = ((frame+pos)[pos_GPSalt+1]<<8)+(frame+pos)[pos_GPSalt] - 5000;
    std = (frame+pos)[pos_GPStim+0];
    min = (frame+pos)[pos_GPStim+1];
    sek = (frame+pos)[pos_GPStim+2];

    fprintf(stdout, " lat: %.6f ", lat);
    fprintf(stdout, " lon: %.6f ", lon);
    fprintf(stdout, " alt: %d ", alt);
    fprintf(stdout, " %02d:%02d:%02d ", std, min, sek);
}



/* -------------------------------------------------------------------------- */


int bits2byte(ui8_t *bits) {
    int i, d = 1, byte = 0;

    for (i = 0; i < 8; i++) {  // little endian
        if      (bits[i] == 1)  byte += d;
        else if (bits[i] == 0)  byte += 0;
        d <<= 1;
    }
    return byte & 0xFF;
}

void print_frame(int len) {
    int i;
    int byte;
    //int err = 0;

    printf("\n");
    for (i = 0; i < len; i++) {
        byte = bits2byte(bitframe+10*i+2);
        frame[i] = byte;
        //printf("%02X ", byte);
    }

    if (option_raw) {
        for (i = 0; i < len; i++) {
            fprintf(stdout, "%02x ", frame[i]);
        }
        fprintf(stdout, "\n");
    }
    //else
    {
        if ((frame[0] == 0x01) && (frame[1] == 0x02)) { // GPS Data Packet
            print_gps(0x00);  // packet offset in frame
            fprintf(stdout, "\n");
        }
    }

}

int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;
    int i, len;

    int bitpos = 0,
        bit_ = 0,
        bit;
    char Bit = 0 /*,Bit0*/;
    float l = 0, l0 = 0, bitslen = 0;

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
            return 0;
        }
/*
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
*/
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
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

    bitpos = 0;

    while (!read_bits(fp, &Bit, &l)) {

        if (l < 0.1) {
            bitslen = 0;
            continue;
        }

        if ((l0 < BIT01  &&  l < BIT01) || (l0 >= BIT01  &&  l >= BIT01)) {
            bitslen += l;
        }
        else {
            //fprintf(stderr, " %.1f\n", bitslen);
            if (Bit < 'a') bit = 0; else bit = 1;  // gesucht ist Bit zuvor
            len = (int)(bitslen+0.5);
            if (len < SYNC) {
            for (i = 0; i < len/2; i++) {
                //printf("%d", bit);
                bitframe[bitpos] = bit;
                bitpos++;
            }
            //printf(" %.1f=%d ", bitslen, len);
            }
            bitslen = l;
        }

        l0 = l;

        if (Bit < 'a') {
            bit_++;
        }
        else {
          if (bit_> SYNC) {
              if (bitpos < BAUD) print_frame((bitpos+7)/10);
              bitframe[0] = 1;
              bitpos = 1;
              //printf("\n1");
          }
          bit_ = 0;
        }

    }
    printf("\n");

    fclose(fp);

    return 0;
}

