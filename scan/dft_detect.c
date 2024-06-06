
/*
 *  compile:
 *      gcc dft_detect.c -lm -o dft_detect
 *  speedup:
 *      gcc -Ofast dft_detect.c -lm -o dft_detect
 *
 *  author: zilog80
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>

#ifndef M_PI
    #define M_PI  (3.1415926535897932384626433832795)
#endif

typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;
typedef short i16_t;
typedef int   i32_t;


static int option_verbose = 0,  // ausfuehrliche Anzeige
           option_inv = 0,      // invertiert Signal
           option_min = 0,
           option_iq = 0,
           option_dc = 0,
           option_silent = 0,
           option_cont = 0,
           option_d2 = 0,
           option_pcmraw = 0,
           option_singleLpIQ = 0,
           wavloaded = 0;
static int wav_channel = 0;     // audio channel: left


//int  dfm_sps = 2500;
static char dfm_header[] = "10011010100110010101101001010101"; // DFM-09
                        // "01100101011001101010010110101010"; // DFM-06
//int  vai_sps = 4800;
static char rs41_header[] = "00001000011011010101001110001000"
                            "01000100011010010100100000011111";
static char rs92_header[] = //"10100110011001101001"
                            //"10100110011001101001"
                            "10100110011001101001"
                            "10100110011001101001"
                            "1010011001100110100110101010100110101001";

//int  lms_sps = 4800;  // lms6_403MHz
static char lms6_header[] = "0101011000001000""0001110010010111"
                            "0001101010100111""0011110100111110";

//int  mk2a_sps = 9600;  // lms6_1680MHz
static char mk2a_header[] = "0010100111""0010100111""0001001001""0010010101";

//int  m10_sps = 9600;
static char m10_header[] = //"10011001100110010100110010011001";
                                 "1001100110010100110010011001""1010"; // ofs=4/2 in frm_M10()
// frame byte[0..1]: byte[0]=framelen-1, byte[1]=type(8F=M2K2,9F=M10,AF=M10+,20=M20)
// M2K2   : 64 8F : 01100100 10001111
// M10    : 64 9F : 01100100 10011111  (framelen 0x64+1) (baud=9616)
// M10-aux: 76 9F : 01110110 10011111  (framelen 0x76+1)
// M10+   : 64 AF : 01100100 10101111  (w/ gtop-GPS)
// M20    : 45 20 : 01000101 00100000  (framelen 0x45+1) (baud=9600)

//int  meisei_sps = 2400;   // 0xFB6230 =
static char meisei_header[] = "110011001101001101001101010100101010110010101010"; // 11111011 01100010 00110000

//int  mrz_sps = 2400;
static char mrz_header[] = "1001100110011001""1001101010101010"; // 0xAA 0xBF

//int  imet54_sps = 4800;
static char imet54_header[] = "0000000001""0101010101""0001001001""0001001001"; // 0x00 0xAA 0x24 0x24

// Meteosis MTS01 1200 baud
// Lmax
// len(AA AA B4 2B)=32 -> L=1280 // more accurate, +19% slower
// len(AA B4 2B)=24 -> L=960 // same L as meisei, +7% slower
static char mts01_header[] = "10101010""10101010"  // preamble: AA AA
                             "10110100""00101011"; // 10000000: B4 2B //80


// imet_9600 / 1200 Hz;
static char imet_preamble[] = //"11110000111100001111000011110000"
                              //"11110000111100001111000011110000"
                              "11110000111100001111000011110000"
                              "11110000111100001111000011110000"; // 1200 Hz 0xAA 0xAA preamble

//int  imet1ab_sps = 9600; // 1200 bits/sec  // AFSK 1200/2400
static char imet1ab_header[] = "0000""11110000111100001111000011110000""1111"   // idle
                             //"0000""10101100110010101100101010101100""1111"
                               "0000""10101100110010101100101010101100""1111";  // 0x96

// 11110000:1 , 001100110:0 // 11/4=2.1818..
static char imet1rs_header[] =
    "0000""1111""0000""1111""0000""1111"   // preamble
    "0000""1111";

// imet1rs/imet4 1200Hz preamble , lead_out , 8N1 byte: lead-in 8bits lead-out , ...
// 1:1200Hz/0:2200Hz tones, bit-duration 1/1200 sec, phase ...
// bits: 1111111111111111111 10 10000000 10 ..;


// C34/C50: 2400 baud, 1:2900Hz/0:4800Hz
static char c34_preheader[] =
    "01010101010101010101010101010101";   // 2900 Hz tone
    // dft, dB-max(1000Hz..5000Hz) = 2900Hz ?


static char weathex_header[] =
    "10101010""10101010""10101010"       // AA AA AA (preamble)
    "00101101""11010100"; //"10101010";  // 2D D4 55/AA

static char wxr2pn9_header[] =
    "10101010""10101010""10101010"  // AA AA AA (preamble)
    "11000001""10010100"; //"11000001";  // C1 94 C1


typedef struct {
    int sps;  // header: symbol rate, baud
    int hLen;
    int L;
    char *header;
    float BT;
    float spb;
    float thres;
    int herrs;
    float complex *Fm;
    char *type;
    int tn;
    int lpFM;
    int lpIQ;
    float dc;
    float df; // Df = df*sr_base;
} rsheader_t;

#define N_bwIQ 4
static float lpFM_bw[2] = { 4e3, 10e3 };  // FM-audio lowpass bandwidth
static float lpIQ_bw[N_bwIQ] = { 6e3, 12e3, 22e3, 200e3 };  // IF iq lowpass bandwidth
static float set_lpIQ = 0.0;

#define tn_DFM        2
#define tn_RS41       3
#define tn_RS92       4
#define tn_M10        5
#define tn_M20        6
#define tn_LMS6       8
#define tn_MEISEI     9
#define tn_MRZ       12
#define tn_MTS01     13
#define tn_C34C50    15
#define tn_WXR301    16
#define tn_WXRpn9    17
#define tn_MK2LMS    18
#define tn_IMET5     24
#define tn_IMETa     25
#define tn_IMET4     26
#define tn_IMET1rs   28
#define tn_IMET1ab   29

#define Nrs          17
#define idxIMETafsk  14
#define idxRS        15
#define idxI4        16
static rsheader_t rs_hdr[Nrs] = {
    { 2500, 0, 0, dfm_header,     1.0, 0.0, 0.65, 2, NULL, "DFM9",     tn_DFM,     0, 1, 0.0, 0.0}, // DFM6: -2 ?
    { 4800, 0, 0, rs41_header,    0.5, 0.0, 0.70, 2, NULL, "RS41",     tn_RS41,    0, 1, 0.0, 0.0},
    { 4800, 0, 0, rs92_header,    0.5, 0.0, 0.70, 3, NULL, "RS92",     tn_RS92,    0, 1, 0.0, 0.0}, // RS92NGP: 1680/400=4.2
    { 4800, 0, 0, lms6_header,    1.0, 0.0, 0.60, 8, NULL, "LMS6",     tn_LMS6,    0, 1, 0.0, 0.0}, // lmsX: 7?
    { 4800, 0, 0, imet54_header,  0.5, 0.0, 0.80, 2, NULL, "IMET5",    tn_IMET5,   0, 1, 0.0, 0.0}, // (rs_hdr[idxI5])
    { 9616, 0, 0, mk2a_header,    1.0, 0.0, 0.70, 2, NULL, "MK2LMS",   tn_MK2LMS,  1, 2, 0.0, 0.0}, // Mk2a/LMS6-1680 , --IQ: decimate > 170kHz ...
    { 9608, 0, 0, m10_header,     1.0, 0.0, 0.76, 2, NULL, "M10",      tn_M10,     1, 2, 0.0, 0.0}, // M10.tn=5 (baud=9616) , M20.tn=6 (baud=9600)
    { 2400, 0, 0, meisei_header,  1.0, 0.0, 0.70, 2, NULL, "MEISEI",   tn_MEISEI,  0, 2, 0.0, 0.0},
    { 2400, 0, 0, mrz_header,     1.5, 0.0, 0.80, 2, NULL, "MRZ",      tn_MRZ,     0, 1, 0.0, 0.0},
    { 1200, 0, 0, mts01_header,   1.0, 0.0, 0.65, 2, NULL, "MTS01",    tn_MTS01,   0, 0, 0.0, 0.0},
    { 5800, 0, 0, c34_preheader,  1.5, 0.0, 0.80, 2, NULL, "C34C50",   tn_C34C50,  0, 2, 0.0, 0.0}, // C34/C50 2900 Hz tone
    { 4800, 0, 0, weathex_header, 1.0, 0.0, 0.65, 2, NULL, "WXR301",   tn_WXR301,  0, 3, 0.0, 0.0},
    { 5000, 0, 0, wxr2pn9_header, 1.0, 0.0, 0.65, 2, NULL, "WXRPN9",   tn_WXRpn9,  0, 3, 0.0, 0.0},
    { 9600, 0, 0, imet1ab_header, 1.0, 0.0, 0.80, 2, NULL, "IMET1AB",  tn_IMET1ab, 1, 3, 0.0, 0.0}, // (rs_hdr[idxAB])
    { 9600, 0, 0, imet_preamble,  0.5, 0.0, 0.80, 4, NULL, "IMETafsk", tn_IMETa  , 1, 1, 0.0, 0.0}, // IMET1AB, IMET1RS (IQ)IMET4
    { 9600, 0, 0, imet1rs_header, 0.5, 0.0, 0.80, 2, NULL, "IMET1RS",  tn_IMET1rs, 0, 3, 0.0, 0.0}, // (rs_hdr[idxRS]) IMET4: lpIQ=0 ...
    { 9600, 0, 0, imet1rs_header, 0.5, 0.0, 0.80, 2, NULL, "IMET4",    tn_IMET4,   1, 1, 0.0, 0.0}, // (rs_hdr[idxI4])
};

static int idx_MTS01 = -1,
           idx_C34C50 = -1,
           idx_WXR301 = -1,
           idx_WXRPN9 = -1,
           idx_IMET1AB = -1;


static int rs_detect2[Nrs];

static int rs_d2() {
    int tn = 0;
    for (tn = 0; tn < Nrs; tn++) {
        if ( rs_detect2[tn] > 1 ) break;
    }
    return tn;
}

static int reset_d2() {
    int n = 0;
    for (n = 0; n < Nrs; n++) rs_detect2[n] = 0;
    return 0;
}


/*
// m10-false-positive:
// m10-preamble similar to rs41-preamble, parts of rs92/imet1ab, imet1ab; diffs:
// - iq: - modulation-index rs41 < rs92 < m10,
//       - power level / frame < 1s, noise
// - fm: - frame duration <-> noise (variance/standard deviation)
//       - pulse-shaping
//           m10: 00110011 at 9600 sps
//           rs41: 0 1 0 1 at 4800 sps
// - after header, m10-baudrate < rs41-baudrate
// - m10 top-carrier, fm-mean/average
// - m10-header ..110(1)0110011()011.. bit shuffle
// - m10 frame byte[1]=type(M2K2,M10,M10+)
*/

