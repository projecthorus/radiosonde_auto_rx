
/*
 *  imet-54
 *  sync header: correlation/matched filter
 *  files: imet54mod.c demod_mod.h demod_mod.c
 *  compile:
 *      gcc -c demod_mod.c
 *      gcc imet54mod.c demod_mod.o -lm -o imet54mod
 *
 *  author: zilog80
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef CYGWIN
  #include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif

//typedef unsigned char  ui8_t;
//typedef unsigned short ui16_t;
//typedef unsigned int   ui32_t;
//typedef short i16_t;
//typedef int   i32_t;

#include "demod_mod.h"


typedef struct {
    i8_t vbs;  // verbose output
    i8_t raw;  // raw frames
    i8_t crc;
    i8_t ecc;  // Hamming ECC
    i8_t sat;  // GPS sat data
    i8_t ptu;  // PTU: temperature humidity (pressure)
    i8_t inv;
    i8_t aut;
    i8_t jsn;  // JSON output (auto_rx)
    i8_t slt;  // silent
} option_t;



#define BITS            (10)
#define FRAME_LEN       (220)
#define BITFRAME_LEN    (FRAME_LEN*BITS)


typedef struct {
    int out;
    int frnr;
    char id[9];
    ui32_t SNu32;
    int week; int timems; int gpssec;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double alt;
    double vH; double vD; double vV;
    ui8_t frame[FRAME_LEN+4];
    ui8_t frame_bits[BITFRAME_LEN+8];
    int jsn_freq;   // freq/kHz (SDR)
    option_t option;
} gpx_t;


#define HEADLEN 140
#define FRAMESTART ((HEADLEN)/BITS)


static char imet54_header[] = //"0000000001""0101010101""0000000001""0101010101"
                              //"0000000001""0101010101""0000000001""0101010101"
                                "0000000001""0101010101""0000000001""0101010101"
                                "0000000001""0101010101""0000000001""0101010101"
                                "0000000001""0101010101""0000000001""0101010101"  // 10x2: 8N1 0x00 0xAA
                                "0001001001""0001001001";//"0001001001""0001001001"; // 8N1 4x 0x24

//  preamble 10x 0x00 0xAA , sync: 4x 0x24 (, 0x42)
//static ui8_t imet54_header_bytes[8] = { 0x00, 0xAA, 0x00, 0xAA, 0x24, 0x24, 0x24, 0x24 }; // 0x42

/* ------------------------------------------------------------------------------------ */

#define BAUD_RATE 4798  //4800

/* ------------------------------------------------------------------------------------ */

static int de8n1(ui8_t *in, ui8_t *out, int len) {
    int n = 0;

    for (n = 0; n < len; n++) {
        if (n % 10 > 0 && n % 10 < 9) {
            *out = in[n];
            out++;
        }
    }

    return 0;
}

static int deinter64(ui8_t *in, ui8_t *out, int len) {
    int i, j;
    int n = 0;
    unsigned char bits64[8][8];

    while (n+64 <= len)
    {
        for (i = 0; i < 8; i++) {
            for (j = 0; j < 8; j++) bits64[i][j] = in[n + 8*i+j];
        }
        for (i = 0; i < 8; i++) {
            //for (j = 0; j < 8; j++) out[n + 8*i+j] = bits64[j][7-i];
            for (j = 0; j < 8; j++) out[n + 8*i+j] = bits64[j][i];
        }
        n += 64;
    }
    return len - n;
}

static ui8_t G[8][4] =  // Generator
                       {{ 1, 1, 0, 1},
                        { 1, 0, 1, 1},
                        { 1, 0, 0, 0},
                        { 0, 1, 1, 1},
                        { 0, 1, 0, 0},
                        { 0, 0, 1, 0},
                        { 0, 0, 0, 1},
                        { 1, 1, 1, 0}};
static ui8_t H[4][8] =  // Parity-Check
                       {{ 1, 0, 1, 0, 1, 0, 1, 0},
                        { 0, 1, 1, 0, 0, 1, 1, 0},
                        { 0, 0, 0, 1, 1, 1, 1, 0},
                        { 1, 1, 1, 1, 1, 1, 1, 1}};
static ui8_t He[8] = { 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x8}; // Spalten von H:
                                                                // 1-bit-error-Syndrome
