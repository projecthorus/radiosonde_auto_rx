
/*
 *  compile:
 *
 *      gcc -Ofast iq_dec.c -lm -o iq_dec
 *
 *
 *  usage:
 *
 *      ./iq_dec [--bo <b>] [--iq <fq>] [iq_baseband.wav]   # <b>=8,16,32 bit output
 *      ./iq_dec [--bo <b>] [--iq <fq>] - <sr> <bs> [iq_baseband.raw]
 *
 *      ./iq_dec [--bo <b>] [--wav] [--FM] [--iq <fq>] iq_baseband.wav
 *      ./iq_dec [--bo <b>] [--wav] [--decFM] [--iq <fq>] - <sr> <bs> [iq_baseband.raw]
 *               --iq <fq>  : center at fq=freq/sr (default: 0.0)
 *               --wav      : output wav header
 *               --FM/decFM : FM demodulation
 *               --bo <b>   : output bits per sample b=8,16,32  (u8, s16, f32 (default))
 *
 *
 *  author: zilog80
 */


/* ------------------------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define FM_GAIN (0.8)

/* ------------------------------------------------------------------------------------ */


#include <math.h>
#include <complex.h>

#ifndef M_PI
    #define M_PI  (3.1415926535897932384626433832795)
#endif
#define _2PI  (6.2831853071795864769252867665590)

#define LP_IQ    1
#define LP_FM    2
#define LP_IQFM  4


#ifndef INTTYPES
#define INTTYPES
typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;
typedef unsigned long long ui64_t;
typedef char  i8_t;
typedef short i16_t;
typedef int   i32_t;
#endif


typedef struct {
    FILE *fp;
    //
    int sr;       // sample_rate
    int bps;      // bits/sample
    int nch;      // channels
    int ch;       // select channel
    //
    int bps_out;
    //
    ui32_t sample_in;
    ui32_t sample_out;
    //

    // IQ-data
    //int opt_iq; // always IQ input
    int opt_iqdc; // in f32read_cblock() anyway


    double V_noise;
    double V_signal;
    double SNRdB;

    // decimate
    int exlut;
    int opt_nolut; // default: exlut
    int opt_IFmin;
    int decM;
    int decFM;
    ui32_t sr_base;
    ui32_t dectaps;
    ui32_t sample_decX;
    ui32_t lut_len;
    ui32_t sample_decM;
    float complex *decXbuffer;
    float complex *decMbuf;
    float complex *ex; // exp_lut
    double xlt_fq;

    int opt_fm;
    int opt_lp;

    // IF: lowpass
    int lpIQ_bw;
    int lpIQtaps; // ui32_t
    float lpIQ_fbw;
    float *ws_lpIQ;
    float complex *lpIQ_buf;

    // FM: lowpass
    int lpFM_bw;
    int lpFMtaps; // ui32_t
    float *ws_lpFM;
    float *lpFM_buf;
    float *fm_buffer;

} dsp_t;


typedef struct {
    int sr;       // sample_rate
    int sr_out;
    int bps;      // bits_sample  bits/sample
    int bps_out;
    int nch;      // channels
    int sel_ch;   // select wav channel
} pcm_t;



/* ------------------------------------------------------------------------------------ */

static int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}

static int read_wav_header(pcm_t *pcm, FILE *fp) {
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

    if (bits_sample != 8 && bits_sample != 16 && bits_sample != 32) return -1;

    if (sample_rate == 900001) sample_rate -= 1;

    pcm->sr  = sample_rate;
    pcm->bps = bits_sample;
    pcm->nch = channels;

    return 0;
}