/*
// rs92
// imet1ab-false-positive
// ...
*/

#define FM_GAIN (0.8)

static int sr_base = 0;
static int sr_if = 0;

static int sample_rate = 0, bits_sample = 0, channels = 0;
static int wav_ch = 0;  // 0: links bzw. mono; 1: rechts

static ui32_t sample_in, sample_out, delay;

static int M;

static float *buf_fm[N_bwIQ];
static float *bufs = NULL;

static char *rawbits = NULL;

/* ------------------------------------------------------------------------------------ */

// decimation
static ui32_t dsp__sr_base;
static ui32_t dsp__dectaps;
static ui32_t dsp__sample_decX;
static int dsp__decM = 1;
static float complex *dsp__decXbuffer;
static float complex *dsp__decMbuf;
static float complex *dsp__ex; // exp_lut
static ui32_t dsp__lut_len;
static ui32_t dsp__sample_decM;

static float *ws_dec;
static double dsp__xlt_fq = 0.0;


static int LOG2N, N_DFT;

static float complex  *ew;

static float complex  *X, *Z, *cx;
static float *xn;
static float *db;

// FM: lowpass
static float *ws_lpFM[2];
static int dsp__lpFMtaps; // ui32_t
static float complex *Y;
static float complex *WS[2];
// IF: lowpass
static float *ws_lpIQ[N_bwIQ]; // only N_bwIQ-1 used
static int dsp__lpIQtaps; // ui32_t
static float complex *lpIQ_buf;


static void dft_raw(float complex *Z) {
    int s, l, l2, i, j, k;
    float complex  w1, w2, T;

    j = 1;
    for (i = 1; i < N_DFT; i++) {
        if (i < j) {
            T = Z[j-1];
            Z[j-1] = Z[i-1];
            Z[i-1] = T;
        }
        k = N_DFT/2;
        while (k < j) {
            j = j - k;
            k = k/2;
        }
        j = j + k;
    }

    for (s = 0; s < LOG2N; s++) {
        l2 = 1 << s;
        l  = l2 << 1;
        w1 = (float complex)1.0;
        w2 = ew[s]; // cexp(-I*M_PI/(float)l2)
        for (j = 1; j <= l2; j++) {
            for (i = j; i <= N_DFT; i += l) {
                k = i + l2;
                T = Z[k-1] * w1;
                Z[k-1] = Z[i-1] - T;
                Z[i-1] = Z[i-1] + T;
            }
            w1 = w1 * w2;
        }
    }
}

static void dft(float *x, float complex *Z) {
    int i;
    for (i = 0; i < N_DFT; i++)  Z[i] = (float complex)x[i];
    dft_raw(Z);
}

