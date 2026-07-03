
/*
   C50
   (recommended: sample rate 48kHz)
   gcc c50iq.c -lm -o c50iq
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include <math.h>

#ifdef CYGWIN
  #include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif

// optional JSON "version"
//  (a) set global
//      gcc -DVERSION_JSN [-I<inc_dir>] ...
#ifdef VERSION_JSN
  #include "version_jsn.h"
#endif
// or
//  (b) set local compiler option, e.g.
//      gcc -DVER_JSN_STR=\"0.0.2\" ...


typedef unsigned char  ui8_t;
typedef unsigned int   ui32_t;

#ifndef M_PI
    #define M_PI  (3.1415926535897932384626433832795)
#endif
#define _2PI  (6.2831853071795864769252867665590)

#define LP_IQ    1
#define LP_FM    2

#define FM_GAIN (0.8)


static int  option_verbose = 0,
            option_xor = 0,
            option_xor_auto = 0,
            option_raw = 0,
            option_ptu = 0,
            option_json = 0;


typedef struct {
    //int frnr;
    int sn;
    int jahr; int monat; int tag;
    int std; int min; int sek;
    float lat; float lon; float alt;
    ui32_t raw_lat; ui32_t raw_lon; ui32_t raw_alt;
    unsigned chk;
    float T; float RH;
    int jsn_freq;   // freq/kHz (SDR)
} gpx_t;

static gpx_t gpx;

/* ------------------------------------------------------------------------------------ */

#define BAUD_RATE 2400

typedef struct {
    int sr;       // sample_rate
    int bps;      // bits_sample  bits/sample
    int nch;      // channels
    int sel_ch;   // select wav channel
} pcm_t;

static int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}