static float write_wav_header(pcm_t *pcm) {
    FILE *fp = stdout;
    ui32_t sr  = pcm->sr_out;
    ui32_t bps = pcm->bps_out;
    ui32_t data = 0;

    fwrite("RIFF", 1, 4, fp);
    data = 0; // bytes-8=headersize-8+datasize
    fwrite(&data,  1, 4, fp);
    fwrite("WAVE", 1, 4, fp);

    fwrite("fmt ", 1, 4, fp);
    data = 16; if (bps == 32) data += 2;
    fwrite(&data,  1, 4, fp);

    if (bps == 32) data = 3; // IEEE float
    else           data = 1; // PCM
    fwrite(&data,  1, 2, fp);

    data = pcm->nch; // channels
    fwrite(&data,  1, 2, fp);

    data = sr;
    fwrite(&data,  1, 4, fp);

    data = sr*bps/8;
    fwrite(&data,  1, 4, fp);

    data = (bps+7)/8;
    fwrite(&data,  1, 2, fp);

    data = bps;
    fwrite(&data,  1, 2, fp);

    if (bps == 32) {
        data = 0; // size of extension: 0
        fwrite(&data, 1, 2, fp);
    }

    fwrite("data", 1, 4, fp);
    data = 0xFFFFFFFF; // datasize unknown
    fwrite(&data,  1, 4, fp);

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
    ui8_t s[4*2*dsp->decM]; //uin8,int16,float32
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
        w[n] = 7938/18608.0 - 9240/18608.0*cos(_2PI*n/(taps-1)) + 1430/18608.0*cos(4*M_PI*n/(taps-1)); // Blackmann
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
        w[n] = 7938/18608.0 - 9240/18608.0*cos(_2PI*n/(taps-1)) + 1430/18608.0*cos(4*M_PI*n/(taps-1)); // Blackmann
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

static float complex lowpass0(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    ui32_t n;
    double complex w = 0;
    for (n = 0; n < taps; n++) {
        w += buffer[(sample+n)%taps]*ws[taps-1-n];
    }
    return (float complex)w;
}
//static __attribute__((optimize("-ffast-math"))) float complex lowpass()
static float complex lowpass(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    float complex w = 0;
    int n; // -Ofast
    int S = taps - (sample % taps);
    for (n = 0; n < taps; n++) {
        w += buffer[n]*ws[S+n]; // ws[taps+s-n] = ws[(taps+sample-n)%taps]
    }
    return w;
// symmetry: ws[n] == ws[taps-1-n]
}
static float complex lowpass2(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    float complex w = 0;
    int n;
    int s = sample % taps;
    int S1 = s;
    int S1N = S1-taps;
    int n0 = taps-s;
    for (n = 0; n < n0; n++) {
        w += buffer[S1+n]*ws[n];
    }
    for (n = n0; n < taps; n++) {
        w += buffer[S1N+n]*ws[n];
    }
    return w;
// symmetry: ws[n] == ws[taps-1-n]
}

static float re_lowpass0(float buffer[], ui32_t sample, ui32_t taps, float *ws) {
    ui32_t n;
    double w = 0;
    for (n = 0; n < taps; n++) {
        w += buffer[(sample+n)%taps]*ws[taps-1-n];
    }
    return (float)w;
}
static float re_lowpass(float buffer[], ui32_t sample, ui32_t taps, float *ws) {
    float w = 0;
    int n;
    int S = taps - (sample % taps);
    for (n = 0; n < taps; n++) {
        w += buffer[n]*ws[S+n]; // ws[taps+s-n] = ws[(taps+sample-n)%taps]
    }
    return w;
}


static int ifblock(dsp_t *dsp, float complex *z_out) {

    float complex z;
    int j;

    if ( f32read_cblock(dsp) < dsp->decM ) return EOF;

    for (j = 0; j < dsp->decM; j++) {
        if (dsp->opt_nolut) {
            double _s_base = (double)(dsp->sample_in*dsp->decM+j); // dsp->sample_dec
            double f0 = dsp->xlt_fq*_s_base;
            z = dsp->decMbuf[j] * cexp(f0*_2PI*I);
        }
        else if (dsp->exlut) {
            z = dsp->decMbuf[j] * dsp->ex[dsp->sample_decM];
        }
        else {
            z = dsp->decMbuf[j];
        }
        dsp->sample_decM += 1; if (dsp->sample_decM >= dsp->lut_len) dsp->sample_decM = 0;

        dsp->decXbuffer[dsp->sample_decX] = z;
        dsp->sample_decX += 1; if (dsp->sample_decX >= dsp->dectaps) dsp->sample_decX = 0;
    }
    if (dsp->decM > 1)
    {
        z = lowpass(dsp->decXbuffer, dsp->sample_decX, dsp->dectaps, ws_dec);
    }

    *z_out = z;

    dsp->sample_in += 1;

    return 0;
}

static int if_fm(dsp_t *dsp, float complex *z_out, float *s) {

    static float complex z0;
    float complex z, w;
    float s_fm = 0.0f;
    float gain = FM_GAIN;
    ui32_t _sample = dsp->sample_in * dsp->decFM;
    int m;
    int j;

    for (m = 0; m < dsp->decFM; m++)
    {

        if ( f32read_cblock(dsp) < dsp->decM ) return EOF;

        for (j = 0; j < dsp->decM; j++) {
            if (dsp->opt_nolut) {
                double _s_base = (double)(_sample*dsp->decM+j); // dsp->sample_dec
                double f0 = dsp->xlt_fq*_s_base;
                z = dsp->decMbuf[j] * cexp(f0*_2PI*I);
            }
            else if (dsp->exlut) {
                z = dsp->decMbuf[j] * dsp->ex[dsp->sample_decM];
            }
            else {
                z = dsp->decMbuf[j];
            }
            dsp->sample_decM += 1; if (dsp->sample_decM >= dsp->lut_len) dsp->sample_decM = 0;

            dsp->decXbuffer[dsp->sample_decX] = z;
            dsp->sample_decX += 1; if (dsp->sample_decX >= dsp->dectaps) dsp->sample_decX = 0;
        }
        if (dsp->decM > 1)
        {
            z = lowpass(dsp->decXbuffer, dsp->sample_decX, dsp->dectaps, ws_dec);
        }

        // IF-lowpass
        if (dsp->opt_lp & LP_IQ) {
            dsp->lpIQ_buf[_sample % dsp->lpIQtaps] = z;
            z = lowpass(dsp->lpIQ_buf, _sample+1, dsp->lpIQtaps, dsp->ws_lpIQ);
        }

        if (dsp->opt_fm) {
            w = z * conj(z0);
            s_fm = gain * carg(w)/M_PI;
            z0 = z;

            // FM-lowpass
            if (dsp->opt_lp & LP_FM) {
                dsp->lpFM_buf[_sample % dsp->lpFMtaps] = s_fm;
                if (m+1 == dsp->decFM) {
                    s_fm = re_lowpass(dsp->lpFM_buf, _sample+1, dsp->lpFMtaps, dsp->ws_lpFM);
                }
            }
        }

        *z_out = z;

        _sample += 1;

    }

    *s = s_fm;

    dsp->sample_in += 1;

    return 0;
}


/* -------------------------------------------------------------------------- */

#define IF_SAMPLE_RATE      48000
#define IF_SAMPLE_RATE_MIN  32000

static int IF_min = IF_SAMPLE_RATE;

#define IF_TRANSITION_BW (4e3)  // 4kHz transition width
#define FM_TRANSITION_BW (2e3)  // 2kHz transition width


static int init_buffers(dsp_t *dsp) {

    int K = 0;
    int n, k;


    // decimate
    int IF_sr = IF_min; // designated IF sample rate
    int decM = 1; // decimate M:1
    int sr_base = dsp->sr;
    float f_lp; // dec_lowpass: lowpass_bandwidth/2
    float t_bw; // dec_lowpass: transition_bandwidth
    int taps; // dec_lowpass: taps

    //if (dsp->opt_IFmin) IF_sr = IF_SAMPLE_RATE_MIN;
    if (IF_sr > sr_base) IF_sr = sr_base;
    if (IF_sr < sr_base) {
        while (sr_base % IF_sr) IF_sr += 1;
        decM = sr_base / IF_sr;
    }

    f_lp = (IF_sr+20e3)/(4.0*sr_base); // for IF=48k
    t_bw = (IF_sr-20e3)/*/2.0*/;
    if (dsp->opt_IFmin) {
        t_bw = (IF_sr-12e3);
    }
    if (t_bw < 0) t_bw = 10e3;
    t_bw /= sr_base;
    taps = 4.0/t_bw; if (taps%2==0) taps++;

    taps = lowpass_init(f_lp, taps, &ws_dec); // decimate lowpass
    if (taps < 0) return -1;
    dsp->dectaps = (ui32_t)taps;

    dsp->sr_base = sr_base;
    dsp->sr = IF_sr; // sr_base/decM
    dsp->decM = decM;

    fprintf(stderr, "IF: %d\n", IF_sr);
    fprintf(stderr, "dec: %d\n", decM);


    if (dsp->exlut && !dsp->opt_nolut)
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
            double t = f0*(double)n;
            dsp->ex[n] = cexp(t*_2PI*I);
        }
    }

    dsp->decXbuffer = calloc( dsp->dectaps+1, sizeof(float complex));
    if (dsp->decXbuffer == NULL) return -1;

    dsp->decMbuf = calloc( dsp->decM+1, sizeof(float complex));
    if (dsp->decMbuf == NULL) return -1;


    // IF lowpass
    if (dsp->opt_lp & LP_IQ)
    {
        float f_lp; // lowpass_bw
        int taps; // lowpass taps: 4*sr/transition_bw

        f_lp = 24e3/(float)dsp->sr/2.0; // default
        if (dsp->lpIQ_bw) f_lp = dsp->lpIQ_bw/(float)dsp->sr/2.0;
        taps = 4*dsp->sr/IF_TRANSITION_BW; if (taps%2==0) taps++;
        taps = lowpass_init(f_lp, taps, &dsp->ws_lpIQ); if (taps < 0) return -1;

        dsp->lpIQ_fbw = f_lp;
        dsp->lpIQtaps = taps;
        dsp->lpIQ_buf = calloc( dsp->lpIQtaps+3, sizeof(float complex));
        if (dsp->lpIQ_buf == NULL) return -1;

    }

    // FM lowpass
    if (dsp->opt_lp & LP_FM)
    {
        float f_lp; // lowpass_bw
        int taps; // lowpass taps: 4*sr/transition_bw

        f_lp = 10e3/(float)dsp->sr; // default
        if (dsp->lpFM_bw > 0) f_lp = dsp->lpFM_bw/(float)dsp->sr;
        taps = 4*dsp->sr/FM_TRANSITION_BW; if (taps%2==0) taps++;
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


    if (dsp->nch < 2) return -1;

    return K;
}

