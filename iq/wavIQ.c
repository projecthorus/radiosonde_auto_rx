
/*
   gcc wavIQ.c -lm -o wavIQ

   ./wavIQ -s dfmIQ.wav > IQswap.wav

   ./wavIQ -t -10600 IQswap.wav > IQtransl1.wav
   ./wavIQ -l 6000 IQtransl1.wav > IQlowpass1.wav
   ./wavIQ -d1 IQlowpass1.wav > IQdemod1.wav
   sox IQdemod1.wav -r 48000 IQdemod48k1.wav

   ./wavIQ -t 11200 IQswap.wav > IQtransl2.wav
   ./wavIQ -l 6000 IQtransl2.wav > IQlowpass2.wav
   ./wavIQ -d1 IQlowpass2.wav > IQdemod2.wav
   sox IQdemod2.wav -r 48000 IQdemod48k2.wav

   ./wavIQ -fm IQdemod48k1.wav > dfmIQ2.wav

*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>


#define  SWAPIQ     0x10
#define  TRANSLATE  0x20
#define  LOWPASS    0x30
#define  DEMOD      0x40
#define  FMMOD      0x50

#define  PI  (3.1415926535897932384626433832795)


int sample_rate, bits_sample;

int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}
int read_wavheader(FILE *fp, int *sample_rate, int *bits_sample,
                             unsigned char chIn, unsigned char chOut, FILE *fout) {
    unsigned int channels = 0, size = 0;
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;

    if (fread(txt, 1, 4, fp) < 4) return -1;  fwrite(txt, 1, 4, fout);
    if (strncmp(txt, "RIFF", 4)) return -1;

    if (fread(dat, 1, 4, fp) < 4) return -1;
    size = dat[0] | (dat[1] << 8) | (dat[2] << 16) | (dat[3] << 24); //fprintf(stderr, "filesize: 0x%08x = %d\n", size+8, size+8);
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
    channels = dat[0] + (dat[1] << 8);
    dat[0] = chOut; fwrite(dat, 1, 2, fout);

    // sample_rate
    if (fread(dat, 1, 4, fp) < 4) return -1;  fwrite(dat, 1, 4, fout);
    *sample_rate = dat[0] | (dat[1] << 8) | (dat[2] << 16) | (dat[3] << 24); //memcpy(&sr, dat, 4);

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
    *bits_sample = dat[0] + (dat[1] << 8);
    if ((*bits_sample != 8) && (*bits_sample != 16)) return -2;

    // pos_dat = 36L + info
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;  fprintf(fout, "%c", byte & 0xFF);
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "data", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;
    size = dat[0] | (dat[1] << 8) | (dat[2] << 16) | (dat[3] << 24); //fprintf(stderr, "datasize: 0x%08x = %d\n", size, size);
    size = (size*chOut)/chIn;
    for (byte = 0; byte < 4; byte++) { dat[byte] = size & 0xFF; size >>= 8; }
    fwrite(dat, 1, 4, fout);

    if (channels != chIn) return -3;  // I&Q: chIn=2
/*
    fprintf(stderr, "sample_rate: %d\n", *sample_rate);
    fprintf(stderr, "bits       : %d\n", *bits_sample);
    fprintf(stderr, "channels   : %d\n", channels);
*/
    return 0;
}

int read_csample(FILE *fp, double complex *z) {
    short x = 0, y = 0;

    if (fread( &x, bits_sample/8, 1, fp) != 1) return EOF;
    if (fread( &y, bits_sample/8, 1, fp) != 1) return EOF;

    *z = x + I*y;

    if (bits_sample == 8) {
        *z -= 128 + I*128;
    }

    return 0;
}

int write_csample(FILE *fp, double complex w) {
    int u, v;

    if (bits_sample == 16) { w *= 256.0; }

    u = creal(w);
    v = cimag(w);

    if (bits_sample == 8) {
        u += 128;
        v += 128;
    }
                                        // 16 bit  (short)  ->  (int)
    fwrite( &u, bits_sample/8, 1, fp);  // +  0000 .. 7FFF  ->  0000 0000 .. 0000 7FFF
    fwrite( &v, bits_sample/8, 1, fp);  // -  8000 .. FFFF  ->  FFFF 8000 .. FFFF FFFF

    return 0;

}

