
/*
    Meteosis MTS01 ?

    files: demod_mod.c, demod_mod.h, mts01mod.c
    gcc -O3 -c demod_mod.c
    gcc mts01mod.c demod_mod.o -lm -o mts01mod
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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


#include "demod_mod.h"


typedef struct {
    i8_t vbs;  // verbose output
    i8_t raw;  // raw frames
    i8_t inv;
    i8_t jsn;  // JSON output (auto_rx)
    i8_t aut;
} option_t;


#define BAUD_RATE   1200


#define BITS 8
#define HEADLEN 32
#define FRAMESTART (HEADLEN)

static char rawheader[] = "10101010""10101010"  // preamble: AA AA
                          "10110100""00101011"; // 10000000: B4 2B 80

#define OFS 1

#define FRAMELEN       (130+OFS)
#define BITFRAMELEN    (8*FRAMELEN)
#define DATLEN  128


typedef struct {
    int frnr;
    int year; int month; int day;
    int hrs; int min; int sec;
    double lat; double lon; double alt;
    double vH; double vD;
    float T; float RH;
    int batt;
    char ID[8+4];
    ui8_t frame_bytes[FRAMELEN+4];
    char frame_bits[BITFRAMELEN+8];
    char frm_str[FRAMELEN+4];
    int jsn_freq;   // freq/kHz (SDR)
    option_t option;
} gpx_t;


static ui32_t crc16_re(ui8_t bytes[], int len) {
    ui32_t crc16poly = 0x8005; //rev(0xA001)
    ui32_t rem = 0xFFFF; // init value
    int i, j;
    ui32_t re = 0;
    for (i = 0; i < len; i++) {
        rem = rem ^ (bytes[i] << 8);
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

    for (j = 0; j < 16; j++) {
        if (rem & (1<<(15-j)))  re |= (1<<j);
    }

    return re;
}

static int bits2bytes(char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < FRAMELEN) {

        byteval = 0;
        d = 1;
        for (i = 0; i < 8; i++) {
            //bit = bitstr[bitpos+i]; /* little endian */
            bit = bitstr[bitpos+7-i];  /* big endian */
            if         (bit == '1')     byteval += d;
            else /*if ((bit == '0') */  byteval += 0;
            d <<= 1;
        }
        bitpos += 8;
        bytes[bytepos++] = byteval;

    }

    //while (bytepos < FRAME_LEN) bytes[bytepos++] = 0;

    return 0;
}

static int fn(gpx_t *gpx, int n) {
    int pos = 0;
    if (n <= 0) return 0;
    while (n > 0 && pos < DATLEN) {
        if (gpx->frm_str[pos] == '\0') n -= 1;
        pos += 1;
    }
    return pos;
}

static float get_Temp(float R) {
// Thermistor approximation
// 1/T = 1/To + 1/B log(r) , r=R/Ro
    float B0 = 3000.0;       // B/Kelvin
    float T0 = 0.0 + 273.15;
    float R0 = 15.0;
    float T = 0;             // T/Kelvin
    if (R > 0)  T = 1.0/(1.0/T0 + 1.0/B0 * log(R/R0));
    return  T - 273.15;      // Celsius
}