static int free_buffers(dsp_t *dsp) {

    // decimate
    if (dsp->decXbuffer) { free(dsp->decXbuffer); dsp->decXbuffer = NULL; }
    if (dsp->decMbuf)    { free(dsp->decMbuf);    dsp->decMbuf    = NULL; }
    if (dsp->exlut && !dsp->opt_nolut) {
        if (dsp->ex)     { free(dsp->ex);         dsp->ex         = NULL; }
    }

    if (ws_dec) { free(ws_dec); ws_dec = NULL; }


    // IF lowpass
    if (dsp->opt_lp & LP_IQ)
    {
        if (dsp->ws_lpIQ)  { free(dsp->ws_lpIQ);  dsp->ws_lpIQ  = NULL; }
        if (dsp->lpIQ_buf) { free(dsp->lpIQ_buf); dsp->lpIQ_buf = NULL; }
    }
    // FM lowpass
    if (dsp->opt_lp & LP_FM)
    {
        if (dsp->ws_lpFM)  { free(dsp->ws_lpFM);  dsp->ws_lpFM  = NULL; }
        if (dsp->lpFM_buf) { free(dsp->lpFM_buf); dsp->lpFM_buf = NULL; }
    }

    return 0;
}

/* ------------------------------------------------------------------------------------ */