static void Nidft(float complex *Z, float complex *z) {
    int i;
    for (i = 0; i < N_DFT; i++)  z[i] = conj(Z[i]);
    dft_raw(z);
    // idft():
    // for (i = 0; i < N_DFT; i++)  z[i] = conj(z[i])/(float)N_DFT; // hier: z reell
}

static float freq2bin(int f) {
    return  f * N_DFT / (float)sample_rate;
}

static float bin2freq(int k) {
    float fq = k / (float)N_DFT;
    if ( fq >= 0.5) fq -= 1.0;
    return fq*sample_rate;
}

/* ------------------------------------------------------------------------------------ */

static int getCorrDFT(int K, unsigned int pos, float *maxv, unsigned int *maxvpos, rsheader_t *rshd) {
    int i;
    int mp = -1;
    float mx = 0.0;
    float mx2 = 0.0;
    float re_cx = 0.0;
    double xnorm = 1.0;
    unsigned int mpos = 0;

    float dc = 0.0;
    rshd->dc = 0.0;

    if (K + rshd->L > N_DFT) return -1;
//    if (sample_out < rshd->L) return -2; // nur falls K-4 < L

    if (pos == 0) pos = sample_out;

    bufs = buf_fm[rshd->lpIQ];

    for (i = 0; i < K+rshd->L; i++) xn[i] = bufs[(pos+M -(K+rshd->L-1) + i) % M];
    while (i < N_DFT) xn[i++] = 0.0;

    dft(xn, X);


    //dc = get_bufmu(pos-sample_out); //oder: dc = creal(X[0])/(K+rshd->L) = avg(xn) // zu lang (M10)

    dc = 0.0;
    if (option_dc) {
        //X[0] = 0; // all samples in window
        // L < K
        for (i=K-rshd->L; i<K+rshd->L;i++) dc += xn[i]; // only last 2L samples (avoid M10 carrier offset)
        dc /= 2.0*(float)rshd->L;
        X[0] -= N_DFT*dc  * 0.98;
    }
    rshd->dc = dc;

    if (option_iq) {
        // FM-lowpass(xn)
        for (i = 0; i < N_DFT; i++) X[i] *= WS[rshd->lpFM][i];
    }

    if (option_dc || option_iq) { // mx = mx(xn[]), xn(lowpass, dc)
        Nidft(X, cx);
        for (i = 0; i < N_DFT; i++) xn[i] = creal(cx[i])/(float)N_DFT;
    }
    for (i = 0; i < N_DFT; i++) Z[i] = X[i] * rshd->Fm[i];
    Nidft(Z, cx);


    // relativ Peak - Normierung erst zum Schluss;
    // dann jedoch nicht zwingend corr-Max wenn FM-Amplitude bzw. norm(x) nicht konstant
    // (z.B. rs41 Signal-Pausen). Moeglicherweise wird dann wahres corr-Max in dem
    //  K-Fenster nicht erkannt, deshalb K nicht zu gross waehlen.
    //
    mx2 = 0.0;                                 // t = L-1
    for (i = rshd->L-1; i < K+rshd->L; i++) {  // i=t .. i=t+K < t+1+K
        re_cx = creal(cx[i]);  // imag(cx)=0
        //if (fabs(re_cx) > fabs(mx)) {
        if (re_cx*re_cx > mx2) {
            mx = re_cx;
            mx2 = mx*mx;
            mp = i;
        }
    }
    if (mp == rshd->L-1 || mp == K+rshd->L-1) return -4; // Randwert
    //  mp == t            mp == K+t

    mpos = pos - (K + rshd->L-1) + mp; // t = L-1

    xnorm = 0.0;
    for (i = 0; i < rshd->L; i++) xnorm += xn[mp-i]*xn[mp-i];
    xnorm = sqrt(xnorm);

    mx /= xnorm*N_DFT;

    if (option_iq) mpos -= dsp__lpFMtaps/2;  // lowpass delay

    *maxv = mx;
    *maxvpos = mpos;

    if (option_dc) {
        rshd->df = rshd->dc / (2.0*FM_GAIN*dsp__decM);  // freq offset estimate
    }

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

static int read_wav_header(FILE *fp, int wav_channel) {
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;

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

    if (wav_channel >= 0  &&  wav_channel < channels) wav_ch = wav_channel;
    else wav_ch = 0;
    //fprintf(stderr, "channel-In : %d\n", wav_ch+1);

    if (bits_sample != 8 && bits_sample != 16 && bits_sample != 32) return -1;

    if (sample_rate == 900001) sample_rate -= 1;

    return 0;
}

static int f32read_sample(FILE *fp, float *s) {
    int i;
    unsigned int word = 0;
    short *b = (short*)&word;
    float *f = (float*)&word;

    for (i = 0; i < channels; i++) {

        if (fread( &word, bits_sample/8, 1, fp) != 1) return EOF;

        if (i == wav_ch) {  // i = 0: links bzw. mono
            //if (bits_sample ==  8)  sint = b-128;   // 8bit: 00..FF, centerpoint 0x80=128
            //if (bits_sample == 16)  sint = (short)b;

            if (bits_sample == 32) {
                *s = *f;
            }
            else {
                if (bits_sample ==  8) { *b -= 128; }
                *s = *b/128.0;
                if (bits_sample == 16) { *s /= 256.0; }
            }
        }
    }

    return 0;
}


// IQ-dc
typedef struct {
    double sumIQx;
    double sumIQy;
    float avgIQx;
    float avgIQy;
    ui32_t cnt;
    ui32_t maxcnt;
} iq_dc_t;
static iq_dc_t IQdc;

static int f32read_csample(FILE *fp, float complex *z) {

    float x, y;

    if (bits_sample == 32) { //float32
        float f[2];
        if (fread( f, bits_sample/8, 2, fp) != 2) return EOF;
        x = f[0];
        y = f[1];
    }
    else if (bits_sample == 16) { //int16
        short b[2];
        if (fread( b, bits_sample/8, 2, fp) != 2) return EOF;
        x = b[0]/32768.0;
        y = b[1]/32768.0;
    }
    else {  // bits_sample == 8   //uint8
        ui8_t u[2];
        if (fread( u, bits_sample/8, 2, fp) != 2) return EOF;
        x = (u[0]-128)/128.0;
        y = (u[1]-128)/128.0;
    }

    *z = (x - IQdc.avgIQx) + I*(y - IQdc.avgIQy);

    IQdc.sumIQx += x;
    IQdc.sumIQy += y;
    IQdc.cnt += 1;
    if (IQdc.cnt == IQdc.maxcnt) {
        IQdc.avgIQx = IQdc.sumIQx/(float)IQdc.maxcnt;
        IQdc.avgIQy = IQdc.sumIQy/(float)IQdc.maxcnt;
        IQdc.sumIQx = 0; IQdc.sumIQy = 0; IQdc.cnt = 0;
    }

    return 0;
}

static int f32read_cblock(FILE *fp) {

    int n;
    int len;
    float x, y;

    len = dsp__decM;

    if (bits_sample == 8) { //uint8
        ui8_t u[2*dsp__decM];
        len = fread( u, bits_sample/8, 2*dsp__decM, fp) / 2;
        //for (n = 0; n < len; n++) dsp__decMbuf[n] = (u[2*n]-128)/128.0 + I*(u[2*n+1]-128)/128.0;
        // u8: 0..255, 128 -> 0V
        for (n = 0; n < len; n++) {
            x = (u[2*n  ]-128)/128.0;
            y = (u[2*n+1]-128)/128.0;
            dsp__decMbuf[n] = (x-IQdc.avgIQx) + I*(y-IQdc.avgIQy);
            IQdc.sumIQx += x;
            IQdc.sumIQy += y;
            IQdc.cnt += 1;
            if (IQdc.cnt == IQdc.maxcnt) {
                IQdc.avgIQx = IQdc.sumIQx/(float)IQdc.maxcnt;
                IQdc.avgIQy = IQdc.sumIQy/(float)IQdc.maxcnt;
                IQdc.sumIQx = 0; IQdc.sumIQy = 0; IQdc.cnt = 0;
            }
        }
    }
    else if (bits_sample == 16) { //int16
        short b[2*dsp__decM];
        len = fread( b, bits_sample/8, 2*dsp__decM, fp) / 2;
        for (n = 0; n < len; n++) {
            x = b[2*n  ]/32768.0;
            y = b[2*n+1]/32768.0;
            dsp__decMbuf[n] = (x-IQdc.avgIQx) + I*(y-IQdc.avgIQy);
            IQdc.sumIQx += x;
            IQdc.sumIQy += y;
            IQdc.cnt += 1;
            if (IQdc.cnt == IQdc.maxcnt) {
                IQdc.avgIQx = IQdc.sumIQx/(float)IQdc.maxcnt;
                IQdc.avgIQy = IQdc.sumIQy/(float)IQdc.maxcnt;
                IQdc.sumIQx = 0; IQdc.sumIQy = 0; IQdc.cnt = 0;
            }
        }
    }
    else { // bits_sample == 32   //float32
        float f[2*dsp__decM];
        len = fread( f, bits_sample/8, 2*dsp__decM, fp) / 2;
        for (n = 0; n < len; n++) {
            x = f[2*n];
            y = f[2*n+1];
            dsp__decMbuf[n] = (x-IQdc.avgIQx) + I*(y-IQdc.avgIQy);
            IQdc.sumIQx += x;
            IQdc.sumIQy += y;
            IQdc.cnt += 1;
            if (IQdc.cnt == IQdc.maxcnt) {
                IQdc.avgIQx = IQdc.sumIQx/(float)IQdc.maxcnt;
                IQdc.avgIQy = IQdc.sumIQy/(float)IQdc.maxcnt;
                IQdc.sumIQx = 0; IQdc.sumIQy = 0; IQdc.cnt = 0;
            }
        }
    }

    return len;
}

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

// struct { int taps; double *ws}
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


static int f32buf_sample(FILE *fp, int inv) {
    float _s = 0.0;
    float s[N_bwIQ];
    static float complex z0_fm0;
    static float complex z0_fm1;
    static float complex z0_fm2;
    static float complex z0;
    float complex z_fm0=0, z_fm1=0, z_fm2=0;
    float complex z, w;
    double gain = FM_GAIN;
    int i;

    if (option_iq)
    {
        if (option_iq == 5) { // baseband decimation
            //ui32_t s_reset = dsp__dectaps*dsp__lut_len;
            int j;
            if ( f32read_cblock(fp) < dsp__decM ) return EOF;
            for (j = 0; j < dsp__decM; j++) {
                dsp__decXbuffer[dsp__sample_decX] = dsp__decMbuf[j] * dsp__ex[dsp__sample_decM];
                dsp__sample_decM += 1; if (dsp__sample_decM >= dsp__lut_len) dsp__sample_decM = 0;
                dsp__sample_decX += 1; if (dsp__sample_decX >= dsp__dectaps) dsp__sample_decX = 0;
            }
            z = lowpass(dsp__decXbuffer, dsp__sample_decX, dsp__dectaps, ws_dec);

        }
        else if ( f32read_csample(fp, &z) == EOF ) return EOF;

        // IF-lowpass
        // a) detect signal bandwidth/center-fq (not reliable), or
        // b) N_bwIQ FM-streams
        //
        lpIQ_buf[sample_in % dsp__lpIQtaps] = z;
        z_fm0 = lowpass(lpIQ_buf, sample_in+1, dsp__lpIQtaps, ws_lpIQ[0]);
        if (option_singleLpIQ) {
            z_fm1 = z_fm0;
            z_fm2 = z_fm0;
        }
        else {
            z_fm1 = lowpass(lpIQ_buf, sample_in+1, dsp__lpIQtaps, ws_lpIQ[1]);
            z_fm2 = lowpass(lpIQ_buf, sample_in+1, dsp__lpIQtaps, ws_lpIQ[2]);
        }
        // IQ: different modulation indices h=h(rs) -> FM-demod
        w = z_fm0 * conj(z0_fm0);
        s[0] = gain * carg(w)/M_PI;
        z0_fm0 = z_fm0;

        if (option_singleLpIQ) {
            s[1] = s[0]; z0_fm1 = z_fm1;
            s[2] = s[0]; z0_fm2 = z_fm2;
        }
        else {
            w = z_fm1 * conj(z0_fm1);
            s[1] = gain * carg(w)/M_PI;
            z0_fm1 = z_fm1;

            w = z_fm2 * conj(z0_fm2);
            s[2] = gain * carg(w)/M_PI;
            z0_fm2 = z_fm2;
        }

        w = z * conj(z0);
        s[3] = gain * carg(w)/M_PI;
        z0 = z;
    }
    else
    {
        if (f32read_sample(fp, &_s) == EOF) return EOF;
        for (i = 0; i < N_bwIQ; i++) s[i] = _s;
    }

    for (i = 0; i < N_bwIQ; i++) {
        if (inv) s[i]= -s[i];
        buf_fm[i][sample_in % M] = s[i];
    }


    sample_out = sample_in - delay;

    sample_in += 1;

    return 0;
}

static int read_bufbit(int symlen, char *bits, unsigned int mvp, int reset, float dc, rsheader_t *rshd) {
// symlen==2: manchester2 0->10,1->01->1: 2.bit

    static unsigned int rcount;
    static float rbitgrenze;

    double sum = 0.0;

    bufs = buf_fm[rshd->lpIQ];

    if (reset) {
        rcount = 0;
        rbitgrenze = 0;
    }

    // bei symlen=2 (Manchester) kein dc noetig,
    // allerdings M10-header mit symlen=1

    rbitgrenze += rshd->spb;
    do {
        sum += bufs[(rcount + mvp + M) % M] - dc;
        rcount++;
    } while (rcount < rbitgrenze);  // n < spb

    if (symlen == 2) {
        rbitgrenze += rshd->spb;
        do {
            sum -= bufs[(rcount + mvp + M) % M] - dc;
            rcount++;
        } while (rcount < rbitgrenze);  // n < spb
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

static int headcmp(int symlen, unsigned int mvp, int inv, rsheader_t *rshd) {
    int errs = 0;
    int pos;
    int step = 1;
    int len = 0;
    char sign = 0;
    float dc = 0.0;

    if (option_dc)
    {
/*
        len = rshd->L;
        for (pos = 0; pos < len; pos++) {
            dc += (double)bufs[(mvp - 1 - pos + M) % M];
        }
        dc /= (double)len;
*/
        dc = rshd->dc;
    }

    if (symlen != 1) step = 2;
    if (inv) sign=1;

    len = rshd->hLen;
    for (pos = 0; pos < len; pos += step) {
        read_bufbit(symlen, rawbits+pos, mvp+1-(int)(rshd->hLen*rshd->spb), pos==0, dc, rshd);
    }
    rawbits[pos] = '\0';

    while (len > 0) {
        if ((rawbits[len-1]^sign) != rshd->header[len-1]) errs += 1;
        len--;
    }

    return errs;
}


static ui8_t bits2byte(char *bitstr) {
    int i, bit, d, byteval;
    int bitpos;

    bitpos = 0;
    byteval = 0;
    d = 1;
    for (i = 0; i < 8; i++) {
        //bit=*(bitstr+bitpos+i); /* little endian */
        bit=*(bitstr+bitpos+7-i);  /* big endian */
        if         (bit == '1')    byteval += d;
        else /*if ((bit == '0')*/  byteval += 0;
        d <<= 1;
    }

    return byteval & 0xFF;
}

static int hw(ui8_t byte) {
    int i;
    int d = 0;
    for (i = 0; i < 8; i++) {
        d += (byte & 1);
        byte >>= 1;
    }
    return d;
}

static ui32_t frm_M10(unsigned int mvp, int inv, rsheader_t *rshd) {
    float dc = 0.0;
    int pos2;
    char bit0 = '0';
    char mb[2];
    char frmbit[16+1];
    ui8_t b[2];
    ui32_t bytes;

    int ofs = (strlen(rshd->header) - 28)/2;

    if (ofs < 0 || ofs > 8) ofs = 0;

    if (option_dc) dc = rshd->dc;

    bit0 = 0x30 + (inv > 0);
    for (pos2 = 0; pos2 < 16; pos2 += 1) {
        if (pos2 < ofs) {
            mb[0] = rshd->header[28+2*pos2] ^ (inv>0);
        }
        else {
            read_bufbit(2, mb, mvp, pos2==ofs, dc, rshd);
        }
        frmbit[pos2] = 0x31 ^ (bit0 ^ mb[0]);
        bit0 = mb[0];
    }
    frmbit[pos2] = '\0';

    b[0] = bits2byte(frmbit);
    b[1] = bits2byte(frmbit+8);
    bytes = (b[0]<<8) | b[1];

    return bytes;
}

/* -------------------------------------------------------------------------- */

#define IF_SAMPLE_RATE      48000
#define IF_SAMPLE_RATE_MIN  32000

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


static double norm2_match(float *match, int n) {
    int i;
    double x, y = 0.0;
    for (i = 0; i < n; i++) {
        x = match[i];
        y += x*x;
    }
    return y;
}

static int init_buffers() {

    int i, j, pos;
    double t;
    double b0, b1, b2, b;
    float normMatch;

    int p2 = 1;
    int K, L;
    int n, k;
    float *match = NULL;
    float *m = NULL;

    double BT = 0.5;
    double sigma = sqrt(log(2)) / (2*M_PI*BT);

    char *bits = NULL;
    float spb = 0.0;

    int hLen = 0;
    int Lmax = 0;

    sr_base = sample_rate;
    sr_if = sample_rate;


    if (option_iq == 5)
    {
        int IF_sr = IF_SAMPLE_RATE; // designated IF sample rate
        int decM = 1; // decimate M:1
        float f_lp; // dec_lowpass: lowpass_bw/2
        float t_bw; // dec_lowpass: transition_bw
        int taps; // dec_lowpass: taps
        int wideIF = 0;

        if (set_lpIQ > IF_sr) IF_sr = set_lpIQ;

        wideIF = IF_sr > 60e3;

        sr_base = sample_rate;

        if (option_min) IF_sr = IF_SAMPLE_RATE_MIN;
        if (IF_sr > sr_base) IF_sr = sr_base;
        if (IF_sr < sr_base) {
            while (sr_base % IF_sr) IF_sr += 1;
            decM = sr_base / IF_sr;
        }

        f_lp = (IF_sr+20e3)/(4.0*sr_base);    // IF=48k
        t_bw = (IF_sr-20e3)/*/2.0*/;
        if (wideIF) {                         // IF=96k
            f_lp = (IF_sr+60e3)/(4.0*sr_base);
            t_bw = (IF_sr-60e3)/*/2.0*/;
        }
        else
        if (option_min) {
            t_bw = (IF_sr-12e3);
        }
        if (t_bw < 0) t_bw = 10e3;
        t_bw /= sr_base;
        taps = 4.0/t_bw; if (taps%2==0) taps++;

        taps = lowpass_init(f_lp, taps, &ws_dec);
        if (taps < 0) return -1;
        dsp__dectaps = taps;

        dsp__sr_base = sr_base;
        sample_rate = IF_sr; // sr_base/decM
        dsp__decM = decM;

        sr_if = IF_sr;

        fprintf(stderr, "IF: %d\n", IF_sr);
        fprintf(stderr, "dec: %d\n", decM);
    }
    if (option_iq == 5)
    {
        // look up table, exp-rotation
        int W = 2*8; // 16 Hz window
        int d = 1; // 1..W , groesster Teiler d <= W von sr_base
        int freq = (int)( dsp__xlt_fq * (double)dsp__sr_base + 0.5);
        int freq0 = freq; // init
        double f0 = freq0 / (double)dsp__sr_base; // init

        for (d = W; d > 0; d--) { // groesster Teiler d <= W von sr
            if (dsp__sr_base % d == 0) break;
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

        dsp__lut_len = dsp__sr_base / d;
        f0 = freq0 / (double)dsp__sr_base;

        dsp__ex = calloc(dsp__lut_len+1, sizeof(float complex));
        if (dsp__ex == NULL) return -1;
        for (n = 0; n < dsp__lut_len; n++) {
            t = f0*(double)n;
            dsp__ex[n] = cexp(t*2*M_PI*I);
        }


        dsp__decXbuffer = calloc( dsp__dectaps+1, sizeof(float complex));
        if (dsp__decXbuffer == NULL) return -1;

        dsp__decMbuf = calloc( dsp__decM+1, sizeof(float complex));
        if (dsp__decMbuf == NULL) return -1;
    }


    if (option_iq)
    {
        float f_lp; // lowpass_bw
        int taps; // lowpass taps: 4*sr/transition_bw

        // FM lowpass -> xn[] in getCorrDFT()
        taps = 4*sample_rate/2e3; if (taps%2==0) taps++; // 2kHz transition
        //
        f_lp = lpFM_bw[0]/(float)sample_rate;  // RS41,DFM: 4kHz (FM-audio)
        taps = lowpass_init(f_lp, taps, &ws_lpFM[0]); if (taps < 0) return -1;
        //
        f_lp = lpFM_bw[1]/(float)sample_rate;  // M10: 10kHz (FM-audio)
        taps = lowpass_init(f_lp, taps, &ws_lpFM[1]); if (taps < 0) return -1;
        //
        dsp__lpFMtaps = taps;

        // IF lowpass
        taps = 4*sample_rate/4e3; if (taps%2==0) taps++; // 4kHz transition
        if (set_lpIQ > 100.0) { // set_lpIQ > 100Hz: overwrite lpIQ_bw[]
            lpIQ_bw[0] = set_lpIQ;
            lpIQ_bw[1] = set_lpIQ;
            lpIQ_bw[2] = set_lpIQ;
            option_singleLpIQ = 1;
        }
        //
        f_lp = lpIQ_bw[0]/(float)sample_rate/2.0;  // MTS01: 6kHz (IF/IQ)
        taps = lowpass_init(f_lp, taps, &ws_lpIQ[0]); if (taps < 0) return -1;
        //
        f_lp = lpIQ_bw[1]/(float)sample_rate/2.0;  // RS41,DFM: 12kHz (IF/IQ)
        taps = lowpass_init(f_lp, taps, &ws_lpIQ[1]); if (taps < 0) return -1;
        //
        f_lp = lpIQ_bw[2]/(float)sample_rate/2.0;  // M10: 22kHz (IF/IQ)
        taps = lowpass_init(f_lp, taps, &ws_lpIQ[2]); if (taps < 0) return -1;
        //
        dsp__lpIQtaps = taps;
        lpIQ_buf = calloc( dsp__lpIQtaps+3, sizeof(float complex));
        if (lpIQ_buf == NULL) return -1;

    }

    memset(&IQdc, 0, sizeof(IQdc));
    IQdc.maxcnt = sample_rate/32;
    if (dsp__decM > 1) IQdc.maxcnt *= dsp__decM;


    for (j = 0; j < Nrs; j++) {
        #ifdef NOMTS01
        if ( strncmp(rs_hdr[j].type, "MTS01", 5) == 0 ) idx_MTS01 = j;
        #endif
        #ifdef NOC34C50
        if ( strncmp(rs_hdr[j].type, "C34C50", 6) == 0 ) idx_C34C50 = j;
        #endif
        #ifdef NOWXR301
        if ( strncmp(rs_hdr[j].type, "WXR301", 5) == 0 ) idx_WXR301 = j;
        if ( strncmp(rs_hdr[j].type, "WXRPN9", 5) == 0 ) idx_WXRPN9 = j;
        #endif
        #ifdef NOIMET1AB
        if ( strncmp(rs_hdr[j].type, "IMET1AB", 7) == 0 ) idx_IMET1AB = j;
        #endif
    }

    for (j = 0; j < Nrs; j++) {
        rs_hdr[j].spb = sample_rate/(float)rs_hdr[j].sps;
        rs_hdr[j].hLen = strlen(rs_hdr[j].header);
        rs_hdr[j].L = rs_hdr[j].hLen * rs_hdr[j].spb + 0.5;
        if (j != idx_MTS01 && j != idx_C34C50 && j != idx_WXR301 && j != idx_WXRPN9 && j != idx_IMET1AB) {
            if (rs_hdr[j].hLen > hLen) hLen = rs_hdr[j].hLen;
            if (rs_hdr[j].L > Lmax) Lmax = rs_hdr[j].L;
        }
    }

    // L = hLen * sample_rate/2500.0 + 0.5; // max(hLen*spb)
    L = 2*Lmax;

    M = 3*L;
    //if (samples_per_bit < 6) M = 6*N;

    sample_in = 0;

    p2 = 1;
    while (p2 < M) p2 <<= 1;
    while (p2 < 0x2000) p2 <<= 1;  // or 0x4000, if sample not too short
    N_DFT = p2;
    K = N_DFT - L;
    LOG2N = log(N_DFT)/log(2)+0.1; // 32bit cpu ... intermediate floating-point precision
    //while ((1 << LOG2N) < N_DFT) LOG2N++;  // better N_DFT = (1 << LOG2N) ...

    delay = L/16;
    M = N_DFT + delay + 8; // L+K < M


    rawbits = (char *)calloc( hLen+1, sizeof(char)); if (rawbits == NULL) return -100;
    for (j = 0; j < N_bwIQ; j++) {
        buf_fm[j]  = (float *)calloc( M+1, sizeof(float)); if (buf_fm[j]  == NULL) return -100;
    }
    bufs = buf_fm[N_bwIQ-1];


    xn = calloc(N_DFT+1, sizeof(float));  if (xn == NULL) return -1;
    db = calloc(N_DFT+1, sizeof(float));  if (db == NULL) return -1;

    ew = calloc(LOG2N+1, sizeof(float complex));  if (ew == NULL) return -1;
    X  = calloc(N_DFT+1, sizeof(float complex));  if (X  == NULL) return -1;
    Z  = calloc(N_DFT+1, sizeof(float complex));  if (Z  == NULL) return -1;
    cx = calloc(N_DFT+1, sizeof(float complex));  if (cx == NULL) return -1;

    for (n = 0; n < LOG2N; n++) {
        k = 1 << n;
        ew[n] = cexp(-I*M_PI/(float)k);
    }

    match = (float *)calloc( L+1, sizeof(float)); if (match == NULL) return -1;
    m = (float *)calloc(N_DFT+1, sizeof(float));  if (m  == NULL) return -1;


    for (j = 0; j < idxRS; j++)
    {
        rs_hdr[j].Fm = (float complex *)calloc(N_DFT+1, sizeof(float complex));  if (rs_hdr[j].Fm == NULL) return -1;
        bits = rs_hdr[j].header;
        spb = rs_hdr[j].spb;
        sigma = sqrt(log(2)) / (2*M_PI*rs_hdr[j].BT);

        for (i = 0; i < rs_hdr[j].L; i++) {

            pos = i/spb;
            t = (i - pos*spb)/spb - 0.5;

            b1 = ((bits[pos] & 0x1) - 0.5)*2.0;
            b = b1*pulse(t, sigma);

            if (pos > 0) {
                b0 = ((bits[pos-1] & 0x1) - 0.5)*2.0;
                b += b0*pulse(t+1, sigma);
            }

            if (pos < hLen-1) {
                b2 = ((bits[pos+1] & 0x1) - 0.5)*2.0;
                b += b2*pulse(t-1, sigma);
            }

            match[i] = b;
        }

        normMatch = sqrt(norm2_match(match, rs_hdr[j].L));
        for (i = 0; i < rs_hdr[j].L; i++) {
            match[i] /= normMatch;
        }

        for (i = 0; i < rs_hdr[j].L; i++) m[rs_hdr[j].L-1 - i] = match[i]; // t = L-1
        while (i < N_DFT) m[i++] = 0.0;
        dft(m, rs_hdr[j].Fm);

    }


    if (option_iq)
    {
        for (j = 0; j < 2; j++) {
            WS[j] = (float complex *)calloc(N_DFT+1, sizeof(float complex));  if (WS[j] == NULL) return -1;
            for (i = 0; i < dsp__lpFMtaps; i++) m[i] = ws_lpFM[j][i];
            while (i < N_DFT) m[i++] = 0.0;
            dft(m, WS[j]);
        }
        Y = (float complex *)calloc(N_DFT+1, sizeof(float complex));  if (Y == NULL) return -1;
    }


    free(match); match = NULL;
    free(m); m = NULL;

    return K;
}

static int free_buffers() {
    int j;

    for (j = 0; j < N_bwIQ; j++) {
        if (buf_fm[j])  { free(buf_fm[j]);  buf_fm[j]  = NULL; }
    }

    if (rawbits) { free(rawbits); rawbits = NULL; }

    if (xn) { free(xn); xn = NULL; }
    if (db) { free(xn); xn = NULL; }
    if (ew) { free(ew); ew = NULL; }
    if (X)  { free(X);  X  = NULL; }
    if (Z)  { free(Z);  Z  = NULL; }
    if (cx) { free(cx); cx = NULL; }

    for (j = 0; j < idxRS; j++) {
        if (rs_hdr[j].Fm) { free(rs_hdr[j].Fm); rs_hdr[j].Fm = NULL; }
    }


    // iq buffers

    if (option_iq == 5)
    {
        if (dsp__decXbuffer) { free(dsp__decXbuffer); dsp__decXbuffer = NULL; }
        if (dsp__decMbuf) { free(dsp__decMbuf); dsp__decMbuf = NULL; }
        if (dsp__ex) { free(dsp__ex); dsp__ex = NULL; }

    }

    if (option_iq) {
        for (j = 0; j < 2; j++) {
            if (ws_lpFM[j]) { free(ws_lpFM[j]); ws_lpFM[j] = NULL; }
            if (WS[j]) { free(WS[j]); WS[j] = NULL; }
        }
        if (Y) { free(Y); Y = NULL; }

        for (j = 0; j < N_bwIQ-1; j++) {
            if (ws_lpIQ[j]) { free(ws_lpIQ[j]); ws_lpIQ[j] = NULL; }
        }
        if (lpIQ_buf) { free(lpIQ_buf); lpIQ_buf = NULL; }
    }


    return 0;
}

/* ------------------------------------------------------------------------------------ */


int main(int argc, char **argv) {

    FILE *fp = NULL;
    char *fpname = NULL;

    int j;
    int k, K;
    float mv[Nrs];
    unsigned int mv_pos[Nrs], mv0_pos[Nrs];
    int mp[Nrs];

    int header_found = 0;
    int herrs;
    float thres = 0.76;
    float tl = -1.0;

    int j_max;
    float mv_max;

    int d2_tn = Nrs;


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
            fprintf(stderr, "       -v          (verbose)\n");
            fprintf(stderr, "       -c          (continuous)\n");
            fprintf(stderr, "       --iq        (IF iq-data)\n");
            fprintf(stderr, "       --IQ <fq>   (baseband IQ at fq)\n");
            fprintf(stderr, "       --bw <kHz>  (set IQ filter bw/kHz)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "--iq") == 0) ) { option_iq = 1; }
        else if   (strcmp(*argv, "--IQ") == 0) { // fq baseband -> IF (rotate from and decimate)
            double fq = 0.0;                     // --IQ <fq> , -0.5 < fq < 0.5
            ++argv;
            if (*argv) fq = atof(*argv);
            else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            dsp__xlt_fq = -fq; // S(t) -> S(t)*exp(-f*2pi*I*t)
            option_iq = 5;
        }
        else if   (strcmp(*argv, "--bw") == 0) { // set IQ filter bandwidth / kHz
            double bw_kHz = 0.0;
            ++argv;
            if (*argv) bw_kHz = atof(*argv); else return -1;
            if (bw_kHz < 1.0) bw_kHz = 0.0; // min. 1kHz
            set_lpIQ = bw_kHz * 1e3;
        }
        else if ( (strcmp(*argv, "--dc") == 0) ) { option_dc = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if ( (strcmp(*argv, "-L") == 0) ) {
            // L-band 1680kHz (IQ: decimation not limited)
            lpIQ_bw[0] = 20e3;
            lpIQ_bw[1] = 32e3;
            lpIQ_bw[2] = 200e3;
            lpIQ_bw[3] = 400e3;
        }
        else if ( (strcmp(*argv, "-c") == 0) || (strcmp(*argv, "--cnt") == 0) ) {
            option_cont = 1;
        }
        else if ( (strcmp(*argv, "-s") == 0) || (strcmp(*argv, "--silent") == 0) ) {
            option_silent = 1;
        }
        else if ( (strcmp(*argv, "-t") == 0) || (strcmp(*argv, "--time") == 0) ) {
            ++argv;
            if (*argv) tl = atof(*argv);
            else return -50;
        }
        else if ( (strcmp(*argv, "-d2") == 0) ) {
            option_d2 = 1;
        }
        else if ( (strcmp(*argv, "--ch2") == 0) ) { wav_channel = 1; }  // right channel (default: 0=left)
        else if ( (strcmp(*argv, "--ths") == 0) ) {
            ++argv;
            if (*argv) {
                thres = atof(*argv);
                for (j = 0; j < Nrs; j++) rs_hdr[j].thres = thres;
            }
            else return -50;
        }
        else if (strcmp(*argv, "-") == 0) {
            ++argv;
            if (*argv) sample_rate = atoi(*argv); else return -1;
            ++argv;
            if (*argv) bits_sample = atoi(*argv); else return -1;
            channels = 2;
            if (sample_rate < 1 || (bits_sample != 8 && bits_sample != 16 && bits_sample != 32)) {
                fprintf(stderr, "- <sr> <bs>\n");
                return -1;
            }
            option_pcmraw = 1;
        }
        else {
            fp = fopen(*argv, "rb");
            if (fp == NULL) {
                fprintf(stderr, "error: open %s\n", *argv);
                return -50;
            }
            wavloaded = 1;
        }
        ++argv;
    }
    if (!wavloaded) fp = stdin;

    if (option_d2) {
        option_cont = 0;
    }

    if (option_pcmraw == 0) {
        j = read_wav_header(fp, wav_channel);
        if ( j < 0 ) {
            fclose(fp);
            fprintf(stderr, "error: wav header\n");
            return -50;
        }
    }

    if (option_iq && channels < 2) {
        fprintf(stderr, "error: iq channels < 2\n");
        return -50;
    }

    K = init_buffers();
    if ( K < 0 ) {
        fprintf(stderr, "error: init buffers\n");
        return -50;
    };

    for (j = 0; j < Nrs; j++) {
        mv[j] = 0.0;
        mv_pos[j] = 0;
        mp[j] = 0;
    }
    j_max = 0;
    mv_max = 0.0;

    k = 0;

    while ( f32buf_sample(fp, option_inv) != EOF ) {

        if (tl > 0 && sample_in > (tl+1)*sample_rate) break;  // (int)sample_out < 0

        k += 1;

        if (k >= K-4) {
            for (j = 0; j <= idxIMETafsk; j++) { // incl. IMET-preamble

                if ( j == idx_MTS01 ) continue;   // only ifdef NOMTS01
                if ( j == idx_C34C50 ) continue;  // only ifdef NOC34C50
                if ( j == idx_WXR301 ) continue;  // only ifdef NOWXR301
                if ( j == idx_WXRPN9 ) continue;  // only ifdef NOWXR301
                if ( j == idx_IMET1AB ) continue; // only ifdef NOIMET1AB

                mv0_pos[j] = mv_pos[j];
                mp[j] = getCorrDFT(K, 0, mv+j, mv_pos+j, rs_hdr+j);
            }
            k = 0;
        }
        else {
            //for (j = 0; j < Nrs; j++) mv[j] = 0.0;
            continue;
        }

        header_found = 0;
        for (j = 0; j <= idxIMETafsk; j++) // incl. IMET-preamble
        {
            if (mp[j] > 0 && (mv[j] > rs_hdr[j].thres || mv[j] < -rs_hdr[j].thres)) {
                if (mv_pos[j] > mv0_pos[j]) {

                    herrs = headcmp(1, mv_pos[j], mv[j]<0, rs_hdr+j);
                    if (herrs < rs_hdr[j].herrs)    // max bit-errors in header
                    {
                        if ( strncmp(rs_hdr[j].type, "M10", 3) == 0 || strncmp(rs_hdr[j].type, "M20", 3) == 0)
                        {
                            ui32_t bytes = frm_M10(mv_pos[j], mv[j]<0, rs_hdr+j);
                            int len = (bytes >> 8) & 0xFF;
                            int h = hw(bytes & 0x0F); // type byte xF or x0 ?
                            if (h < 2 || h == 2 && (bytes&0xF0) == 0x20) {
                                rs_hdr[j].type = "M20";
                                rs_hdr[j].tn = tn_M20;  // M20: 45 20
                            }
                            else {
                                rs_hdr[j].type = "M10";
                                rs_hdr[j].tn = tn_M10;  // M10: 64 9F , M10+: 64 AF , M10-dop: 64 49  (len > 0x60)
                            }
                        }

                        if ( strncmp(rs_hdr[j].type, "IMETafsk", 8) == 0 ) // ? j == idxIMETafsk
                        {
                            int n, m;
                            int D = N_DFT/2 - 3;
                            float df;
                            float pow2200, pow2400;
                            int bin2200, bin2400;

                            for (n = 0; n < N_DFT; n++) {
                                xn[n] = 0.0;
                                db[n] = 0.0;
                            }

                            n = 0;
                            while (n < sample_rate) { // 1 sec

                                if (f32buf_sample(fp, option_inv) == EOF) break;//goto ende;

                                xn[n % D] = buf_fm[rs_hdr[j].lpIQ][sample_out % M];
                                n++;

                                if (n % D == 0) {
                                    dft(xn, X);
                                    for (m = 0; m < N_DFT; m++) db[m] += cabs(X[m]);
                                }
                            }

                            df = bin2freq(1);
                            m = 50.0/df;
                            if (m < 1) m = 1;
                            if (freq2bin(2500) > N_DFT/2) goto ende;

                            bin2200 = freq2bin(2200);
                            pow2200 = 0.0;
                            for (n = 0; n < m; n++) pow2200 += db[ bin2200 - m/4 + n ];

                            bin2400 = freq2bin(2400);
                            pow2400 = 0.0;
                            for (n = 0; n < m; n++) pow2400 += db[ bin2400 - m/4 + n ];


                            mv[j] = fabs(mv[j]);

                            if (pow2200 > pow2400) {  // IMET1RS: peak1: 1200Hz > peak2: 2200Hz > pow(800Hz)
                                int bin800 = freq2bin(800);
                                float pow800 = 0.0;
                                for (n = 0; n < m; n++) pow800 += db[ bin800 - m/4 + n ];
                                if (pow2200 > pow800) { // IMET -> IMET1RS/IMET4
                                    int _j0 = j;
                                    if (option_iq && set_lpIQ > 50e3) j = idxRS; else j = idxI4;
                                    mv[j] = mv[_j0];
                                    mv_pos[j] = mv_pos[_j0];
                                    rs_hdr[j].dc = rs_hdr[_j0].dc;
                                    rs_hdr[j].df = rs_hdr[_j0].df;
                                    mv[_j0] = 0.0;
                                    header_found = 1;
                                }
                                else mv[j] = 0.0;
                            }
                            else { // IMET -> IMET1AB ?
                                // IMET1AB post-processing might block MRZ detection
                                // skip after number of tries or detect imet1ab directly
                                //
                                mv[j] = 0.0;
                            }
                        }
                        else { // if not IMET
                            header_found = 1;
                        }

                        if (header_found) {
                            if (!option_silent && (mv[j] > rs_hdr[j].thres || mv[j] < -rs_hdr[j].thres)) {
                                if (option_d2) {
                                    rs_detect2[j] += 1;
                                    d2_tn = rs_d2();
                                    if ( d2_tn == Nrs ) header_found = 0;
                                }
                                if ( !option_d2 || j == d2_tn ) {
                                    if (option_verbose) fprintf(stdout, "sample: %d\n", mv_pos[j]);
                                    fprintf(stdout, "%s: %.4f", rs_hdr[j].type, mv[j]);
                                    if (option_dc && option_iq) {
                                        fprintf(stdout, " , %+.1fHz", rs_hdr[j].df*sr_base);
                                        if (option_verbose) {
                                            fprintf(stdout, "   [ fq-ofs: %+.6f", rs_hdr[j].df);
                                            fprintf(stdout, " = %+.1fHz ]", rs_hdr[j].df*sr_base);
                                        }
                                    }
                                    fprintf(stdout, "\n");
                                }
                            }
                            // if ((j < 3) && mv[j] < 0) header_found = -1;

                            if ( fabs(mv_max) < fabs(mv[j]) ) { // j-weights?
                                mv_max = mv[j];
                                j_max = j;
                            }
                        }
                    }
                }
            }
        }

        if (header_found && !option_cont || d2_tn < Nrs) break;
        header_found = 0;
        for (j = 0; j < Nrs; j++) mv[j] = 0.0;
    }

ende:
    free_buffers();
    fclose(fp);

    // return only best result
    // latest: j
    if (mv_max) {
        if (mv_max < 0 && j_max < 3) header_found = -1;
        else header_found = 1;
    }
    else header_found = 0;

    return (header_found * rs_hdr[j_max].tn);
}

