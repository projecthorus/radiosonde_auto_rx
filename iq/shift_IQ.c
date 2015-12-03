
/*
 *  ./shift IQ.wav [freq] [> IQ_translated.wav]  ## translate
 *  ./shift IQ.wav - [> IQ_swap.wav]             ## swap IQ
 */

/*
    sdr#<rev1381: sdrsharpIQ.wav = conj(rtlsdrIQ.wav)  ->  rtlsdrIQ.wav: swapIQ
*/
/*
    time domain                       frequency domain
    w(t) = exp(t*i*2pi*f0)*z(t)  <->  W(f) = Z(f-f0)

    z = x + iy
    w = u + iv = exp(ia)*z
    u = x*cos(a) - y*sin(a);
    v = x*sin(a) + y*cos(a);
    
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>


#define  PI  (3.1415926535897932384626433832795)

int sample_rate, bits_sample;


int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}
int read_IQwavheader(FILE *fp, int *sample_rate, int *bits_sample, FILE *fout) {
    int channels = 0;
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;

    if (fread(txt, 1, 4, fp) < 4) return -1; else fwrite(txt, 1, 4, fout);
    if (strncmp(txt, "RIFF", 4)) return -1;

    // pos_WAVE = 8L
    if (fread(txt, 1, 4, fp) < 4) return -1; else fwrite(txt, 1, 4, fout);
    if (fread(txt, 1, 4, fp) < 4) return -1; else fwrite(txt, 1, 4, fout);
    if (strncmp(txt, "WAVE", 4)) return -1;

    // pos_fmt = 12L
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1; else fprintf(fout, "%c", byte & 0xFF);
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "fmt ", p) == 4) break;
    }
    
    if (fread(dat, 1, 4, fp) < 4) return -1; else fwrite(dat, 1, 4, fout);
    if (fread(dat, 1, 2, fp) < 2) return -1; else fwrite(dat, 1, 2, fout);

    if (fread(dat, 1, 2, fp) < 2) return -1; else fwrite(dat, 1, 2, fout);
    channels = dat[0] + (dat[1] << 8);

    if (fread(dat, 1, 4, fp) < 4) return -1; else fwrite(dat, 1, 4, fout);
    *sample_rate = dat[0] | (dat[1] << 8) | (dat[2] << 16) | (dat[3] << 24); //memcpy(&sr, dat, 4);

    if (fread(dat, 1, 4, fp) < 4) return -1; else fwrite(dat, 1, 4, fout);
    if (fread(dat, 1, 2, fp) < 2) return -1; else fwrite(dat, 1, 2, fout);

    if (fread(dat, 1, 2, fp) < 2) return -1; else fwrite(dat, 1, 2, fout);
    *bits_sample = dat[0] + (dat[1] << 8);

    // pos_dat = 36L + info
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1; else fprintf(fout, "%c", byte & 0xFF);
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "data", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1; else fwrite(dat, 1, 4, fout);


    fprintf(stderr, "sample_rate: %d\n", *sample_rate);
    fprintf(stderr, "bits       : %d\n", *bits_sample);
    fprintf(stderr, "channels   : %d\n", channels);

    if ((*bits_sample != 8) && (*bits_sample != 16)) return -2;
    if (channels != 2) return -3;

    return 0;
}

int read_csample(FILE *fp, double complex *z) {
    short x, y;

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

    if (bits_sample == 8) {
        w += 128 + I*128;
    }

    u = creal(w);
    v = cimag(w);
                                        // 16 bit  (short)  ->  (int)
    fwrite( &u, bits_sample/8, 1, fp);  // +  0000 .. 7FFF  ->  0000 0000 .. 0000 7FFF
    fwrite( &v, bits_sample/8, 1, fp);  // -  8000 .. FFFF  ->  FFFF 8000 .. FFFF FFFF

    return 0;

}


int main(int argc, char *argv[]) {
    FILE *fp, *fout;
    int sample = 0;
    double t, f = 0;
    double complex  z, w;
    int swap = 0;

    if (argv[1]) {
        
        fp = fopen(argv[1], "rb");
        if (fp == NULL) return -1;

        fout = stdout; //fopen("tmp_out.wav", "wb");
        if (read_IQwavheader(fp, &sample_rate, &bits_sample, fout) != 0) {
            fprintf(stderr, "error: wav header\n");
            return -1;
        }

        if (argv[2]) {
            if (strcmp(argv[2], "-") == 0) swap = 1;
            else f = (double)atoi(argv[2]);  // f0
        }

        sample = 0;
        for ( ; ; ) {

            if (read_csample(fp, &z) == EOF) break;

            if (swap) { // swap IQ
                w = cimag(z) + I*creal(z);  // = cexp(I*PI/2) * conj(z) = I * conj(z);
                // W(f) = Z(-f); fuer Spiegelung reicht: w = conj(z)
            }
            else { // translate
                t = (double)sample / sample_rate;
                w = cexp(t*2*PI*f*I) * z;
            }

            write_csample(fout, w);
            sample++;

        }

        fclose(fp);
        fclose(fout);
    }

    return 0;
}

