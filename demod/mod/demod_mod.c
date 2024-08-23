
/*
 *  sync header: correlation/matched filter
 *  compile:
 *      gcc -c demod_mod.c
 *  speedup:
 *      gcc -O2 -c demod_mod.c
 *   or
 *      gcc -Ofast -c demod_mod.c
 *
 *  author: zilog80
 */

/* ------------------------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demod_mod.h"

#define FM_GAIN (0.8)

/* ------------------------------------------------------------------------------------ */


#ifndef EXT_FSK

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
                    dft->win[n] = 0.5 * ( 1.0 - cos(_2PI*n/(float)(dft->N2-1)) );
                    break ;
            case 2: // Hamming
                    dft->win[n] = 25/46.0 - (1.0 - 25/46.0)*cos(_2PI*n / (float)(dft->N2-1));
                    break ;
            case 3: // Blackmann
                    dft->win[n] =  7938/18608.0
                                 - 9240/18608.0*cos(_2PI*n / (float)(dft->N2-1))
                                 + 1430/18608.0*cos(4*M_PI*n / (float)(dft->N2-1));
                    break ;
        }
    }
    while (n < dft->N) dft->win[n++] = 0.0;

    return 0;
}


/* ------------------------------------------------------------------------------------ */

static int getCorrDFT(dsp_t *dsp, float thres) {
    int i;
    int mp = -1;
    float mx = 0.0;
    float mx2 = 0.0;
    float re_cx = 0.0;
    float xnorm = 1;
    ui32_t mpos = 0;
    ui32_t pos = dsp->sample_out;

    float *sbuf = dsp->bufs;
    float *dcbuf = dsp->fm_buffer;

    dsp->mv = 0.0;
    dsp->dc = 0.0;

    if (dsp->K + dsp->L > dsp->DFT.N) return -1;
    if (dsp->sample_out < dsp->L) return -2;


    for (i = 0; i < dsp->K + dsp->L; i++) dsp->DFT.xn[i] = sbuf[(pos+dsp->M -(dsp->K + dsp->L-1) + i) % dsp->M];
    while (i < dsp->DFT.N) dsp->DFT.xn[i++] = 0.0;


    rdft(&dsp->DFT, dsp->DFT.xn, dsp->DFT.X);


    if (dsp->opt_dc) {
        /*
        //X[0] = 0; // nicht ueber gesamte Laenge ... M10
        //
        // L < K ?  // only last 2L samples (avoid M10 carrier offset)
        double dc = 0.0;
        for (i = dsp->K - dsp->L; i < dsp->K + dsp->L; i++) dc += dsp->DFT.xn[i];
        dc /= 2.0*(float)dsp->L;
        dsp->DFT.X[0] -= dsp->DFT.N * dc  ;//* 0.95;
        */
        dsp->DFT.X[0] = 0;
        Nidft(&dsp->DFT, dsp->DFT.X, dsp->DFT.cx);
        for (i = 0; i < dsp->DFT.N; i++) dsp->DFT.xn[i] = creal(dsp->DFT.cx[i])/(float)dsp->DFT.N;
    }

    for (i = 0; i < dsp->DFT.N; i++) dsp->DFT.Z[i] = dsp->DFT.X[i]*dsp->DFT.Fm[i];

    Nidft(&dsp->DFT, dsp->DFT.Z, dsp->DFT.cx);


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

    //xnorm = sqrt(dsp->qs[(mpos + 2*dsp->M) % dsp->M]); // Nvar = L
    xnorm = 0.0;
    for (i = 0; i < dsp->L; i++) xnorm += dsp->DFT.xn[mp-i]*dsp->DFT.xn[mp-i];
    xnorm = sqrt(xnorm);

    mx /= xnorm*dsp->DFT.N;

    dsp->mv = mx;
    dsp->mv_pos = mpos;

    if (pos == dsp->sample_out) dsp->buffered = dsp->sample_out - dsp->mv_pos;


    dsp->mv2 = 0.0f;
    dsp->mv2_pos = 0;
    if (dsp->opt_dc) {
        if (dsp->opt_iq >= 2 && fabs(mx) < thres) { /*&& !dsp->locked*/
            mx = 0.0f;
            mpos = 0;

            for (i = 0; i < dsp->K + dsp->L; i++) dsp->DFT.xn[i] = dcbuf[(pos+dsp->M -(dsp->K + dsp->L-1) + i) % dsp->M];
            while (i < dsp->DFT.N) dsp->DFT.xn[i++] = 0.0;
            rdft(&dsp->DFT, dsp->DFT.xn, dsp->DFT.X);

            dsp->DFT.X[0] = 0;
            Nidft(&dsp->DFT, dsp->DFT.X, dsp->DFT.cx);
            for (i = 0; i < dsp->DFT.N; i++) dsp->DFT.xn[i] = creal(dsp->DFT.cx[i])/(float)dsp->DFT.N;

            for (i = 0; i < dsp->DFT.N; i++) dsp->DFT.Z[i] = dsp->DFT.X[i]*dsp->DFT.Fm[i];

            Nidft(&dsp->DFT, dsp->DFT.Z, dsp->DFT.cx);

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

            xnorm = 0.0;
            for (i = 0; i < dsp->L; i++) xnorm += dsp->DFT.xn[mp-i]*dsp->DFT.xn[mp-i];
            xnorm = sqrt(xnorm);

            mx /= xnorm*dsp->DFT.N;


            dsp->mv2 = mx;
            dsp->mv2_pos = mpos - (dsp->lpFMtaps - (dsp->sps-1))/2;

            if (dsp->mv2 > thres || dsp->mv2 < -thres) {
                dsp->mv = dsp->mv2;
                dsp->mv_pos = dsp->mv2_pos;

                if (pos == dsp->sample_out) dsp->buffered = dsp->sample_out - dsp->mv2_pos;
            }
        }
    }


    if (dsp->opt_dc)
    {
        double dc = 0.0;
        int mp_ofs = 0;
        if (dsp->opt_iq >= 2  &&  dsp->mv2_pos == 0) {
            mp_ofs = (dsp->lpFMtaps - (dsp->sps-1))/2;
        }
        dc = 0.0;  // rs41 without preamble?
        // unbalanced header?
        for (i = 0; i < dsp->L; i++) dc += dcbuf[(mp_ofs + mpos - i + dsp->M) % dsp->M];
        dc /= (float)dsp->L;
        dsp->dc = dc;
    }


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

/*
static int get_SNR_rs41(dsp_t *dsp) {

    if (dsp->opt_iq)
    // if(dsp->rs_typ == RS41)
    {
        if (dsp->sample_posnoise > 0) // rs41
        {
            if (dsp->sample_out >= dsp->sample_posframe && dsp->sample_out < dsp->sample_posframe+dsp->len_sq) {
                if (dsp->sample_out == dsp->sample_posframe) dsp->V_signal = 0.0;
                dsp->V_signal += cabs(dsp->rot_iqbuf[dsp->sample_out % dsp->N_IQBUF]);
            }
            if (dsp->sample_out == dsp->sample_posframe+dsp->len_sq) dsp->V_signal /= (double)dsp->len_sq;

            if (dsp->sample_out >= dsp->sample_posnoise && dsp->sample_out < dsp->sample_posnoise+dsp->len_sq) {
                if (dsp->sample_out == dsp->sample_posnoise) dsp->V_noise = 0.0;
                dsp->V_noise += cabs(dsp->rot_iqbuf[dsp->sample_out % dsp->N_IQBUF]);
            }
            if (dsp->sample_out == dsp->sample_posnoise+dsp->len_sq) {
                dsp->V_noise /= (double)dsp->len_sq;
                if (dsp->V_signal > 0 && dsp->V_noise > 0) {
                    // iq-samples/V [-1..1]
                    // dBw = 2*dBv, P=c*U*U
                    // dBw = 2*10*log10(V/V0)
                    dsp->SNRdB = 20.0 * log10(dsp->V_signal/dsp->V_noise+1e-20);
                }
            }
        }
    }
    else dsp->SNRdB = 0;

    return 0;
}
*/

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
    ui32_t n;            // sample: oldest_sample
    double complex w = 0;
    for (n = 0; n < taps; n++) {
        w += buffer[(sample+n)%taps]*ws[taps-1-n];
    }
    return (float complex)w;
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
    float complex w = 0;
    int n; // -Ofast
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
static float complex lowpass0_sym(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    ui32_t n;
    double complex w = buffer[(sample+(taps-1)/2) % taps]*ws[(taps-1)/2]; // (N+1)/2 = (N-1)/2 + 1
    for (n = 0; n < (taps-1)/2; n++) {
        w += (buffer[(sample+n)%taps]+buffer[(sample+taps-n-1)%taps])*ws[n];
    }
    return (float complex)w;
}
static float complex lowpass2_sym(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    float complex w = 0;
    int n;
    int s = sample % taps; // lpIQ
    int SW = (taps-1)/2;
    int B1 = s + SW;
    int n1 = SW - s;
    int n0 = 0;

    if (s > SW) {
        B1 -= taps;
        n1 = -n1 - 1;
        n0 = B1+n1+1;
    }

    w = buffer[B1]*ws[SW];

    for (n = 1; n < n1+1; n++) {
        w += (buffer[B1 + n] + buffer[B1 - n]) * ws[SW+n];
    }

    for (n = 0; n < SW-n1; n++) {
        w += (buffer[s + n] + buffer[s-1 - n]) * ws[SW+SW-n];
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


int f32buf_sample(dsp_t *dsp, int inv) {
    float s = 0.0;
    float s_fm = s;
    float xneu, xalt;

    float complex z, w, z0;
    double gain = FM_GAIN;

    double t = dsp->sample_in / (double)dsp->sr;

    if (dsp->opt_iq)
    {
        if (dsp->opt_iq == 5) {
            int j;
            if ( f32read_cblock(dsp) < dsp->decM ) return EOF;
            for (j = 0; j < dsp->decM; j++) {
                if (dsp->opt_nolut) {
                    double _s_base = (double)(dsp->sample_in*dsp->decM+j); // dsp->sample_dec
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
                z = lowpass(dsp->decXbuffer, dsp->sample_decX, dsp->dectaps, ws_dec); // oldest sample: dsp->sample_decX
            }
        }
        else if ( f32read_csample(dsp, &z) == EOF ) return EOF;

        if (dsp->opt_dc && !dsp->opt_nolut)
        {
            z *= cexp(-t*_2PI*dsp->Df*I);
        }


        // IF-lowpass
        if (dsp->opt_lp & LP_IQ) {
            dsp->lpIQ_buf[dsp->sample_in % dsp->lpIQtaps] = z;
            z = lowpass(dsp->lpIQ_buf, dsp->sample_in+1, dsp->lpIQtaps, dsp->ws_lpIQ);
        }


        z0 = dsp->rot_iqbuf[(dsp->sample_in-1 + dsp->N_IQBUF) % dsp->N_IQBUF];
        w = z * conj(z0);
        s_fm = gain * carg(w)/M_PI;

        dsp->rot_iqbuf[dsp->sample_in % dsp->N_IQBUF] = z;  // sample_in & (N-1) , N = (1<<LOG2N)


        if (dsp->opt_iq >= 2)
        {
            if (dsp->opt_iq >= 2) {
                double xbit = 0.0;
                //float complex xi = cexp(+I*M_PI*dsp->h/dsp->sps);
                //double f1 = -dsp->h*dsp->sr/(2.0*dsp->sps);
                //double f2 = -f1;

                float complex X0 = 0;
                float complex X  = 0;

                int n = dsp->sps;
                double tn = (dsp->sample_in-n) / (double)dsp->sr;
                //t = dsp->sample_in / (double)dsp->sr;
                //z = dsp->rot_iqbuf[dsp->sample_in % dsp->N_IQBUF];
                z0 = dsp->rot_iqbuf[(dsp->sample_in-n + dsp->N_IQBUF) % dsp->N_IQBUF];

                // f1
                X0 = z0 * cexp(-tn*dsp->iw1); // alt
                X  = z  * cexp(-t *dsp->iw1); // neu
                dsp->F1sum +=  X - X0;

                // f2
                X0 = z0 * cexp(-tn*dsp->iw2); // alt
                X  = z  * cexp(-t *dsp->iw2); // neu
                dsp->F2sum +=  X - X0;

                xbit = cabs(dsp->F2sum) - cabs(dsp->F1sum);

                s = xbit / dsp->sps;
            }
            else if (0 && dsp->opt_iq == 4) {
                double xbit = 0.0;
                //float complex xi = cexp(+I*M_PI*dsp->h/dsp->sps);
                //double f1 = -dsp->h*dsp->sr/(2*dsp->sps);
                //double f2 = -f1;

                float complex X1 = 0;
                float complex X2 = 0;

                int n = dsp->sps;

                while (n > 0) {
                    n--;
                    t = -n / (double)dsp->sr;
                    z = dsp->rot_iqbuf[(dsp->sample_in - n + dsp->N_IQBUF) % dsp->N_IQBUF];  // +1
                    X1 += z*cexp(-t*dsp->iw1);
                    X2 += z*cexp(-t*dsp->iw2);
                }

                xbit = cabs(X2) - cabs(X1);

                s = xbit / dsp->sps;
            }
        }
        else {
            s = s_fm;
        }
    }
    else {
        if (f32read_sample(dsp, &s) == EOF) return EOF;
        s_fm = s;
    }

    // FM-lowpass
    if (dsp->opt_lp & LP_FM) {
        dsp->lpFM_buf[dsp->sample_in % dsp->lpFMtaps] = s_fm;
        s_fm = re_lowpass(dsp->lpFM_buf, dsp->sample_in+1, dsp->lpFMtaps, dsp->ws_lpFM);
        if (dsp->opt_iq < 2) s = s_fm;
    }

    dsp->fm_buffer[dsp->sample_in % dsp->M] = s_fm;

    if (inv) s = -s;
    dsp->bufs[dsp->sample_in % dsp->M] = s;  // sample_in & (M-1) , M = (1<<LOG2N)


    xneu = dsp->bufs[(dsp->sample_in  ) % dsp->M];
    xalt = dsp->bufs[(dsp->sample_in+dsp->M - dsp->Nvar) % dsp->M];
    dsp->xsum +=  xneu - xalt;                 // + xneu - xalt
    dsp->qsum += (xneu - xalt)*(xneu + xalt);  // + xneu*xneu - xalt*xalt
    dsp->xs[dsp->sample_in % dsp->M] = dsp->xsum;
    dsp->qs[dsp->sample_in % dsp->M] = dsp->qsum;


    dsp->sample_out = dsp->sample_in - dsp->delay;

    dsp->sample_in += 1;

    return 0;
}

static int read_bufbit(dsp_t *dsp, int symlen, char *bits, ui32_t mvp, int pos) {
// symlen==2: manchester2 0->10,1->01->1: 2.bit

    double rbitgrenze = pos*symlen*dsp->sps;
    ui32_t rcount = ceil(rbitgrenze);//+0.99; // dfm?

    double sum = 0.0;
    double dc = 0.0;

    if (dsp->opt_dc && dsp->opt_iq < 2) dc = dsp->dc;

    // bei symlen=2 (Manchester) kein dc noetig: -dc+dc=0 ;
    // allerdings M10-header mit symlen=1

    rbitgrenze += dsp->sps;
    do {
        sum += dsp->bufs[(rcount + mvp + dsp->M) % dsp->M] - dc;
        rcount++;
    } while (rcount < rbitgrenze);  // n < dsp->sps

    if (symlen == 2) {
        rbitgrenze += dsp->sps;
        do {
            sum -= dsp->bufs[(rcount + mvp + dsp->M) % dsp->M] - dc;
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

    //if (opt_dc == 0 || dsp->opt_iq > 1) dsp->dc = 0;

    if (dsp->symhd != 1) step = 2; // step == symhd
    if (inv) sign=1;

    for (pos = 0; pos < len; pos++) {                  // L = dsp->hdrlen * dsp->sps + 0.5;
        //read_bufbit(dsp, dsp->symhd, dsp->rawbits+pos*step, mvp+1-(int)(len*dsp->sps), pos);
        read_bufbit(dsp, dsp->symhd, dsp->rawbits+pos*step, dsp->mv_pos+1-dsp->L, pos);
    }

    pos = len*step; // == hdrlen
    dsp->rawbits[pos] = '\0';

    while (pos > 0) {
        if ((dsp->rawbits[pos-1]^sign) != dsp->hdr[pos-1]) errs += 1;
        pos--;
    }

    return errs;
}

/* -------------------------------------------------------------------------- */

int read_slbit(dsp_t *dsp, int *bit, int inv, int ofs, int pos, float l, int spike) {
// symlen==2: manchester2 10->0,01->1: 2.bit

    float sample;
    float avg;
    float ths = 0.5, scale = 0.27;

    double sum = 0.0;
    double mid;
    //double l = 1.0;

    double bg = pos*dsp->symlen*dsp->sps;

    double dc = 0.0;

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
            if (spike && fabs(sample - avg) > ths) {
                avg = 0.5*(dsp->bufs[(dsp->sample_out-dsp->buffered-1 + ofs + dsp->M) % dsp->M]
                          +dsp->bufs[(dsp->sample_out-dsp->buffered+1 + ofs + dsp->M) % dsp->M]);
                sample = avg + scale*(sample - avg); // spikes
            }
            sample -= dc;

            if (l < 0 || (mid-l < dsp->sc && dsp->sc < mid+l)) sum -= sample;

            dsp->sc++;
        } while (dsp->sc < bg);  // n < dsp->sps
    }

    mid = bg + (dsp->sps-1)/2.0;
    bg += dsp->sps;
    do {
        if (dsp->buffered > 0) dsp->buffered -= 1;
        else if (f32buf_sample(dsp, inv) == EOF) return EOF;

        sample = dsp->bufs[(dsp->sample_out-dsp->buffered + ofs + dsp->M) % dsp->M];
        if (spike && fabs(sample - avg) > ths) {
            avg = 0.5*(dsp->bufs[(dsp->sample_out-dsp->buffered-1 + ofs + dsp->M) % dsp->M]
                      +dsp->bufs[(dsp->sample_out-dsp->buffered+1 + ofs + dsp->M) % dsp->M]);
            sample = avg + scale*(sample - avg); // spikes
        }
        sample -= dc;

        if (l < 0 || (mid-l < dsp->sc && dsp->sc < mid+l)) sum += sample;

        dsp->sc++;
    } while (dsp->sc < bg);  // n < dsp->sps


    if (sum >= 0) *bit = 1;
    else          *bit = 0;

    return 0;
}

int read_softbit(dsp_t *dsp, hsbit_t *shb, int inv, int ofs, int pos, float l, int spike) {
// symlen==2: manchester2 10->0,01->1: 2.bit

    float sample;
    float avg;
    float ths = 0.5, scale = 0.27;

    double sum = 0.0;
    double mid;
    //double l = 1.0;

    double bg = pos*dsp->symlen*dsp->sps;
    double dc = 0.0;

    ui8_t bit = 0;


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
            if (spike && fabs(sample - avg) > ths) {
                avg = 0.5*(dsp->bufs[(dsp->sample_out-dsp->buffered-1 + ofs + dsp->M) % dsp->M]
                          +dsp->bufs[(dsp->sample_out-dsp->buffered+1 + ofs + dsp->M) % dsp->M]);
                sample = avg + scale*(sample - avg); // spikes
            }
            sample -= dc;

            if (l < 0 || (mid-l < dsp->sc && dsp->sc < mid+l)) sum -= sample;

            dsp->sc++;
        } while (dsp->sc < bg);  // n < dsp->sps
    }

    mid = bg + (dsp->sps-1)/2.0;
    bg += dsp->sps;
    do {
        if (dsp->buffered > 0) dsp->buffered -= 1;
        else if (f32buf_sample(dsp, inv) == EOF) return EOF;

        sample = dsp->bufs[(dsp->sample_out-dsp->buffered + ofs + dsp->M) % dsp->M];
        if (spike && fabs(sample - avg) > ths) {
            avg = 0.5*(dsp->bufs[(dsp->sample_out-dsp->buffered-1 + ofs + dsp->M) % dsp->M]
                      +dsp->bufs[(dsp->sample_out-dsp->buffered+1 + ofs + dsp->M) % dsp->M]);
            sample = avg + scale*(sample - avg); // spikes
        }
        sample -= dc;

        if (l < 0 || (mid-l < dsp->sc && dsp->sc < mid+l)) sum += sample;

        dsp->sc++;
    } while (dsp->sc < bg);  // n < dsp->sps


    if (sum >= 0) bit = 1;
    else          bit = 0;

    shb->hb = bit;
    shb->sb = (float)sum;

    return 0;
}

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

#define IF_TRANSITION_BW (4e3)  // 4kHz transition width
#define FM_TRANSITION_BW (2e3)  // 2kHz transition width

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

int init_buffers(dsp_t *dsp) {

    int i, pos;
    float b0, b1, b2, b, t;
    float normMatch;
    double sigma = sqrt(log(2)) / (_2PI*dsp->BT);

    int p2 = 1;
    int K, L, M;
    int n, k;
    float *m = NULL;


    // decimate
    if (dsp->opt_iq == 5)
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
        dsp->_spb /= (float)decM;
        dsp->decM = decM;

        fprintf(stderr, "IF: %d\n", IF_sr);
        fprintf(stderr, "dec: %d\n", decM);
    }
    if (dsp->opt_iq == 5)
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
        taps = 4*dsp->sr/IF_TRANSITION_BW; if (taps%2==0) taps++;
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


    L = dsp->hdrlen * dsp->sps + 0.5;
    M = 3*L;
    //if (dsp->sps < 6) M = 6*L;

    dsp->delay = L/16;
    dsp->sample_in = 0;

    p2 = 1;
    while (p2 < M) p2 <<= 1;
    while (p2 < 0x2000) p2 <<= 1;  // or 0x4000, if sample not too short
    M = p2;
    dsp->DFT.N = p2;
    dsp->DFT.LOG2N = log(dsp->DFT.N)/log(2)+0.1; // 32bit cpu ... intermediate floating-point precision
    //while ((1 << dsp->DFT.LOG2N) < dsp->DFT.N) dsp->DFT.LOG2N++;  // better N = (1 << LOG2N) ...

    K = M-L - dsp->delay; // L+K < M

    dsp->DFT.sr = dsp->sr;

    dsp->K = K;
    dsp->L = L;
    dsp->M = M; // = (1<<LOG2N)

    dsp->Nvar = L; // wenn Nvar fuer xnorm, dann Nvar=rshd.L


    dsp->bufs  = (float *)calloc( M+1, sizeof(float)); if (dsp->bufs  == NULL) return -100;
    dsp->match = (float *)calloc( L+1, sizeof(float)); if (dsp->match == NULL) return -100;

    dsp->xs = (float *)calloc( M+1, sizeof(float)); if (dsp->xs == NULL) return -100;
    dsp->qs = (float *)calloc( M+1, sizeof(float)); if (dsp->qs == NULL) return -100;

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

        dsp->N_IQBUF = dsp->DFT.N; // = (1<<LOG2N)
        dsp->rot_iqbuf = calloc(dsp->N_IQBUF+1, sizeof(float complex));  if (dsp->rot_iqbuf == NULL) return -1;
    }

    dsp->fm_buffer = (float *)calloc( M+1, sizeof(float));  if (dsp->fm_buffer == NULL) return -1; // dsp->bufs[]


    if (dsp->opt_iq)
    {
        double f1 = -dsp->h*dsp->sr/(2.0*dsp->sps);
        double f2 = -f1;
        dsp->iw1 = _2PI*I*f1;
        dsp->iw2 = _2PI*I*f2;
    }

    return K;
}