int read_sample(FILE *fp, double *z) {  // channels == 1
    short x = 0;

    if (fread( &x, bits_sample/8, 1, fp) != 1) return EOF;

    if (bits_sample == 8) {
        x -= 128;
    }

    *z = x/128.0;
    if (bits_sample == 16) { *z = x/256.0; }

    return 0;
}

int write_sample(FILE *fp, double x) {
    int b;

    if (bits_sample == 8) {
        x = x + 128.0;
    }
    else x *= 256.0;

    b = (int)x;
                                        // 16 bit  (short)  ->  (int)
    fwrite( &b, bits_sample/8, 1, fp);  // +  0000 .. 7FFF  ->  0000 0000 .. 0000 7FFF
                                        // -  8000 .. FFFF  ->  FFFF 8000 .. FFFF FFFF
    return 0;

}

//
// lowpass
double sinc(double x) {
    double y;
    if (x == 0) y = 1;
    else y = sin(PI*x)/(PI*x);
    return y;
}

double *ws = NULL;

int lowpass_init(int fs, int lpf) {  // lpf = lowpass freq bandwidth = 2*lp_freq
    double f = (double)lpf/fs;
    int M = 2*((2*fs)/1200);
    double *h, *w;
    double norm = 0;
    int n;

    ws = (double*)calloc( M+1, sizeof(double));

    h  = (double*)calloc( M+1, sizeof(double));
    w  = (double*)calloc( M+1, sizeof(double));

    for (n = 0; n < M+1; n++) {
        w[n] = 0.54 - 0.46*cos(2*PI*n/M) + 0.08*cos(4*PI*n/M);
        h[n] = 2*f*sinc(2*f*(n-M/2));
        ws[n] = w[n]*h[n];
        norm += ws[n];
    }
    for (n = 0; n < M+1; n++) {
        ws[n] /= norm;
    }

    free(h); h = NULL;
    free(w); w = NULL;

    return M;
}

double complex lowpass(double complex buffer[], int sample, int M) {
    int n;
    double complex w = 0;

    if (sample > M) {
        for (n = 0; n < M+1; n++) {
            w += buffer[(sample+n+1)%(M+1)]*ws[M-n];
        }
    }
    return w;
}
// lowpass
//


