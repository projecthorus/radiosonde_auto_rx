
/*
 *  sync header: correlation/matched filter
 *  compile:
 *      gcc -c demod_mod.c
 *
 *  author: zilog80
 */

/* ------------------------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demod_mod.h"

/* ------------------------------------------------------------------------------------ */


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
                    dft->win[n] = 25/46.0 + (1.0 - 25/46.0)*cos(2*M_PI*n / (float)(dft->N2-1));
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

    dsp->mv = 0.0;
    dsp->dc = 0.0;

    if (dsp->K + dsp->L > dsp->DFT.N) return -1;
    if (dsp->sample_out < dsp->L) return -2;


    dsp->dc = get_bufmu(dsp, pos - dsp->sample_out); //oder unten: dft_dc = creal(X[0])/(K+L);
    // wenn richtige Stelle (Varianz pruefen, kein M10-carrier?), dann von bufs[] subtrahieren


    for (i = 0; i < dsp->K + dsp->L; i++) (dsp->DFT).xn[i] = dsp->bufs[(pos+dsp->M -(dsp->K + dsp->L-1) + i) % dsp->M];
    while (i < dsp->DFT.N) (dsp->DFT).xn[i++] = 0.0;

    rdft(&dsp->DFT, dsp->DFT.xn, dsp->DFT.X);

    // dft_dc = creal(dsp->DFT.X[0])/dsp->DFT.N;

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
    for (i = 0; i < dsp->L; i++) xnorm += dsp->bufs[(mpos-i + dsp->M) % dsp->M]*dsp->bufs[(mpos-i + dsp->M) % dsp->M];
    xnorm = sqrt(xnorm);

    mx /= xnorm*(dsp->DFT).N;

    dsp->mv = mx;
    dsp->mv_pos = mpos;

    if (pos == dsp->sample_out) dsp->buffered = dsp->sample_out - mpos;

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

float read_wav_header(pcm_t *pcm, FILE *fp) {
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;
    int sample_rate = 0, bits_sample = 0, channels = 0;

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

    if (pcm->sel_ch < 0  ||  pcm->sel_ch >= channels) pcm->sel_ch = 0; // default channel: 0
    fprintf(stderr, "channel-In : %d\n", pcm->sel_ch+1);

    if ((bits_sample != 8) && (bits_sample != 16)) return -1;


    pcm->sr  = sample_rate;
    pcm->bps = bits_sample;
    pcm->nch = channels;

    return 0;
}

static int f32read_sample(dsp_t *dsp, float *s) {
    int i;
    short b = 0;

    for (i = 0; i < dsp->nch; i++) {

        if (fread( &b, dsp->bps/8, 1, dsp->fp) != 1) return EOF;

        if (i == dsp->ch) {  // i = 0: links bzw. mono
            //if (bits_sample ==  8)  sint = b-128;   // 8bit: 00..FF, centerpoint 0x80=128
            //if (bits_sample == 16)  sint = (short)b;

            if (dsp->bps ==  8) { b -= 128; }
            *s = b/128.0;
            if (dsp->bps == 16) { *s /= 256.0; }
        }
    }

    return 0;
}

static int f32read_csample(dsp_t *dsp, float complex *z) {
    short x = 0, y = 0;

    if (fread( &x, dsp->bps/8, 1, dsp->fp) != 1) return EOF;
    if (fread( &y, dsp->bps/8, 1, dsp->fp) != 1) return EOF;

    *z = x + I*y;

    if (dsp->bps ==  8) { *z -= 128 + I*128; }
    *z /= 128.0;
    if (dsp->bps == 16) { *z /= 256.0; }

    return 0;
}

float get_bufvar(dsp_t *dsp, int ofs) {
    float mu  = dsp->xs[(dsp->sample_out+dsp->M + ofs) % dsp->M]/dsp->Nvar;
    float var = dsp->qs[(dsp->sample_out+dsp->M + ofs) % dsp->M]/dsp->Nvar - mu*mu;
    return var;
}

float get_bufmu(dsp_t *dsp, int ofs) {
    float mu  = dsp->xs[(dsp->sample_out+dsp->M + ofs) % dsp->M]/dsp->Nvar;
    return mu;
}