static
int read_wav_header(pcm_t *pcm, FILE *fp) {
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


typedef struct {
    FILE *fp;
    //
    int sr;       // sample_rate
    int bps;      // bits/sample
    int nch;      // channels
    int ch;       // select channel
    //
    float sps;    // samples per symbol
    float br;     // baud rate
    //
    ui32_t sample_in;
    ui32_t sample_count;
    ui32_t sc;
    int M;
    float *bufs;
    float mv;
    ui32_t mv_pos;
    ui32_t pre_pos;
    //

    // IQ-data
    int opt_iq;
    int opt_iqdc;
    float complex iqbuf[2]; // float complex *rot_iqbuf;

    // dc offset
    int opt_dc;
    int locked;
    double dc;
    double Df;
    double dDf;
    float xsum;

    // decimate
    int opt_nolut; // default: LUT
    int opt_IFmin;
    int decM;
    ui32_t sr_base;
    ui32_t dectaps;
    ui32_t sample_decX;
    ui32_t lut_len;
    ui32_t sample_decM;
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

    int opt_fmdec;
    int decFM;
    int sr_fm;

} dsp_t;


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

static float complex lowpass1a(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    double complex w = 0;
    ui32_t n;
    ui32_t S = taps-1 + (sample % taps);
    for (n = 0; n < taps; n++) {
        w += buffer[n]*ws[S-n]; // ws[taps+s-n] = ws[(taps+sample-n)%taps]
    }
    return (float complex)w;
// symmetry: ws[n] == ws[taps-1-n]
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
    float complex w = 0;     // -Ofast
    int n;
    int s = sample % taps; // lpIQ
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

static float re_lowpass(float buffer[], ui32_t sample, ui32_t taps, float *ws) {
    float w = 0;
    int n;
    int S = taps - (sample % taps);
    for (n = 0; n < taps; n++) {
        w += buffer[n]*ws[S+n]; // ws[taps+s-n] = ws[(taps+sample-n)%taps]
    }
    return w;
}

static
int f32_sample(dsp_t *dsp, float *out) {
    float s = 0.0;
    float s_fm = s;

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

        if (dsp->opt_iq)
        {
            if (dsp->opt_iq >= 5) {
                ui32_t s_reset = dsp->dectaps*dsp->lut_len;
                int j;
                if ( f32read_cblock(dsp) < dsp->decM ) return EOF;
                for (j = 0; j < dsp->decM; j++) {
                    if (dsp->opt_nolut) {
                        double _s_base = (double)(_sample*dsp->decM+j); // dsp->sample_dec
                        double f0 = dsp->xlt_fq*_s_base - dsp->Df*_s_base/(double)dsp->sr_base;
                        z = dsp->decMbuf[j] * cexp(f0*_2PI*I);
                    }
                    else {
                        z = dsp->decMbuf[j] * dsp->ex[dsp->sample_decM];
                    }
                    dsp->sample_decM += 1; if (dsp->sample_decM >= dsp->lut_len) dsp->sample_decM = 0;

                    dsp->decXbuffer[dsp->sample_decX] = z;
                    dsp->sample_decX += 1; if (dsp->sample_decX >= dsp->dectaps) dsp->sample_decX = 0;
                }
                if (dsp->decM > 1)
                {
                    z = lowpass(dsp->decXbuffer, dsp->sample_decX, dsp->dectaps, ws_dec);
                }
            }
            else if ( f32read_csample(dsp, &z) == EOF ) return EOF;

            if (dsp->opt_dc && !dsp->opt_nolut) {
                z *= cexp(-t*_2PI*dsp->Df*I);
            }


            // IF-lowpass
            if (dsp->opt_lp & LP_IQ) {
                dsp->lpIQ_buf[_sample % dsp->lpIQtaps] = z;
                z = lowpass(dsp->lpIQ_buf, _sample+1, dsp->lpIQtaps, dsp->ws_lpIQ);
            }


            z0 = dsp->iqbuf[(_sample-1) & 1];  // z0 = dsp->rot_iqbuf[(_sample-1 + dsp->N_IQBUF) % dsp->N_IQBUF];
            w = z * conj(z0);
            s_fm = gain * carg(w)/M_PI;

            dsp->iqbuf[_sample & 1] = z;  // dsp->rot_iqbuf[_sample % dsp->N_IQBUF] = z;


            s = s_fm; //opt_iq=1,6
        }
        else {
            if (f32read_sample(dsp, &s) == EOF) return EOF;
            s_fm = s; //opt_iq==0
        }

        // FM-lowpass
        if (dsp->opt_lp & LP_FM) {
            dsp->lpFM_buf[_sample % dsp->lpFMtaps] = s_fm;
            if (m+1 == decFM) {
                s_fm = re_lowpass(dsp->lpFM_buf, _sample+1, dsp->lpFMtaps, dsp->ws_lpFM);
                if (dsp->opt_iq < 2 || dsp->opt_iq > 5) s = s_fm; //opt_iq==0,1,6
            }
        }

        _sample += 1;

    }

    if (dsp->opt_dc && !dsp->opt_iq)
    {
        s -= dsp->dc*0.4;
    }


    dsp->bufs[dsp->sample_in % dsp->M] = s;


    if (dsp->opt_dc)
    {
        float xneu, xalt;
        xneu = dsp->bufs[ dsp->sample_in % dsp->M];
        xalt = dsp->bufs[(dsp->sample_in+1) % dsp->M];
        dsp->xsum +=  xneu - xalt;

        if ((dsp->sample_in+dsp->pre_pos) % dsp->sr == 0)
        {
            double dc = dsp->xsum / (double)dsp->M;
            dsp->dc = dc;
            dsp->dDf = dsp->sr * dsp->dc / (2.0*FM_GAIN);  // remaining freq offset
            dsp->Df += dsp->dDf*0.5;

            if (dsp->opt_iq) {
                if (fabs(dsp->dDf) > 2e3) {
                    if (dsp->locked) {
                        dsp->locked = 0;
                        dsp->ws_lpIQ = dsp->ws_lpIQ0;
                    }
                }
                else {
                    if (dsp->locked == 0) {
                        dsp->locked = 1;
                        dsp->ws_lpIQ = dsp->ws_lpIQ1;
                    }
                }
            }
            //DBG: if (dsp->opt_iq) fprintf(stderr, "Df: %+.3f\n", dsp->Df);
        }
    }

    dsp->sample_in += 1;

    *out = s;

    return 0;
}


/* -------------------------------------------------------------------------- */
static int datetime2GPSweek(int yyyy, int mm, int dd,
                            int hr, int min, int sec,
                            int *week, int *tow) {
    int ww = 0;
    int tt = 0;
    int gpsDays = 0;

    if ( mm < 3 ) { yyyy -= 1; mm += 12; }

    gpsDays = (int)(365.25*yyyy) + (int)(30.6001*(mm+1.0)) + dd - 723263; // 1980-01-06

    ww = gpsDays / 7;
    tt = gpsDays % 7;
    tt = tt*86400 + hr*3600 + min*60 + sec;

    *week = ww;
    *tow  = tt;

    return 0;
}
/* ------------------------------------------------------------------------------------ */


#define IF_SAMPLE_RATE      48000
#define IF_SAMPLE_RATE_MIN  32000

#define IF_TRANSITION_BW (4e3)  // (min) transition width
#define FM_TRANSITION_BW (2e3)  // (min) transition width


static
int init_buffers(dsp_t *dsp) {
    int i, pos;
    float b0, b1, b2, b;
    double t;
    int n, k;


    // decimate
    if (dsp->opt_iq >= 5)
    {
        int IF_sr = IF_SAMPLE_RATE; // designated IF sample rate
        int decM = 1; // decimate M:1
        int sr_base = dsp->sr;
        float f_lp; // dec_lowpass: lowpass_bandwidth/2
        float t_bw; // dec_lowpass: transition_bandwidth
        int taps; // dec_lowpass: taps

        if (dsp->opt_IFmin) IF_sr = IF_SAMPLE_RATE_MIN;

        if (IF_sr > sr_base) IF_sr = sr_base;
        if (IF_sr < sr_base) {
            while (sr_base % IF_sr) IF_sr += 1;
            decM = sr_base / IF_sr;
        }

        f_lp = (IF_sr+20e3)/(4.0*sr_base);
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
        dsp->sps /= (float)decM;
        dsp->decM = decM;

        fprintf(stderr, "IF: %d\n", IF_sr);
        fprintf(stderr, "dec: %d\n", decM);
    }
    if (dsp->opt_iq >= 5)
    {
        if (!dsp->opt_nolut)
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
                dsp->ex[n] = cexp(t*_2PI*I);
            }
        }

        dsp->decXbuffer = calloc( dsp->dectaps+1, sizeof(float complex));
        if (dsp->decXbuffer == NULL) return -1;

        dsp->decMbuf = calloc( dsp->decM+1, sizeof(float complex));
        if (dsp->decMbuf == NULL) return -1;
    }

    // IF lowpass
    if (dsp->opt_iq && (dsp->opt_lp & LP_IQ))
    {
        float f_lp; // lowpass_bw
        int taps; // lowpass taps: 4*sr/transition_bw

        f_lp = 24e3/(float)dsp->sr/2.0; // default
        if (dsp->lpIQ_bw) f_lp = dsp->lpIQ_bw/(float)dsp->sr/2.0;
        taps = 4*dsp->sr/IF_TRANSITION_BW;
        //if (dsp->sr > 80e3) taps = taps/2;
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
        }
    }

    // FM lowpass
    if (dsp->opt_lp & LP_FM)
    {
        float f_lp; // lowpass_bw
        int taps; // lowpass taps: 4*sr/transition_bw

        f_lp = 10e3/(float)dsp->sr; // default
        if (dsp->lpFM_bw > 0) f_lp = dsp->lpFM_bw/(float)dsp->sr;
        taps = 4*dsp->sr/FM_TRANSITION_BW; if (taps%2==0) taps++;
        if (dsp->decFM > 1)
        {
            f_lp *= 2; //if (dsp->opt_iq >= 2 && dsp->opt_iq < 6) f_lp *= 2;
            taps = taps/2;
        }
        if (dsp->sr > 100e3) taps = taps/2;
        if (taps%2==0) taps++;
        taps = lowpass_init(f_lp, taps, &dsp->ws_lpFM); if (taps < 0) return -1;

        dsp->lpFMtaps = taps;
        dsp->lpFM_buf = calloc( dsp->lpFMtaps+3, sizeof(float)); // re_lowpass: size(float)  (complex)lowpass: sizeof(float complex)
        if (dsp->lpFM_buf == NULL) return -1;
    }


    memset(&IQdc, 0, sizeof(IQdc));
    IQdc.maxlim = dsp->sr;
    IQdc.maxcnt = IQdc.maxlim/32; // 32,16,8,4,2,1
    if (dsp->decM > 1) {
        IQdc.maxlim *= dsp->decM;
        IQdc.maxcnt *= dsp->decM;
    }


    dsp->sample_in = 0;
    dsp->M = dsp->sps*32;  // a) dec buffer , b) len average/dc

    dsp->bufs = (float *)calloc( dsp->M+1, sizeof(float)); if (dsp->bufs  == NULL) return -100;

    if (dsp->opt_iq)
    {
        if (dsp->nch < 2) return -1;
    }


    return 0;
}