static int print_frame(gpx_t *gpx, int pos) {
    int i, j;
    int crcdat, crcval, crc_ok;

    if (pos/8 < OFS+DATLEN) return -1;

    bits2bytes(gpx->frame_bits, gpx->frame_bytes);

    // CRC
    crcdat = (gpx->frame_bytes[OFS+DATLEN+1]<<8) | gpx->frame_bytes[OFS+DATLEN];
    crcval = crc16_re(gpx->frame_bytes+OFS, DATLEN);
    crc_ok = (crcdat == crcval);

    if (gpx->option.raw) {
        if (gpx->option.raw == 1) {
            for (j = 0; j < FRAMELEN; j++) {
                printf("%02X ", gpx->frame_bytes[j]);
            }
            printf(" # [%04X:%04X]", crcdat, crcval);
            printf(" # [%s]", crc_ok ? "OK" : "NO");
        }
        else {
            for (j = 0; j < BITFRAMELEN; j++) {
                printf("%c", gpx->frame_bits[j]);
                if (j % 8 == 7) printf(" ");
            }
        }
        printf("\n");
    }
    else {

        // ASCII-String:
        // SN/ID?,?,counter,YYMMDDhhmmss,?,lat,lon,alt?,?,?,?,?,?,?,?,?,?,?

        printf("%s", gpx->frame_bytes+OFS);
        printf("  [%s]", crc_ok ? "OK" : "NO");
        printf("\n");


        memset(gpx->frm_str, 0, FRAMELEN);
        strncpy(gpx->frm_str, gpx->frame_bytes+OFS, DATLEN);
        for (j = 0; j < DATLEN; j++) {
            if (gpx->frm_str[j] == ',') gpx->frm_str[j] = '\0';
        }

        int pos_ID = fn(gpx, 0);
        strncpy(gpx->ID, gpx->frm_str+pos_ID, 8);

        int pos_FRNR = fn(gpx, 2);
        gpx->frnr = atoi(gpx->frm_str+pos_FRNR);

        int pos_DATETIME = fn(gpx, 3);
        char datetime_str[12+1];
        strncpy(datetime_str, gpx->frm_str+pos_DATETIME, 12);
        datetime_str[12] = '\0';
        gpx->sec = atoi(datetime_str+10); datetime_str[10] = '\0';
        gpx->min = atoi(datetime_str+ 8); datetime_str[ 8] = '\0';
        gpx->hrs = atoi(datetime_str+ 6); datetime_str[ 6] = '\0';
        gpx->day   = atoi(datetime_str+ 4); datetime_str[ 4] = '\0';
        gpx->month = atoi(datetime_str+ 2); datetime_str[ 2] = '\0';
        gpx->year  = atoi(datetime_str) + 2000;

        int pos_BATT = fn(gpx, 4);
        gpx->batt = atof(gpx->frm_str+pos_BATT);

        int pos_LAT = fn(gpx, 5);
        gpx->lat = atof(gpx->frm_str+pos_LAT);

        int pos_LON = fn(gpx, 6);
        gpx->lon = atof(gpx->frm_str+pos_LON);

        int pos_ALT = fn(gpx, 7);
        gpx->alt = atof(gpx->frm_str+pos_ALT);

        int pos_VD = fn(gpx, 8);  // 0..360 Heading
        gpx->vD = atof(gpx->frm_str+pos_VD);

        int pos_VH = fn(gpx, 9); // m/s vH
        gpx->vH = atof(gpx->frm_str+pos_VH);

        int pos_rawT1 = fn(gpx, 11);
        int pos_rawT2 = fn(gpx, 12);
        int pos_rawRH = fn(gpx, 13);

        gpx->T = get_Temp(atof(gpx->frm_str+pos_rawT1)); // rawT1==rawT2


        if (gpx->option.vbs) {
            printf(" [%4d] ", gpx->frnr);
            printf(" (%s) ", gpx->ID);
            printf(" %4d-%02d-%02d ", gpx->year, gpx->month, gpx->day);
            printf("%02d:%02d:%02d ", gpx->hrs, gpx->min, gpx->sec);
            printf(" lat: %.6f  lon: %.6f  alt: %.0f ", gpx->lat, gpx->lon, gpx->alt);
            printf("  vH: %4.1f  D: %5.1f ", gpx->vH, gpx->vD);
            printf(" Vbat:%.1fV ", gpx->batt/1000.0);
            if (gpx->T > -270.0f) printf("  T=%.1fC ", gpx->T);
            printf("\n");
        }

        if (gpx->option.jsn) {
            if (crc_ok) {
                // UTC oder GPS?
                char *ver_jsn = NULL;
                printf("{ \"type\": \"%s\"", "MTS01");
                printf(", \"frame\": %d, \"id\": \"MTS01-%s\", \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f",
                       gpx->frnr, gpx->ID, gpx->year, gpx->month, gpx->day, gpx->hrs, gpx->min, (float)gpx->sec, gpx->lat, gpx->lon, gpx->alt, gpx->vH, gpx->vD );
                printf(", \"batt\": %.2f", gpx->batt/1000.0);
                if (gpx->T > -270.0f) printf(", \"temp\": %.1f", gpx->T);
                if (gpx->jsn_freq > 0) {
                    printf(", \"freq\": %d", gpx->jsn_freq);
                }

                // Reference time/position
                printf(", \"ref_datetime\": \"%s\"", "UTC" ); // {"GPS", "UTC"} GPS-UTC=leap_sec ?
                printf(", \"ref_position\": \"%s\"", "MSL" ); // {"GPS", "MSL"} GPS=ellipsoid , MSL=geoid ?

                #ifdef VER_JSN_STR
                    ver_jsn = VER_JSN_STR;
                #endif
                if (ver_jsn && *ver_jsn != '\0') printf(", \"version\": \"%s\"", ver_jsn);
                printf(" }\n");
            }
        }

        if (gpx->option.vbs || gpx->option.jsn && crc_ok) {
            printf("\n");
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------- */


int main(int argc, char **argv) {

    int option_min = 0;
    int option_iq = 0;
    int option_iqdc = 0;
    int option_lp = 0;
    int option_dc = 0;
    int option_noLUT = 0;
    int option_softin = 0;
    int option_pcmraw = 0;
    int wavloaded = 0;
    int sel_wavch = 0;     // audio channel: left
    int spike = 0;
    int cfreq = -1;

    float baudrate = -1;

    FILE *fp = NULL;
    char *fpname = NULL;

    int k;

    int bit;
    int bitpos = 0;
    int bitQ;
    int pos;
    hsbit_t hsbit, hsbit1;

    //int headerlen = 0;

    int header_found = 0;

    float thres = 0.76;
    float _mv = 0.0;

    float lpIQ_bw = 4e3;

    int symlen = 1;
    int bitofs = 0; // 0 .. +2
    int shift = 0;

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
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -r, --raw\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            gpx.option.vbs = 1;
        }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            gpx.option.raw = 1;
        }
        else if ( (strcmp(*argv, "-R") == 0) || (strcmp(*argv, "--RAW") == 0) ) {
            gpx.option.raw = 2;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            gpx.option.inv = 1;
        }
        else if ( (strcmp(*argv, "--br") == 0) ) {
            ++argv;
            if (*argv) {
                baudrate = atof(*argv);
                if (baudrate < 1000 || baudrate > 1400) baudrate = BAUD_RATE; // default: 1200
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "--spike") == 0) ) {
            spike = 1;
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
            if (bw > 4.6 && bw < 48.0) lpIQ_bw = bw*1e3;
            option_lp |= LP_IQ;
        }
        else if   (strcmp(*argv, "--lpFM") == 0) { option_lp |= LP_FM; }  // FM lowpass
        else if   (strcmp(*argv, "--dc") == 0) { option_dc = 1; }
        else if   (strcmp(*argv, "--noLUT") == 0) { option_noLUT = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if   (strcmp(*argv, "--json") == 0) {
            gpx.option.jsn = 1;
        }
        else if   (strcmp(*argv, "--jsn_cfq") == 0) {
            int frq = -1;  // center frequency / Hz
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
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
    // LUT recommended if decM > 2
    //
    if (option_noLUT && option_iq == 5) dsp.opt_nolut = 1; else dsp.opt_nolut = 0;

    if (cfreq > 0) gpx.jsn_freq = (cfreq+500)/1000;


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
        dsp.BT = 1.5; // bw/time (ISI) // 1.0..2.0  // ?
        dsp.h = 0.9;  // 1.2 modulation index       // ? f2-f1=1275Hz
        dsp.opt_iq = option_iq;
        dsp.opt_iqdc = option_iqdc;
        dsp.opt_lp = option_lp;
        dsp.lpIQ_bw = lpIQ_bw; //4e3; // IF lowpass bandwidth
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

        //headerlen = dsp.hdrlen;


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


    while ( 1 )
    {
        if (option_softin) {
            header_found = find_softbinhead(fp, &hdb, &_mv, option_softin == 2);
        }
        else {                                                              // FM-audio:
            header_found = find_header(&dsp, thres, 2, bitofs, dsp.opt_dc); // optional 2nd pass: dc=0
            _mv = dsp.mv;
        }

        if (header_found == EOF) break;

        // mv == correlation score
        if (_mv*(0.5-gpx.option.inv) < 0) {
            gpx.option.inv ^= 0x1;  // M10: irrelevant
        }

        if (header_found) {

            bitpos = 0;
            pos = 0;

            while ( pos < BITFRAMELEN ) {

                if (option_softin) {
                    float s = 0.0;
                    bitQ = f32soft_read(fp, &s, option_softin == 2);
                    if (bitQ != EOF) {
                        bit = (s>=0.0);
                        hsbit.hb = bit;
                        hsbit.sb = s;
                    }
                }
                else {
                    float bl = -1;
                    if (option_iq >= 2) spike = 0;
                    if (option_iq > 2)  bl = 2.0;
                    //bitQ = read_slbit(&dsp, &bit, 0, bitofs, bitpos, bl, spike); // symlen=1
                    bitQ = read_softbit2p(&dsp, &hsbit, 0, bitofs, bitpos, bl, spike, &hsbit1); // symlen=1
                    bit = hsbit.hb;
                }
                if ( bitQ == EOF ) { break; }

                gpx.frame_bits[pos] = 0x30 + bit;
                pos++;
                bitpos += 1;
            }
            gpx.frame_bits[pos] = '\0';
            print_frame(&gpx, pos);
            if (pos < BITFRAMELEN) break;

            header_found = 0;
        }
    }

    if (!option_softin) free_buffers(&dsp);
    else {
        if (hdb.buf) { free(hdb.buf); hdb.buf = NULL; }
    }


    fclose(fp);

    return 0;
}