static int get_SNR(dsp_t *dsp) {

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

int f32buf_sample(dsp_t *dsp, int inv) {
    float s = 0.0;
    float xneu, xalt;

    float complex z, w, z0;
    //static float complex z0; //= 1.0;
    double gain = 0.8;
    int n;

    double t = dsp->sample_in / (double)dsp->sr;

    if (dsp->opt_iq) {

        if ( f32read_csample(dsp, &z) == EOF ) return EOF;
        dsp->raw_iqbuf[dsp->sample_in % dsp->N_IQBUF] = z;

        z *= cexp(-t*2*M_PI*dsp->df*I);
        z0 = dsp->rot_iqbuf[(dsp->sample_in-1 + dsp->N_IQBUF) % dsp->N_IQBUF];
        w = z * conj(z0);
        s = gain * carg(w)/M_PI;
        //z0 = z;
        dsp->rot_iqbuf[dsp->sample_in % dsp->N_IQBUF] = z;

        /*  //if (rs_type==rs41) get_SNR(dsp);
            // rs41, constant amplitude, avg/filter
            s = 0.0;
            for (n = 0; n < dsp->sps; n++) s += cabs(dsp->rot_iqbuf[(dsp->sample_in - n + dsp->N_IQBUF) % dsp->N_IQBUF]);
            s /= (float)n;
        */

        if (dsp->opt_iq >= 2)
        {
            double xbit = 0.0;
            //float complex xi = cexp(+I*M_PI*dsp->h/dsp->sps);
            double f1 = -dsp->h*dsp->sr/(2*dsp->sps);
            double f2 = -f1;

            float complex X1 = 0;
            float complex X2 = 0;

            n = dsp->sps;
            while (n > 0) {
                n--;
                t = -n / (double)dsp->sr;
                z = dsp->rot_iqbuf[(dsp->sample_in - n + dsp->N_IQBUF) % dsp->N_IQBUF];  // +1
                X1 += z*cexp(-t*2*M_PI*f1*I);
                X2 += z*cexp(-t*2*M_PI*f2*I);
            }

            xbit = cabs(X2) - cabs(X1);

            s = xbit / dsp->sps;
        }
    }
    else {
        if (f32read_sample(dsp, &s) == EOF) return EOF;
    }

    if (inv) s = -s;                   // swap IQ?
    dsp->bufs[dsp->sample_in % dsp->M] = s - dsp->dc_ofs;

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


    rbitgrenze += dsp->sps;
    do {
        sum += dsp->bufs[(rcount + mvp + dsp->M) % dsp->M];
        rcount++;
    } while (rcount < rbitgrenze);  // n < dsp->sps

    if (symlen == 2) {
        rbitgrenze += dsp->sps;
        do {
            sum -= dsp->bufs[(rcount + mvp + dsp->M) % dsp->M];
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

    if (opt_dc && errs < 3) {
        dsp->dc_ofs += dsp->dc;
    }

    return errs;
}

int get_fqofs_rs41(dsp_t *dsp, ui32_t mvp, float *freq, float *snr) {
    int j;
    int buf_start;
    int presamples;

    // if(dsp->rs_typ == RS41_PREAMBLE) ...
    if (dsp->opt_iq)
    {
        presamples = 256*dsp->sps;

        if (presamples > dsp->DFT.N2) presamples = dsp->DFT.N2;

        buf_start = mvp - dsp->hdrlen*dsp->sps - presamples;

        while (buf_start < 0) buf_start += dsp->N_IQBUF;

        for (j = 0; j < dsp->DFT.N2; j++) {
            dsp->DFT.Z[j] = dsp->DFT.win[j]*dsp->raw_iqbuf[(buf_start+j) % dsp->N_IQBUF];
        }
        while (j < dsp->DFT.N) dsp->DFT.Z[j++] = 0;

        raw_dft(&dsp->DFT, dsp->DFT.Z);
        dsp->df = bin2freq(&dsp->DFT, max_bin(&dsp->DFT, dsp->DFT.Z));

        // if |df|<eps, +-2400Hz dominant (rs41)
        if (fabs(dsp->df) > 1000.0) dsp->df = 0.0;


        dsp->sample_posframe = dsp->sample_in;  // > sample_out  //mvp - dsp->hdrlen*dsp->sps;
        dsp->sample_posnoise = mvp + dsp->sr*7/8.0; // rs41


        *freq = dsp->df;
        *snr = dsp->SNRdB;
    }
    else return -1;

    return 0;
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

            if ( l < 0 || (mid-l < dsp->sc && dsp->sc < mid+l)) sum -= sample;

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

        if ( l < 0 || (mid-l < dsp->sc && dsp->sc < mid+l)) sum += sample;

        dsp->sc++;
    } while (dsp->sc < bg);  // n < dsp->sps


    if (sum >= 0) *bit = 1;
    else          *bit = 0;

    return 0;
}

/* -------------------------------------------------------------------------- */


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
    double sigma = sqrt(log(2)) / (2*M_PI*dsp->BT);

    int p2 = 1;
    int K, L, M;
    int n, k;
    float *m = NULL;


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
    dsp->M = M;

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

        dsp->N_IQBUF = dsp->DFT.N;
        dsp->raw_iqbuf = calloc(dsp->N_IQBUF+1, sizeof(float complex));  if (dsp->raw_iqbuf == NULL) return -1;
        dsp->rot_iqbuf = calloc(dsp->N_IQBUF+1, sizeof(float complex));  if (dsp->rot_iqbuf == NULL) return -1;

        dsp->len_sq = dsp->sps*8;
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
        if (dsp->raw_iqbuf)  { free(dsp->raw_iqbuf); dsp->raw_iqbuf = NULL; }
        if (dsp->rot_iqbuf)  { free(dsp->rot_iqbuf); dsp->rot_iqbuf = NULL; }
    }

    return 0;
}

/* ------------------------------------------------------------------------------------ */

ui32_t get_sample(dsp_t *dsp) {
    return dsp->sample_out;
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
            mp = getCorrDFT(dsp); // correlation score -> dsp->mv
            //if (option_auto == 0 && dsp->mv < 0) mv = 0;
            k = 0;
        }
        else {
            dsp->mv = 0.0;
            continue;
        }

        if (dsp->mv > thres || dsp->mv < -thres) {

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

