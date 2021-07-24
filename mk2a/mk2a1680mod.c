
/*
   Sippican MkIIa
   LMS-6 (1680 MHz)
        (modulation index h = 10..10.5 (deviation +/- 50kHz))
        gcc -Ofast mk2a1680mod.c -lm -o mk2mod
        ./mk2mod -v --iq <fq> --lpIQ --lpFM --crc iq_base.wav
        # default IQ lowpass 180k
        # sr=375k: lpbw=145k..165k
        # sr=185k: lpbw=155k..175k
        ./mk2mod -v --iq <fq> --lpIQ --lpbw 160 --lpFM --crc iq_base.wav
        # frequency correction / tracking: --dc
        ./mk2mod -v --iq <fq> --lpIQ --lpbw 160 --lpFM --dc --crc iq_rfbase.wav
        ./mk2mod -v --iq0 --lpIQ --lpbw 160 --lpFM --crc iq_if.wav
        ./mk2mod -v --lpFM --crc fm_audio.wav
        # FM decimation: --decFM
        ./mk2mod -v --iq <fq> --lpbw 160 --decFM --crc iq_base.wav
        ./mk2mod -vv --dc --iq <fq> --lpbw 160 --decFM --crc iq_base.wav
        # --IQ
        ./mk2mod -vv --IQ <fq> --crc iq_base.wav
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>

// optional JSON "version"
//  (a) set global
//      gcc -DVERSION_JSN [-I<inc_dir>] ...
#ifdef VERSION_JSN
  #include "version_jsn.h"
#endif
// or
//  (b) set local compiler option, e.g.
//      gcc -DVER_JSN_STR=\"0.0.2\" ...


/* ------------------------------------------------------------------------------------------------- */
// -------------------------------------------------------------------------------------------------
//#include "demod_mod_Lband.h"

#ifndef M_PI
    #define M_PI  (3.1415926535897932384626433832795)
#endif

typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;
typedef char  i8_t;
typedef short i16_t;
typedef int   i32_t;


typedef struct {
    int sr;       // sample_rate
    int LOG2N;
    int N;
    int N2;
    float *xn;
    float complex  *ew;
    float complex  *Fm;
    float complex  *X;
    float complex  *Z;
    float complex  *cx;
    float complex  *win; // float real
} dft_t;


typedef struct {
    FILE *fp;
    //
    int sr;       // sample_rate
    int bps;      // bits/sample
    int nch;      // channels
    int ch;       // select channel
    //
    int symlen;
    int symhd;
    float sps;    // samples per symbol
    float _spb;   // samples per bit
    float br;     // baud rate
    //
    ui32_t sample_in;
    ui32_t sample_out;
    ui32_t sample_fm;
    ui32_t delay;
    ui32_t sc;
    int buffered;
    int L;
    int M;
    int K;
    float *match;
    float *bufs;
    float mv;
    ui32_t mv_pos;
    //

    // IQ-data
    int opt_iq;
    int opt_iqdc;
    int N_IQBUF;
    float complex *rot_iqbuf;
    float complex F1sum;
    float complex F2sum;

    //
    char *rawbits;
    char *hdr;
    int hdrlen;

    //
    float BT; // bw/time (ISI)
    float h;  // modulation index

    // DFT
    dft_t DFT;

    // dc offset
    int opt_dc;
    int locked;
    double dc;
    double Df;
    double dDf;


    // decimate
    int opt_IFmin;
    int decM;
    ui32_t sr_base;
    ui32_t dectaps;
    ui32_t sample_dec;
    ui32_t lut_len;
    float complex *decXbuffer;
    float complex *decMbuf;
    float complex *ex; // exp_lut
    double xlt_fq;

    // IF: lowpass
    int opt_lp;
    int lpIQ_bw;
    float lpIQ_fbw;
    int lpIQtaps; // ui32_t
    float *ws_lpIQ0;
    float *ws_lpIQ1;
    float *ws_lpIQ;
    float complex *lpIQ_buf;

    // FM: lowpass
    int lpFM_bw;
    int lpFMtaps; // ui32_t
    float *ws_lpFM;
    float *lpFM_buf;
	float *fm_buffer;

    int opt_fmdec;
    int decFM;

} dsp_t;


typedef struct {
    int sr;       // sample_rate
    int bps;      // bits_sample  bits/sample
    int nch;      // channels
    int sel_ch;   // select wav channel
} pcm_t;


typedef struct {
    ui8_t hb;
    float sb;
} hsbit_t;


typedef struct {
    char *hdr;
    char *buf;
    float *sbuf;
    int len;
    int bufpos;
    float thb;
    float ths;
} hdb_t;

// -------------------------------------------------------------------------------------------------
// demod_mod_Lband.c

#define FM_DEC  4     // 2, 4
#define FM_GAIN (0.8)

static void raw_dft(dft_t *dft, float complex *Z) {
    int s, l, l2, i, j, k;
    float complex  w1, w2, T;

    j = 1;
    for (i = 1; i < dft->N; i++) {
        if (i < j) {
            T = Z[j-1];
            Z[j-1] = Z[i-1];
            Z[i-1] = T;
        }
        k = dft->N/2;
        while (k < j) {
            j = j - k;
            k = k/2;
        }
        j = j + k;
    }

    for (s = 0; s < dft->LOG2N; s++) {
        l2 = 1 << s;
        l  = l2 << 1;
        w1 = (float complex)1.0;
        w2 = dft->ew[s]; // cexp(-I*M_PI/(float)l2)
        for (j = 1; j <= l2; j++) {
            for (i = j; i <= dft->N; i += l) {
                k = i + l2;
                T = Z[k-1] * w1;
                Z[k-1] = Z[i-1] - T;
                Z[i-1] = Z[i-1] + T;
            }
            w1 = w1 * w2;
        }
    }
}

static void cdft(dft_t *dft, float complex *z, float complex *Z) {
    int i;
    for (i = 0; i < dft->N; i++)  Z[i] = z[i];
    raw_dft(dft, Z);
}

static void rdft(dft_t *dft, float *x, float complex *Z) {
    int i;
    for (i = 0; i < dft->N; i++)  Z[i] = (float complex)x[i];
    raw_dft(dft, Z);
}

static void Nidft(dft_t *dft, float complex *Z, float complex *z) {
    int i;
    for (i = 0; i < dft->N; i++)  z[i] = conj(Z[i]);
    raw_dft(dft, z);
    // idft():
    // for (i = 0; i < dft->N; i++)  z[i] = conj(z[i])/(float)dft->N; // hier: z reell
}

static float bin2freq0(dft_t *dft, int k) {
    float fq = dft->sr * k / /*(float)*/dft->N;
    if (fq >= dft->sr/2.0) fq -= dft->sr;
    return fq;
}
static float bin2freq(dft_t *dft, int k) {
    float fq = k / (float)dft->N;
    if ( fq >= 0.5) fq -= 1.0;
    return fq*dft->sr;
}
static float bin2fq(dft_t *dft, int k) {
    float fq = k / (float)dft->N;
    if ( fq >= 0.5) fq -= 1.0;
    return fq;
}

static int max_bin(dft_t *dft, float complex *Z) {
    int k, kmax;
    double max;

    max = 0; kmax = 0;
    for (k = 0; k < dft->N; k++) {
        if (cabs(Z[k]) > max) {
            max = cabs(Z[k]);
            kmax = k;
        }
    }

    return kmax;
}

static int dft_window(dft_t *dft, int w) {
    int n;

    if (w < 0 || w > 3) return -1;

    for (n = 0; n < dft->N2; n++) {
        switch (w)
        {
            case 0: // (boxcar)
                    dft->win[n] = 1.0;
                    break;
            case 1: // Hann
                    dft->win[n] = 0.5 * ( 1.0 - cos(2*M_PI*n/(float)(dft->N2-1)) );
                    break ;
            case 2: // Hamming
                    dft->win[n] = 25/46.0 - (1.0 - 25/46.0)*cos(2*M_PI*n / (float)(dft->N2-1));
                    break ;
            case 3: // Blackmann
                    dft->win[n] =  7938/18608.0
                                 - 9240/18608.0*cos(2*M_PI*n / (float)(dft->N2-1))
                                 + 1430/18608.0*cos(4*M_PI*n / (float)(dft->N2-1));
                    break ;
        }
    }
    while (n < dft->N) dft->win[n++] = 0.0;

    return 0;
}


/* ------------------------------------------------------------------------------------ */