static
int free_buffers(dsp_t *dsp) {

    if (dsp->bufs)  { free(dsp->bufs);  dsp->bufs  = NULL; }

    // decimate
    if (dsp->opt_iq >= 5)
    {
        if (dsp->decXbuffer) { free(dsp->decXbuffer); dsp->decXbuffer = NULL; }
        if (dsp->decMbuf)    { free(dsp->decMbuf);    dsp->decMbuf    = NULL; }
        if (!dsp->opt_nolut) {
            if (dsp->ex)     { free(dsp->ex);         dsp->ex         = NULL; }
        }

        if (ws_dec) { free(ws_dec); ws_dec = NULL; }
    }

    // IF lowpass
    if (dsp->opt_iq && (dsp->opt_lp & LP_IQ))
    {
        if (dsp->ws_lpIQ0) { free(dsp->ws_lpIQ0); dsp->ws_lpIQ0 = NULL; }
        if (dsp->ws_lpIQ1) { free(dsp->ws_lpIQ1); dsp->ws_lpIQ1 = NULL; }
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

#define BITS 10   // (8N1: 0 bbbbbbbb 1)
#define LEN_BITFRAME  (9*BITS)
#define HEADLEN 20

static char header[] =   "00000000010111111111"; // 0x00 0xFF
static char buf[HEADLEN+1] = "x";
static int bufpos = -1;

static int    bitpos;
static ui8_t  bits[LEN_BITFRAME+20] = { 0,  0, 0, 0, 0, 0, 0, 0, 0,  1,
                                        0,  1, 1, 1, 1, 1, 1, 1, 1,  1};
static ui8_t  bytes[LEN_BITFRAME/BITS+2];

static float *buffer = NULL;

/* ------------------------------------------------------------------------------------ */


static void inc_bufpos() {
  bufpos = (bufpos+1) % HEADLEN;
}

static int compare() {
    int i=0, j = bufpos;

    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != header[HEADLEN-1-i]) break;
        j--;
        i++;
    }
    return i;
}

