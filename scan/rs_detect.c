
/*
 *  detect/identify radiosondes
 *  DFM, RS92-SGP, RS41, M10, iMet-1-AB
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifdef CYGWIN
  #include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif


typedef unsigned char ui8_t;
typedef unsigned int ui32_t;

time_t rawtime;
struct tm *gmt;


int wavloaded = 0,
    option_gmt = 0,
    option_silent = 0;


#define  DFM   2
#define  RS41  3
#define  RS92  4
#define  M10   5
#define  iMet  6


#define HEADLEN 32
#define HEADOFS  0  // HEADLEN+HEADOFS=32

//int  dfm_baudrate = 2500;
char dfm_header[] = "01100101011001101010010110101010";

//int  vai_baudrate = 4800;
char rs41_header[] = "00001000011011010101001110001000"
                     "01000100011010010100100000011111";
char rs92_header[] = "10100110011001101001"
                     "10100110011001101001"
                     "10100110011001101001"
                     "10100110011001101001"
                     "1010011001100110100110101010100110101001";

//int  m10_baudrate = 9600;
char m10_header[] = "00110011001100110101100110110011";

//int  imet_baudrate = 9600;
char imet_header[] = "11110000111100001111000011110000"
           "11110000""10101100110010101100101010101100"
           "11110000""10101100110010101100101010101100";


char buf25[HEADLEN+1] = "x";
int bufpos25 = -1;

char buf48[HEADLEN+1] = "x";
int bufpos48 = -1;

char buf96[HEADLEN+1] = "x";
int bufpos96 = -1;


/* -------------------------------------------------------------------------- */

int sample_rate = 0, bits_sample = 0, channels = 0;

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


    if (!option_silent) {
        fprintf(stderr, "sample_rate: %d\n", sample_rate);
        fprintf(stderr, "bits       : %d\n", bits_sample);
        fprintf(stderr, "channels   : %d\n", channels);
    }

    if ((bits_sample != 8) && (bits_sample != 16)) return -1;

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

int read_bits_fsk(FILE *fp, int *bit, int *len) {
    int n, sample;

    n = 0;
    do{
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        sample_count++;
        par_alt = par;
        par =  (sample >= 0) ? 1 : -1;  // 8bit: 0..127,128..255 (-128..-1,0..127)
        n++;
    } while (par*par_alt > 0);

    *len = n;

    *bit = (1+par_alt)/2;  // oben 1, unten -1
    //*bit = (1-par_alt)/2;// sdr#<rev1381?, invers: unten 1, oben -1

    return 0;
}

/* -------------------------------------------------------------------------- */

char inv(char c) {
    if (c == '0') return '1';
    if (c == '1') return '0';
    return c;
}


void inc_buf(int *bufpos) {
  *bufpos = (*bufpos+1) % HEADLEN;
}

int compare(char buf[], char header[], int bufpos) {
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
        if (buf[j] != inv(header[HEADOFS+HEADLEN-1-i])) break;
        j--;
        i++;
    }
    if (i == HEADLEN) return -1;

    return 0;
}


/* -------------------------------------------------------------------------- */


int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;
    int header_found, bit, len, i;
    int len25, len48, len96;
    int zeit = 0;

#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _setmode(_fileno(stdin), _O_BINARY);
    setbuf(stdout, NULL);
#endif

    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [-t nn] audio.wav\n", fpname);
            return 0;
        }
        else if ( (strcmp(*argv, "-s") == 0) || (strcmp(*argv, "--silent") == 0) ) {
            option_silent = 1;
        }
        else if ( (strcmp(*argv, "-z") == 0) || (strcmp(*argv, "--zulu") == 0) ) {
            option_gmt = 1;
        }
        else if ( (strcmp(*argv, "-t") == 0) || (strcmp(*argv, "--time") == 0) ) {
            ++argv;
            if (*argv) zeit = atoi(*argv);
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


    i = read_wav_header(fp);
    if (i) {
        fclose(fp);
        return -1;
    }

    header_found = 0;

    while (!read_bits_fsk(fp, &bit, &len) && !header_found) {

        len25 = (int) (len * 2500.0 / sample_rate + 0.5);
        len48 = (int) (len * 4800.0 / sample_rate + 0.5);
        len96 = (int) (len * 9600.0 / sample_rate + 0.5);

        for (i = 0; i < len25; i++) {
            inc_buf(&bufpos25);
            buf25[bufpos25] = 0x30 + bit;  // Ascii
            header_found = compare(buf25, dfm_header, bufpos25) * DFM;
            if (header_found) goto ende;
        }

        for (i = 0; i < len48; i++) {
            inc_buf(&bufpos48);
            buf48[bufpos48] = 0x30 + bit;  // Ascii
            header_found = compare(buf48, rs41_header+16, bufpos48) * RS41;
            if (header_found) goto ende;
            header_found = compare(buf48, rs92_header, bufpos48) * RS92;
            if (header_found) goto ende;
        }

        for (i = 0; i < len96; i++) {
            inc_buf(&bufpos96);
            buf96[bufpos96] = 0x30 + bit;  // Ascii
            header_found = compare(buf96, m10_header, bufpos96) * M10;
            if (header_found) goto ende;
            header_found = compare(buf96, imet_header+32, bufpos96) * iMet;
            if (header_found) goto ende;
        }

        if (zeit > 0  &&  sample_count > zeit*sample_rate) goto ende;

    }

ende:
    fclose(fp);

    if (!option_silent) {
        printf("sample: %lu\n", sample_count);
        if (zeit) printf("%ds = %d samples\n", zeit, zeit*sample_rate);
        printf("found: ");
        if (!header_found) printf("NO");
        else {
            time(&rawtime);
            gmt = gmtime(&rawtime);
            if (header_found < 0) printf("-");
            if (header_found*header_found == DFM*DFM)   printf("DFM");
            if (header_found*header_found == RS41*RS41) printf("RS41");
            if (header_found*header_found == RS92*RS92) printf("RS92");
            if (header_found*header_found == M10*M10)   printf("M10");
            if (header_found*header_found == iMet*iMet) printf("iMet");
            if (option_gmt) {
                printf(" (%4d-%02d-%02d %02d:%02dZ) ", gmt->tm_year+1900, gmt->tm_mon,
                                                       gmt->tm_mday, gmt->tm_hour, gmt->tm_min);
            }
        }
        printf("\n");
    }

    return header_found;
}