int main(int argc, char *argv[]) {
    FILE *fp = NULL, *fout = NULL;
    char *fpname = NULL;
    unsigned char chIn = 0, chOut = 0;
    int option = 0, phi = 0, wavloaded = 0;
    int sample = 0, M = 0;
    double t, s, f = 0, fm, b, omega = 0;
    double complex  z = 0, z0 = 0, w = 0,
                    *buffer = NULL;
    double gain = 40.0;

    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s <option> file.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -s         (swap IQ)\n");
            fprintf(stderr, "       -t <freq>  (translate)\n");
            fprintf(stderr, "       -l <freq>  (lowpass)\n");
            fprintf(stderr, "       -d1,2      (demod fm)\n");
            fprintf(stderr, "       -fm        (fm-mod)\n");
            return 0;
        }
        else if (strcmp(*argv, "-s") == 0) option = SWAPIQ;
        else if (strcmp(*argv, "-t") == 0) {
            ++argv;
            if (*argv) {
                if (strcmp(*argv, "-") == 0) f = 4800;
                else f = (double)atoi(*argv);
                option = TRANSLATE;
            }
            else return -1;
        }
        else if (strcmp(*argv, "-l") == 0) {
            ++argv;
            if (*argv) {
                if (strcmp(*argv, "-") == 0) f = 0;
                else f = (double)atoi(*argv);
                option = LOWPASS;
            }
            else return -1;
        }
        else if (strcmp(*argv, "-d1") == 0) { option = DEMOD; phi = 1; }
        else if (strcmp(*argv, "-d2") == 0) { option = DEMOD; phi = 2; }
        else if (strcmp(*argv, "-fm") == 0) { option = FMMOD; }
        else {
            if (!option) return 0;
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


    if (     (option & 0xF0) == (DEMOD & 0xF0) ) {
        chIn  = 2;
        chOut = 1;
    }
    else if ((option & 0xF0) == (FMMOD & 0xF0) ) {
        chIn  = 1;
        chOut = 2;
    }
    else {
        chIn  = 2;
        chOut = 2;
    }

    fout = stdout; //fopen("tmp_out.wav", "wb");
    if (read_wavheader(fp, &sample_rate, &bits_sample, chIn, chOut, fout) != 0) {
        fprintf(stderr, "error: wav header\n");
        return -1;
    }


    sample = 0;

    switch ( option ) {

        case SWAPIQ:
            while ( read_csample(fp, &z) != EOF ) {
                w = cimag(z) + I*creal(z);  // = cexp(I*PI/2) * conj(z) = I * conj(z);
                write_csample(fout, w);
                sample++;
            }
            break;

        case TRANSLATE:
            while ( read_csample(fp, &z) != EOF ) {
                t = (double)sample / sample_rate;
                w = cexp(t*2*PI*f*I) * z;
                write_csample(fout, w);
                sample++;
            }
            break;

        case LOWPASS:
            M = lowpass_init(sample_rate, f);
            buffer = (double complex*)calloc( M+1, sizeof(double complex));
            w = 0;
            while ( read_csample(fp, &z) != EOF ) {
                buffer[sample % (M+1)] = z;
                if (sample > M) {
                    w = lowpass(buffer, sample, M);
                }
                write_csample(fout, w);
                sample++;
            }
            free(ws); ws = NULL;
            free(buffer); buffer = NULL;
            break;

        case DEMOD:
            z0 = 0;
            while ( read_csample(fp, &z) != EOF ) {
                w = z * conj(z0);
                switch ( phi ) {  // phi'-Algo
                  case 1: fm = carg(w); break; // = atan2( cimag(w) , creal(w) );
                  case 2: fm = cimag(w) / (cabs(z0)*cabs(z0)); break;
                }         //fm3 = cimag(w); // FM: |z|=const
                write_sample(fout, fm*gain);
                z0 = z;
                sample++;
            }
            break;

        case FMMOD:
            b = 0.7;
            omega = 0;
            while ( read_sample(fp, &s) != EOF ) {
                // integrate phi'
                omega += 2*PI * b*s ;  // mod 2*PI
                w = cexp(I*omega);
                write_csample(fout, w*gain);
                sample++;
            }
            break;

    }

    fclose(fp);
    fclose(fout);

    return 0;
}