static ui32_t bits2val_le(ui8_t *bits, int len) { // little endian
    int j;
    ui32_t val;
    if ((len < 0) || (len > 32)) return -1; // = 0xFFFF
    val = 0;
    for (j = 0; j < len; j++) {
        val |= bits[j] << j;
    }
    return val;
}

static int check(ui8_t code[8]) {
    int i, j;
    ui32_t synval = 0;
    ui8_t syndrom[4];
    int ret=0;

    for (i = 0; i < 4; i++) {
        syndrom[i] = 0;
        for (j = 0; j < 8; j++) {
            syndrom[i] ^= H[i][j] & code[j];
        }
    }
    synval = bits2val_le(syndrom, 4);
    if (synval) {
        ret = -1;
        for (j = 0; j < 8; j++) {   // 1-bit-error
            if (synval == He[j]) {
                ret = j+1;
                break;
            }
        }
    }
    else ret = 0;
    if (ret > 0) code[ret-1] ^= 0x1;

    return ret;
}

static ui8_t ham_lut[16] = { 0x00, 0x87, 0x99, 0x1E, 0xAA, 0x2D, 0x33, 0xB4,
                             0x4B, 0xCC, 0xD2, 0x55, 0xE1, 0x66, 0x78, 0xFF };

static ui8_t hamming(int opt_ecc, ui8_t *cwb, ui8_t *sym) {
    int j;
    int ecc = 0;
    ui8_t byt = 0;
    ui8_t nib = 0;
    ui8_t ret = 0;

    if (opt_ecc) {
        ecc = check(cwb);
    }

    byt = 0;
    for (j = 0; j < 8; j++) {
        byt |= (cwb[j]&1) << j;
    }

    for (nib = 0; nib < 16; nib++) {
        if (byt == ham_lut[nib]) break;
    }
    *sym = nib;

    if (ecc < 0 || nib >= 16) ret = 0xF0;
    else if (ecc > 0)         ret = 1;
    else                      ret = 0;

    return ret; // { 0, 1, 0xF0 }
}


/* ------------------------------------------------------------------------------------ */

static ui32_t u4be(ui8_t *bytes) {  // 32bit unsigned int
    ui32_t val = 0;
    int i;
    val = 0;
    for (i = 0; i < 4; i++) {
        val |= bytes[i] << (8*(3-i));
    }
    return val;
}
static i32_t i4be(ui8_t *bytes) {  // 32bit signed int
    i32_t val = 0;
    int i;
    val = 0;
    for (i = 0; i < 4; i++) {
        val |= bytes[i] << (8*(3-i));
    }
    return val;
}


#define pos_SN        0x00  // 4 byte
#define pos_GPStime   0x04  // 4 byte
#define pos_GPSlat    0x08  // 4 byte
#define pos_GPSlon    0x0C  // 4 byte
#define pos_GPSalt    0x10  // 4 byte

static int get_GPS(gpx_t *gpx) {
    int val;
    int valdeg;
    float valmin;

    // time
    val = i4be(gpx->frame+pos_GPStime); //u4?
    gpx->timems = val;
    gpx->sek = (val%100000)/1e3;
    val /= 1000;
    val /= 100;
    gpx->min = val % 100;
    val /= 100;
    gpx->std = val % 100;

    // lat
    val = i4be(gpx->frame+pos_GPSlat);
    valdeg = val/1e6;
    valmin = (val/1e6-valdeg)*100.0/60.0;
    gpx->lat = (float)valdeg+valmin;

    // lon
    val = i4be(gpx->frame+pos_GPSlon);
    valdeg = val/1e6;
    valmin = (val/1e6-valdeg)*100.0/60.0;
    gpx->lon = (float)valdeg+valmin;

    // alt
    val = i4be(gpx->frame+pos_GPSalt);
    gpx->alt = val / 1e1;

    return 0;
}

static int get_SN(gpx_t *gpx) {
    gpx->SNu32 = u4be(gpx->frame+pos_SN);
    return 0;
}

/* ------------------------------------------------------------------------------------ */

