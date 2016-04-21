
/*
 * pilotsonde
 * FSK, 4800 baud, 8N1, little endian
 *
 * gcc -o m12 m12.c -lm
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef WIN
  #include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;

typedef struct {
    int lat; int lon; int alt;
    int vE; int vN; int vU;
    double vH; double vD; double vV;
    int date; int time;
} datum_t;

datum_t datum;

int option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 0,      // rohe Frames
    option_inv = 0,      // invertiert Signal
    option_auto = 0,
    option_dc = 0,       // non-constant bias
    option_res = 0,      // genauere Bitmessung
    wavloaded = 0;


#define BAUD_RATE  4800

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

unsigned long sample_count = 0;
int wlen;
int *sample_buff = NULL;

int read_filter_sample(FILE *fp) {
    int i;                          // wenn sample_buff[] ein 8N1-byte umfasst,
    int s0, s, y;                   // mit (max+min)/2 Mittelwert bestimmen;
    static int min, max;            // Glaettung durch lowpass/moving average empfohlen

    s = read_signed_sample(fp);
    if (s == EOF_INT) return EOF_INT;

    s0 = sample_buff[sample_count % wlen];
    sample_buff[sample_count % wlen] = s;

    y = 0;
    if (sample_count >  wlen-1) {

        if (s < min)  min = s;
        else {
            if (s0 <= min) {
                min = sample_buff[0];
                for (i = 1; i < wlen; i++) {
                    if (sample_buff[i] < min)  min = sample_buff[i];
                }
            }
        }

        if (s > max)  max = s;
        else {
            if (s0 >= max) {
                max = sample_buff[0];
                for (i = 1; i < wlen; i++) {
                    if (sample_buff[i] > max)  max = sample_buff[i];
                }
            }
        }

        y = sample_buff[(sample_count+wlen-1)%wlen] - (min+max)/2;

    }
    else if (sample_count == wlen-1) {
        min = sample_buff[0];
        max = sample_buff[0];
        for (i = 1; i < wlen; i++) {
            if (sample_buff[i] < min)  min = sample_buff[i];
            if (sample_buff[i] > max)  max = sample_buff[i];
        }
        y = sample_buff[(sample_count+wlen-1)%wlen] - (min+max)/2;
    }

    sample_count++;

    return y;
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

        if (option_dc) sample = read_filter_sample(fp);
        else           sample = read_signed_sample(fp);

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

/* -------------------------------------------------------------------------- */


#define BITS    (1+8+1)  // 8N1 = 10bit/byte: 0bbbbbbbb1
#define HEADLEN (20)     // HEADLEN+HEADOFS=32 <= strlen(header)
#define HEADOFS  0

              //  A   A       A   A    
char header[] = "0010101011""0010101011";

#define FRAME_LEN       (50+1)
#define BITFRAME_LEN    (FRAME_LEN*BITS)

char buf[HEADLEN];
int bufpos = -1;

#define FRAMESTART 0
char  frame_bits[BITFRAME_LEN+4];
ui8_t frame_bytes[FRAME_LEN+10];


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


int bits2bytes(char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bitpos < BITFRAME_LEN) {

        byteval = 0;
        d = 1;
        for (i = 1; i < BITS-1; i++) { 
            bit=*(bitstr+bitpos+i);        /* little endian */
            //bit=*(bitstr+bitpos+BITS-1-i);  /* big endian */
            if         (bit == '1')                     byteval += d;
            else /*if ((bit == '0') || (bit == 'x'))*/  byteval += 0;
            d <<= 1;
        }
        byteval &= 0xFF;
        bitpos += BITS;

       if (bytepos == 0 && byteval == 0xAA) continue;

        bytes[bytepos++] = byteval & 0xFF;
        
    }

    //while (bytepos < FRAME_LEN) bytes[bytepos++] = 0;

    return 0;
}


/* -------------------------------------------------------------------------- */

#define OFS           (0x00)
#define pos_GPSlat    (OFS+0x03)  // 4 byte
#define pos_GPSlon    (OFS+0x07)  // 4 byte
#define pos_GPSalt    (OFS+0x0B)  // 4 byte
#define pos_GPSvE     (OFS+0x0F)  // 2 byte
#define pos_GPSvN     (OFS+0x11)  // 2 byte
#define pos_GPSvU     (OFS+0x13)  // 2 byte
#define pos_GPStime   (OFS+0x15)  // 4 byte
#define pos_GPSdate   (OFS+0x19)  // 4 byte


int get_GPSpos() {
    int i;
    ui8_t bytes[4];
    int val;

    for (i = 0; i < 4; i++)  bytes[i] = frame_bytes[pos_GPSlat + i];
    val = 0;
    for (i = 0; i < 4; i++)  val |= bytes[i] << (8*(3-i));
    datum.lat = val;

    for (i = 0; i < 4; i++)  bytes[i] = frame_bytes[pos_GPSlon + i];
    val = 0;
    for (i = 0; i < 4; i++)  val |= bytes[i] << (8*(3-i));
    datum.lon = val;

    for (i = 0; i < 4; i++)  bytes[i] = frame_bytes[pos_GPSalt + i];
    val = 0;
    for (i = 0; i < 4; i++)  val |= bytes[i] << (8*(3-i));
    datum.alt = val;

    return 0;
}