/*
$ ./dfm06 -v IQdemod48k1.wav 
[174] 2015-03-17 13:09:50.0  lat: 49.8792503  lon: 7.5075282  h: 12690.37   dir:  10.7  hV:  6.32  vV:  4.24  (ID:219892) 
[176] 2015-03-17 13:09:52.0  lat: 49.8793662  lon: 7.5075404  h: 12700.00   dir: 355.6  hV:  6.54  vV:  4.26 
[177] 2015-03-17 13:09:53.0  lat: 49.8794272  lon: 7.5075275  h: 12704.36   dir: 351.9  hV:  7.02  vV:  4.43 
[178] 2015-03-17 13:09:53.0  lat: 49.8794272  lon: 7.5075275  h: 12704.36   dir: 351.9  hV:  7.02  vV:  4.43  (ID:219892) 
[179] 2015-03-17 13:09:55.0  lat: 49.8795518  lon: 7.5074921  h: 12712.85   dir: 348.7  hV:  7.69  vV:  4.32 
[180] 2015-03-17 13:09:56.0  lat: 49.8796194  lon: 7.5074641  h: 12716.99   dir: 343.1  hV:  8.10  vV:  4.06 
[181] 2015-03-17 13:09:57.0  lat: 49.8796903  lon: 7.5074358  h: 12721.57   dir: 348.4  hV:  8.32  vV:  4.21  (ID:219892) 
[182] 2015-03-17 13:09:58.0  lat: 49.8797624  lon: 7.5074170  h: 12726.25   dir: 352.8  hV:  8.05  vV:  3.97 
[183] 2015-03-17 13:09:59.0  lat: 49.8798346  lon: 7.5074085  h: 12730.47   dir: 357.8  hV:  8.26  vV:  4.05 
[184] 2015-03-17 13:10:00.0  lat: 49.8799102  lon: 7.5074023  h: 12734.09   dir: 356.9  hV:  8.32  vV:  3.75  (ID:219892) 
[185] 2015-03-17 13:10:01.0  lat: 49.8799832  lon: 7.5073979  h: 12738.27   dir: 358.5  hV:  8.00  vV:  3.86 
[187] 2015-03-17 13:10:03.0  lat: 49.8801186  lon: 7.5073991  h: 12747.34   dir:   3.3  hV:  7.46  vV:  3.92 
[189] 2015-03-17 13:10:05.0  lat: 49.8802583  lon: 7.5073884  h: 12756.31   dir: 356.1  hV:  7.96  vV:  4.06  (ID:219892) 
[190] 2015-03-17 13:10:06.0  lat: 49.8803284  lon: 7.5073828  h: 12761.98   dir: 359.1  hV:  7.65  vV:  4.39 
[191] 2015-03-17 13:10:07.0  lat: 49.8803981  lon: 7.5073832  h: 12766.71   dir:   1.8  hV:  7.89  vV:  4.33  (ID:219892) 
$ ./dfm06 -v IQdemod48k2.wav 
[ 98] 2015-03-17 13:09:50.0  lat: 50.0256187  lon: 7.7031148  h: 7769.69   dir:  29.8  hV: 24.51  vV: -3.19 
[100] 2015-03-17 13:09:52.0  lat: 50.0260194  lon: 7.7033832  h: 7763.78   dir:  17.4  hV: 23.08  vV: -2.86  (ID:314377) 
[102] 2015-03-17 13:09:54.0  lat: 50.0263766  lon: 7.7035243  h: 7758.33   dir:  14.3  hV: 17.77  vV: -2.91  (ID:314377) 
[103] 2015-03-17 13:09:55.0  lat: 50.0265190  lon: 7.7035243  h: 7758.33   dir:  14.3  hV: 17.77  vV: -2.91 
[104] 2015-03-17 13:09:56.0  lat: 50.0266437  lon: 7.7036882  h: 7751.58   dir:  32.0  hV: 15.49  vV: -3.21 
[105] 2015-03-17 13:09:57.0  lat: 50.0267637  lon: 7.7038221  h: 7748.31   dir:  38.6  hV: 17.80  vV: -3.20 
[106] 2015-03-17 13:09:58.0  lat: 50.0268982  lon: 7.7039888  h: 7744.80   dir:  38.2  hV: 20.83  vV: -3.03  (ID:314377) 
[107] 2015-03-17 13:09:59.0  lat: 50.0270588  lon: 7.7041688  h: 7741.81   dir:  34.0  hV: 23.10  vV: -3.05 
[108] 2015-03-17 13:10:00.0  lat: 50.0272429  lon: 7.7043378  h: 7738.74   dir:  27.3  hV: 24.46  vV: -3.11 
[109] 2015-03-17 13:10:01.0  lat: 50.0274422  lon: 7.7044741  h: 7735.21   dir:  20.1  hV: 24.08  vV: -3.33  (ID:314377) 
[110] 2015-03-17 13:10:02.0  lat: 50.0276412  lon: 7.7045709  h: 7731.91   dir:  14.7  hV: 22.36  vV: -3.25 
[111] 2015-03-17 13:10:03.0  lat: 50.0278256  lon: 7.7046368  h: 7728.77   dir:  11.8  hV: 19.88  vV: -3.16 
[112] 2015-03-17 13:10:05.0  lat: 50.0281218  lon: 7.7047649  h: 7723.05   dir:  23.9  hV: 15.49  vV: -2.95  (ID:314377) 
[114] 2015-03-17 13:10:07.0  lat: 50.0283693  lon: 7.7050185  h: 7716.84   dir:  38.6  hV: 18.67  vV: -3.14 
*/