static int getCorrDFT(dsp_t *dsp) {
    int i;
    int mp = -1;
    float mx = 0.0;
    float mx2 = 0.0;
    float re_cx = 0.0;
    float xnorm = 1;
    ui32_t mpos = 0;
    ui32_t pos = dsp->sample_out;

    double dc = 0.0;
    int mp_ofs = 0;
    float *sbuf = dsp->bufs;

    dsp->mv = 0.0;
    dsp->dc = 0.0;

    if (dsp->K + dsp->L > dsp->DFT.N) return -1;
    if (dsp->sample_out < dsp->L) return -2;


    if (dsp->opt_iq > 1 && dsp->opt_iq < 6 && dsp->opt_dc) {
        mp_ofs = (dsp->sps-1)/2;
        sbuf = dsp->fm_buffer;
    }
    else {
        sbuf = dsp->bufs;
    }
    for (i = 0; i < dsp->K + dsp->L; i++) (dsp->DFT).xn[i] = sbuf[(pos+dsp->M -(dsp->K + dsp->L-1) + i) % dsp->M];
    while (i < dsp->DFT.N) (dsp->DFT).xn[i++] = 0.0;


    rdft(&dsp->DFT, dsp->DFT.xn, dsp->DFT.X);


    if (dsp->opt_dc) {
        //X[0] = 0; // nicht ueber gesamte Laenge ... M10
        //
        // L < K ?  // only last 2L samples (avoid M10 carrier offset)

        //dc = 0.0;
        //for (i = dsp->K - dsp->L; i < dsp->K + dsp->L; i++) dc += (dsp->DFT).xn[i];
        //dc /= 2.0*(float)dsp->L;

        dc = 0.0;
        for (i = dsp->K /*- dsp->L*/; i < dsp->K + dsp->L; i++) dc += (dsp->DFT).xn[i];
        dc /= 1.0*(float)dsp->L;

        dsp->DFT.X[0] -= dsp->DFT.N * dc  ;//* 0.95;
        Nidft(&dsp->DFT, dsp->DFT.X, (dsp->DFT).cx);
        for (i = 0; i < dsp->DFT.N; i++) (dsp->DFT).xn[i] = creal((dsp->DFT).cx[i])/(float)dsp->DFT.N;
    }

    for (i = 0; i < dsp->DFT.N; i++) dsp->DFT.Z[i] = dsp->DFT.X[i]*dsp->DFT.Fm[i];

    Nidft(&dsp->DFT, dsp->DFT.Z, dsp->DFT.cx);

    if (fabs(dc) < 0.5) dsp->dc = dc;


    // relativ Peak - Normierung erst zum Schluss;
    // dann jedoch nicht zwingend corr-Max wenn FM-Amplitude bzw. norm(x) nicht konstant
    // (z.B. rs41 Signal-Pausen). Moeglicherweise wird dann wahres corr-Max in dem
    //  K-Fenster nicht erkannt, deshalb K nicht zu gross waehlen.
    //
    mx2 = 0.0;                                      // t = L-1
    for (i = dsp->L-1; i < dsp->K + dsp->L; i++) {  // i=t .. i=t+K < t+1+K
        re_cx = creal(dsp->DFT.cx[i]);  // imag(cx)=0
        if (re_cx*re_cx > mx2) {
            mx = re_cx;
            mx2 = mx*mx;
            mp = i;
        }
    }
    if (mp == dsp->L-1 || mp == dsp->K + dsp->L-1) return -4; // Randwert
    //  mp == t           mp == K+t

    mpos = pos - (dsp->K + dsp->L-1) + mp; // t = L-1

    // header: mpos-L .. mpos (CA CA CA 24 52)
    // dc(header) ? -> Mk2a: 0xCA preamble, mpos-L .. mpos-2/5*L
    if (dsp->opt_dc)
    {
        dc = 0.0;
        //for (i = 0; i < dsp->L; i++) dc += sbuf[(mpos - i + dsp->M) % dsp->M];
        //dc /= (float)dsp->L; dc *= 0.8f;
        for (i = 2*dsp->L/5; i < dsp->L; i++) dc += sbuf[(mpos - i + dsp->M) % dsp->M];
        dc /= (float)dsp->L*3/5.0;
        dsp->dc = dc;
    }


    //xnorm = sqrt(dsp->qs[(mpos + 2*dsp->M) % dsp->M]); // Nvar = L
    xnorm = 0.0;
    for (i = 0; i < dsp->L; i++) xnorm += (dsp->DFT).xn[mp-i]*(dsp->DFT).xn[mp-i];
    xnorm = sqrt(xnorm);

    mx /= xnorm*(dsp->DFT).N;

    if (dsp->opt_iq > 1 && dsp->opt_iq < 6 && dsp->opt_dc) mpos += mp_ofs;

    dsp->mv = mx;
    dsp->mv_pos = mpos;

    if (pos == dsp->sample_out) dsp->buffered = dsp->sample_out - mpos;

// FM: s = gain * carg(w)/M_PI = gain * dphi / PI // gain=0.8
// FM audio gain? dc relative to FM-envelope?!
//
    dsp->dDf = dsp->sr * dsp->dc / (2.0*FM_GAIN);  // remaining freq offset

    return mp;
}

/* ------------------------------------------------------------------------------------ */

static int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}

static
float read_wav_header(pcm_t *pcm, FILE *fp) {
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;
    int sample_rate = 0, bits_sample = 0, channels = 0;

    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "RIFF", 4) && strncmp(txt, "RF64", 4)) return -1;

    if (fread(txt, 1, 4, fp) < 4) return -1;
    // pos_WAVE = 8L
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "WAVE", 4))  return -1;

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

    if (pcm->sel_ch < 0  ||  pcm->sel_ch >= channels) pcm->sel_ch = 0; // default channel: 0
    //fprintf(stderr, "channel-In : %d\n", pcm->sel_ch+1); // nur wenn nicht IQ

    if (bits_sample != 8 && bits_sample != 16 && bits_sample != 32) return -1;

    if (sample_rate == 900001) sample_rate -= 1;

    pcm->sr  = sample_rate;
    pcm->bps = bits_sample;
    pcm->nch = channels;

    return 0;
}


static int f32read_sample(dsp_t *dsp, float *s) {
    int i;
    unsigned int word = 0;
    short *b = (short*)&word;
    float *f = (float*)&word;

    for (i = 0; i < dsp->nch; i++) {

        if (fread( &word, dsp->bps/8, 1, dsp->fp) != 1) return EOF;

        if (i == dsp->ch) {  // i = 0: links bzw. mono
            //if (bits_sample ==  8)  sint = b-128;   // 8bit: 00..FF, centerpoint 0x80=128
            //if (bits_sample == 16)  sint = (short)b;

            if (dsp->bps == 32) {
                *s = *f;
            }
            else {
                if (dsp->bps ==  8) { *b -= 128; }
                *s = *b/128.0;
                if (dsp->bps == 16) { *s /= 256.0; }
            }
        }
    }

    return 0;
}

typedef struct {
    double sumIQx;
    double sumIQy;
    float avgIQx;
    float avgIQy;
    float complex avgIQ;
    ui32_t cnt;
    ui32_t maxcnt;
    ui32_t maxlim;
} iq_dc_t;
static iq_dc_t IQdc;

static int f32read_csample(dsp_t *dsp, float complex *z) {

    float x, y;

    if (dsp->bps == 32) { //float32
        float f[2];
        if (fread( f, dsp->bps/8, 2, dsp->fp) != 2) return EOF;
        x = f[0];
        y = f[1];
    }
    else if (dsp->bps == 16) { //int16
        short b[2];
        if (fread( b, dsp->bps/8, 2, dsp->fp) != 2) return EOF;
        x = b[0]/32768.0;
        y = b[1]/32768.0;
    }
    else {  // dsp->bps == 8   //uint8
        ui8_t u[2];
        if (fread( u, dsp->bps/8, 2, dsp->fp) != 2) return EOF;
        x = (u[0]-128)/128.0;
        y = (u[1]-128)/128.0;
    }

    *z = x + I*y;

    // IQ-dc removal optional
    if (dsp->opt_iqdc) {
        *z -= IQdc.avgIQ;

        IQdc.sumIQx += x;
        IQdc.sumIQy += y;
        IQdc.cnt += 1;
        if (IQdc.cnt == IQdc.maxcnt) {
            IQdc.avgIQx = IQdc.sumIQx/(float)IQdc.maxcnt;
            IQdc.avgIQy = IQdc.sumIQy/(float)IQdc.maxcnt;
            IQdc.avgIQ  = IQdc.avgIQx + I*IQdc.avgIQy;
            IQdc.sumIQx = 0; IQdc.sumIQy = 0; IQdc.cnt = 0;
            if (IQdc.maxcnt < IQdc.maxlim) IQdc.maxcnt *= 2;
        }
    }

    return 0;
}

static int f32read_cblock(dsp_t *dsp) {

    int n;
    int len;
    float x, y;
    ui8_t s[4*2*dsp->decM]; //uin8,int16,flot32
    ui8_t *u = (ui8_t*)s;
    short *b = (short*)s;
    float *f = (float*)s;


    len = fread( s, dsp->bps/8, 2*dsp->decM, dsp->fp) / 2;

    //for (n = 0; n < len; n++) dsp->decMbuf[n] = (u[2*n]-128)/128.0 + I*(u[2*n+1]-128)/128.0;
    // u8: 0..255, 128 -> 0V
    for (n = 0; n < len; n++) {
        if (dsp->bps == 8) { //uint8
            x = (u[2*n  ]-128)/128.0;
            y = (u[2*n+1]-128)/128.0;
        }
        else if (dsp->bps == 16) { //int16
            x = b[2*n  ]/32768.0;
            y = b[2*n+1]/32768.0;
        }
        else { // dsp->bps == 32   //float32
            x = f[2*n];
            y = f[2*n+1];
        }

        // baseband: IQ-dc removal mandatory
        dsp->decMbuf[n] = (x-IQdc.avgIQx) + I*(y-IQdc.avgIQy);

        IQdc.sumIQx += x;
        IQdc.sumIQy += y;
        IQdc.cnt += 1;
        if (IQdc.cnt == IQdc.maxcnt) {
            IQdc.avgIQx = IQdc.sumIQx/(float)IQdc.maxcnt;
            IQdc.avgIQy = IQdc.sumIQy/(float)IQdc.maxcnt;
            IQdc.avgIQ  = IQdc.avgIQx + I*IQdc.avgIQy;
            IQdc.sumIQx = 0; IQdc.sumIQy = 0; IQdc.cnt = 0;
            if (IQdc.maxcnt < IQdc.maxlim) IQdc.maxcnt *= 2;
        }
    }

    return len;
}


// decimate lowpass
static float *ws_dec;