#include <unistd.h>

static int write_cpx_blk(dsp_t *dsp, float complex *z, int len) {
    int j, l;
    short b[2*len];
    ui8_t u[2*len];
    float xy[2*len];
    int bps = dsp->bps_out;
    int fd = 1; // STDOUT_FILENO

    for (j = 0; j < len; j++) {
        xy[2*j  ] = creal(z[j]);
        xy[2*j+1] = cimag(z[j]);
    }

    if (bps == 32) {
        l = write(fd, xy, 2*len*bps/8);
    }
    else {
        for (j = 0; j < 2*len; j++) xy[j] *= 128.0; // 127.0
        if (bps == 8) {
            for (j = 0; j < 2*len; j++) {
                xy[j] += 128.0; // x *= scale8b;
                u[j] = (ui8_t)(xy[j]); //b = (int)(x+0.5);
            }
            l = write(fd, u, 2*len*bps/8);
        }
        else { // bps == 16
            for (j = 0; j < 2*len; j++) {
                xy[j] *= 256.0;
                b[j] = (short)xy[j]; //b = (int)(x+0.5);
            }
            l = write(fd, b, 2*len*bps/8);
        }
    }

    return l*8/(2*bps);
}

// fwrite return items: size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
static int fwrite_cpx_blk(dsp_t *dsp, float complex *z, int len) {
    int j, l;
    short b[2*len];
    ui8_t u[2*len];
    float xy[2*len];
    int bps = dsp->bps_out;
    FILE *fo = stdout;

    for (j = 0; j < len; j++) {
        xy[2*j  ] = creal(z[j]);
        xy[2*j+1] = cimag(z[j]);
    }

    if (bps == 32) {
        l = fwrite(xy, 2*bps/8, len, fo);
    }
    else {
        for (j = 0; j < 2*len; j++) xy[j] *= 128.0; // 127.0
        if (bps == 8) {
            for (j = 0; j < 2*len; j++) {
                xy[j] += 128.0; // x *= scale8b;
                u[j] = (ui8_t)(xy[j]); //b = (int)(x+0.5);
            }
            l = fwrite(u, 2*bps/8, len, fo);
        }
        else { // bps == 16
            for (j = 0; j < 2*len; j++) {
                xy[j] *= 256.0;
                b[j] = (short)xy[j]; //b = (int)(x+0.5);
            }
            l = fwrite(b, 2*bps/8, len, fo);
        }
    }

    return l;
}