static int print_position(gpx_t *gpx, int ecc, int ecc_gps) {

    get_SN(gpx);
    get_GPS(gpx);

    if ( !gpx->option.slt )
    {
        fprintf(stdout, " (%d) ", gpx->SNu32);

        fprintf(stdout, " %02d:%02d:%06.3f ", gpx->std, gpx->min, gpx->sek);
        fprintf(stdout, " lat: %.5f ", gpx->lat);
        fprintf(stdout, " lon: %.5f ", gpx->lon);
        fprintf(stdout, " alt: %.1f ", gpx->alt);


        if (gpx->option.ecc && ecc != 0) {
            fprintf(stdout, " # (%d)", ecc);
            fprintf(stdout, " [%d]", ecc_gps);
        }

        fprintf(stdout, "\n");
    }

    if (gpx->option.jsn && ecc >= 0) { // ecc_gps >= 0 not reliable?
        fprintf(stdout, "{ \"type\": \"%s\"", "IMET5");
        fprintf(stdout, ", \"id\": \"%u\", \"datetime\": \"%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f",
                gpx->SNu32, gpx->std, gpx->min, gpx->sek, gpx->lat, gpx->lon, gpx->alt);
        //fprintf(stdout, ", \"subtype\": \"%s\"", "IMET54");
        if (gpx->jsn_freq > 0) {
            fprintf(stdout, ", \"freq\": %d", gpx->jsn_freq);
        }
        fprintf(stdout, " }\n");
        fprintf(stdout, "\n");
    }

    return 0;
}

static void print_frame(gpx_t *gpx, int len, int b2B) {
    int i, j;
    int ecc = 0, ecc_gps = 0;
    ui8_t bits8n1[BITFRAME_LEN+10]; // (RAW)BITFRAME_LEN
    ui8_t bits[BITFRAME_LEN]; // 8/10 (RAW)BITFRAME_LEN
    ui8_t nib[FRAME_LEN];
    ui8_t ec[FRAME_LEN];
    ui32_t ofs = 3*8; // (0x24 0x24) 0x24 0x24 0x42 : 3*8


    if (b2B)
    {
        for (i = len; i < BITFRAME_LEN; i++) gpx->frame_bits[i] = 0;

        memset(bits8n1, 0, BITFRAME_LEN+10);
        memset(bits, 0, BITFRAME_LEN);
        memset(bits, 0, BITFRAME_LEN);


        de8n1(gpx->frame_bits, bits8n1, len);
        len = (8*len)/10;

        len -= ofs;
        j = deinter64(bits8n1+ofs, bits, len);
        len -= j;

        for (j = 0; j < len/8; j++) ec[j] = hamming(gpx->option.ecc, bits+8*j, nib+j);

        for (j = 0; j < len/16; j++) gpx->frame[j] = (nib[2*j]<<4) | (nib[2*j+1] & 0xF);

        ecc = 0;
        ecc_gps = 0;
        for (j = 0; j < len/8; j++) { // alt. only GPS block
            ecc += ec[j];
            if (ec[j] > 0x10) ecc = -1;
            if (j < pos_GPSalt+4+8) ecc_gps = ecc;
            if (ecc < 0) break;
        }
    }
    else {
        ecc = -2; // TODO: parse ecc-info from raw file
        ecc_gps = ecc;
    }

    if (gpx->option.raw)
    {
        for (i = 0; i < len/16; i++) {
            fprintf(stdout, "%02X", gpx->frame[i]);
            if (gpx->option.raw > 1)
            {
                fprintf(stdout, " ");
                if (gpx->option.raw == 4 && i % 4 == 3) fprintf(stdout, " ");
            }
        }
        if (gpx->option.ecc && ecc != 0) {
            fprintf(stdout, " # (%d)", ecc);
            fprintf(stdout, " [%d]", ecc_gps);
        }
        fprintf(stdout, "\n");

        if (gpx->option.slt /*&& gpx->option.jsn*/) {
            print_position(gpx, ecc, ecc_gps);
        }
    }
    else
    {
        print_position(gpx, ecc, ecc_gps);
    }
}

/* -------------------------------------------------------------------------- */