static double sinc(double x) {
    double y;
    if (x == 0) y = 1;
    else y = sin(M_PI*x)/(M_PI*x);
    return y;
}

static int lowpass_init(float f, int taps, float **pws) {
    double *h, *w;
    double norm = 0;
    int n;
    float *ws = NULL;

    if (taps % 2 == 0) taps++; // odd/symmetric

    if ( taps < 1 ) taps = 1;

    h = (double*)calloc( taps+1, sizeof(double)); if (h == NULL) return -1;
    w = (double*)calloc( taps+1, sizeof(double)); if (w == NULL) return -1;
    ws = (float*)calloc( 2*taps+1, sizeof(float)); if (ws == NULL) return -1;

    for (n = 0; n < taps; n++) {
        w[n] = 7938/18608.0 - 9240/18608.0*cos(2*M_PI*n/(taps-1)) + 1430/18608.0*cos(4*M_PI*n/(taps-1)); // Blackmann
        h[n] = 2*f*sinc(2*f*(n-(taps-1)/2));
        ws[n] = w[n]*h[n];
        norm += ws[n]; // 1-norm
    }
    for (n = 0; n < taps; n++) {
        ws[n] /= norm; // 1-norm
    }

    for (n = 0; n < taps; n++) ws[taps+n] = ws[n]; // duplicate/unwrap

    *pws = ws;

    free(h); h = NULL;
    free(w); w = NULL;

    return taps;
}


static int lowpass_update(float f, int taps, float *ws) {
    double *h, *w;
    double norm = 0;
    int n;

    if (taps % 2 == 0) taps++; // odd/symmetric

    if ( taps < 1 ) taps = 1;

    h = (double*)calloc( taps+1, sizeof(double)); if (h == NULL) return -1;
    w = (double*)calloc( taps+1, sizeof(double)); if (w == NULL) return -1;

    for (n = 0; n < taps; n++) {
        w[n] = 7938/18608.0 - 9240/18608.0*cos(2*M_PI*n/(taps-1)) + 1430/18608.0*cos(4*M_PI*n/(taps-1)); // Blackmann
        h[n] = 2*f*sinc(2*f*(n-(taps-1)/2));
        ws[n] = w[n]*h[n];
        norm += ws[n]; // 1-norm
    }
    for (n = 0; n < taps; n++) {
        ws[n] /= norm; // 1-norm
    }

    for (n = 0; n < taps; n++) ws[taps+n] = ws[n];

    free(h); h = NULL;
    free(w); w = NULL;

    return taps;
}

static float complex lowpass(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    ui32_t n;
    ui32_t s = sample % taps;
    double complex w = 0;
    for (n = 0; n < taps; n++) {
        w += buffer[n]*ws[taps+s-n]; // ws[taps+s-n] = ws[(taps+sample-n)%taps]
    }
    return (float complex)w;
// symmetry: ws[n] == ws[taps-1-n]
}

static float re_lowpass(float buffer[], ui32_t sample, ui32_t taps, float *ws) {
    ui32_t n;
    ui32_t s = sample % taps;
    double w = 0;
    for (n = 0; n < taps; n++) {
        w += buffer[n]*ws[taps+s-n]; // ws[taps+s-n] = ws[(taps+sample-n)%taps]
    }
    return (float)w;
}


static
int f32buf_sample(dsp_t *dsp, int inv) {
    float s = 0.0;

    float complex z, w, z0;
    double gain = FM_GAIN;

    ui32_t decFM = 1;
    ui32_t _sample = dsp->sample_in;
    int m = 0;

    if (dsp->opt_fmdec) {
        decFM = dsp->decFM;
        _sample = dsp->sample_in * decFM;
    }

    for (m = 0; m < decFM; m++)
    {
        double t = _sample / (double)dsp->sr;

        if (dsp->opt_iq) {

            if (dsp->opt_iq >= 5) {
                ui32_t s_reset = dsp->dectaps*dsp->lut_len;
                int j;
                if ( f32read_cblock(dsp) < dsp->decM ) return EOF;
                for (j = 0; j < dsp->decM; j++) {
                    z = dsp->decMbuf[j] * dsp->ex[dsp->sample_dec % dsp->lut_len];
                    dsp->decXbuffer[dsp->sample_dec % dsp->dectaps] = z;
                    dsp->sample_dec += 1;
                    if (dsp->sample_dec == s_reset) dsp->sample_dec = 0;
                }
                if (dsp->decM > 1)
                {
                    z = lowpass(dsp->decXbuffer, dsp->sample_dec, dsp->dectaps, ws_dec);
                }
            }
            else if ( f32read_csample(dsp, &z) == EOF ) return EOF;

            z *= cexp(-t*2*M_PI*dsp->Df*I);


            // IF-lowpass
            if (dsp->opt_lp & 1) {
                dsp->lpIQ_buf[_sample % dsp->lpIQtaps] = z;
                z = lowpass(dsp->lpIQ_buf, _sample, dsp->lpIQtaps, dsp->ws_lpIQ);
            }


            z0 = dsp->rot_iqbuf[(_sample-1 + dsp->N_IQBUF) % dsp->N_IQBUF];
            w = z * conj(z0);
            s = gain * carg(w)/M_PI;

            dsp->rot_iqbuf[_sample % dsp->N_IQBUF] = z;


            // FM-lowpass
            if (dsp->opt_lp & 2) {
                dsp->lpFM_buf[_sample % dsp->lpFMtaps] = s;
                if (m+1 == decFM) {
                    s = re_lowpass(dsp->lpFM_buf, _sample, dsp->lpFMtaps, dsp->ws_lpFM);
                }
            }

            dsp->fm_buffer[(_sample - dsp->lpFMtaps/2 + dsp->M) % dsp->M] = s;


            if (0 && dsp->opt_iq >= 2 && dsp->opt_iq < 6)
            {
                double xbit = 0.0;
                //float complex xi = cexp(+I*M_PI*dsp->h/dsp->sps);
                double f1 = -dsp->h*dsp->sr/(2*dsp->sps);
                double f2 = -f1;

                float complex X0 = 0;
                float complex X  = 0;

                int n = dsp->sps;
                double tn = (_sample-n) / (double)dsp->sr;
                //t = _sample / (double)dsp->sr;
                //z = dsp->rot_iqbuf[_sample % dsp->N_IQBUF];
                z0 = dsp->rot_iqbuf[(_sample-n + dsp->N_IQBUF) % dsp->N_IQBUF];

                // f1
                X0 = z0 * cexp(-tn*2*M_PI*f1*I); // alt
                X  = z  * cexp(-t *2*M_PI*f1*I); // neu
                dsp->F1sum +=  X - X0;

                // f2
                X0 = z0 * cexp(-tn*2*M_PI*f2*I); // alt
                X  = z  * cexp(-t *2*M_PI*f2*I); // neu
                dsp->F2sum +=  X - X0;

                xbit = cabs(dsp->F2sum) - cabs(dsp->F1sum);

                s = xbit / dsp->sps;
            }
            else if (dsp->opt_iq >= 2 && dsp->opt_iq < 6)
            {
                double xbit = 0.0;
                //float complex xi = cexp(+I*M_PI*dsp->h/dsp->sps);
                double f1 = -dsp->h*dsp->sr/(2*dsp->sps);
                double f2 = -f1;

                float complex X1 = 0;
                float complex X2 = 0;

                int n = dsp->sps;
                float sk = dsp->sps/2.4f;

                while (n > 0) {
                    n--;
                    if (n > sk && n < dsp->sps-sk)
                    {
                        t = -n / (double)dsp->sr;
                        z = dsp->rot_iqbuf[(dsp->sample_in - n + dsp->N_IQBUF) % dsp->N_IQBUF];  // +1
                        X1 += z*cexp(-t*2*M_PI*f1*I);
                        X2 += z*cexp(-t*2*M_PI*f2*I);
                    }
                }

                xbit = cabs(X2) - cabs(X1);

                s = xbit / dsp->sps;
            }
        }
        else {
            if (f32read_sample(dsp, &s) == EOF) return EOF;
        }

        _sample += 1;

    }

    if (inv) s = -s;
    dsp->bufs[dsp->sample_in % dsp->M] = s;

    dsp->sample_out = dsp->sample_in - dsp->delay;

    dsp->sample_in += 1;

    return 0;
}

