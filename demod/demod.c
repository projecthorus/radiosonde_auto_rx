
/*
 *  sync header: correlation/matched filter
 *  compile:
 *      gcc -c demod.c
 *
 *  author: zilog80
 */

/* ------------------------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;
typedef short i16_t;
typedef int   i32_t;

//#include "demod.h"


static unsigned int sample_in, sample_out, delay;
static int buffered = 0;

static int N, M;

static float *match = NULL,
             *bufs  = NULL,
             *corrbuf = NULL;

static char *rawbits = NULL;

static int Nvar = 0; // < M
static double xsum=0, qsum=0;
static float *xs = NULL,
             *qs = NULL;


/* ------------------------------------------------------------------------------------ */


static int sample_rate = 0, bits_sample = 0, channels = 0;
static float samples_per_bit = 0;

static int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}

float read_wav_header(FILE *fp, float baudrate) {
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

    samples_per_bit = sample_rate/baudrate;

    fprintf(stderr, "samples/bit: %.2f\n", samples_per_bit);

    return samples_per_bit;
}

static int f32read_sample(FILE *fp, float *s) {
    int i;
    short b = 0;

    for (i = 0; i < channels; i++) {

        if (fread( &b, bits_sample/8, 1, fp) != 1) return EOF;

        if (i == 0) {  // i = 0: links bzw. mono
            //if (bits_sample ==  8)  sint = b-128;   // 8bit: 00..FF, centerpoint 0x80=128
            //if (bits_sample == 16)  sint = (short)b;

            if (bits_sample ==  8) { b -= 128; }
            *s = b/128.0;
            if (bits_sample == 16) { *s /= 256.0; }
        }
    }

    return 0;
}

float get_bufvar(int ofs) {
    float mu  = xs[(sample_out+M + ofs) % M]/Nvar;
    float var = qs[(sample_out+M + ofs) % M]/Nvar - mu*mu;
    return var;
}

int getmaxCorr(float *maxv, unsigned int *maxvpos, int len) {
// In: current Max: maxv at maxvpos
// Out: Max
// Maximum im Intervall [sample_out-slen, sample_out-1]
// Randwerte zaehlen nicht als Extremwerte;
// nur neu berechnen, wenn neue Werte groesser als altes Max
    int slen, pos;
    unsigned int mpos=0;
    float m, s0, s, s1;

    int posIn = 0; // -1..0..1; // rs41:0
    float S_neu = corrbuf[(sample_out+M+posIn) % M];
    float S_vor = corrbuf[(sample_out+M+posIn-1) % M];

    if (sample_in < delay) return 0;

    slen = len*samples_per_bit;

    if (slen > M) slen = M;

    if ( (sample_out - *maxvpos >= slen-4) ||
         (sample_out - *maxvpos < slen  &&  *maxv <= S_vor && S_vor >= S_neu) )
    {
        m = -1.0;
        for (pos = 1; pos < slen+posIn; pos++) {
            s0 = corrbuf[(sample_out + 2*M - slen + pos-1) % M];
            s  = corrbuf[(sample_out + 2*M - slen + pos  ) % M];
            s1 = corrbuf[(sample_out + 2*M - slen + pos+1) % M];
            if (s > m && s>=s0 && s>=s1) {
                m = s;
                mpos = sample_out - slen + pos;
            }
        }
        *maxv = m;
        *maxvpos = mpos;
    }

    buffered = sample_out-mpos;

    return -buffered;
}

int f32buf_sample(FILE *fp, int inv, int cm) {
    static unsigned int sample_in0;
    int i;
    float s = 0.0;
    float xneu, xalt,
          corr  = 0.0,
          norm2 = 0.0;


    if (f32read_sample(fp, &s) == EOF) return EOF;

    if (inv) s = -s;
    bufs[sample_in % M] = s;

    xneu = bufs[(sample_in  ) % M];
    xalt = bufs[(sample_in+M - Nvar) % M];
    xsum +=  xneu - xalt;                 // + xneu - xalt
    qsum += (xneu - xalt)*(xneu + xalt);  // + xneu*xneu - xalt*xalt
    xs[sample_in % M] = xsum;
    qs[sample_in % M] = qsum;


    if (cm) {
        if (sample_in > sample_in0+1 || sample_in <= sample_in0) {
            for (i = 0; i < M; i++) corrbuf[i] = 0.0; // -1.0
        }
        for (i = 0; i < N; i++) {
            corr += match[i]*bufs[(sample_in+M -(N-1) + i) % M];
        }
        norm2 = qsum; //=qs[(sample_in) % M]; // N=Nvar ;  N>Nvar, approx: norm2 *= N/(float)Nvar
            //norm2 = qs[(sample_in+M - Nvar) % M] + qs[(sample_in) % M]; // N=2*Nvar
            //for (i = 0; i < N; i+=Nvar) norm2 += qs[(sample_in+M - i) % M]; // N=k*Nvar
        corrbuf[sample_in % M] = corr/sqrt(norm2);
        sample_in0 = sample_in;
    }


    sample_out = sample_in - delay;

    sample_in += 1;

    return 0;
}