static int fwrite_fm(dsp_t *dsp, float s) {
    int bps = dsp->bps_out;
    FILE *fpo = stdout;
    ui8_t u = 0;
    i16_t b = 0;
    ui32_t *w = (ui32_t*)&s;

    if (bps == 8) {
        s *= 127.0;
        s += 128.0;
        u = (ui8_t)s;
        w = (ui32_t*)&u;
    }
    else if (bps == 16) {
        s *= 127.0*256.0;
        b = (i16_t)s;
        w = (ui32_t*)&b;
    }
    fwrite( w, bps/8, 1, fpo);

    return 0;
}

static int fwrite_fm_blk(dsp_t *dsp, float *s, int len) {
    int j, l;
    short b[len];
    ui8_t u[len];
    float x[len];
    int bps = dsp->bps_out;
    FILE *fo = stdout;

    for (j = 0; j < len; j++) {
        x[j] = s[j];
    }

    if (bps == 32) {
        l = fwrite(x, bps/8, len, fo);
    }
    else {
        for (j = 0; j < len; j++) x[j] *= 128.0; // 127.0
        if (bps == 8) {
            for (j = 0; j < len; j++) {
                x[j] += 128.0; // x *= scale8b;
                u[j] = (ui8_t)(x[j]); //b = (int)(x+0.5);
            }
            l = fwrite(u, bps/8, len, fo);
        }
        else { // bps == 16
            for (j = 0; j < len; j++) {
                x[j] *= 256.0;
                b[j] = (short)x[j]; //b = (int)(x+0.5);
            }
            l = fwrite(b, bps/8, len, fo);
        }
    }

    return l;
}


/* ------------------------------------------------------------------------------------ */


#define ZLEN 64