static int bits2bytes8N1(ui8_t bits[], ui8_t bytes[], int n) {
    int i, j, byteval=0, d=1;

    for (j = 0; j < n; j++) {
        byteval=0; d=1;
        for (i = 1; i < BITS-1; i++) {  // little endian
        /* for (i = 7; i >= 0; i--) { // big endian */
            if     (bits[BITS*j+i] == 1)   byteval += d;
            else /*(bits[BITS*j+i] == 0)*/ byteval += 0;
            d <<= 1;
        }
        bytes[j] = byteval;
    }
    return 0;
}

static int xor_base = 0x7CA00000 ^ 0x2EFCF;  // TODO
static int xmask = 0;
static int xor_count = 0;

static void printGPX() {
    int i;

        if (gpx.sn) printf("( %d ) ", gpx.sn);
        printf(" %04d-%02d-%02d", gpx.jahr, gpx.monat, gpx.tag);
        printf(" %02d:%02d:%02d", gpx.std, gpx.min, gpx.sek);
        printf(" ");
        printf(" lat: %.5f", gpx.lat);
        printf(" lon: %.5f", gpx.lon);
        printf(" alt: %.1f", gpx.alt);

        if (option_ptu && (gpx.T > -273.0 || gpx.RH > -0.5)) {
            printf(" ");
            if (gpx.T > -273.0) printf(" T=%.1fC", gpx.T);
            if (gpx.RH > -0.5) printf(" RH=%.0f%%", gpx.RH);
        }

        if (option_verbose) {
            printf("  # ");
            for (i = 0; i < 5; i++) printf("%d", (gpx.chk>>i)&1);
            if (option_ptu) for (i = 6; i < 8; i++) printf("%d", (gpx.chk>>i)&1);
        }

        printf("\n");
}

