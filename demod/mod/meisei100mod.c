
/*
 *  Meisei iMS-100
 *
 *  sync header: correlation/matched filter
 *  files: meisei100mod.c demod_mod.c demod_mod.h bch_ecc_mod.c bch_ecc_mod.h
 *  compile, either (a) or (b):
 *  (a)
 *      gcc -c demod_mod.c
 *      gcc -DINCLUDESTATIC meisei100mod.c demod_mod.o -lm -o meisei100mod
 *  (b)
 *      gcc -c demod_mod.c
 *      gcc -c bch_ecc_mod.c
 *      gcc meisei100mod.c demod_mod.o bch_ecc_mod.o -lm -o meisei100mod
 *
 *  usage:
 *      ./meisei100mod --ecc -v <audio.wav>
 *
 * author: zilog80
 */

/*
PCM-FM, 1200 baud biphase-S
1200 bit pro Sekunde: zwei Frames, die wiederum in zwei Subframes unterteilt werden koennen, d.h. 4 mal 300 bit.

Variante 1 (RS-11G)
<option -1>
049DCE1C667FDD8F537C8100004F20764630A20000000010040436 FB623080801F395FFE08A76540000FE01D0C2C1E75025006DE0A07
049DCE1C67008C73D7168200004F0F764B31A2FFFF000010270B14 FB6230000000000000000000000000000000000000000000001D59

0x00..0x02  HEADER  0x049DCE
0x03..0x04  16 bit  0.5s-counter, count%2=0:
0x1B..0x1D  HEADER  0xFB6230
0x20..0x23  32 bit  GPS-lat * 1e7 (DD.dddddd)
0x24..0x27  32 bit  GPS-lon * 1e7 (DD.dddddd)
0x28..0x2B  32 bit  GPS-alt * 1e2 (m)
0x2C..0x2D  16 bit  GPS-vH  * 1e2 (m/s)
0x2E..0x2F  16 bit  GPS-vD  * 1e2 (degree) (0..360 unsigned)
0x30..0x31  16 bit  GPS-vU  * 1e2 (m/s)
0x32..0x35  32 bit  date jjJJMMTT

0x00..0x02  HEADER  0x049DCE
0x03..0x04  16 bit  0.5s-counter, count%2=1:
0x17..0x18  16 bit  time ms xxyy, 00.000-59.000
0x19..0x1A  16 bit  time hh:mm
0x1B..0x1D  HEADER  0xFB6230


0x049DCE ^ 0xFB6230 = 0xFFFFFE


Variante 2 (iMS-100)
<option -2>
049DCE3E228023DBF53FA700003C74628430C100000000ABE00B3B FB62302390031EECCC00E656E42327562B2436C4C01CDB0F18B09A
049DCE3E23516AF62B3FC700003C7390D131C100000000AB090000 FB62300000000000032423222422202014211B13220000000067C4

0x00..0x02  HEADER  0x049DCE
0x03..0x04  16 bit  0.5s-counter, count%2=0:
0x07..0x0A  32 bit  cfg[cnt%64] (float32); cfg[0,16,32,48]=SN
0x11..0x12  30xx, xx=C1(ims100?),A2(rs11?)
0x13..0x14  16 bit  temperature, main sensor, raw
0x15..0x16  16 bit  humidity, raw
0x17..0x18  16 bit  time ms yyxx, 00.000-59.000
0x19..0x1A  16 bit  time hh:mm
0x1B..0x1D  HEADER  0xFB6230
0x1E..0x1F  16 bit  ? date (TT,MM,JJ)=(date/1000,(date/10)%100,(date%10)+10)
0x20..0x23  32 bit  GPS-lat * 1e4 (NMEA DDMM.mmmm)
0x24..0x27  32 bit  GPS-lon * 1e4 (NMEA DDMM.mmmm)
0x28..0x2A  24 bit  GPS-alt * 1e2 (m)
0x30..0x31  16 bit  GPS-vD  * 1e2 (degree)
0x32..0x33  16 bit  GPS-vH  * 1.944e2 (knots)

0x00..0x02  HEADER  0x049DCE
0x03..0x04  16 bit  0.5s-counter, count%2=1:
0x07..0x0A  32 bit  cfg[cnt%64] (float32); freq=400e3+cfg[15]*1e2/kHz
0x11..0x12  31xx, xx=C1(ims100?),A2(rs11?)
0x17..0x18  16 bit  1024-counter yyxx, +0x400=1024; rollover synchron zu ms-counter, nach rollover auch +0x300=768
0x1B..0x1D  HEADER  0xFB6230
0x20..0x21  16 bit  GPS-vV * 1.944e1 (knots)
0x22..0x23  yy00..yy03 (yy00: GPS PRN?)


Die 46bit-Bloecke sind BCH-Codewoerter. Es handelt sich um einen (63,51)-Code mit Generatorpolynom
x^12+x^10+x^8+x^5+x^4+x^3+1;
gekuerzt auf (46,34), die letzten 12 bit sind die BCH-Kontrollbits.

Die 34 Nachrichtenbits sind aufgeteilt in 16+1+16+1, d.h. nach einem 16 bit Block kommt ein Paritaetsbit,
dass 1 ist, wenn die Anzahl 1en in den 16 bit davor gerade ist, und sonst 0.
*/

/*
2 "raw" symbols -> 1 biphase-symbol (bit): 2400 (raw) baud
ecc: option_b, exact symbol rate; if necessary, adjust --br <baud>
e.g. -b --br 2398
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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


//typedef unsigned char  ui8_t;
//typedef unsigned short ui16_t;
//typedef unsigned int   ui32_t;
//typedef short i16_t;

#include "demod_mod.h"

//#define  INCLUDESTATIC 1
#ifdef INCLUDESTATIC
    #include "bch_ecc_mod.c"
#else
    #include "bch_ecc_mod.h"
#endif


#define BITFRAME_LEN    1200
#define RAWBITFRAME_LEN (BITFRAME_LEN*2)

typedef struct {
    int frnr; int frnr1;
    int ref_yr;
    int jahr; int monat; int tag;
    int std; int min; float sek;
    double lat; double lon; double alt;
    double vH; double vD; double vV;
    ui16_t f_ref;
    float T; float RH;
    char frame_rawbits[RAWBITFRAME_LEN+10];
    ui8_t frame_bits[BITFRAME_LEN+10];
    ui32_t ecc;
    float cfg[64];
    ui64_t cfg_valid;
    ui32_t _sn;
    float sn; //  0 mod 16
    float fq; // 15 mod 64
    int jsn_freq;   // freq/kHz (SDR)
    int frm0_count; int frm0_valid;
    int frm1_count; int frm1_valid;
    int vV_valid;
    RS_t RS;
} gpx_t;

/* -------------------------------------------------------------------------- */

