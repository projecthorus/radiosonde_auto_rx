

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
    float mv2;
    ui32_t mv2_pos;
    //
    int N_norm;
    int Nvar;
    float xsum;
    float qsum;
    float *xs;
    float *qs;

    // IQ-data
    int opt_iq;
    int opt_iqdc;
    int N_IQBUF;
    float complex *rot_iqbuf;
    float complex F1sum;
    float complex F2sum;
    //
    double complex iw1;
    double complex iw2;


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
    //

    ui32_t sample_posframe;
    ui32_t sample_posnoise;

    double V_noise;
    double V_signal;
    double SNRdB;

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
    float *fm_buffer;

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


int read_wav_header(pcm_t *, FILE *);
int f32buf_sample(dsp_t *, int);
int read_slbit(dsp_t *, int*, int, int, int, float, int);
int read_softbit(dsp_t *, hsbit_t *, int, int, int, float, int);
int read_softbit2p(dsp_t *dsp, hsbit_t *shb, int inv, int ofs, int pos, float l, int spike, hsbit_t *shb1);

int init_buffers(dsp_t *);
int free_buffers(dsp_t *);

int find_header(dsp_t *, float, int, int, int);

int f32soft_read(FILE *fp, float *s, int inv);
int find_binhead(FILE *fp, hdb_t *hdb, float *score);
int find_softbinhead(FILE *fp, hdb_t *hdb, float *score, int inv);