static void printJSON() {
    // UTC or GPS time ?
    char *ver_jsn = NULL;
    char json_sonde_id[] = "C50-xxxx\0\0\0\0\0\0\0";
    int gps_week = 0,
        gps_tow = 0;
    ui32_t datetime_cnt = 0;

    // seconds since GPS (ignoring leap seconds, C50=UTC)
    datetime2GPSweek(gpx.jahr, gpx.monat, gpx.tag, gpx.std, gpx.min, gpx.sek, &gps_week, &gps_tow);
    datetime_cnt = gps_week*604800 + gps_tow; // SECONDS_IN_WEEK=7*86400=604800  ( unsigned roll-over: 2116-02-11 )

    if (gpx.sn) {
        sprintf(json_sonde_id, "C50-%u", gpx.sn);
    }
    printf("{ \"type\": \"%s\"", "C50");
    printf(", \"frame\": %u", datetime_cnt); // frame number based on date/time
    printf(", \"id\": \"%s\", ", json_sonde_id);
    printf("\"datetime\": \"%04d-%02d-%02dT%02d:%02d:%02dZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.1f",
           gpx.jahr, gpx.monat, gpx.tag, gpx.std, gpx.min, gpx.sek, gpx.lat, gpx.lon, gpx.alt);
    if (option_ptu && (gpx.T > -273.0 || gpx.RH > -0.5)) {
        if (gpx.T > -273.0) printf(", \"temp\": %.1f", gpx.T);
        if (gpx.RH > -0.5) printf(", \"humidity\": %.1f", gpx.RH);
    }
    if (gpx.jsn_freq > 0) {
        printf(", \"freq\": %d", gpx.jsn_freq);
    }

    // Reference time/position
    printf(", \"ref_datetime\": \"%s\"", "UTC" ); // {"GPS", "UTC"} GPS-UTC=leap_sec
    printf(", \"ref_position\": \"%s\"", "MSL" ); // {"GPS", "MSL"} GPS=ellipsoid , MSL=geoid

    #ifdef VER_JSN_STR
        ver_jsn = VER_JSN_STR;
    #endif
    if (ver_jsn && *ver_jsn != '\0') printf(", \"version\": \"%s\"", ver_jsn);
    printf(" }\n");
    //printf("\n");
}

// Chechsum Fletcher16
static unsigned check2(ui8_t *bytes, int len) {
    int sum1, sum2;
    int i;

    sum1 = 0;
    sum2 = 0;
    for (i = 0; i < len; i++) {
        sum1 += bytes[i];
        sum2 += (len-i)*bytes[i];
    }
    sum1 = sum1 & 0xFF;
    sum2 = (-1-sum2) & 0xFF; // = (~sum2) & 0xFF;

    return sum2 | (sum1<<8);
}
/* // equivalent
static unsigned check16(ui8_t *bytes, int len) {
    unsigned sum1, sum2;
    int i;
    sum1 = sum2 = 0;
    for (i = 0; i < len; i++) {
        sum1 = (sum1 + bytes[i]) % 0x100;
        sum2 = (sum2 + sum1) % 0x100;
    }
    sum2 = (~sum2) & 0xFF;  // 1's complement
    return sum2 | (sum1<<8);
}
*/

static float NMEAll2(int ll) {  // NMEA GGA,GLL: ll/1e5=(D)DDMM.mmmm
    int deg = ll / 10000000;
    float min = (ll - deg*10000000)/1e5;
    return deg+min/60.0;
}