static int read_bufbit(int symlen, char *bits, unsigned int mvp, int reset) {
// symlen==2: manchester2 0->10,1->01->1: 2.bit

    static unsigned int rcount;
    static float rbitgrenze;

    double sum = 0.0;

    if (reset) {
        rcount = 0;
        rbitgrenze = 0;
    }


    rbitgrenze += samples_per_bit;
    do {
        sum += bufs[(rcount + mvp + M) % M];
        rcount++;
    } while (rcount < rbitgrenze);  // n < samples_per_bit

    if (symlen == 2) {
        rbitgrenze += samples_per_bit;
        do {
            sum -= bufs[(rcount + mvp + M) % M];
            rcount++;
        } while (rcount < rbitgrenze);  // n < samples_per_bit
    }


    if (symlen != 2) {
        if (sum >= 0) *bits = '1';
        else          *bits = '0';
    }
    else {
        if (sum >= 0) strncpy(bits, "10", 2);
        else          strncpy(bits, "01", 2);
    }

    return 0;
}

int headcmp(int symlen, char *hdr, int len, unsigned int mvp) {
    int errs = 0;
    int pos;
    int step = 1;
    if (symlen != 1) step = 2;

    for (pos = 0; pos < len; pos += step) {
        read_bufbit(symlen, rawbits+pos, mvp+1-(int)(len*samples_per_bit), pos==0);
    }
    rawbits[pos] = '\0';

    while (len > 0) {
        if (rawbits[len-1] != hdr[len-1]) errs += 1;
        len--;
    }

    return errs;
}

/* -------------------------------------------------------------------------- */

int read_sbit(FILE *fp, int symlen, int *bit, int inv, int ofs, int reset, int cm) {
// symlen==2: manchester2 10->0,01->1: 2.bit

    static double bitgrenze;
    static unsigned long scount;

    float sample;

    double sum = 0.0;

    if (reset) {
        scount = 0;
        bitgrenze = 0;
    }

    if (symlen == 2) {
        bitgrenze += samples_per_bit;
        do {
            if (buffered > 0) buffered -= 1;
            else if (f32buf_sample(fp, inv, cm) == EOF) return EOF;

            sample = bufs[(sample_out-buffered + ofs + M) % M];
            sum -= sample;

            scount++;
        } while (scount < bitgrenze);  // n < samples_per_bit
    }

    bitgrenze += samples_per_bit;
    do {
        if (buffered > 0) buffered -= 1;
        else if (f32buf_sample(fp, inv, cm) == EOF) return EOF;

        sample = bufs[(sample_out-buffered + ofs + M) % M];
        sum += sample;

        scount++;
    } while (scount < bitgrenze);  // n < samples_per_bit

    if (sum >= 0) *bit = 1;
    else          *bit = 0;

    return 0;
}

/* -------------------------------------------------------------------------- */

static double norm2_match() {
    int i;
    double x, y = 0.0;
    for (i = 0; i < N; i++) {
        x = match[i];
        y += x*x;
    }
    return y;
}

int init_buffers(char hdr[], int hLen, int shape) {
    //hLen = strlen(header) = HEADLEN;

    int i, pos;
    float b, x;
    float normMatch;

    float alpha, sqalp, a = 1.0;


    N = hLen * samples_per_bit;
    M = 2*N; // >= N
    Nvar = N; //N/2; // = N/k

    bufs  = (float *)calloc( M+1, sizeof(float)); if (bufs  == NULL) return -100;
    match = (float *)calloc( N+1, sizeof(float)); if (match == NULL) return -100;
    corrbuf = (float *)calloc( M+1, sizeof(float)); if (corrbuf == NULL) return -100;

    xs = (float *)calloc( M+1, sizeof(float)); if (xs == NULL) return -100;
    qs = (float *)calloc( M+1, sizeof(float)); if (qs == NULL) return -100;


    rawbits = (char *)calloc( N+1, sizeof(char)); if (rawbits == NULL) return -100;

    for (i = 0; i < M; i++) bufs[i] = 0.0;
    for (i = 0; i < M; i++) corrbuf[i] = 0.0;

    alpha = exp(0.8);
    sqalp = sqrt(alpha/M_PI);
    //a = sqalp;

    for (i = 0; i < N; i++) {
        pos = i/samples_per_bit;
        x = (i - pos*samples_per_bit)*2.0/samples_per_bit - 1;
        a = sqalp;

        if (   ( pos < hLen-1 &&  hdr[pos]!=hdr[pos+1]  &&  x > 0.0 )
            || ( pos >  0     &&  hdr[pos-1]!=hdr[pos]  &&  x < 0.0 ) )  // x=0: a=sqalp
        {
            switch (shape) {
                case  1: if ( fabs(x) > 0.6 ) a *= (1 - fabs(x))/0.6;
                         break;
                case  2: a = sqalp * exp(-alpha*x*x);
                         break;
                case  3: a = 1 - fabs( x );
                         break;
                default: a = sqalp;
                         if (i-pos*samples_per_bit < 2 ||
                             i-pos*samples_per_bit > samples_per_bit-2) a = 0.8*sqalp;
            }
        }

        b = ((hdr[pos] & 0x1) - 0.5)*2.0; // {-1,+1}
        b *= a;

        match[i] = b;
    }

    normMatch = sqrt(norm2_match());
    for (i = 0; i < N; i++) {
        match[i] /= normMatch;
    }


    delay = N/4;
    sample_in = 0;

    return 0;
}

int free_buffers() {

    if (match) { free(match); match = NULL; }
    if (bufs)  { free(bufs);  bufs  = NULL; }
    if (xs)  { free(xs);  xs  = NULL; }
    if (qs)  { free(qs);  qs  = NULL; }
    if (corrbuf) { free(corrbuf); corrbuf = NULL; }
    if (rawbits) { free(rawbits); rawbits = NULL; }

    return 0;
}

/* ------------------------------------------------------------------------------------ */

unsigned int get_sample() {
    return sample_out;
}