int get_GPSvel() {
    int i;
    ui8_t bytes[2];
    short vel16;
    double vx, vy, vz, dir;

    for (i = 0; i < 2; i++)  bytes[i] = frame_bytes[pos_GPSvE + i];
    vel16 = bytes[0] << 8 | bytes[1];
    datum.vE = vel16;
    vx = vel16 / 1e2; // east

    for (i = 0; i < 2; i++)  bytes[i] = frame_bytes[pos_GPSvN + i];
    vel16 = bytes[0] << 8 | bytes[1];
    datum.vN = vel16;
    vy= vel16 / 1e2; // north

    for (i = 0; i < 2; i++)  bytes[i] = frame_bytes[pos_GPSvU + i];
    vel16 = bytes[0] << 8 | bytes[1];
    datum.vU = vel16;
    vz = vel16 / 1e2; // up

    datum.vH = sqrt(vx*vx+vy*vy);
    dir = atan2(vx, vy) * 180 / M_PI;
    if (dir < 0) dir += 360;
    datum.vD = dir;
    datum.vV = vz;

    return 0;
}

int get_GPStime() {
    int i;
    ui8_t bytes[4];
    int val;

    for (i = 0; i < 4; i++)  bytes[i] = frame_bytes[pos_GPStime + i];
    val = 0;
    for (i = 0; i < 4; i++)  val |= bytes[i] << (8*(3-i));
    datum.time = val;

    return 0;
}

int get_GPSdate() {
    int i;
    ui8_t bytes[4];
    int val;

    for (i = 0; i < 4; i++)  bytes[i] = frame_bytes[pos_GPSdate + i];
    val = 0;
    for (i = 0; i < 4; i++)  val |= bytes[i] << (8*(3-i));
    datum.date = val;

    return 0;
}


/* -------------------------------------------------------------------------- */

int print_pos() {
    int err;

    err = 0;
    err |= get_GPSpos();
    err |= get_GPSvel();
    err |= get_GPStime();
    err |= get_GPSdate();

    if (!err) {
        //fprintf(stdout, " (%06d)", datum.date);
        fprintf(stdout, " %02d-%02d-%02d", datum.date/10000, (datum.date%10000)/100, datum.date%100);
        //fprintf(stdout, " (%09d)", datum.time);
        fprintf(stdout, " %02d:%02d:%06.3f ", datum.time/10000000, (datum.time%10000000)/100000, (datum.time%100000)/1000.0);

        fprintf(stdout, " lat: %.6f° ", datum.lat/1e6);
        fprintf(stdout, " lon: %.6f° ", datum.lon/1e6);
        fprintf(stdout, " alt: %.2fm ", datum.alt/1e2);

        //fprintf(stdout, "  (%.1f , %.1f , %.1f) ", datum.vE/1e2, datum.vN/1e2, datum.vU/1e2);
        fprintf(stdout, "  vH: %.1fm/s  D: %.1f°  vV: %.1fm/s", datum.vH, datum.vD, datum.vV);
    }
    fprintf(stdout, "\n");

    return err;
}

void print_frame(int pos) {
    int i;

    bits2bytes(frame_bits, frame_bytes);

    if (option_raw) {
        if (option_raw == 2) {
            for (i = 0; i < BITFRAME_LEN; i++) {
                fprintf(stdout, "%c", frame_bits[i]);
                if (i%10==0 || i%10==8) fprintf(stdout, " ");
            }
            fprintf(stdout, "\n");
        }
        else {
            for (i = 0; i < FRAME_LEN; i++) {
                fprintf(stdout, "%02x ", frame_bytes[i]);
            }
            fprintf(stdout, "\n");
        }
    }

    else print_pos();

}


int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;
    int i, bit, len;
    int pos;
    int header_found = 0;


#ifdef WIN
    _setmode(fileno(stdin), _O_BINARY);  // _setmode(_fileno(stdin), _O_BINARY);
    setbuf(stdout, NULL);
#endif

    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            //fprintf(stderr, "       -v, --verbose\n");
            fprintf(stderr, "       -r, --raw; -R\n");
            fprintf(stderr, "       -i, --invert; --auto\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-R") == 0) ) option_raw = 2;
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
        }
        else if ( (strcmp(*argv, "--auto") == 0) ) {
            option_auto = 1;
        }
        else if ( (strcmp(*argv, "--dc") == 0) ) {
            option_dc = 1;
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

    if (option_dc) {
        wlen = 2*BITS*samples_per_bit;
        sample_buff = (int*)calloc(wlen, sizeof(int));
        if (sample_buff == NULL) {
            fprintf(stderr, "error malloc\n");
            return -1;
        }
    }
    

    pos = FRAMESTART;

    while (!read_bits_fsk(fp, &bit, &len)) {

        if (len == 0) { // reset_frame();
            if (pos > (pos_GPSdate+7)*BITS) {
                for (i = pos; i < BITFRAME_LEN; i++) frame_bits[i] = 0x30 + 0;
                print_frame(pos);//byte_count
                header_found = 0;
                pos = FRAMESTART;
            }
            //inc_bufpos();
            //buf[bufpos] = 'x';
            continue;   // ...
        }

        for (i = 0; i < len; i++) {

            inc_bufpos();
            buf[bufpos] = 0x30 + bit;  // Ascii

            if (!header_found) {
                header_found = compare2();
                //if (header_found) fprintf(stdout, "[%c] ", header_found>0?'+':'-');
                if (header_found < 0) option_inv ^= 0x1;
                // printf("[%c] ", option_inv?'-':'+');
            }
            else {
                frame_bits[pos] = 0x30 + bit;  // Ascii
                pos++;
            
                if (pos == BITFRAME_LEN) {
                    print_frame(pos);//FRAME_LEN
                    header_found = 0;
                    pos = FRAMESTART;
                }
            }

        }
    }

    printf("\n");

    fclose(fp);
    if (option_dc) free(sample_buff);

    return 0;
}