int free_buffers(dsp_t *dsp) {

    if (dsp->match) { free(dsp->match); dsp->match = NULL; }
    if (dsp->bufs)  { free(dsp->bufs);  dsp->bufs  = NULL; }
    if (dsp->xs)  { free(dsp->xs);  dsp->xs  = NULL; }
    if (dsp->qs)  { free(dsp->qs);  dsp->qs  = NULL; }
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
    if (dsp->opt_iq == 5)
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

    if (dsp->fm_buffer) { free(dsp->fm_buffer); dsp->fm_buffer = NULL; }

    return 0;
}

/* ------------------------------------------------------------------------------------ */


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
            mp = getCorrDFT(dsp, thres); // correlation score -> dsp->mv
            //if (option_auto == 0 && dsp->mv < 0) mv = 0;
            k = 0;
        }
        else {
            dsp->mv = 0.0;
            continue;
        }

        if (dsp->mv  > thres || dsp->mv  < -thres)
        {
            if (dsp->opt_dc)
            {
                if (dsp->opt_iq) {
                    if (fabs(dsp->dDf) > 100.0)
                    {
                        double diffDf = dsp->dDf*0.6; //0.4
                        if (1 && dsp->opt_iq >= 2) {
                            // update rot_iqbuf, F1sum, F2sum
                            //double f1 = -dsp->h*dsp->sr/(2*dsp->sps);
                            //double f2 = -f1;
                            float complex X1 = 0;
                            float complex X2 = 0;
                            float complex _z = 0;
                            int _n = dsp->sps;
                            while ( _n > 0 )
                            {
                                // update rot_iqbuf
                                double _tn = (dsp->sample_in - _n) / (double)dsp->sr;
                                dsp->rot_iqbuf[(dsp->sample_in - _n + dsp->N_IQBUF) % dsp->N_IQBUF] *= cexp(-_tn*_2PI*diffDf*I);
                                //
                                //update/reset F1sum, F2sum
                                _z = dsp->rot_iqbuf[(dsp->sample_in - _n + dsp->N_IQBUF) % dsp->N_IQBUF];
                                X1 += _z*cexp(-_tn*dsp->iw1);
                                X2 += _z*cexp(-_tn*dsp->iw2);
                                _n--;
                            }
                            dsp->F1sum = X1;
                            dsp->F2sum = X2;
                        }
                        dsp->Df += diffDf;
                    }
                    if (fabs(dsp->dDf) > 1e3) {
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


#else
// external FSK demod: read float32 soft symbols

int read_wav_header(pcm_t *pcm, FILE *fp) {}
int f32buf_sample(dsp_t *dsp, int inv) {}
int read_slbit(dsp_t *dsp, int *bit, int inv, int ofs, int pos, float l, int spike) {}
int read_softbit(dsp_t *dsp, hsbit_t *shb, int inv, int ofs, int pos, float l, int spike) {}
int read_softbit2p(dsp_t *dsp, hsbit_t *shb, int inv, int ofs, int pos, float l, int spike, hsbit_t *shb1) {}

int init_buffers(dsp_t *dsp) {}
int free_buffers(dsp_t *dsp) {}

int find_header(dsp_t *dsp, float thres, int hdmax, int bitofs, int opt_dc) {}

#endif


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

int find_binhead(FILE *fp, hdb_t *hdb, float *score) {
    int bit;
    int headlen = hdb->len;
    float mv;

    //*score = 0.0;

    while ( (bit = fgetc(fp)) != EOF )
    {
        bit &= 1;

        hdb->bufpos = (hdb->bufpos+1) % headlen;
        hdb->buf[hdb->bufpos] = 0x30 | bit;  // Ascii

        mv = cmp_hdb(hdb);
        if ( fabs(mv) > hdb->thb ) {
            *score = mv;
            return 1;
        }
    }

    return EOF;
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

int f32soft_read(FILE *fp, float *s, int inv) {
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

    if (inv) *s = -*s;

    return 0;
}

int find_softbinhead(FILE *fp, hdb_t *hdb, float *score, int inv) {
    int headlen = hdb->len;
    float sbit;
    float mv;

    //*score = 0.0;

    while ( f32soft_read(fp, &sbit, inv) != EOF )
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