static float f32e2(ui32_t num) {
    ui32_t val;
    float f;
/*
    int i;
    double e, s, m;

    val = 0;
    for (i=31;i>=24;i--) { val |= ((num>>i)&1)<<(i-24); }
    e = (double)val-127-2;  // exponent

    val = 0;
    for (i=22;i>= 0;i--) { val |= ((num>>i)&1)<<i; }
    m = (double)val/(1<<23);  // mantissa

    s = (num>>23)&1 ? -1.0 : +1.0 ;  // sign

    f = s*(1+m)*pow(2,e);
*/
    val  = (num     &   0x800000)<<8;  // sign
    val |= (num>>1) & 0x7F800000;      // exponent
    val |=  num     &   0x7FFFFF;      // mantissa

    memcpy(&f, &val, 4);
    f /= 4.0;  // e -= 127+2;

    return f;
}

/* -------------------------------------------------------------------------- */

#define BAUD_RATE 2400  // raw symbol rate; bit=biphase_symbol, bitrate=1200


#define HEADLEN 24
#define RAWHEADLEN (2*HEADLEN)

static char header0x049DCE[] =                      // 0x049DCE =
"101010101011010100101011001101001100101011001101"; // 00000100 10011101 11001110
static char header0x049DCEbits[] = "000001001001110111001110";
                                  //111110110110001000110000
static char header0xFB6230[] =                      // 0xFB6230 =
"110011001101001101001101010100101010110010101010"; // 11111011 01100010 00110000
static char header0xFB6230bits[] = "111110110110001000110000";
                                                    // 0x049DCE ^ 0xFB6230 = 0xFFFFFE

static char *rawheader = header0x049DCE;

/* -------------------------------------------------------------------------- */

static int biphi_s(char* frame_rawbits, ui8_t *frame_bits) {
    int j = 0;
    int byt;

    j = 0;
    while ((byt = frame_rawbits[2*j]) && frame_rawbits[2*j+1]) {
        if ((byt < 0x30) || (byt > 0x31)) break;

        if ( frame_rawbits[2*j] == frame_rawbits[2*j+1] ) { byt = 1; }
        else                                              { byt = 0; }

        frame_bits[j] = byt;
        j++;
    }
    frame_bits[j] = 0;
    return j;
}

/* -------------------------------------------------------------------------- */

static ui32_t bits2val(ui8_t bits[], int len) {
    int j;
    ui8_t bit;
    ui32_t val;
    if ((len < 0) || (len > 32)) return -1;
    val = 0;
    for (j = 0; j < len; j++) {
                bit = bits[j];
                val |= (bit << (len-1-j)); // big endian
                //val |= (bit << j);      // little endian
    }
    return val;
}

static int get_w16(ui8_t *subframe_bits, int j) {
    if (j < 0 || j > 11) return -1;
    return bits2val(subframe_bits+HEADLEN+46*(j/2)+17*(j%2), 16);
}

/* -------------------------------------------------------------------------- */

static int sanity_check_rs11g_config_temperature(gpx_t *gpx) {
    int result = 1;
    float R_old = 0;
    float T_old = INFINITY;
    int i;

    // All resistance values in the R-T interpolation table must be positive and monotonically increasing
    for (i = 0; i < 11; i++) {
        if (gpx->cfg[37+i] <= R_old) {
            result = 0;
        }
        R_old = gpx->cfg[37+i];
    }

    // All temperature values in the R-T interpolation table must be monotonically decreasing
    for (i = 0; i < 11; i++) {
        if (gpx->cfg[17+i] >= T_old) {
            result = 0;
        }
        T_old = gpx->cfg[17+i];
    }

    return result;
}

static int sanity_check_ims100_config_temperature(gpx_t *gpx) {
    int result = 1;
    float R_old = 0;
    float T_old = INFINITY;
    int i;

    // All resistance values in the R-T interpolation table must be positive and monotonically increasing
    for (i = 0; i < 12; i++) {
        if (gpx->cfg[33+i] <= R_old) {
            result = 0;
        }
        R_old = gpx->cfg[33+i];
    }

    // All temperature values in the R-T interpolation table must be monotonically decreasing
    for (i = 0; i < 12; i++) {
        if (gpx->cfg[17+i] >= T_old) {
            result = 0;
        }
        T_old = gpx->cfg[17+i];
    }

    return result;
}

/* -------------------------------------------------------------------------- */

static int reset_gpx(gpx_t *gpx) {
    int j;
    for (j = 0; j < 64; j++) gpx->cfg[j] = 0.0f;
    // DON'T RESET frame_(raw)bits and Reed-Solomon RS !
    gpx->sn = -1;
    gpx->frnr = gpx->frnr1 = 0;
    gpx->jahr = gpx->monat = gpx->tag = 0;
    gpx->std = gpx->min = 0; gpx->sek = 0.0f;
    gpx->lat = gpx->lon = gpx->alt = 0.0;
    gpx->vH = gpx->vD = gpx->vV = 0.0;
    gpx->vV_valid = 0;
    gpx->f_ref = 0;
    gpx->RH = NAN;
    gpx->T = NAN;
    gpx->cfg_valid = 0;
    gpx->_sn = 0;
    gpx->fq = 0.0f;
    gpx->frm0_count = 0; gpx->frm0_valid = 0;
    gpx->frm1_count = 0; gpx->frm1_valid = 0;
    return 0;
}

/* -------------------------------------------------------------------------- */

static int est_year_ims100(int _y, int _yr) {
    int yr_rollover = 20; // default: 2020..2029
    int yr_offset = 20;
    if (_yr > 2003 && _yr < 2100) {
        yr_rollover = _yr - 2004;
        yr_offset = (yr_rollover / 10) * 10;
    }
    _y %= 10;
    _y += yr_offset;
    if (_y < yr_rollover) _y += 10;
    return 2000+_y;
}

/* -------------------------------------------------------------------------- */