int main(int argc, char *argv[]) {

    //int option_inv = 0;    // invertiert Signal
    int option_min = 0;
    //int option_iq = 5;
    int option_iqdc = 0;
    int option_lp = 0;
    int option_dc = 0;
    int option_noLUT = 0;
    int option_pcmraw = 0;
    int option_wav = 0;
    int option_fm = 0;
    int option_decFM = 0;

    int wavloaded = 0;

    FILE *fp;
    char *fpname = NULL;

    int k;

    int bitQ;

    int bps_out = 32;
    float lpIQ_bw = 10e3;


    pcm_t pcm = {0};
    dsp_t dsp = {0};  //memset(&dsp, 0, sizeof(dsp));


    setbuf(stdout, NULL);


    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       --iq0,2,3    (IQ data)\n");
            return 0;
        }
        else if   (strcmp(*argv, "--iqdc") == 0) { option_iqdc = 1; }  // iq-dc removal
        else if   (strcmp(*argv, "--iq") == 0) { // fq baseband -> IF (rotate from and decimate)
            double fq = 0.0;                     // --iq <fq> , -0.5 < fq < 0.5
            ++argv;
            if (*argv) fq = atof(*argv);
            else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            dsp.xlt_fq = -fq; // S(t) -> S(t)*exp(-f*2pi*I*t)
            dsp.exlut = 1;
            //option_iq = 5;
        }
        else if   (strcmp(*argv, "--IFbw") == 0) {  // min IF bandwidth / kHz
            int ifbw = 0;
            ++argv;
            if (*argv) ifbw = atoi(*argv);
            else return -1;
            if (ifbw*1000 >= IF_SAMPLE_RATE_MIN) IF_min = ifbw*1000;
            // ?option_lp |= LP_IQ;
        }
        else if   (strcmp(*argv, "--lpIQ") == 0) { option_lp |= LP_IQ; }  // IQ/IF lowpass
        else if   (strcmp(*argv, "--lpbw") == 0) {  // IQ lowpass BW / kHz
            double bw = 0.0;
            ++argv;
            if (*argv) bw = atof(*argv);
            else return -1;
            if (bw > 1.0f) lpIQ_bw = bw*1e3;
            option_lp |= LP_IQ;
        }
        else if   (strcmp(*argv, "--FM") == 0) { option_fm = 1; }
        else if   (strcmp(*argv, "--lpFM") == 0) {
            option_lp |= LP_FM;  // FM lowpass
            option_fm = 1;
        }
        else if   (strcmp(*argv, "--decFM") == 0) {   // FM decimation
            option_decFM = 4;
            option_lp |= LP_FM;  // FM lowpass
            option_fm = 1;
        }
        else if   (strcmp(*argv, "--dc") == 0) { option_dc = 1; }
        else if   (strcmp(*argv, "--noLUT") == 0) { option_noLUT = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if (strcmp(*argv, "--wav") == 0) {
            option_wav = 1;
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
        else if (strcmp(*argv, "--bo") == 0) {
            ++argv;
            if (*argv) bps_out = atoi(*argv); else return -1;
            if ((bps_out != 8 && bps_out != 16 && bps_out != 32)) {
                bps_out = 0;
            }
        }
        else {
            fp = fopen(*argv, "rb");
            if (fp == NULL) {
                fprintf(stderr, "error: open %s\n", *argv);
                return -1;
            }
            wavloaded = 1;
        }
        ++argv;
    }
    if (!wavloaded) fp = stdin;

    if (/*option_iq == 5 &&*/ option_dc) option_lp |= LP_FM;

    // LUT faster for decM, however frequency correction after decimation
    // LUT recommonded if decM > 2
    //
    if (option_noLUT /*&& option_iq == 5*/) dsp.opt_nolut = 1; else dsp.opt_nolut = 0;


    pcm.sel_ch = 0;
    if (option_pcmraw == 0) {
        k = read_wav_header(&pcm, fp);
        if ( k < 0 ) {
            fclose(fp);
            fprintf(stderr, "error: wav header\n");
            return -1;
        }
    }


    // init dsp
    //
    dsp.fp = fp;
    dsp.sr = pcm.sr;
    dsp.bps = pcm.bps;
    dsp.nch = pcm.nch;
    dsp.ch = pcm.sel_ch;
    //dsp.opt_iq = option_iq;
    dsp.opt_iqdc = option_iqdc; // in f32read_cblock() anyway
    dsp.opt_lp = option_lp;
    dsp.lpIQ_bw = lpIQ_bw;  // 10e3 // IF lowpass bandwidth
    dsp.lpFM_bw = 6e3; // FM audio lowpass
    dsp.opt_IFmin = option_min;
    dsp.bps_out = bps_out;

    if (option_fm) dsp.opt_fm = 1;

    k = init_buffers(&dsp);
    if ( k < 0 ) {
        fprintf(stderr, "error: init buffers\n");
        return -1;
    }
    // base: dsp.sr_base
    // if  : dsp.sr

    dsp.decFM = 1;
    if (option_decFM) {
        int fm_sr = dsp.sr;
        while (fm_sr % 2 == 0 && fm_sr/2 >= 48000) {
            fm_sr /= 2;
            dsp.decFM *= 2;
        }
        // if (dsp.decFM > 1) option_lp |= LP_FM; // set above
        dsp.opt_fm = 1;
    }

    pcm.sr_out = dsp.sr;
    pcm.bps_out = dsp.bps_out;
    if (option_fm) {
        pcm.nch = 1;
        pcm.sr_out = dsp.sr / dsp.decFM;
    }
    if (option_wav) write_wav_header( &pcm );


    int len = ZLEN;
    int l, n = 0;

    float complex z_vec[ZLEN]; // init ?
    float s_vec[ZLEN];

    bitQ = 0;
    while ( bitQ != EOF )
    {
        bitQ = if_fm(&dsp, z_vec+n, s_vec+n);
        n++;
        if (n == len || bitQ == EOF) {
            if (bitQ == EOF) n--;
            if (dsp.opt_fm) {
                l = fwrite_fm_blk(&dsp, s_vec, n);
            }
            else {
                l = fwrite_cpx_blk(&dsp, z_vec, n);
            }
            n = 0;
        }
    }


    free_buffers(&dsp);

    fclose(fp);

    return 0;
}