static int read_bufbit(dsp_t *dsp, int symlen, char *bits, ui32_t mvp, int pos) {
// symlen==2: manchester2 0->10,1->01->1: 2.bit

    double rbitgrenze = pos*symlen*dsp->sps;
    ui32_t rcount = ceil(rbitgrenze);//+0.99; // dfm?

    double sum = 0.0;

    // bei symlen=2 (Manchester) kein dc noetig: -dc+dc=0 ;
    // allerdings M10-header mit symlen=1

    rbitgrenze += dsp->sps;
    do {
        sum += dsp->bufs[(rcount + mvp + dsp->M) % dsp->M] - dsp->dc;
        rcount++;
    } while (rcount < rbitgrenze);  // n < dsp->sps

    if (symlen == 2) {
        rbitgrenze += dsp->sps;
        do {
            sum -= dsp->bufs[(rcount + mvp + dsp->M) % dsp->M] - dsp->dc;
            rcount++;
        } while (rcount < rbitgrenze);  // n < dsp->sps
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

static int headcmp(dsp_t *dsp, int opt_dc) {
    int errs = 0;
    int pos;
    int step = 1;
    char sign = 0;
    int len = dsp->hdrlen/dsp->symhd;
    int inv = dsp->mv < 0;

    if (opt_dc == 0 || dsp->opt_iq > 1) dsp->dc = 0; // reset? e.g. 2nd pass

    if (dsp->symhd != 1) step = 2;
    if (inv) sign=1;

    for (pos = 0; pos < len; pos++) {                  // L = dsp->hdrlen * dsp->sps + 0.5;
        //read_bufbit(dsp, dsp->symhd, dsp->rawbits+pos*step, mvp+1-(int)(len*dsp->sps), pos);
        read_bufbit(dsp, dsp->symhd, dsp->rawbits+pos*step, dsp->mv_pos+1-dsp->L, pos);
    }
    dsp->rawbits[pos] = '\0';

    while (len > 0) {
        if ((dsp->rawbits[len-1]^sign) != dsp->hdr[len-1]) errs += 1;
        len--;
    }

    return errs;
}

/* -------------------------------------------------------------------------- */

static
int read_softbit2p(dsp_t *dsp, hsbit_t *shb, int inv, int ofs, int pos, float l, int spike, hsbit_t *shb1) {
// symlen==2: manchester2 10->0,01->1: 2.bit

    float sample, sample1;
    float avg;
    float ths = 0.5, scale = 0.27;

    double sum = 0.0, sum1 = 0.0;
    double mid;
    //double l = 1.0;

    double bg = pos*dsp->symlen*dsp->sps;

    double dc = 0.0;

    ui8_t bit = 0, bit1 = 0;


    if (dsp->opt_dc && dsp->opt_iq < 2) dc = dsp->dc;

    if (pos == 0) {
        bg = 0;
        dsp->sc = 0;
    }


    if (dsp->symlen == 2) {
        mid = bg + (dsp->sps-1)/2.0;
        bg += dsp->sps;
        do {
            if (dsp->buffered > 0) dsp->buffered -= 1;
            else if (f32buf_sample(dsp, inv) == EOF) return EOF;

            sample = dsp->bufs[(dsp->sample_out-dsp->buffered + ofs + dsp->M) % dsp->M];
            sample1 = dsp->bufs[(dsp->sample_out-dsp->buffered + ofs-1 + dsp->M) % dsp->M];
            if (spike && fabs(sample - avg) > ths) {
                avg = 0.5*(dsp->bufs[(dsp->sample_out-dsp->buffered-1 + ofs + dsp->M) % dsp->M]
                          +dsp->bufs[(dsp->sample_out-dsp->buffered+1 + ofs + dsp->M) % dsp->M]);
                sample = avg + scale*(sample - avg); // spikes
            }
            sample -= dc;
            sample1 -= dc;

            if (l < 0 || (mid-l < dsp->sc && dsp->sc < mid+l)) {
                sum -= sample;
                sum1 -= sample1;
            }

            dsp->sc++;
        } while (dsp->sc < bg);  // n < dsp->sps
    }

    mid = bg + (dsp->sps-1)/2.0;
    bg += dsp->sps;
    do {
        if (dsp->buffered > 0) dsp->buffered -= 1;
        else if (f32buf_sample(dsp, inv) == EOF) return EOF;

        sample = dsp->bufs[(dsp->sample_out-dsp->buffered + ofs + dsp->M) % dsp->M];
        sample1 = dsp->bufs[(dsp->sample_out-dsp->buffered + ofs-1 + dsp->M) % dsp->M];
        if (spike && fabs(sample - avg) > ths) {
            avg = 0.5*(dsp->bufs[(dsp->sample_out-dsp->buffered-1 + ofs + dsp->M) % dsp->M]
                      +dsp->bufs[(dsp->sample_out-dsp->buffered+1 + ofs + dsp->M) % dsp->M]);
            sample = avg + scale*(sample - avg); // spikes
        }
        sample -= dc;
        sample1 -= dc;

        if (l < 0 || (mid-l < dsp->sc && dsp->sc < mid+l)) {
            sum += sample;
            sum1 += sample1;
        }

        dsp->sc++;
    } while (dsp->sc < bg);  // n < dsp->sps


    if (sum >= 0) bit = 1;
    else          bit = 0;
    shb->hb = bit;
    shb->sb = (float)sum;

    if (sum1 >= 0) bit1 = 1;
    else           bit1 = 0;
    shb1->hb = bit1;
    shb1->sb = (float)sum1;

    return 0;
}

/* -------------------------------------------------------------------------- */

#define IF_SAMPLE_RATE      48000
#define IF_SAMPLE_RATE_MIN  32000

#define IF_TRANSITION_BW (8e3)  // (min) transition width
#define FM_TRANSITION_BW (2e3)  // (min) transition width

#define SQRT2 1.4142135624   // sqrt(2)
// sigma = sqrt(log(2)) / (2*PI*BT):
//#define SIGMA 0.2650103635   // BT=0.5: 0.2650103635 , BT=0.3: 0.4416839392

// Gaussian FM-pulse
static double Q(double x) {
    return 0.5 - 0.5*erf(x/SQRT2);
}
static double pulse(double t, double sigma) {
    return Q((t-0.5)/sigma) - Q((t+0.5)/sigma);
}


static double norm2_vect(float *vect, int n) {
    int i;
    double x, y = 0.0;
    for (i = 0; i < n; i++) {
        x = vect[i];
        y += x*x;
    }
    return y;
}

static
int init_buffers_Lband(dsp_t *dsp) {
    int Lscale = 4;
    int i, pos;
    float b0, b1, b2, b;
    float normMatch;
    double t;
    double sigma = sqrt(log(2)) / (2*M_PI*dsp->BT);

    int p2 = 1;
    int K, L, M;
    int n, k;
    float *m = NULL;


    if (dsp->opt_iq >= 5)
    {
        int IF_sr = IF_SAMPLE_RATE*Lscale; // designated IF sample rate
        int decM = 1; // decimate M:1
        int sr_base = dsp->sr;
        float f_lp; // dec_lowpass: lowpass_bandwidth/2
        float t_bw; // dec_lowpass: transition_bandwidth
        int taps; // dec_lowpass: taps

        if (dsp->opt_IFmin) IF_sr = IF_SAMPLE_RATE_MIN*Lscale;
        if (IF_sr > sr_base) IF_sr = sr_base;
        if (IF_sr < sr_base) {
            while (sr_base % IF_sr) IF_sr += 1;
            decM = sr_base / IF_sr;
        }

        f_lp = (IF_sr+60e3)/(4.0*sr_base);
        t_bw = (IF_sr-180e3)/*/2.0*/;
        if (dsp->opt_IFmin) {
            t_bw = (IF_sr-80e3);
        }
        if (t_bw < 0) t_bw = 160e3;
        t_bw /= sr_base;
        taps = 4.0/t_bw; if (taps%2==0) taps++;

        taps = lowpass_init(f_lp, taps, &ws_dec); // decimate lowpass
        if (taps < 0) return -1;
        dsp->dectaps = (ui32_t)taps;

        dsp->sr_base = sr_base;
        dsp->sr = IF_sr; // sr_base/decM
        dsp->sps /= (float)decM;
        dsp->_spb /= (float)decM;
        dsp->decM = decM;

        fprintf(stderr, "IF: %d\n", IF_sr);
        fprintf(stderr, "dec: %d\n", decM);
    }
    if (dsp->opt_iq >= 5)
    {
        // look up table, exp-rotation
        int W = 2*8; // 16 Hz window
        int d = 1; // 1..W , groesster Teiler d <= W von sr_base
        int freq = (int)( dsp->xlt_fq * (double)dsp->sr_base + 0.5);
        int freq0 = freq; // init
        double f0 = freq0 / (double)dsp->sr_base; // init

        for (d = W; d > 0; d--) { // groesster Teiler d <= W von sr
            if (dsp->sr_base % d == 0) break;
        }
        if (d == 0) d = 1; // d >= 1 ?

        for (k = 0; k < W/2; k++) {
            if ((freq+k) % d == 0) {
                freq0 = freq + k;
                break;
            }
            if ((freq-k) % d == 0) {
                freq0 = freq - k;
                break;
            }
        }

        dsp->lut_len = dsp->sr_base / d;
        f0 = freq0 / (double)dsp->sr_base;

        dsp->ex = calloc(dsp->lut_len+1, sizeof(float complex));
        if (dsp->ex == NULL) return -1;
        for (n = 0; n < dsp->lut_len; n++) {
            t = f0*(double)n;
            dsp->ex[n] = cexp(t*2*M_PI*I);
        }


        dsp->decXbuffer = calloc( dsp->dectaps+1, sizeof(float complex));
        if (dsp->decXbuffer == NULL) return -1;

        dsp->decMbuf = calloc( dsp->decM+1, sizeof(float complex));
        if (dsp->decMbuf == NULL) return -1;
    }

    // IQ lowpass
    if (dsp->opt_iq && (dsp->opt_lp & 1))
    {
        float f_lp; // lowpass_bw
        int taps; // lowpass taps: 4*sr/transition_bw

        // IF lowpass
        f_lp = 160e3/(float)dsp->sr/2.0; // default
        if (dsp->lpIQ_bw) f_lp = dsp->lpIQ_bw/(float)dsp->sr/2.0;
        taps = 4*dsp->sr/IF_TRANSITION_BW;
        if (dsp->sr > 100e3) taps = taps/2;
        if (dsp->sr > 200e3) taps = taps/2;
        if (taps%2==0) taps++;
        taps = lowpass_init(1.5*f_lp, taps, &dsp->ws_lpIQ0); if (taps < 0) return -1;
        taps = lowpass_init(f_lp, taps, &dsp->ws_lpIQ1); if (taps < 0) return -1;

        dsp->lpIQ_fbw = f_lp;
        dsp->lpIQtaps = taps;
        dsp->lpIQ_buf = calloc( dsp->lpIQtaps+3, sizeof(float complex));
        if (dsp->lpIQ_buf == NULL) return -1;

        dsp->ws_lpIQ = dsp->ws_lpIQ1;
        // dc-offset: if not centered, (acquisition) filter bw = lpIQ_bw + 4kHz
        // coarse acquisition:
        if (dsp->opt_dc) {
            dsp->locked = 0;
            dsp->ws_lpIQ = dsp->ws_lpIQ0;
            //taps = lowpass_update(1.5*dsp->lpIQ_fbw, dsp->lpIQtaps, dsp->ws_lpIQ); if (taps < 0) return -1;
        }
        // locked:
        //taps = lowpass_update(dsp->lpIQ_fbw, dsp->lpIQtaps, dsp->ws_lpIQ); if (taps < 0) return -1;
    }

    // FM lowpass
    if (dsp->opt_lp & 2)
    {
        float f_lp; // lowpass_bw
        int taps; // lowpass taps: 4*sr/transition_bw

        f_lp = 10e3/(float)dsp->sr; // default
        if (dsp->lpFM_bw > 0) f_lp = dsp->lpFM_bw/(float)dsp->sr;
        taps = 4*dsp->sr/FM_TRANSITION_BW;
        if (dsp->decFM > 1) {
            f_lp *= 2.0;
            taps = taps/2;
        }
        if (dsp->sr > 100e3) taps = taps/2;
        if (dsp->sr > 200e3) taps = taps/2;
        if (dsp->opt_iq == 5) taps = taps/2;
        if (taps%2==0) taps++;
        taps = lowpass_init(f_lp, taps, &dsp->ws_lpFM); if (taps < 0) return -1;

        dsp->lpFMtaps = taps;
        dsp->lpFM_buf = calloc( dsp->lpFMtaps+3, sizeof(float complex));
        if (dsp->lpFM_buf == NULL) return -1;
    }

    memset(&IQdc, 0, sizeof(IQdc));
    IQdc.maxlim = dsp->sr;
    IQdc.maxcnt = IQdc.maxlim/32; // 32,16,8,4,2,1
    if (dsp->decM > 1) {
        IQdc.maxlim *= dsp->decM;
        IQdc.maxcnt *= dsp->decM;
    }


    // FM dec: sps = sps_if / FM_DEC
    L = dsp->hdrlen * dsp->sps + 0.5;
    M = 3*L;
    //if (dsp->sps < 6) M = 6*L;

    dsp->delay = L/16;
    dsp->sample_in = 0;

    p2 = 1;
    while (p2 < M) p2 <<= 1;
    while (p2 < 0x2000) p2 <<= 1;  // 0x1000 if header distance too short, or reduce K  // 0x4000, if sample not too short
    M = p2;
    dsp->DFT.N = p2; // 2*p2
    dsp->DFT.LOG2N = log(dsp->DFT.N)/log(2)+0.1; // 32bit cpu ... intermediate floating-point precision
    //while ((1 << dsp->DFT.LOG2N) < dsp->DFT.N) dsp->DFT.LOG2N++;  // better N = (1 << LOG2N) ...

    K = M-L - dsp->delay; // L+K < M
    // header distance 24 52 4d .. 24 52 54 : 790 bits
    while (K > 790*dsp->sps) K--;

    dsp->DFT.sr = dsp->sr;

    dsp->K = K;
    dsp->L = L;
    dsp->M = M;


    dsp->bufs  = (float *)calloc( M+1, sizeof(float)); if (dsp->bufs  == NULL) return -100;
    dsp->match = (float *)calloc( L+1, sizeof(float)); if (dsp->match == NULL) return -100;


    dsp->rawbits = (char *)calloc( 2*dsp->hdrlen+1, sizeof(char)); if (dsp->rawbits == NULL) return -100;


    for (i = 0; i < M; i++) dsp->bufs[i] = 0.0;


    for (i = 0; i < L; i++) {
        pos = i/dsp->sps;
        t = (i - pos*dsp->sps)/dsp->sps - 0.5;

        b1 = ((dsp->hdr[pos] & 0x1) - 0.5)*2.0;
        b = b1*pulse(t, sigma);

        if (pos > 0) {
            b0 = ((dsp->hdr[pos-1] & 0x1) - 0.5)*2.0;
            b += b0*pulse(t+1, sigma);
        }

        if (pos < dsp->hdrlen-1) {
            b2 = ((dsp->hdr[pos+1] & 0x1) - 0.5)*2.0;
            b += b2*pulse(t-1, sigma);
        }

        dsp->match[i] = b;
    }

    normMatch = sqrt( norm2_vect(dsp->match, L) );
    for (i = 0; i < L; i++) {
        dsp->match[i] /= normMatch;
    }


    dsp->DFT.xn = calloc(dsp->DFT.N+1, sizeof(float));  if (dsp->DFT.xn == NULL) return -1;

    dsp->DFT.Fm = calloc(dsp->DFT.N+1, sizeof(float complex));  if (dsp->DFT.Fm == NULL) return -1;
    dsp->DFT.X  = calloc(dsp->DFT.N+1, sizeof(float complex));  if (dsp->DFT.X  == NULL) return -1;
    dsp->DFT.Z  = calloc(dsp->DFT.N+1, sizeof(float complex));  if (dsp->DFT.Z  == NULL) return -1;
    dsp->DFT.cx = calloc(dsp->DFT.N+1, sizeof(float complex));  if (dsp->DFT.cx == NULL) return -1;

    dsp->DFT.ew = calloc(dsp->DFT.LOG2N+1, sizeof(float complex));  if (dsp->DFT.ew == NULL) return -1;

    // FFT window
    // a) N2 = N
    // b) N2 < N (interpolation)
    dsp->DFT.win = calloc(dsp->DFT.N+1, sizeof(float complex));  if (dsp->DFT.win == NULL) return -1; // float real
    dsp->DFT.N2 = dsp->DFT.N;
    //dsp->DFT.N2 = dsp->DFT.N/2 - 1; // N=2^log2N
    dft_window(&dsp->DFT, 1);

    for (n = 0; n < dsp->DFT.LOG2N; n++) {
        k = 1 << n;
        dsp->DFT.ew[n] = cexp(-I*M_PI/(float)k);
    }

    m = calloc(dsp->DFT.N+1, sizeof(float));  if (m  == NULL) return -1;
    for (i = 0; i < L; i++) m[L-1 - i] = dsp->match[i]; // t = L-1
    while (i < dsp->DFT.N) m[i++] = 0.0;
    rdft(&dsp->DFT, m, dsp->DFT.Fm);

    free(m); m = NULL;


    if (dsp->opt_iq)
    {
        if (dsp->nch < 2) return -1;

        dsp->N_IQBUF = dsp->DFT.N;
        dsp->rot_iqbuf = calloc(dsp->N_IQBUF+1, sizeof(float complex));  if (dsp->rot_iqbuf == NULL) return -1;
    }

    dsp->fm_buffer = (float *)calloc( M+1, sizeof(float));  if (dsp->fm_buffer == NULL) return -1; // dsp->bufs[]


    return K;
}

static
int free_buffers(dsp_t *dsp) {

    if (dsp->match) { free(dsp->match); dsp->match = NULL; }
    if (dsp->bufs)  { free(dsp->bufs);  dsp->bufs  = NULL; }
    if (dsp->rawbits) { free(dsp->rawbits); dsp->rawbits = NULL; }

    if (dsp->DFT.xn) { free(dsp->DFT.xn); dsp->DFT.xn = NULL; }
    if (dsp->DFT.ew) { free(dsp->DFT.ew); dsp->DFT.ew = NULL; }
    if (dsp->DFT.Fm) { free(dsp->DFT.Fm); dsp->DFT.Fm = NULL; }
    if (dsp->DFT.X)  { free(dsp->DFT.X);  dsp->DFT.X  = NULL; }
    if (dsp->DFT.Z)  { free(dsp->DFT.Z);  dsp->DFT.Z  = NULL; }
    if (dsp->DFT.cx) { free(dsp->DFT.cx); dsp->DFT.cx = NULL; }

    if (dsp->DFT.win) { free(dsp->DFT.win); dsp->DFT.win = NULL; }

    if (dsp->opt_iq)
    {
        if (dsp->rot_iqbuf) { free(dsp->rot_iqbuf); dsp->rot_iqbuf = NULL; }
    }

    // decimate
    if (dsp->opt_iq >= 5)
    {
        if (dsp->decXbuffer) { free(dsp->decXbuffer); dsp->decXbuffer = NULL; }
        if (dsp->decMbuf)    { free(dsp->decMbuf);    dsp->decMbuf    = NULL; }
        if (dsp->ex)         { free(dsp->ex);         dsp->ex         = NULL; }

        if (ws_dec) { free(ws_dec); ws_dec = NULL; }
    }

    // IF lowpass
    if (dsp->opt_iq && (dsp->opt_lp & 1))
    {
        if (dsp->ws_lpIQ0) { free(dsp->ws_lpIQ0); dsp->ws_lpIQ0 = NULL; }
        if (dsp->ws_lpIQ1) { free(dsp->ws_lpIQ1); dsp->ws_lpIQ1 = NULL; }
        if (dsp->lpIQ_buf) { free(dsp->lpIQ_buf); dsp->lpIQ_buf = NULL; }
    }
    if (dsp->opt_lp & 2)
    {
        if (dsp->ws_lpFM)  { free(dsp->ws_lpFM);  dsp->ws_lpFM  = NULL; }
        if (dsp->lpFM_buf) { free(dsp->lpFM_buf); dsp->lpFM_buf = NULL; }
    }

    if (dsp->fm_buffer) { free(dsp->fm_buffer); dsp->fm_buffer = NULL; }

    return 0;
}

/* ------------------------------------------------------------------------------------ */

static
int find_header(dsp_t *dsp, float thres, int hdmax, int bitofs, int opt_dc) {
    ui32_t k = 0;
    ui32_t mvpos0 = 0;
    int mp;
    int header_found = 0;
    int herrs;

    while ( f32buf_sample(dsp, 0) != EOF ) {

        k += 1;
        if (k >= dsp->K-4) {
            mvpos0 = dsp->mv_pos;
            mp = getCorrDFT(dsp); // correlation score -> dsp->mv
            //if (option_auto == 0 && dsp->mv < 0) mv = 0;
            k = 0;
        }
        else {
            dsp->mv = 0.0;
            continue;
        }

        if (dsp->mv > thres || dsp->mv < -thres)
        {
            if (dsp->opt_dc) {
                dsp->Df += dsp->dDf*0.4;
                if (dsp->opt_iq) {
                    if (fabs(dsp->dDf) > 20*1e3) {  // L-band
                        if (dsp->locked) {
                            dsp->locked = 0;
                            dsp->ws_lpIQ = dsp->ws_lpIQ0;
                            // alt: lowpass_update(1.5*dsp->lpIQ_fbw, dsp->lpIQtaps, dsp->ws_lpIQ);
                        }
                    }
                    else {
                        if (dsp->locked == 0) {
                            dsp->locked = 1;
                            dsp->ws_lpIQ = dsp->ws_lpIQ1;
                            // alt: lowpass_update(dsp->lpIQ_fbw, dsp->lpIQtaps, dsp->ws_lpIQ);
                        }
                    }
                }
            }

            if (dsp->mv_pos > mvpos0) {

                header_found = 0;
                herrs = headcmp(dsp, opt_dc);
                if (herrs <= hdmax) header_found = 1; // max bitfehler in header

                if (header_found) return 1;
            }
        }

    }

    return EOF;
}

/* ------------------------------------------------------------------------------------ */


static float cmp_hdb(hdb_t *hdb) { // bit-errors?
    int i, j;
    int headlen = hdb->len;
    int berrs1 = 0, berrs2 = 0;

    i = 0;
    j = hdb->bufpos;
    while (i < headlen) {
        if (j < 0) j = headlen-1;
        if (hdb->buf[j] != hdb->hdr[headlen-1-i]) berrs1 += 1;
        j--;
        i++;
    }

    i = 0;
    j = hdb->bufpos;
    while (i < headlen) {
        if (j < 0) j = headlen-1;
        if ((hdb->buf[j]^0x01) != hdb->hdr[headlen-1-i]) berrs2 += 1;
        j--;
        i++;
    }

    if (berrs2 < berrs1) return (-headlen+berrs2)/(float)headlen;
    else                 return ( headlen-berrs1)/(float)headlen;

    return 0;
}

static float corr_softhdb(hdb_t *hdb) { // max score in window probably not needed
    int i, j;
    int headlen = hdb->len;
    double sum = 0.0;
    double normx = 0.0,
           normy = 0.0;
    float x, y;

    i = 0;
    j = hdb->bufpos + 1;

    while (i < headlen) {
        if (j >= headlen) j = 0;
        x = hdb->sbuf[j];
        y = 2.0*(hdb->hdr[i]&0x1) - 1.0;
        sum += y * hdb->sbuf[j];
        normx += x*x;
        normy += y*y;
        j++;
        i++;
    }
    sum /= sqrt(normx*normy);

    return sum;
}

static
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

static
int find_softbinhead(FILE *fp, hdb_t *hdb, float *score) {
    int headlen = hdb->len;
    float sbit;
    float mv;

    //*score = 0.0;

    while ( f32soft_read(fp, &sbit) != EOF )
    {
        hdb->bufpos = (hdb->bufpos+1) % headlen;
        hdb->sbuf[hdb->bufpos] = sbit;

        mv = corr_softhdb(hdb);

        if ( fabs(mv) > hdb->ths ) {
            *score = mv;
            return 1;
        }
    }

    return EOF;
}

// -------------------------------------------------------------------------------------------------
/* ------------------------------------------------------------------------------------------------- */


typedef struct {
    i8_t vbs;  // verbose output
    i8_t raw;  // raw frames
    i8_t crc;  // CRC check output
    i8_t ecc;  //
    i8_t sat;  // GPS sat data
    i8_t ptu;  // PTU: temperature humidity (pressure)
    i8_t dwp;  // PTU derived: dew point
    i8_t inv;
    i8_t aut;
    i8_t jsn;  // JSON output (auto_rx)
    i8_t slt;  // silent (only raw/json)
} option_t;


/* -------------------------------------------------------------------------- */

#define BAUD_RATE   (9616.0)  // 9616..9618

#define BITS (1+8+1)  // 8N1 = 10bit/byte

                    //  CA          CA          CA          24         52
static char header[] = "0010100111""0010100111""0010100111""0001001001""0010010101";
                         //  CA          CA          CA          24          52             54
static char rawheader54[] = "0010100111""0010100111""0010100111""0001001001""0010010101";//"0001010101";
                         //  CA          CA          CA          24          52             4D
static char rawheader4D[] = "0010100111""0010100111""0010100111""0001001001""0010010101";//"0101100101";

#define SYNCLEN 40
// moeglicherweise auch anderes sync-byte als 0xCA moeglich
static char sync[]   = "0010100111""0010100111""0010100111""0010100111"; // CA CA CA CA

#define FRAMESTART 0
#define FRMSTART (2*BITS)  // < header_len

#define FRAME_LEN       (176) //(960+2)   // max; min 36+3 GPS
#define BITFRAME_LEN    (FRAME_LEN*BITS)


typedef struct {
    int frnr;
    int prev_frnr;
    ui32_t id;
    int week; int gpstow;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double alt;
    double vH; double vD; double vV;
    double vE; double vN; double vU;
    char  frame_bits[BITFRAME_LEN +9];
    ui8_t frame_bytes[FRAME_LEN];  // = { 0x24, 0x54, 0x00, 0x00}; // dataheader
    //int freq;
    int jsn_freq;   // freq/kHz (SDR)
    option_t option;
} gpx_t;


static int findsync(gpx_t *gpx, int pos) {
    int i = 0;
    int j = pos-SYNCLEN;

    if (j < 0) return 0;

    while (i < SYNCLEN) {
        if (gpx->frame_bits[j+i] != sync[i]) break;
        i++;
    }
    if (i == SYNCLEN) return 1;

    return 0;
}

static int bits2bytes(char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < FRAME_LEN) {

        byteval = 0;
        d = 1;
        for (i = 1; i < BITS-1; i++) {
            bit = *(bitstr+bitpos+i); /* little endian */
            //bit = *(bitstr+bitpos+BITS-1-i);  /* big endian */
            if (bit == '\0') goto frame_end;
            if         (bit == '1')     byteval += d;
            else /*if ((bit == '0') */  byteval += 0;
            d <<= 1;
        }
        bitpos += BITS;
        bytes[bytepos++] = byteval;

    }
frame_end:
    for (i = bytepos; i < FRAME_LEN; i++) bytes[i] = 0;

    return bytepos;
}

/* -------------------------------------------------------------------------- */

static int crc16_0(ui8_t frame[], int len) {
    int crc16poly = 0x1021;
    int rem = 0x0, i, j;
    int byte;

    for (i = 0; i < len; i++) {
        byte = frame[i];
        rem = rem ^ (byte << 8);
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



#define OFS 2  // (0x2452 ..)
#define pos_SondeID  (OFS+0x02)  // 2 byte (LSB)
#define pos_FrameNb  (OFS+0x04)  // 2 byte
//GPS Position
#define pos_GPSTOW   (OFS+0x08)  // 4 byte, subframe 0x(2452)54
#define pos_GPSlat   (OFS+0x10)  // 4 byte, subframe 0x(2452)54
#define pos_GPSlon   (OFS+0x14)  // 4 byte, subframe 0x(2452)54
#define pos_GPSalt   (OFS+0x18)  // 4 byte, subframe 0x(2452)54
//GPS Velocity East-North-Up (ENU)
#define pos_GPSvO    (OFS+0x1C)  // 3 byte, subframe 0x(2452)54
#define pos_GPSvN    (OFS+0x1F)  // 3 byte, subframe 0x(2452)54
#define pos_GPSvV    (OFS+0x22)  // 3 byte, subframe 0x(2452)54
// full 1680MHz-ID, config-subblock:sonde_id
#define pos_FullID   (OFS+0x30)  // 2+2 byte (LSB,MSB), subframe 0x(2452)4D


static int check_CRC(gpx_t *gpx, int len) {
    ui32_t crclen = 0,
           crcdat = 0;
/*
    if      (frame_bytes[OFS] == 0x4D) crclen = 67;
    else if (frame_bytes[OFS] == 0x54) crclen = 172; // 172, 146? variable? Mk2a, LMS6-1680?
    else crclen = len;
*/
    crclen = len;
    crcdat = (gpx->frame_bytes[crclen]<<8) | gpx->frame_bytes[crclen+1];
    if ( crcdat != crc16_0(gpx->frame_bytes, crclen) ) {
        return 1;  // CRC NO
    }
    else return 0; // CRC OK
}

static int get_FrameNb(gpx_t *gpx) {

    gpx->frnr = (gpx->frame_bytes[pos_FrameNb] << 8) + gpx->frame_bytes[pos_FrameNb+1];

    return 0;
}


//char weekday[7][3] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
static char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static int get_GPStime(gpx_t *gpx) {
    int i;
    int gpstime = 0, // 32bit
        day;
    float ms;

    gpstime = 0;
    for (i = 0; i < 4; i++) {
        gpstime |= gpx->frame_bytes[pos_GPSTOW + i] << (8*(3-i));
    }

    gpx->gpstow = gpstime;

    ms = gpstime % 1000;
    gpstime /= 1000;

    day = gpstime / (24 * 3600);
    gpstime %= (24*3600);

    if ((day < 0) || (day > 6)) return -1;
    gpx->wday = day;
    gpx->std = gpstime / 3600;
    gpx->min = (gpstime % 3600) / 60;
    gpx->sek = gpstime % 60 + ms/1000.0;

    return 0;
}

static double B60B60 = 0xB60B60;  // 2^32/360 = 0xB60B60.xxx

static int get_GPSlat(gpx_t *gpx) {
    int i;
    int gpslat;

    gpslat = 0;
    for (i = 0; i < 4; i++) {
        gpslat |= gpx->frame_bytes[pos_GPSlat + i] << (8*(3-i));
    }
    gpx->lat = gpslat / (double)B60B60;

    return 0;
}

static int get_GPSlon(gpx_t *gpx) {
    int i;
    int gpslon;

    gpslon = 0;
    for (i = 0; i < 4; i++) {
        gpslon |= gpx->frame_bytes[pos_GPSlon + i] << (8*(3-i));
    }
    gpx->lon = gpslon / (double)B60B60;

    return 0;
}

static int get_GPSalt(gpx_t *gpx) {
    int i;
    int gpsheight;

    gpsheight = 0;
    for (i = 0; i < 4; i++) {
        gpsheight |= gpx->frame_bytes[pos_GPSalt + i] << (8*(3-i));
    }
    gpx->alt = gpsheight / 1000.0;

    if (gpx->alt < -100 || gpx->alt > 60000) return -1;
    return 0;
}

static int get_GPSvel24(gpx_t *gpx) {
    ui8_t *gpsVel_bytes;
    int vel24;
    double vx, vy, vz, dir; //, alpha;

    gpsVel_bytes = gpx->frame_bytes+pos_GPSvO;
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vx = vel24 / 1e3; // ost

    gpsVel_bytes = gpx->frame_bytes+pos_GPSvN;
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vy= vel24 / 1e3; // nord

    gpsVel_bytes = gpx->frame_bytes+pos_GPSvV;
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vz = vel24 / 1e3; // hoch

    gpx->vE = vx;
    gpx->vN = vy;
    gpx->vU = vz;


    gpx->vH = sqrt(vx*vx+vy*vy);
    /*
    alpha = atan2(vy, vx)*180/M_PI;  // ComplexPlane (von x-Achse nach links) - GeoMeteo (von y-Achse nach rechts)
    dir = 90-alpha;                  // z=x+iy= -> i*conj(z)=y+ix=re(i(pi/2-t)), Achsen und Drehsinn vertauscht
    if (dir < 0) dir += 360;         // atan2(y,x)=atan(y/x)=pi/2-atan(x/y) , atan(1/t) = pi/2 - atan(t)
    gpx->vD2 = dir;
    */
    dir = atan2(vx, vy) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx->vD = dir;

    gpx->vV = vz;

    return 0;
}

static void print_frame(gpx_t *gpx, int len, dsp_t *dsp) {

    int i, crc_err = 0;
    int flen = len/BITS;

    for (i = len; i < BITFRAME_LEN; i++) gpx->frame_bits[i] = 0;  // oder: '0'
    bits2bytes(gpx->frame_bits, gpx->frame_bytes);

    while (flen > 2 && gpx->frame_bytes[flen-1] == 0xCA) flen--; // if crc != 0xYYCA ...

    crc_err = check_CRC(gpx, flen-2);
    if (crc_err) { // crc_bytes == sync_bytes?
        crc_err = check_CRC(gpx, flen-1);
        if (crc_err == 0) flen += 1;
        else {
            crc_err = check_CRC(gpx, flen);
            if (crc_err == 0) flen += 2;
        }
    }

    if (gpx->option.raw)
    {
        for (i = 0; i < flen; i++) printf("%02x ", gpx->frame_bytes[i]);
        if (gpx->option.crc) {
            if (crc_err==0) printf(" [OK]"); else printf(" [NO]");
        }
        printf("\n");
    }

    {
        if (gpx->frame_bytes[OFS] == 0x4D  &&  len/BITS > pos_FullID+4) {
            if ( !crc_err ) {
                if (gpx->frame_bytes[pos_SondeID]   == gpx->frame_bytes[pos_FullID]  &&
                    gpx->frame_bytes[pos_SondeID+1] == gpx->frame_bytes[pos_FullID+1]) {
                    ui32_t __id =  (gpx->frame_bytes[pos_FullID+2]<<24) | (gpx->frame_bytes[pos_FullID+3]<<16)
                                 | (gpx->frame_bytes[pos_FullID]  << 8) |  gpx->frame_bytes[pos_FullID+1];
                    gpx->id = __id;
                }
            }
        }

        if (gpx->frame_bytes[OFS] == 0x54  &&  len/BITS > pos_GPSalt+4) {

            get_FrameNb(gpx);
            get_GPStime(gpx);
            get_GPSlat(gpx);
            get_GPSlon(gpx);
            get_GPSalt(gpx);

            if (gpx->option.vbs >= 2) {
                printf("<");
                printf("s=%+.2f", dsp->mv);
                if (dsp->opt_dc && dsp->opt_iq) {
                    //printf(" f=%+.4f", -dsp->xlt_fq);
                    printf(" Df=%+.1fkHz", dsp->Df/1e3);
                    if (gpx->option.vbs == 3) {
                        printf(" (IF=%+.4f,", dsp->Df/(double)dsp->sr);
                        printf("IQ=%+.4f)", dsp->Df/(double)dsp->sr_base);
                    }
                }
                printf("> ");
            }

            if ( !crc_err ) {
                ui32_t _id = (gpx->frame_bytes[pos_SondeID]<<8) | gpx->frame_bytes[pos_SondeID+1];
                if ((gpx->id & 0xFFFF) != _id) gpx->id = _id;
            }
            if (gpx->option.vbs && !crc_err) {
                if (gpx->id & 0xFFFF0000) printf(" (%u)", gpx->id);
                else if (gpx->id) printf(" (0x%04X)", gpx->id);
            }

            printf(" [%5d] ", gpx->frnr);

            printf("%s ", weekday[gpx->wday]);
            printf("%02d:%02d:%06.3f ", gpx->std, gpx->min, gpx->sek); // falls Rundung auf 60s: Ueberlauf
            printf(" lat: %.5f ", gpx->lat);
            printf(" lon: %.5f ", gpx->lon);
            printf(" alt: %.2fm ", gpx->alt);

            get_GPSvel24(gpx);
            printf("  vH: %.1fm/s  D: %.1f  vV: %.1fm/s ", gpx->vH, gpx->vD, gpx->vV);
            //if (gpx->option.verbose == 2) printf("  (%.1f ,%.1f,%.1f) ", gpx->vE, gpx->vN, gpx->vU);

            if (gpx->option.crc) {
                if (crc_err==0) printf(" [OK]"); else printf(" [NO]");
            }

            printf("\n");

            if (gpx->option.jsn) {
                // Print JSON output required by auto_rx.
                if (crc_err==0 && (gpx->id & 0xFFFF0000)) { // CRC-OK and FullID
                    if (gpx->prev_frnr != gpx->frnr) { //|| gpx->id != _id0
                        // UTC oder GPS?
                        char *ver_jsn = NULL;
                        printf("{ \"type\": \"%s\"", "LMS");
                        printf(", \"frame\": %d, \"id\": \"LMS6-%d\", \"datetime\": \"%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f",
                               gpx->frnr, gpx->id, gpx->std, gpx->min, gpx->sek, gpx->lat, gpx->lon, gpx->alt, gpx->vH, gpx->vD, gpx->vV );
                        printf(", \"subtype\": \"%s\"", "MK2A");
                        if (gpx->jsn_freq > 0) {
                            printf(", \"freq\": %d", gpx->jsn_freq);
                        }
                        #ifdef VER_JSN_STR
                            ver_jsn = VER_JSN_STR;
                        #endif
                        if (ver_jsn && *ver_jsn != '\0') printf(", \"version\": \"%s\"", ver_jsn);
                        printf(" }\n");
                        printf("\n");
                        fflush(stdout);
                        gpx->prev_frnr = gpx->frnr;
                    }
                }
            }
        }
    }

}


int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;
    int cfreq = -1;
    float baudrate = -1;

    float _bl = -1.0;
    float _h = 10.4;
    float lpIQ_bw = 180e3;

    int option_softin = 0;
    int option_pcmraw = 0;
    int sel_wavch = 0;
    int wavloaded = 0;


    int option_min = 0;
    int option_iq = 0;
    int option_iqdc = 0;
    int option_lp = 0;
    int option_dc = 0;
    int option_decFM = 0;

    int k;

    int bit;
    int bitpos = 0;
    int bitQ;
    int pos;
    hsbit_t hsbit, hsbit1;

    int header_found = 0;

    float thres = 0.7;
    float _mv = 0.0;

    int symlen = 1;
    int bitofs = 0; // fm:0 , iq:+1
    int shift = 0;

    pcm_t pcm = {0};
    dsp_t dsp = {0};  //memset(&dsp, 0, sizeof(dsp));
    gpx_t gpx = {0};
    hdb_t hdb = {0};


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
            gpx.option.vbs = 1;
        }
        else if ( (strcmp(*argv, "-vv") == 0) ) gpx.option.vbs = 2;
        else if ( (strcmp(*argv, "-vvv") == 0) ) gpx.option.vbs = 3;
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            gpx.option.raw = 1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            gpx.option.inv = 1;
        }
        else if   (strcmp(*argv, "--crc") == 0) { gpx.option.crc = 1; }

        else if   (strcmp(*argv, "--ths") == 0) {
            ++argv;
            if (*argv) {
                thres = atof(*argv);
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "--br") == 0) ) {
            ++argv;
            if (*argv) {
                baudrate = atof(*argv);
                if (baudrate < 9400 || baudrate > 9800) baudrate = BAUD_RATE; // 9616..9618
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "-d") == 0) ) {
            ++argv;
            if (*argv) {
                shift = atoi(*argv);
                if (shift >  4) shift =  4;
                if (shift < -4) shift = -4;
            }
            else return -1;
        }
        else if   (strcmp(*argv, "--iq0") == 0) { option_iq = 1; }  // differential/FM-demod
        else if   (strcmp(*argv, "--iqdc") == 0) { option_iqdc = 1; }  // iq-dc removal (iq0,2,3)
        else if   (strcmp(*argv, "--IQ") == 0 || strcmp(*argv, "--iq") == 0) { // fq baseband -> IF (rotate from and decimate)
            double fq = 0.0;                                                   // --IQ <fq> , -0.5 < fq < 0.5
            if (strcmp(*argv, "--IQ") == 0) option_iq = 5; else option_iq = 6;
            ++argv;
            if (*argv) fq = atof(*argv);
            else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            dsp.xlt_fq = -fq; // S(t) -> S(t)*exp(-f*2pi*I*t)
        }
        else if   (strcmp(*argv, "--lpIQ") == 0) { option_lp |= 1; }  // IQ lowpass
        else if   (strcmp(*argv, "--lpbw") == 0) {  // IQ lowpass BW / kHz
            double bw = 0.0;
            ++argv;
            if (*argv) bw = atof(*argv);
            else return -1;
            if (bw > 100.0 && bw < 240.0) lpIQ_bw = bw*1e3;
            option_lp |= 1;
        }
        else if   (strcmp(*argv, "--lpFM") == 0) { option_lp |= 2; }  // FM lowpass
        else if   (strcmp(*argv, "--decFM") == 0) {   // FM decimation
            option_lp |= 2;
            option_decFM = 1;
        }
        else if   (strcmp(*argv, "--dc") == 0) { option_dc = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if   (strcmp(*argv, "--json") == 0) {
            gpx.option.jsn = 1;
            gpx.option.crc = 1;
            if (!gpx.option.vbs) gpx.option.vbs = 1;
        }
        else if   (strcmp(*argv, "--jsn_cfq") == 0) {
            int frq = -1;  // center frequency / Hz
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1; // L-band, > 1600 MHz
            cfreq = frq;
        }
        else if (strcmp(*argv, "-") == 0) {
            int sample_rate = 0, bits_sample = 0, channels = 0;
            ++argv;
            if (*argv) sample_rate = atoi(*argv); else return -1;
            ++argv;
            if (*argv) bits_sample = atoi(*argv); else return -1;
            channels = 2;
            if (sample_rate < 1 || (bits_sample != 8 && bits_sample != 16 && bits_sample != 32)) {
                fprintf(stderr, "- <sr> <bs>\n");
                return -1;
            }
            pcm.sr  = sample_rate;
            pcm.bps = bits_sample;
            pcm.nch = channels;
            option_pcmraw = 1;
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
    if (cfreq > 0) gpx.jsn_freq = (cfreq+500)/1000;  ////


    if (!option_softin) {

        if (option_iq == 0 && option_pcmraw) {
            fclose(fp);
            fprintf(stderr, "error: raw data not IQ\n");
            return -1;
        }
        if (option_iq) sel_wavch = 0;

        pcm.sel_ch = sel_wavch;
        if (option_pcmraw == 0) {
            k = read_wav_header(&pcm, fp);
            if ( k < 0 ) {
                fclose(fp);
                fprintf(stderr, "error: wav header\n");
                return -1;
            }
        }

        if (cfreq > 0) {
            int fq_kHz = (cfreq - dsp.xlt_fq*pcm.sr + 500)/1e3;
            gpx.jsn_freq = fq_kHz;
        }

        symlen = 1;

        // init dsp
        //
        dsp.fp = fp;
        dsp.sr = pcm.sr;
        dsp.bps = pcm.bps;
        dsp.nch = pcm.nch;
        dsp.ch = pcm.sel_ch;
        dsp.br = (float)BAUD_RATE;

        if (option_decFM) {
            if (dsp.sr > 4*44000) dsp.opt_fmdec = 1;
        }
        dsp.sps = (float)dsp.sr/dsp.br;
        dsp.decFM = 1;
        if (dsp.opt_fmdec) {
            dsp.decFM = FM_DEC;
            while (dsp.sr % dsp.decFM > 0  &&  dsp.decFM > 1) dsp.decFM /= 2;
            dsp.sps /= (float)dsp.decFM;
        }

        if (option_iq == 5 && option_dc) option_lp |= 2;

        dsp.symlen = symlen;
        dsp.symhd = 1;
        dsp._spb = dsp.sps*symlen;
        dsp.hdr = header;
        dsp.hdrlen = strlen(header);
        dsp.BT = 1.0; // bw/time (ISI) // 1.0..2.0
        dsp.h = _h;  // 10.4..10.7;
        dsp.opt_iq = option_iq;
        dsp.opt_iqdc = option_iqdc;
        dsp.opt_lp = option_lp;
        dsp.lpIQ_bw = lpIQ_bw; // IF lowpass bandwidth
        dsp.lpFM_bw = 10e3; // FM audio lowpass iq0: 10e3 , iq 0.0: 7e3-8e3
        if (option_iq == 6) dsp.lpFM_bw = 6.8e3;
        dsp.opt_dc = option_dc;
        dsp.opt_IFmin = option_min;

        if ( dsp.sps < 8 ) {
            fprintf(stderr, "note: sample rate low (%.1f sps)\n", dsp.sps);
        }

        if (baudrate > 0) {
            dsp.br = (float)baudrate;
            dsp.sps = (float)dsp.sr/dsp.br;
            fprintf(stderr, "sps corr: %.4f\n", dsp.sps);
        }

        k = init_buffers_Lband(&dsp);
        if ( k < 0 ) {
            fprintf(stderr, "error: init buffers\n");
            return -1;
        }

        if (option_iq && !dsp.opt_fmdec) bitofs += 1;
        bitofs += shift;
        _bl = 0.7*dsp.sps/2.0;
        if (_bl < 2.0) _bl = -1;
        if (dsp.opt_fmdec) _bl = -1;
    }
    else {
        // init circular header bit buffer
        hdb.hdr = header;
        hdb.len = strlen(header);
        hdb.thb = 1.0 - 3.1/(float)hdb.len; // 1.0-max_bit_errors/hdrlen
        hdb.bufpos = -1;
        hdb.buf = calloc(hdb.len, sizeof(char));
        if (hdb.buf == NULL) {
            fprintf(stderr, "error: malloc\n");
            return -1;
        }
        hdb.ths = 0.8; // caution/test false positive
        hdb.sbuf = calloc(hdb.len, sizeof(float));
        if (hdb.sbuf == NULL) {
            fprintf(stderr, "error: malloc\n");
            return -1;
        }
    }


    strncpy(gpx.frame_bits, header+strlen(header)-FRMSTART, FRMSTART);

    pos = FRMSTART;


    while ( 1 )
    {
        if (option_softin) {
            header_found = find_softbinhead(fp, &hdb, &_mv);
        }
        else {                                                              // FM-audio:
            header_found = find_header(&dsp, thres, 1, bitofs, dsp.opt_dc); // optional 2nd pass: dc=0
            _mv = dsp.mv;
        }

        if (header_found == EOF) break;

        // mv == correlation score
        if (_mv*(0.5-gpx.option.inv) < 0) {
            if (gpx.option.aut == 0) header_found = 0;
            gpx.option.inv ^= 0x1;
        }

        if (header_found)
        {
            bitpos = 0;
            pos = FRMSTART;


            while ( pos < BITFRAME_LEN && !findsync(&gpx, pos))
            {
                if (option_softin) {
                        float s = 0.0;
                        bitQ = f32soft_read(fp, &s);
                        if (bitQ != EOF) {
                            bit = (s>=0.0);
                        }
                }
                else {
                    float bl = -1;
                    if (option_iq > 2) bl = _bl;
                    bitQ = read_softbit2p(&dsp, &hsbit, 0, bitofs, bitpos, bl, 0, &hsbit1); // symlen=2
                    bit = hsbit.hb;
                }
                if ( bitQ == EOF ) break; // liest 2x EOF

                if (gpx.option.inv) {
                    bit ^= 1;
                    hsbit.hb ^= 1;
                    hsbit.sb = -hsbit.sb;
                }

                gpx.frame_bits[pos] = 0x30 + (hsbit.hb & 1);

                bitpos += 1;
                pos++;
            }
            gpx.frame_bits[pos] = '\0';

            print_frame(&gpx, pos, &dsp);//FRAME_LEN

            header_found = 0;
            pos = FRMSTART;
        }

    }

    if (!option_softin) free_buffers(&dsp);
    else {
        if (hdb.buf) { free(hdb.buf); hdb.buf = NULL; }
    }


    printf("\n");

    fclose(fp);

    return 0;
}