static int evalBytes2(dsp_t *dsp) {
    int i, val = 0;
    ui8_t id = bytes[2];
    unsigned check;
    static unsigned int cnt_dat = -1, cnt_tim = -1,
                        cnt_lat = -1, cnt_lon = -1, cnt_alt = -1,
                        cnt_sn = -1,
                        cnt_t3 = -1, cnt_rh = -1;

    check = ((bytes[7]<<8)|bytes[8]) != check2(bytes+2, 5);

    for (i = 0; i < 4; i++)  val |= bytes[6-i] << (8*i);

    if      (id == 0x14 ) {  // date
        int tag = val / 10000;
        int mon = (val-tag*10000) / 100;
        int jrz = val % 100;
        gpx.tag = tag;
        gpx.monat = mon;
        gpx.jahr = 2000+jrz;
        gpx.chk = (gpx.chk & ~(0x1<<0)) | (check<<0);
        if (check==0) cnt_dat = dsp->sample_count;
    }
    else if (id == 0x15 ) {  // time (UTC)
        int std = val / 10000;
        int min = (val-std*10000) / 100;
        int sek = val % 100;
        gpx.std = std;
        gpx.min = min;
        gpx.sek = sek;
        gpx.chk = (gpx.chk & ~(0x1<<1)) | (check<<1);
        if (check==0) cnt_tim = dsp->sample_count;

        // 2024-10-03, 2024-12-10: xpos?
        xmask = xor_base ^ val;  // TODO
    }
    else if (id == 0x16 ) {  // lat: wie NMEA mit Faktor 1e5   // <-> id==0x32
        gpx.raw_lat = val;
        if (option_xor) val ^= xmask;
        gpx.lat = NMEAll2(val);
        gpx.chk = (gpx.chk & ~(0x1<<2)) | (check<<2);
        if (check==0) {
            cnt_lat = dsp->sample_count;
            if (gpx.lat > 90.0 || gpx.lat < -90.0) xor_count += 1;
            if (option_xor_auto && xor_count > 6) {
                option_xor ^= 1;
                xor_count = 0;
            }
        }
    }
    else if (id == 0x17 ) {  // lon: wie NMEA mit Faktor 1e5   // <-> id==0x33
        gpx.raw_lon = val;
        if (option_xor) val ^= xmask;
        gpx.lon = NMEAll2(val);
        gpx.chk = (gpx.chk & ~(0x1<<3)) | (check<<3);
        if (check==0) {
            cnt_lon = dsp->sample_count;
        }
    }
    else if (id == 0x18 ) {  // alt: decimeter (MSL)   // <-> id==0x34
        gpx.raw_alt = val;
        if (option_xor) val ^= xmask;
        gpx.alt = val/10.0;
        gpx.chk = (gpx.chk & ~(0x1<<4)) | (check<<4);
        if (check==0) {
            cnt_alt = dsp->sample_count;
            if (gpx.alt > 1e5 || gpx.lat < -1e3) xor_count += 1;
        }
    }
    else if (id == 0x64 ) {  // serial number
        if (check==0) gpx.sn = val; // 16 bit
        //gpx.chk = (gpx.chk & ~(0x1<<15)) | (check<<15);
        //if (check==0) cnt_sn = dsp->sample_count;
    }

    if (id == 0x18) {
        printGPX();
        if (option_json && check==0) {
            if ( ((cnt_dat|cnt_tim|cnt_lat|cnt_lon)&0x80000000)==0 &&
                 cnt_alt - cnt_dat < dsp->sr &&
                 cnt_alt - cnt_tim < dsp->sr &&
                 cnt_alt - cnt_lat < dsp->sr &&
                 cnt_alt - cnt_lon < dsp->sr )
            {
                if (cnt_alt - cnt_t3 > dsp->sr) gpx.T = -273.15;
                if (cnt_alt - cnt_rh > dsp->sr) gpx.RH = -1.0;
                if (gpx.alt >= -100.0 && gpx.alt <= 50000.0 && gpx.lat >= -90.0  && gpx.lat <= 90.0) {
                    printJSON();
                }
            }
        }
    }

    // PTU floats
    if (id == 0x03) {  // temperature
        float t = -273.15;
        memcpy(&t, &val, 4);
        if (t < -273.0 || t > 100.0) t = -273.15;
        gpx.T = t;
        gpx.chk = (gpx.chk & ~(0x1<<6)) | (check<<6);
        if (check==0) cnt_t3 = dsp->sample_count;
    }
    if (id == 0x10) {  // rel. humidity
        float rh = -1.0;
        memcpy(&rh, &val, 4);
        if (rh < -0.4 || rh > 110.0) rh = -1.0;
        gpx.RH = rh;
        gpx.chk = (gpx.chk & ~(0x1<<7)) | (check<<7);
        if (check==0) cnt_rh = dsp->sample_count;
    }

    return check;
}

static void printRaw(int n) {
    int j;
    unsigned chkbyt = (bytes[7]<<8) | bytes[8];
    unsigned chksum = check2(bytes+2, 5);
    //if (chksum == chkbyt)
    {
        for (j = 0; j < LEN_BITFRAME; j++) {
            if (j%BITS == 1) printf(" ");
            if (j%BITS == 9) printf(" ");
            printf("%d", bits[j]);
        }
        printf("  :  ");
        printf("%02X%02X ", bytes[0], bytes[1]);
        printf("%02X ", bytes[2]);
        printf("%02X%02X%02X%02X ", bytes[3], bytes[4], bytes[5], bytes[6]);
        printf("%02X%02X", bytes[7], bytes[8]); // chkbyt
        if (option_verbose) {
            printf("  #  %04X", chksum);
            if (chksum == chkbyt) printf(" [OK]"); else printf(" [NO]");
        }
        printf("\n");
    }
}