int main(int argc, char *argv[]) {

    //int option_inv = 0;    // invertiert Signal
    int option_min = 0;
    int option_iq = 0;
    int option_iqdc = 0;
    int option_lp = 0;
    int option_dc = 0;
    int option_softin = 0;
    int option_pcmraw = 0;
    int wavloaded = 0;
    int sel_wavch = 0;     // audio channel: left
    int rawhex = 0;
    int cfreq = -1;

    FILE *fp;
    char *fpname = NULL;

    int k;

    int bitpos = 0,
        pos = 0;
    int bit;
    int bitQ;
    hsbit_t hsbit, hsbit1;

    int header_found = 0;

    float thres = 0.7; // dsp.mv threshold
    float _mv = 0.0;

    float lpIQ_bw = 7.4e3;

    int symlen = 1;
    int bitofs = 1; // +0 .. +3
    int shift = 0;

    pcm_t pcm = {0};
    dsp_t dsp = {0};  //memset(&dsp, 0, sizeof(dsp));

    gpx_t gpx = {0};

    hdb_t hdb = {0};


#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _fileno(stdin)
#endif
    setbuf(stdout, NULL);


    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v, -vx, -vv  (info, aux, info/conf)\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       -i, --invert\n");
            //fprintf(stderr, "       --crc        (check CRC)\n");
            fprintf(stderr, "       --ths <x>    (peak threshold; default=%.1f)\n", thres);
            fprintf(stderr, "       --iq0,2,3    (IQ data)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            gpx.option.vbs = 1;
        }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            gpx.option.raw = 1;
        }
        else if ( (strcmp(*argv, "-r4") == 0) ) {
            gpx.option.raw = 4;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            gpx.option.inv = 1;
        }
        else if   (strcmp(*argv, "--ecc" ) == 0) { gpx.option.ecc = 1; }
        else if   (strcmp(*argv, "--sat") == 0) { gpx.option.sat = 1; }
        else if   (strcmp(*argv, "--ptu" ) == 0) { gpx.option.ptu = 1; }
        else if   (strcmp(*argv, "--silent") == 0) { gpx.option.slt = 1; }
        else if   (strcmp(*argv, "--ch2") == 0) { sel_wavch = 1; }  // right channel (default: 0=left)
        else if   (strcmp(*argv, "--auto") == 0) { gpx.option.aut = 1; }
        else if   (strcmp(*argv, "--softin") == 0) { option_softin = 1; }  // float32 soft input
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
        else if   (strcmp(*argv, "--lp") == 0) { option_lp = 1; }  // IQ lowpass
        else if   (strcmp(*argv, "--lpbw") == 0) {  // IQ lowpass BW / kHz
            double bw = 0.0;
            ++argv;
            if (*argv) bw = atof(*argv);
            else return -1;
            if (bw > 4.6 && bw < 24.0) lpIQ_bw = bw*1e3;
            option_lp = 1;
        }
        else if   (strcmp(*argv, "--dc") == 0) { option_dc = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if   (strcmp(*argv, "--json") == 0) {
            gpx.option.jsn = 1;
            gpx.option.ecc = 1;
        }
        else if   (strcmp(*argv, "--jsn_cfq") == 0) {
            int frq = -1;  // center frequency / Hz
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
        }
        else if   (strcmp(*argv, "--rawhex") == 0) { rawhex = 2; }  // raw hex input
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


    if (gpx.option.raw && gpx.option.jsn) gpx.option.slt = 1;

    if (cfreq > 0) gpx.jsn_freq = (cfreq+500)/1000;


    #ifdef EXT_FSK
    if (!option_softin) {
        option_softin = 1;
        fprintf(stderr, "reading float32 soft symbols\n");
    }
    #endif

    if (!rawhex) {

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

            // imet54: BT=1.0, h=0.8,1.0 ?
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
            dsp.symhd  = symlen;
            dsp._spb = dsp.sps*symlen;
            dsp.hdr = imet54_header;
            dsp.hdrlen = strlen(imet54_header);
            dsp.BT = 1.0; // bw/time (ISI) // 0.3..0.5  // TODO
            dsp.h = 0.8; //0.7;  // 0.7..0.8? modulation index abzgl. BT  // TODO
            dsp.opt_iq = option_iq;
            dsp.opt_iqdc = option_iqdc;
            dsp.opt_lp = option_lp;
            dsp.lpIQ_bw = lpIQ_bw;  // 7.4e3 (6e3..8e3) // IF lowpass bandwidth
            dsp.lpFM_bw = 6e3; // FM audio lowpass
            dsp.opt_dc = option_dc;
            dsp.opt_IFmin = option_min;

            if ( dsp.sps < 8 ) {
                fprintf(stderr, "note: sample rate low (%.1f sps)\n", dsp.sps);
            }


            k = init_buffers(&dsp); // BT=0.5  (IQ-Int: BT > 0.5 ?)
            if ( k < 0 ) {
                fprintf(stderr, "error: init buffers\n");
                return -1;
            }

            //if (option_iq >= 2) bitofs += 1; // FM: +1 , IQ: +2
            bitofs += shift;
        }
        else {
            // init circular header bit buffer
            hdb.hdr = imet54_header;
            hdb.len = strlen(imet54_header);
            hdb.thb = 1.0 - 3.1/(float)hdb.len; // 1.0-max_bit_errors/hdrlen
            hdb.bufpos = -1;
            hdb.buf = calloc(hdb.len, sizeof(char));
            if (hdb.buf == NULL) {
                fprintf(stderr, "error: malloc\n");
                return -1;
            }
            hdb.ths = 0.7; // caution/test false positive
            hdb.sbuf = calloc(hdb.len, sizeof(float));
            if (hdb.sbuf == NULL) {
                fprintf(stderr, "error: malloc\n");
                return -1;
            }
        }


        while ( 1 )
        {
            if (option_softin) {
                header_found = find_softbinhead(fp, &hdb, &_mv);
            }
            else {                                                              // FM-audio:
                header_found = find_header(&dsp, thres, 4, bitofs, dsp.opt_dc); // optional 2nd pass: dc=0
                _mv = dsp.mv;
            }
            if (header_found == EOF) break;

            // mv == correlation score
            if (_mv *(0.5-gpx.option.inv) < 0) {
                if (gpx.option.aut == 0) header_found = 0;
                else gpx.option.inv ^= 0x1;
            }

            if (header_found)
            {
                bitpos = 0; // byte_count*8-HEADLEN
                pos = 0;

                while ( pos < BITFRAME_LEN )
                {
                    if (option_softin) {
                        float s = 0.0;
                        bitQ = f32soft_read(fp, &s);
                        if (bitQ != EOF) {
                            bit = (s>=0.0);
                            hsbit.hb = bit;
                            hsbit.sb = s;
                        }
                    }
                    else {
                        float bl = -1;
                        if (option_iq > 2) bl = 2.0;
                        //bitQ = read_slbit(&dsp, &bit, 0, bitofs, bitpos, bl, 0); // symlen=1
                        bitQ = read_softbit2p(&dsp, &hsbit, 0, bitofs, bitpos, bl, 0, &hsbit1); // symlen=1
                        bit = hsbit.hb;
                    }
                    if ( bitQ == EOF ) break; // liest 2x EOF

                    gpx.frame_bits[pos] = hsbit.hb & 1;

                    if (gpx.option.inv) bit ^= 1;

                    bitpos += 1;
                    pos++;
                }

                gpx.frame_bits[pos] = 0;
                print_frame(&gpx, pos, 1);
                if (pos < BITFRAME_LEN) break;
                header_found = 0;

                // bis Ende der Sekunde vorspulen; allerdings Doppel-Frame alle 10 sek
                while ( 0 && bitpos < 3*BITFRAME_LEN/4 ) {
                    if (option_softin) {
                        float s = 0.0;
                        bitQ = f32soft_read(fp, &s);
                    }
                    else {
                        bitQ = read_slbit(&dsp, &bit, 0, bitofs, bitpos, -1, 0); // symlen=1
                    }
                    if (bitQ == EOF) break;
                    bitpos++;
                }

            }
        }

        if (!option_softin) free_buffers(&dsp);
        else {
            if (hdb.buf) { free(hdb.buf); hdb.buf = NULL; }
        }
    }
    else //if (rawhex)
    {
        char buffer_rawhex[2*FRAME_LEN+12];
        char *pbuf = NULL, *buf_sp = NULL;
        ui8_t frmbyte;
        int frameofs = 0, len, i;

        while (1 > 0) {

            pbuf = fgets(buffer_rawhex, 2*FRAME_LEN+12, fp);
            if (pbuf == NULL) break;
            buffer_rawhex[2*FRAME_LEN] = '\0';
            buf_sp = strchr(buffer_rawhex, ' '); // # (%d) ecc-info?
            if (buf_sp != NULL && buf_sp-buffer_rawhex < 2*FRAME_LEN) {
                buffer_rawhex[buf_sp-buffer_rawhex] = '\0';
            }
            len = strlen(buffer_rawhex) / 2;
            if (len > 20) {
                for (i = 0; i < len; i++) { //%2x  SCNx8=%hhx(inttypes.h)
                    sscanf(buffer_rawhex+2*i, "%2hhx", &frmbyte);
                    // wenn ohne %hhx: sscanf(buffer_rawhex+rawhex*i, "%2x", &byte); frame[frameofs+i] = (ui8_t)byte;
                    gpx.frame[frameofs+i] = frmbyte;
                }
                print_frame(&gpx, len*16, 0);
            }
        }
    }


    fclose(fp);

    return 0;
}