int main(int argc, char **argv) {

    int option_verbose = 0,
        option_raw = 0,
        option_dbg = 0,
        option_inv = 0,
        option_ecc = 0,    // BCH(63,51)
        option_jsn = 0;    // JSON output (auto_rx)
    int option_ptu = 0;
    int option_min = 0;
    int option_iq = 0;
    int option_iqdc = 0;
    int option_lp = 0;
    int option_dc = 0;
    int option_noLUT = 0;
    int option_softin = 0;
    int option_pcmraw = 0;
    int sel_wavch = 0;
    int wavloaded = 0;
    int cfreq = -1;

    int option_rs11g = 0,
        option_ims100 = 0;

    float baudrate = -1;

    FILE *fp;
    char *fpname;

    int subframe = 0;
    int err_frm = 0;
    int gps_chk_sum = 0;
    int gps_err = 0;
    int err_blks = 0;

    ui8_t block_err[6];
    int block;

    ui8_t *subframe_bits;

    int counter;
    ui32_t val;
    ui32_t dat2;
    int lat, lat1, lat2,
        lon, lon1, lon2,
        alt, alt1, alt2;
    ui16_t vH, vD;
     i16_t vU;
    double velH, velD, velU;
    int latdeg,londeg;
    double latmin, lonmin;
    ui32_t t1, t2, ms, min, std, tt, mm, jj;

    float sn = -1;
    float freq = -1;

    int k, j;

    int bitpos = 0, bit;
    int bitQ;

    int header_found = 0;

    float thres = 0.7;
    float _mv = 0.0;

    float lpIQ_bw = 16e3;

    int symlen = 1;
    int bitofs = 0; // 0..+1
    int shift = 0;

    int rst_gpx = 0;

    pcm_t pcm = {0};
    dsp_t dsp = {0};  //memset(&dsp, 0, sizeof(dsp));

    hdb_t hdb = {0};

    gpx_t gpx = {0};


#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _setmode(_fileno(stdin), _O_BINARY);
#endif
    setbuf(stdout, NULL);


    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
        help_out:
            fprintf(stderr, "%s <-n> [options] audio.wav\n", fpname);
            fprintf(stderr, "  n=1,2\n");
            fprintf(stderr, "  options:\n");
            //fprintf(stderr, "       -v, --verbose\n");
            fprintf(stderr, "       -r, --raw\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-r") == 0) ) { option_raw = 1; }
        else if ( (strcmp(*argv, "--dbg") == 0) ) { option_dbg = 1; }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;  // nicht noetig
        }
        else if ( (strcmp(*argv, "--ims100") == 0) ) {
            option_ims100 = 1;
        }
        else if ( (strcmp(*argv, "--rs11g") == 0) ) {
            option_rs11g = 1;
        }
        else if   (strcmp(*argv, "--ecc") == 0) { option_ecc = 1; }
        else if (strcmp(*argv, "--ptu") == 0) {
            option_ptu = 1;
        }
        else if ( (strcmp(*argv, "-v") == 0) ) { option_verbose = 1; }
        else if ( (strcmp(*argv, "--br") == 0) ) {
            ++argv;
            if (*argv) {
                baudrate = atof(*argv);
                if (baudrate < 2200 || baudrate > 2600) baudrate = 2400; // default: 2400
            }
            else return -1;
        }
        else if   (strcmp(*argv, "--ch2") == 0) { sel_wavch = 1; }  // right channel (default: 0=left)
        else if   (strcmp(*argv, "--softin") == 0)  { option_softin = 1; }  // float32 soft input
        else if   (strcmp(*argv, "--softinv") == 0) { option_softin = 2; }  // float32 inverted soft input
        else if   (strcmp(*argv, "--ths") == 0) {
            ++argv;
            if (*argv) {
                thres = atof(*argv);
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
        else if   (strcmp(*argv, "--iq2") == 0) { option_iq = 2; }
        else if   (strcmp(*argv, "--iq3") == 0) { option_iq = 3; }  // iq2==iq3
        else if   (strcmp(*argv, "--iqdc") == 0) { option_iqdc = 1; }  // iq-dc removal (iq0,2,3)
        else if   (strcmp(*argv, "--IQ") == 0) { // fq baseband -> IF (rotate from and decimate)
            double fq = 0.0;                     // --IQ <fq> , -0.5 < fq < 0.5
            ++argv;
            if (*argv) fq = atof(*argv);
            else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            dsp.xlt_fq = -fq; // S(t) -> S(t)*exp(-f*2pi*I*t)
            option_iq = 5;
        }
        else if   (strcmp(*argv, "--lpIQ") == 0) { option_lp |= LP_IQ; }  // IQ/IF lowpass
        else if   (strcmp(*argv, "--lpbw") == 0) {  // IQ lowpass BW / kHz
            double bw = 0.0;
            ++argv;
            if (*argv) bw = atof(*argv);
            else return -1;
            if (bw > 4.6 && bw < 32.0) lpIQ_bw = bw*1e3;
            option_lp |= LP_IQ;
        }
        else if   (strcmp(*argv, "--lpFM") == 0) { option_lp |= LP_FM; }  // FM lowpass
        else if   (strcmp(*argv, "--dc") == 0) { option_dc = 1; }
        else if   (strcmp(*argv, "--noLUT") == 0) { option_noLUT = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if   (strcmp(*argv, "--json") == 0) {
            option_jsn = 1;
            option_ecc = 1;
        }
        else if   (strcmp(*argv, "--jsn_cfq") == 0) {
            int frq = -1;  // center frequency / Hz
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
        }
        else if   (strcmp(*argv, "--year") == 0) {
            int _yr = 0;
            ++argv;
            if (*argv) _yr = atoi(*argv); else return -1;
            if (_yr > 2003 && _yr < 2100) gpx.ref_yr = _yr;
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
            if (option_rs11g == 1 && option_ims100 == 1) goto help_out;
            if (!option_raw && option_rs11g == 0 && option_ims100 == 0) option_ims100 = 1;
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

    if (option_iq == 5 && option_dc) option_lp |= LP_FM;

    // LUT faster for decM, however frequency correction after decimation
    // LUT recommonded if decM > 2
    //
    if (option_noLUT && option_iq == 5) dsp.opt_nolut = 1; else dsp.opt_nolut = 0;

    if (cfreq > 0) gpx.jsn_freq = (cfreq+500)/1000;

    // ims100: default ref. year
    if (gpx.ref_yr < 2000) gpx.ref_yr = 2024; // -> 2020..2029


    #ifdef EXT_FSK
    if (!option_softin) {
        option_softin = 1;
        fprintf(stderr, "reading float32 soft symbols\n");
    }
    #endif

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
        dsp.sps = (float)dsp.sr/dsp.br;
        dsp.symlen = symlen;
        dsp.symhd = 1;
        dsp._spb = dsp.sps*symlen;
        dsp.hdr = rawheader;
        dsp.hdrlen = strlen(rawheader);
        dsp.BT = 1.2; // bw/time (ISI) // 1.0..2.0
        dsp.h = 2.4;  // 2.8
        dsp.opt_iq = option_iq;
        dsp.opt_iqdc = option_iqdc;
        dsp.opt_lp = option_lp;
        dsp.lpIQ_bw = lpIQ_bw; //16e3; // IF lowpass bandwidth
        dsp.lpFM_bw = 4e3; // FM audio lowpass
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


        k = init_buffers(&dsp);
        if ( k < 0 ) {
            fprintf(stderr, "error: init buffers\n");
            return -1;
        }

        bitofs += shift;
    }
    else {
        // init circular header bit buffer
        hdb.hdr = rawheader;
        hdb.len = strlen(rawheader);
        //hdb.thb = 1.0 - 3.1/(float)hdb.len; // 1.0-max_bit_errors/hdrlen
        hdb.bufpos = -1;
        hdb.buf = NULL;
        /*
        calloc(hdb.len, sizeof(char));
        if (hdb.buf == NULL) {
            fprintf(stderr, "error: malloc\n");
            return -1;
        }
        */
        hdb.ths = 0.8; // caution/test false positive
        hdb.sbuf = calloc(hdb.len, sizeof(float));
        if (hdb.sbuf == NULL) {
            fprintf(stderr, "error: malloc\n");
            return -1;
        }
    }


    if (option_ecc) {
        rs_init_BCH64(&gpx.RS);
    }

    gpx.sn = -1;
    gpx.RH = NAN;
    gpx.T = NAN;

    while ( 1 )
    {
        if (option_softin) {
            header_found = find_softbinhead(fp, &hdb, &_mv, option_softin == 2);
        }
        else {                                                              // FM-audio:
            header_found = find_header(&dsp, thres, 1, bitofs, dsp.opt_dc); // optional 2nd pass: dc=0
            _mv = dsp.mv;
        }

        if (header_found == EOF) break;

        if (header_found) {

            bitpos = 0;
            for (j = 0; j < HEADLEN; j++) {
                gpx.frame_bits[j] = header0x049DCEbits[j] - 0x30;
            }


            while (bitpos < RAWBITFRAME_LEN/2-RAWHEADLEN) {  // 2*600-48
                if (option_softin) {
                    float s = 0.0;
                    bitQ = f32soft_read(fp, &s, option_softin == 2);
                    if (bitQ != EOF) bit = (s>=0.0); // no soft decoding
                }
                else {
                    bitQ = read_slbit(&dsp, &bit, 0, bitofs, bitpos, -1, 0); // symlen=1
                }
                if (bitQ == EOF) { break; }

                gpx.frame_rawbits[bitpos] = 0x30 + bit;
                bitpos++;
            }

            if (bitpos >= RAWBITFRAME_LEN/2-RAWHEADLEN) {  // 2*600-48
                gpx.frame_rawbits[bitpos] = '\0';

                biphi_s(gpx.frame_rawbits, gpx.frame_bits+HEADLEN);

                gps_chk_sum = 0;
                gps_err = 0;
                err_frm = 0;
                err_blks = 0;

                for (subframe = 0; subframe < 2; subframe++)
                {                                                       // option_ims100:
                    subframe_bits = gpx.frame_bits;                     // subframe 0: 049DCE
                    if (subframe > 0) subframe_bits += BITFRAME_LEN/4;  // subframe 1: FB6230

                    if (option_ecc) {
                        int   errors;
                        ui8_t cw[63+1],  // BCH(63,51), t=2
                              err_pos[4],
                              err_val[4];
                        int check_err;

                        for (block = 0; block < 6; block++) {

                            // prepare block-codeword
                            for (j =  0; j < 46; j++) cw[45-j] = subframe_bits[HEADLEN + block*46+j];
                            for (j = 46; j < 63; j++) cw[j] = 0;

                            errors = rs_decode_bch_gf2t2(&gpx.RS, cw, err_pos, err_val);

                            // check parity,padding
                            if (errors >= 0) {
                                int par = 0;
                                check_err = 0;
                                for (j = 46; j < 63; j++) { if (cw[j] != 0) check_err = 0x1; }
                                par = 1;
                                for (j = 13; j < 13+16; j++) par ^= cw[j];
                                if (cw[12] != par) check_err |= 0x100;
                                par = 1;
                                for (j = 30; j < 30+16; j++) par ^= cw[j];
                                if (cw[29] != par) check_err |= 0x10;
                                if (check_err) errors = -3;
                            }
                            if (errors >= 0) // errors > 0
                            {
                                for (j = 0; j < 46; j++) subframe_bits[HEADLEN + block*46+j] = cw[45-j];
                            }

                            if (errors < 0) {
                                if (errors == -3) block_err[block] = 0xF;
                                else              block_err[block] = 0xE;
                                err_frm += 1;
                            }
                            else  block_err[block] = errors;

                            err_blks += (errors != 0);
                        }
                    }

                    if (!option_ims100 && !option_raw) {
            jmpRS11:
                        if (rst_gpx) {
                            reset_gpx(&gpx);
                            sn = -1;
                            freq = -1;
                            rst_gpx = 0;
                        }
                        if (header_found % 2 == 1)
                        {
                            ui16_t w16[2];
                            ui32_t w32;
                            //float *fcfg = (float *)&w32;
                            float fw32;

                            val = bits2val(subframe_bits+HEADLEN, 16);
                            counter = val & 0xFFFF;
                            printf("[%d] ", counter);

                            // 0x30yy, 0x31yy
                            val = bits2val(subframe_bits+HEADLEN+46*3+17, 16);
                            if ( (val & 0xFF) >= 0xC0 && err_frm == 0) {
                                option_ims100 = 1;
                                printf("\n");
                                rst_gpx = 1;
                                goto jmpIMS;
                            }

                            w16[0] = bits2val(subframe_bits+HEADLEN+46*1   , 16);
                            w16[1] = bits2val(subframe_bits+HEADLEN+46*1+17, 16);
                            //w32 = (w16[1]<<16) | w16[0];
                            w32 =  ( (w16[1]&0xFF00)>>8 | (w16[1]&0xFF)<<8 ) << 16
                                 | ( (w16[0]&0xFF00)>>8 | (w16[0]&0xFF)<<8 );
                            fw32 = f32e2(w32);

                            if (option_dbg) {
                                printf(" # [%02d] %08x : %.1f # ", counter % 64, w32, fw32);
                            }

                            if (err_blks == 0) // err_frm zu schwach
                            {
                                gpx.cfg[counter%64] = fw32;
                                gpx.cfg_valid |= 1uLL << (counter%64);

                                // SN
                                if (counter % 16 == 0) { sn = fw32; gpx.sn = fw32; gpx._sn = w32; }
                                // freq
                                if (counter % 64 == 15) { freq = 403700+fw32*100.0; gpx.fq = freq; }

                                //PTU: Save reference frequency (sent in both even and odd frames)
                                if (counter % 4 == 0) {
                                    gpx.f_ref = bits2val(subframe_bits+HEADLEN+0*46+17, 16);
                                }

                                if (counter % 2 == 0) {
                                    if (option_ptu) {
                                        gpx.T = NAN;
                                        gpx.RH = NAN;
                                        if (gpx.f_ref != 0) {  // must know the reference frequency
                                            int T_cfg = ((gpx.cfg_valid & 0x0000FFFE0FFE0000LL) == 0x0000FFFE0FFE0000LL); // cfg[47:33,27:17]
                                            int U_cfg = ((gpx.cfg_valid & 0x001E000000000000LL) == 0x001E000000000000LL); // cfg[52:49]
                                            // Necessary parameters must exist and their values must meet the requirements
                                            if (T_cfg && sanity_check_rs11g_config_temperature(&gpx)) {
                                                ui16_t t_raw = bits2val(subframe_bits+HEADLEN+2*46+17, 16);
                                                float f = ((float)t_raw / (float)gpx.f_ref) * 4.0f;
                                                if (f > 1.0f) {
                                                    // Use config coefficients to transform measured frequency to absolute resistance (kOhms)
                                                    f = 1.0f / (f - 1.0f);
                                                    float R = gpx.cfg[33] + gpx.cfg[34]*f + gpx.cfg[35]*f*f - gpx.cfg[36];
                                                    // iMS-100 sends known resistance (cfg[44:33]) for 12 temperature sampling points
                                                    // (cfg[28:17]). Actual temperature is found by interpolating in one of these
                                                    // 11 intervals.
                                                    if (R <= gpx.cfg[37]) { // R below min value?
                                                        gpx.T = gpx.cfg[17]; // --> Set T = highest temperature
                                                    } else if (R >= gpx.cfg[47]) { // R above max value?
                                                        gpx.T = gpx.cfg[27]; // --> Set T = lowest temperature
                                                    } else {
                                                        // We now know that R is inside the interpolation range. Sampling points are
                                                        // ordered by increasing resistance (decreasing temperature).
                                                        // (We have verified this in the sanity check above.)
                                                        // Search for the interval that contains R, then interpolate linearly
                                                        // (using log(R)).
                                                        for (j = 0; j < 10; j++) {
                                                            if (R < gpx.cfg[38+j]) {
                                                                f = (logf(R) - logf(gpx.cfg[37+j])) / (logf(gpx.cfg[38+j]) - logf(gpx.cfg[37+j]));
                                                                gpx.T = gpx.cfg[17+j] - f*(gpx.cfg[17+j] - gpx.cfg[18+j]);
                                                                break;
                                                            }
                                                        }
                                                    }
                                                }
                                                if (!isnan(gpx.T)) printf("T=%.1fC ", gpx.T); // better don't use -ffast-math here
                                                else T_cfg = 0;
                                            }
                                            if (U_cfg) {
                                                ui16_t u_raw = bits2val(subframe_bits+HEADLEN+3*46, 16);
                                                float f = ((float)u_raw / (float)gpx.f_ref) * 4.0f;
                                                gpx.RH = gpx.cfg[49] + gpx.cfg[50]*f + gpx.cfg[51]*f*f + gpx.cfg[52]*f*f*f;
                                                // Limit to 0...100%
                                                gpx.RH = fmaxf(gpx.RH, 0.0f);
                                                gpx.RH = fminf(gpx.RH, 100.0f);
                                                printf("RH=%.0f%% ", gpx.RH);
                                            }
                                            if (T_cfg || U_cfg) printf(" ");
                                        }
                                    }
                                }
                            }

                            if (counter % 2 == 1) {
                                t2 = bits2val(subframe_bits+HEADLEN+5*46  , 8);  // LSB
                                t1 = bits2val(subframe_bits+HEADLEN+5*46+8, 8);
                                ms = (t1 << 8) | t2;
                                std = bits2val(subframe_bits+HEADLEN+5*46+17, 8);
                                min = bits2val(subframe_bits+HEADLEN+5*46+25, 8);
                                if (std < 24 && min < 60 && ms < 60000) { // ui32_t ms, min, std
                                    printf("  ");
                                    printf("%02d:%02d:%06.3f ", std, min, (double)ms/1000.0);
                                }
                                printf("\n");

                                if (err_blks == 0) {
                                    gpx.frnr1 = counter;
                                    gpx.std = std;
                                    gpx.min = min;
                                    gpx.sek = (double)ms/1000.0;

                                    if (option_jsn && err_blks==0 && gpx.frnr1-gpx.frnr==1) {
                                        char *ver_jsn = NULL;
                                        char id_str[] = "xxxxxx\0\0\0\0\0\0";
                                        //if (gpx._sn > 0) { sprintf(id_str, "%08x", gpx._sn); }
                                        if (gpx.sn > 0 && gpx.sn < 1e9) {
                                            sprintf(id_str, "%.0f", gpx.sn);
                                        }
                                        printf("{ \"type\": \"%s\"", "MEISEI");
                                        printf(", \"frame\": %d, \"id\": \"RS11G-%s\", \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f",
                                               gpx.frnr, id_str, gpx.jahr, gpx.monat, gpx.tag, gpx.std, gpx.min, gpx.sek, gpx.lat, gpx.lon, gpx.alt, gpx.vH, gpx.vD, gpx.vV );
                                        if (option_ptu) {
                                            if (!isnan(gpx.T)) { // better don't use -ffast-math here
                                                fprintf(stdout, ", \"temp\": %.1f",  gpx.T );
                                            }
                                            if (!isnan(gpx.RH)) { // better don't use -ffast-math here
                                                fprintf(stdout, ", \"humidity\": %.1f",  gpx.RH );
                                            }
                                        }
                                        printf(", \"subtype\": \"RS11G\"");
                                        if (gpx.jsn_freq > 0) {
                                            printf(", \"freq\": %d", gpx.jsn_freq);
                                        }
                                        if (gpx.fq > 0) { // include frequency derived from subframe information if available
                                            fprintf(stdout, ", \"tx_frequency\": %.0f", gpx.fq );
                                        }

                                        // Reference time/position
                                        printf(", \"ref_datetime\": \"%s\"", "UTC" ); // {"GPS", "UTC"} GPS-UTC=leap_sec
                                        printf(", \"ref_position\": \"%s\"", "MSL" ); // {"GPS", "MSL"} GPS=ellipsoid , MSL=geoid

                                        #ifdef VER_JSN_STR
                                            ver_jsn = VER_JSN_STR;
                                        #endif
                                        if (ver_jsn && *ver_jsn != '\0') printf(", \"version\": \"%s\"", ver_jsn);
                                        printf(" }\n");
                                        printf("\n");
                                    }

                                }
                            }
                        }

                        if (header_found % 2 == 0)
                        {
                            if (counter % 2 == 0) {
                                //offset=24+16+1;

                                lat1 = bits2val(subframe_bits+HEADLEN+46*0+17, 16);
                                lat2 = bits2val(subframe_bits+HEADLEN+46*1   , 16);
                                lon1 = bits2val(subframe_bits+HEADLEN+46*1+17, 16);
                                lon2 = bits2val(subframe_bits+HEADLEN+46*2   , 16);
                                alt1 = bits2val(subframe_bits+HEADLEN+46*2+17, 16);
                                alt2 = bits2val(subframe_bits+HEADLEN+46*3   , 16);

                                lat = (lat1 << 16) | lat2;
                                lon = (lon1 << 16) | lon2;
                                alt = (alt1 << 16) | alt2;
                                //printf("%08X %08X %08X :  ", lat, lon, alt);
                                printf("  ");
                                printf("lat: %.5f  lon: %.5f  alt: %.2f", (double)lat/1e7, (double)lon/1e7, (double)alt/1e2);
                                printf("  ");

                                vH = bits2val(subframe_bits+HEADLEN+46*3+17, 16);
                                vD = bits2val(subframe_bits+HEADLEN+46*4   , 16);
                                vU = bits2val(subframe_bits+HEADLEN+46*4+17, 16);
                                velH = (double)vH/1e2;
                                velD = (double)vD/1e2;
                                velU = (double)vU/1e2;
                                printf(" vH: %.2fm/s  D: %.1f  vV: %.2fm/s", velH, velD, velU);
                                printf("  ");

                                jj = bits2val(subframe_bits+HEADLEN+5*46+ 8, 8) + 0x0700;
                                mm = bits2val(subframe_bits+HEADLEN+5*46+17, 8);
                                tt = bits2val(subframe_bits+HEADLEN+5*46+25, 8);
                                if (jj > 1980 && mm > 0 && mm < 13 && tt > 0 && tt < 32) { // ui32_t tt, mm, jj
                                    printf(" %4d-%02d-%02d ", jj, mm, tt);
                                }

                                if (err_blks == 0) { // err_frm zu schwach
                                    gpx.frnr = counter;
                                    gpx.tag = tt;
                                    gpx.monat = mm;
                                    gpx.jahr = jj;
                                    gpx.lat = (double)lat/1e7;
                                    gpx.lon = (double)lon/1e7;
                                    gpx.alt = (double)alt/1e2;
                                    gpx.vH = velH;
                                    gpx.vD = velD;
                                    gpx.vV = velU;
                                }
                                /*
                                if (err_blks == 0 && counter%0x10==0 && gpx._sn > 0) {
                                    if (option_verbose) {
                                        fprintf(stdout, " : sn %.0f (0x%08x)", gpx.sn, gpx._sn);
                                    }
                                }
                                */
                                if (option_verbose && err_blks == 0) {
                                    if (sn > 0) {
                                        printf(" : sn %.0f (0x%08x)", sn, gpx._sn);
                                        sn = -1;
                                    }
                                    if (freq > 0) {
                                        printf(" : fq %.0f", freq); // kHz
                                        freq = -1;
                                    }
                                }
                                printf("\n");
                            }
                        }

                    }
                    else if (option_ims100 && !option_raw) { // iMS-100
            jmpIMS:
                        if (rst_gpx) {
                            reset_gpx(&gpx);
                            sn = -1;
                            freq = -1;
                            rst_gpx = 0;
                        }
                        if (header_found % 2 == 1) { // 049DCE
                            ui16_t w16[2];
                            ui32_t w32;
                            float *fcfg = (float *)&w32;

                            // 1st subframe
                            for (j = 10; j < 12; j++) gps_chk_sum += get_w16(subframe_bits, j);

                            // 0x30C1, 0x31C1
                            val = bits2val(subframe_bits+HEADLEN+46*3+17, 16);
                            if ( (val & 0xFF) < 0xC0 && err_frm == 0) {
                                option_ims100 = 0;
                                printf("\n");
                                rst_gpx = 1;
                                goto jmpRS11;
                            }

                            val = bits2val(subframe_bits+HEADLEN, 16);
                            counter = val & 0xFFFF;

                            /*if (counter % 2 == 0)*/ printf("[%d] ", counter);

                            w16[0] = bits2val(subframe_bits+HEADLEN+46*1   , 16);
                            w16[1] = bits2val(subframe_bits+HEADLEN+46*1+17, 16);
                            w32 = (w16[1]<<16) | w16[0];

                            if (option_dbg) {
                                printf(" # [%02d] %08x : %.1f # ", counter % 64, w32, *fcfg);
                            }
                                             // counter ok   and    w16[] ok  (max 1 error)
                            if (err_frm == 0 && block_err[0] < 2 && block_err[1] < 2)
                            {
                                gpx.cfg[counter%64] = *fcfg;
                                gpx.cfg_valid |= 1uLL << (counter%64);

                                // (main?) SN
                                if (counter % 0x10 == 0) { sn = *fcfg; gpx.sn = sn; gpx._sn = w32; }
                                // freq
                                if (counter % 64 == 15) { freq = 400e3+(*fcfg)*100.0; gpx.fq = freq; }

                                //PTU: Save reference frequency (sent in both even and odd frames)
                                if (counter % 4 == 0) {
                                    gpx.f_ref = bits2val(subframe_bits+HEADLEN+0*46+17, 16);
                                }
                                if (counter % 4 == 3) {
                                    gpx.f_ref = bits2val(subframe_bits+HEADLEN+3*46, 16);
                                }
                            }

                            if (counter % 2 == 0) {
                                gpx.frnr = counter;
                                t1 = bits2val(subframe_bits+HEADLEN+5*46  , 8);  // MSB
                                t2 = bits2val(subframe_bits+HEADLEN+5*46+8, 8);
                                ms = (t1 << 8) | t2;
                                std = bits2val(subframe_bits+HEADLEN+5*46+17, 8);
                                min = bits2val(subframe_bits+HEADLEN+5*46+25, 8);
                                gpx.sek = (float)ms/1000.0;
                                gpx.std = std;
                                gpx.min = min;
                                printf("  ");
                                printf("%02d:%02d:%06.3f ", gpx.std, gpx.min, gpx.sek);
                                printf("  ");

                                if (option_ptu) {
                                    gpx.T = NAN;
                                    gpx.RH = NAN;
                                    if (gpx.f_ref != 0) {  // must know the reference frequency
                                        int T_cfg = ((gpx.cfg_valid & 0x01E01FFE1FFE0000LL) == 0x01E01FFE1FFE0000LL); // cfg[56:53,44:33,28:17]
                                        int U_cfg = ((gpx.cfg_valid & 0x001E000000000000LL) == 0x001E000000000000LL); // cfg[52:49]
                                        // Necessary parameters must exist and their values must meet the requirements
                                        if (T_cfg && sanity_check_ims100_config_temperature(&gpx)) {
                                            ui16_t t_raw = bits2val(subframe_bits+HEADLEN+2*46+17, 16);
                                            float f = ((float)t_raw / (float)gpx.f_ref) * 4.0f;
                                            if (f > 1.0f) {
                                                // Use config coefficients to transform measured frequency to absolute resistance (kOhms)
                                                f = 1.0f / (f - 1.0f);
                                                float R = gpx.cfg[53] + gpx.cfg[54]*f + gpx.cfg[55]*f*f - gpx.cfg[56];
                                                // iMS-100 sends known resistance (cfg[44:33]) for 12 temperature sampling points
                                                // (cfg[28:17]). Actual temperature is found by interpolating in one of these
                                                // 11 intervals.
                                                if (R <= gpx.cfg[33]) { // R below min value?
                                                    gpx.T = gpx.cfg[17]; // --> Set T = highest temperature
                                                } else if (R >= gpx.cfg[44]) { // R above max value?
                                                    gpx.T = gpx.cfg[28]; // --> Set T = lowest temperature
                                                } else {
                                                    // We now know that R is inside the interpolation range. Sampling points are
                                                    // ordered by increasing resistance (decreasing temperature).
                                                    // (We have verified this in the sanity check above.)
                                                    // Search for the interval that contains R, then interpolate linearly
                                                    // (using log(R)).
                                                    for (j = 0; j < 11; j++) {
                                                        if (R < gpx.cfg[34+j]) {
                                                            f = (logf(R) - logf(gpx.cfg[33+j])) / (logf(gpx.cfg[34+j]) - logf(gpx.cfg[33+j]));
                                                            gpx.T = gpx.cfg[17+j] - f*(gpx.cfg[17+j] - gpx.cfg[18+j]);
                                                            break;
                                                        }
                                                    }
                                                }
                                            }
                                            if (!isnan(gpx.T)) printf("T=%.1fC ", gpx.T); // better don't use -ffast-math here
                                            else T_cfg = 0;
                                        }
                                        if (U_cfg) {
                                            ui16_t u_raw = bits2val(subframe_bits+HEADLEN+3*46, 16);
                                            float f = ((float)u_raw / (float)gpx.f_ref) * 4.0f;
                                            gpx.RH = gpx.cfg[49] + gpx.cfg[50]*f + gpx.cfg[51]*f*f + gpx.cfg[52]*f*f*f;
                                            // Limit to 0...100%
                                            gpx.RH = fmaxf(gpx.RH, 0.0f);
                                            gpx.RH = fminf(gpx.RH, 100.0f);
                                            printf("RH=%.0f%% ", gpx.RH);
                                        }
                                        if (T_cfg || U_cfg) printf(" ");
                                    }
                                }
                            }
                        }

                        if (header_found % 2 == 0) // FB6230
                        {
                            // 2nd subframe
                            for (j = 0; j < 11; j++) gps_chk_sum += get_w16(subframe_bits, j);
                            gps_err =  (gps_chk_sum & 0xFFFF) != get_w16(subframe_bits, 11); // 1st+2nd subframe

                            if (counter % 2 == 0) {
                                //offset=24+16+1;
                                int _y = 0;

                                dat2 = bits2val(subframe_bits+HEADLEN, 16);
                                gpx.tag = dat2/1000;
                                gpx.monat = (dat2/10)%100;
                                _y = dat2 % 10;
                                gpx.jahr = est_year_ims100(_y, gpx.ref_yr);
                                printf("(%04d-%02d-%02d) ", gpx.jahr, gpx.monat, gpx.tag);

                                lat1 = bits2val(subframe_bits+HEADLEN+46*0+17, 16);
                                lat2 = bits2val(subframe_bits+HEADLEN+46*1   , 16);
                                lon1 = bits2val(subframe_bits+HEADLEN+46*1+17, 16);
                                lon2 = bits2val(subframe_bits+HEADLEN+46*2   , 16);
                                alt1 = bits2val(subframe_bits+HEADLEN+46*2+17, 16);
                                alt2 = bits2val(subframe_bits+HEADLEN+46*3   ,  8);

                                // NMEA?
                                lat = (lat1 << 16) | lat2;
                                lon = (lon1 << 16) | lon2;
                                alt = (alt1 <<  8) | alt2;
                                latdeg = (int)lat / 1e6;
                                latmin = (double)(lat/1e6-latdeg)*100/60.0;
                                londeg = (int)lon / 1e6;
                                lonmin = (double)(lon/1e6-londeg)*100/60.0;
                                gpx.lat = (double)latdeg+latmin;
                                gpx.lon = (double)londeg+lonmin;
                                gpx.alt = (double)alt/1e2;

                                printf("  ");
                                printf("lat: %.5f  lon: %.5f  alt: %.2f", gpx.lat, gpx.lon, gpx.alt);
                                printf("  ");

                                vD = bits2val(subframe_bits+HEADLEN+46*4+17, 16);
                                vH = bits2val(subframe_bits+HEADLEN+46*5   , 16);
                                velD = (double)vD/1e2;       // course, true
                                velH = (double)vH/1.94384e2; // knots -> m/s
                                gpx.vH = velH;
                                gpx.vD = velD;

                                printf(" (vH: %.1fm/s  D: %.2f)", gpx.vH, gpx.vD);
                                printf("  ");
                            }
                            if (counter % 2 == 1) {
                                // cf. DF9DQ
                                vU = bits2val(subframe_bits+HEADLEN+46*0+17, 16);
                                velU = (double)vU/1.94384e1; // knots -> m/s
                                gpx.vV = velU;
                                gpx.vV_valid = (vU != 0);
                                if (gpx.vV_valid) {
                                    printf("  (vV: %.1fm/s)", gpx.vV);
                                }
                                else {
                                    printf("  (vV: --- m/s)");
                                }
                                printf("  ");
                            }

                            if (counter % 2 == 0) {
                                gpx.frm0_count = counter;
                                if (option_ecc) {
                                    if (gps_err) printf("(no)"); else printf("(ok)");
                                    if (err_frm) printf("[NO]"); else printf("[OK]");
                                    gpx.frm0_valid = (err_frm==0 && gps_err==0);
                                }
                                if (option_verbose) {
                                    if (sn > 0) { // cfg[0,16,32,48]=SN
                                        printf(" : sn %.0f", sn);
                                        sn = -1;
                                    }
                                }
                                printf("\n");
                            }
                            if (counter % 2 == 1) {
                                gpx.frm1_count = counter;
                                if (option_ecc) {
                                    if (gps_err) printf("(no)"); else printf("(ok)");
                                    if (err_frm) printf("[NO]"); else printf("[OK]");
                                    gpx.frm1_valid = (err_frm==0 && gps_err==0);
                                }
                                if (option_verbose) {
                                    if (freq > 0) { // cfg[15]=freq
                                        printf(" : fq %.0f", freq); // kHz
                                        freq = -1;
                                    }
                                }
                                printf("\n");

                                if (option_jsn && gpx.frm0_valid) {
                                    char *ver_jsn = NULL;
                                    char id_str[] = "xxxxxx\0\0\0\0\0\0";
                                    if (gpx.sn > 0 && gpx.sn < 1e9) {
                                        sprintf(id_str, "%.0f", gpx.sn);
                                    }
                                    printf("{ \"type\": \"%s\"", "MEISEI"); // alt: "IMS100"
                                    printf(", \"frame\": %d, \"id\": \"IMS100-%s\", \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f",
                                           gpx.frnr, id_str, gpx.jahr, gpx.monat, gpx.tag, gpx.std, gpx.min, gpx.sek, gpx.lat, gpx.lon, gpx.alt, gpx.vH, gpx.vD );
                                    if (gpx.frm1_valid && (gpx.frm1_count == gpx.frm0_count + 1)) {
                                        if (gpx.vV_valid) printf(", \"vel_v\": %.5f", gpx.vV );
                                    }
                                    if (option_ptu) {
                                        if (!isnan(gpx.T)) { // don't use -ffast-math here
                                            fprintf(stdout, ", \"temp\": %.1f",  gpx.T );
                                        }
                                        if (!isnan(gpx.RH)) { // don't use -ffast-math here
                                            fprintf(stdout, ", \"humidity\": %.1f",  gpx.RH );
                                        }
                                    }
                                    printf(", \"subtype\": \"IMS100\"");
                                    if (gpx.jsn_freq > 0) { // not gpx.fq, because gpx.sn not in every frame
                                        printf(", \"freq\": %d", gpx.jsn_freq);
                                    }
                                    if (gpx.fq > 0) { // include frequency derived from subframe information if available
                                        fprintf(stdout, ", \"tx_frequency\": %.0f", gpx.fq );
                                    }

                                    // Reference time/position
                                    printf(", \"ref_datetime\": \"%s\"", "UTC" ); // {"GPS", "UTC"} GPS-UTC=leap_sec
                                    printf(", \"ref_position\": \"%s\"", "MSL" ); // {"GPS", "MSL"} GPS=ellipsoid , MSL=geoid

                                    #ifdef VER_JSN_STR
                                        ver_jsn = VER_JSN_STR;
                                    #endif
                                    if (ver_jsn && *ver_jsn != '\0') printf(", \"version\": \"%s\"", ver_jsn);
                                    printf(" }\n");
                                    printf("\n");

                                    gpx.frm0_valid = 0;
                                }

                            }
                        }

                    }
                    else { // raw

                        val = bits2val(subframe_bits, HEADLEN);

                        printf("%06X ", val & 0xFFFFFF);
                        //printf("  ");
                        for (j = 0; j < 6; j++) {

                            val = bits2val(subframe_bits+HEADLEN+46*j   , 16);
                            printf("%04X ", val & 0xFFFF);

                            val = bits2val(subframe_bits+HEADLEN+46*j+17, 16);
                            printf("%04X ", val & 0xFFFF);

                            //val = bits2val(subframe_bits+HEADLEN+46*j+34, 12);
                            //printf("%03X ", val & 0xFFF);
                            //printf(" ");
                        }

                        if (option_ecc && option_verbose) {
                            printf("#");
                            for (block = 0; block < 6; block++) printf("%X", block_err[block]);
                            printf("#  ");
                        }

                        if (subframe > 0) printf("\n");
                    }

                    bitpos = 0;
                    header_found += 1;
                }
                header_found = 0;
            }
        }
    }

    printf("\n");


    if (!option_softin) free_buffers(&dsp);
    else {
        if (hdb.buf) { free(hdb.buf); hdb.buf = NULL; }
    }

    fclose(fp);

    return 0;
}