int main(int argc, char *argv[]) {

    FILE *fp;
    char *fpname;
    int i;
    int bit = 8, bit0 = 8;
    int pos = 0, pos0 = 0;
    double pos_bit = 0;
    int header_found = 0;
    double bitlen; // sample_rate/BAUD_RATE
    int len;
    int cfreq = -1;

    int N, ptr;

    float f1, f2;
    float complex F1sum = 0;
    float complex F2sum = 0;
    float complex iw1, iw2;
    float complex X0 = 0;
    float complex X  = 0;
    float xbit = 0.0;
    float s = 0.0;
    float s3 = 0.0;
    float sbuf[3] = {0, 0, 0};

    int n;
    double t  = 0.0;
    double tn = 0.0;
    double x  = 0.0;
    double x0 = 0.0;

    float lpIQ_bw = 16e3;

    int option_iq = 0;
    int option_lp = 0;
    int option_dc = 0;
    int option_decFM = 0;
    int option_noLUT = 0;
    int option_iqdc = 0;
    int option_pcmraw = 0;
    int option_min = 0;
    int wavloaded = 0;
    int sel_wavch = 0;

    int k;

    pcm_t pcm = {0};
    dsp_t dsp = {0};


#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _setmode(_fileno(stdin), _O_BINARY);
#endif
    setbuf(stdout, NULL);


    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v, --verbose\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       --ptu\n");
            fprintf(stderr, "       --iq <fq>    (IQ input, -0.5 < fq < 0.5)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "--xor") == 0) ) {
            option_xor = 1;
        }
        else if ( (strcmp(*argv, "--xor-auto") == 0) ) {
            option_xor_auto = 1;
        }
        else if ( (strcmp(*argv, "--ptu") == 0) ) {
            option_ptu = 1;
        }
        else if ( (strcmp(*argv, "--json") == 0) ) {
            option_verbose = 1;
            option_json = 1;
        }
        else if ( (strcmp(*argv, "--jsn_cfq") == 0) ) {
            int frq = -1;  // center frequency / Hz
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
        }
        else if ( (strcmp(*argv, "--iq") == 0) ) { // fq baseband -> IF (rotate from and decimate)
            double fq = 0.0;                       // --iq <fq> , -0.5 < fq < 0.5
            ++argv;
            if (*argv) fq = atof(*argv);
            else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            dsp.xlt_fq = -fq; // S(t) -> S(t)*exp(-f*2pi*I*t)
            option_iq = 6;
        }
        else if   (strcmp(*argv, "--lpIQ") == 0) { option_lp |= LP_IQ; }  // IQ lowpass
        else if   (strcmp(*argv, "--lpbw") == 0) {  // IQ lowpass BW / kHz
            double bw = 0.0;
            ++argv;
            if (*argv) bw = atof(*argv);
            else return -1;
            if (bw > 4.0 && bw < 256.0) lpIQ_bw = bw*1e3;
            option_lp |= LP_IQ;
        }
        else if   (strcmp(*argv, "--lpFM") == 0) { option_lp |= LP_FM; }  // FM lowpass
        else if   (strcmp(*argv, "--decFM") == 0) {   // FM decimation
            option_decFM = 2;
        }
        else if   (strcmp(*argv, "--dc") == 0) { option_dc = 1; }
        else if   (strcmp(*argv, "--noLUT") == 0) { option_noLUT = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
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
                fprintf(stderr, "error: open %s\n", *argv);
                return -1;
            }
            wavloaded = 1;
        }
        ++argv;
    }
    if (!wavloaded) fp = stdin;

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

    gpx.jsn_freq = 0;
    if (cfreq > 0) {
        int fq_kHz = (cfreq - dsp.xlt_fq*pcm.sr + 500)/1e3;
        gpx.jsn_freq = fq_kHz;
    }

    // init dsp
    //
    dsp.fp = fp;
    dsp.sr = pcm.sr;
    dsp.bps = pcm.bps;
    dsp.nch = pcm.nch;
    dsp.ch = pcm.sel_ch;
    dsp.br = (float)BAUD_RATE;

    dsp.sps = (float)dsp.sr/dsp.br;
    dsp.decFM = 1;

    dsp.opt_iq = option_iq;
    dsp.opt_iqdc = option_iqdc;
    dsp.opt_lp = option_lp;
    dsp.lpIQ_bw = lpIQ_bw; // IF lowpass bandwidth
    dsp.lpFM_bw = 8e3; // FM audio lowpass
    if (option_iq == 6) dsp.lpFM_bw = 8e3;
    else if (option_iq == 5) dsp.lpFM_bw = 8e3;
    dsp.opt_dc = option_dc;
    dsp.opt_IFmin = option_min;

    // LUT faster, however frequency correction after decimation
    // LUT recommonded if decM > 2
    //
    if (option_noLUT && option_iq >= 5) dsp.opt_nolut = 1; else dsp.opt_nolut = 0;


    init_buffers(&dsp); // free

    dsp.sr_fm = dsp.sr;
    bitlen = dsp.sr_fm/(double)BAUD_RATE;

    f1 = 4720.0;  // bit0: 4800Hz
    f2 = 2900.0;  // bit1: 3000Hz
    iw1 = _2PI*I*f1 / (double)dsp.sr_fm;
    iw2 = _2PI*I*f2 / (double)dsp.sr_fm;

    N = 4*bitlen + 0.5;
    buffer = calloc( N+1, sizeof(float)); if (buffer == NULL) return -1;

    n = bitlen*1.3; //+6

    ptr = -1; dsp.sample_count = -1;

    while (f32_sample(&dsp, &s) != EOF) {

        ptr++; dsp.sample_count++;
        if (ptr == N) ptr = 0;
        buffer[ptr] = s;


        t = dsp.sample_count;
        tn = dsp.sample_count-n;

        x = buffer[dsp.sample_count % N];
        x0 = buffer[(dsp.sample_count - n + N) % N];


        // f1
        X0 = x0 * cexp(-tn*iw1); // alt
        X  = x  * cexp(-t *iw1); // neu
        F1sum +=  X - X0;

        // f2
        X0 = x0 * cexp(-tn*iw2); // alt
        X  = x  * cexp(-t *iw2); // neu
        F2sum +=  X - X0;

        xbit = cabs(F2sum) - cabs(F1sum);

        s = xbit / n; //bitlen;

        sbuf[dsp.sample_count % 3] = s;
        s3 = (sbuf[0]+sbuf[1]+sbuf[2])/3.0;


        if ( s3 < 0 ) bit = 0;  // 4800Hz
        else          bit = 1;  // 3000Hz


        if (header_found)
        {
            if (dsp.sample_count >  pos_bit + bitlen/1.65)
            {
                bits[bitpos] = bit;
                bitpos++;
                if (bitpos >= LEN_BITFRAME) {

                    //print_frame(bitpos/BITS);
                    bits2bytes8N1(bits, bytes, bitpos/BITS);

                    if (option_raw) {
                        printRaw(bitpos/BITS);
                    }
                    else {
                        evalBytes2(&dsp);
                    }

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
                pos = dsp.sample_count;  //sample_count-(N-1)/2

                len =  (pos-pos0)/(float)bitlen + 0.5;
                for (i = 0; i < len; i++) {
                    inc_bufpos();
                    buf[bufpos] = 0x30 + bit0;

                    if (!header_found) {
                        if (compare() >= HEADLEN-1) {
                            header_found = 1;

                            bitpos = 10;
                            pos_bit = pos;

                            for (bitpos = 0; bitpos < HEADLEN; bitpos++) bits[bitpos] = header[bitpos] & 0x1;

                        }
                    }
                    else {
                        bits[bitpos] = bit0;
                        bitpos++;
                        if (bitpos >= LEN_BITFRAME) {

                            bits2bytes8N1(bits, bytes, bitpos/BITS);

                            if (option_raw) {
                                printRaw(bitpos/BITS);
                            }
                            else {
                                evalBytes2(&dsp);
                            }

                            bitpos = 0;
                            header_found = 0;
                        }
                    }
                }
                bit0 = bit;
            }
        }
    }

    if (buffer) { free(buffer); buffer = NULL; }
    free_buffers(&dsp);

    printf("\n");

    fclose(fp);

    return 0;
}

