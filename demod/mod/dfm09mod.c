
/*
 *  dfm09 (dfm06)
 *  sync header: correlation/matched filter
 *  files: dfm09mod.c demod_mod.h demod_mod.c
 *  compile:
 *      gcc -c demod_mod.c
 *      gcc dfm09mod.c demod_mod.o -lm -o dfm09mod
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


#include "demod_mod.h"


typedef struct {
    i8_t vbs;  // verbose output
    i8_t raw;  // raw frames
    i8_t crc;  // CRC check output
    i8_t ecc;  // Reed-Solomon ECC
    i8_t sat;  // GPS sat data
    i8_t ptu;  // PTU: temperature
    i8_t inv;
    i8_t aut;
    i8_t jsn;  // JSON output (auto_rx)
    i8_t dst;  // continuous pcks 0..8
    i8_t dbg;
} option_t;

typedef struct {
    int ec;
    float ts;
} pcksts_t;

typedef struct {
    ui8_t max_ch;
    ui8_t nul_ch;
    ui8_t sn_ch;
    ui8_t chXbit;
    ui32_t SN_X;
    ui32_t chX[2];
} sn_t;

#define BITFRAME_LEN  280

typedef struct {
    int frnr;
    int sonde_typ;
    ui32_t SN6;
    ui32_t SN;
    int week; int gpssec;
    int jahr; int monat; int tag;
    int std; int min; float sek;
    double lat; double lon; double alt;
    double dir; double horiV; double vertV;
    float meas24[5+2];
    float status[2];
    float _frmcnt;
    char sonde_id[16]; // "ID__:xxxxxxxx\0\0"
    char frame_bits[BITFRAME_LEN+4];
    char dat_str[9][13+1];
    sn_t snc;
    pcksts_t pck[9];
    option_t option;
    int ptu_out;
} gpx_t;


//#define HEADLEN 32
// DFM09: Manchester2: 01->1,10->0
static char dfm_rawheader[] = "10011010100110010101101001010101"; //->"0100010111001111"; // 0x45CF (big endian)
static char dfm_header[] = "0100010111001111";

/* ------------------------------------------------------------------------------------ */

#define BAUD_RATE   2500

/* ------------------------------------------------------------------------------------ */


#define B 8 // codeword: 8 bit
#define S 4 // davon 4 bit data

#define HEAD 0        //  16 bit
#define CONF (16+0)   //  56 bit
#define DAT1 (16+56)  // 104 bit
#define DAT2 (16+160) // 104 bit
               // frame: 280 bit

static ui8_t H[4][8] =  // Parity-Check
                     {{ 0, 1, 1, 1, 1, 0, 0, 0},
                      { 1, 0, 1, 1, 0, 1, 0, 0},
                      { 1, 1, 0, 1, 0, 0, 1, 0},
                      { 1, 1, 1, 0, 0, 0, 0, 1}};
static ui8_t He[8] = { 0x7, 0xB, 0xD, 0xE, 0x8, 0x4, 0x2, 0x1}; // Spalten von H:
                                                                // 1-bit-error-Syndrome

static ui32_t bits2val(ui8_t *bits, int len) { // big endian
    int j;
    ui32_t val;
    if ((len < 0) || (len > 32)) return -1; // = 0xFFFF
    val = 0;
    for (j = 0; j < len; j++) {
        val |= (bits[j] << (len-1-j));
    }
    return val;
}

static void deinterleave(char *str, int L, ui8_t *block) {
    int i, j;
    for (j = 0; j < B; j++) {  // L = 7, 13
        for (i = 0; i < L; i++) {
            if (str[L*j+i] >= 0x30 && str[L*j+i] <= 0x31) {
                block[B*i+j] = str[L*j+i] - 0x30; // ASCII -> bit
            }
        }
    }
}

static int check(ui8_t code[8]) {
    int i, j;               // Bei Demodulierung durch Nulldurchgaenge, wenn durch Fehler ausser Takt,
    ui32_t synval = 0;      // verschieben sich die bits. Fuer Hamming-Decode waere es besser,
    ui8_t syndrom[4];       // sync zu Beginn mit Header und dann Takt beibehalten fuer decision.
    int ret=0;

    for (i = 0; i < 4; i++) { // S = 4
        syndrom[i] = 0;
        for (j = 0; j < 8; j++) { // B = 8
            syndrom[i] ^= H[i][j] & code[j];
        }
    }
    synval = bits2val(syndrom, 4);
    if (synval) {
        ret = -1;
        for (j = 0; j < 8; j++) {   // 1-bit-error
            if (synval == He[j]) {  // reicht auf databits zu pruefen, d.h.
                ret = j+1;          // (systematischer Code) He[0..3]
                break;
            }
        }
    }
    else ret = 0;
    if (ret > 0) code[ret-1] ^= 0x1;

    return ret;
}

static int hamming(int opt_ecc, ui8_t *ham, int L, ui8_t *sym) {
    int i, j;
    int ecc = 0, ret = 0;      // L = 7, 13
    for (i = 0; i < L; i++) {  // L * 2 nibble (data+parity)
        if (opt_ecc) {
            ecc = check(ham+B*i);
            if (ecc > 0) ret |= (1<<i);
            if (ecc < 0) ret |= ecc; // -1
        }
        for (j = 0; j < S; j++) {  // systematic: bits 0..S-1 data
            sym[S*i+j] = ham[B*i+j];
        }
    }
    return ret;
}

static char nib2chr(ui8_t nib) {
    char c = '_';
    if (nib < 0x10) {
        if (nib < 0xA)  c = 0x30 + nib;
        else            c = 0x41 + nib-0xA;
    }
    return c;
}

static int cnt_biterr(int ec) {
    int i;
    ui8_t ecn = 0;
    for (i = 0; i < 15; i++) {
        if ( (ec>>i)&1 ) ecn++;
    }
    return ecn;
}

static int dat_out(gpx_t *gpx, ui8_t *dat_bits, int ec) {
    int i, ret = 0;
    int fr_id;
    // int jahr = 0, monat = 0, tag = 0, std = 0, min = 0;
    int frnr = 0;
    int msek = 0;
    int lat = 0, lon = 0, alt = 0;
    int nib;
    int dvv;  // signed/unsigned 16bit

    fr_id = bits2val(dat_bits+48, 4);

    if (fr_id >= 0 && fr_id <= 8) {
        for (i = 0; i < 13; i++) {
            nib = bits2val(dat_bits+4*i, 4);
            gpx->dat_str[fr_id][i] = nib2chr(nib);
        }
        gpx->dat_str[fr_id][13] = '\0';

        gpx->pck[fr_id].ts = gpx->_frmcnt; // time_stamp,frame_count,...
        if (gpx->option.ecc) {
            gpx->pck[fr_id].ec = ec; // option_ecc laesst -1 garnicht durch
            if (ec > 0) {
                ui8_t ecn = cnt_biterr(ec);
                gpx->pck[fr_id].ec = ecn;
                if ((gpx->option.dst || gpx->option.jsn) && ecn > 4) gpx->pck[fr_id].ec = -2; // threshold: #errors > 4
            }
        }
    }

    if (fr_id == 0) {
        //start = 0x1000;
        frnr = bits2val(dat_bits+24, 8);
        gpx->frnr = frnr;
    }

    if (fr_id == 1) {
        // 00..31: ? GPS-Sats in Sicht?
        msek = bits2val(dat_bits+32, 16);  // UTC (= GPS - 18sec  ab 1.1.2017)
        gpx->sek = msek/1000.0;
    }

    if (fr_id == 2) {
        lat = bits2val(dat_bits, 32);
        gpx->lat = lat/1e7;
        dvv = (short)bits2val(dat_bits+32, 16);  // (short)? zusammen mit dir sollte unsigned sein
        gpx->horiV = dvv/1e2;
    }

    if (fr_id == 3) {
        lon = bits2val(dat_bits, 32);
        gpx->lon = lon/1e7;
        dvv = bits2val(dat_bits+32, 16) & 0xFFFF;  // unsigned
        gpx->dir = dvv/1e2;
    }

    if (fr_id == 4) {
        alt = bits2val(dat_bits, 32);
        gpx->alt = alt/1e2;
        dvv = (short)bits2val(dat_bits+32, 16);  // signed
        gpx->vertV = dvv/1e2;
    }

    if (fr_id == 5) {
    }

    if (fr_id == 6) { // sat data
    }

    if (fr_id == 7) { // sat data
    }

    if (fr_id == 8) {
        gpx->jahr  = bits2val(dat_bits,   12);
        gpx->monat = bits2val(dat_bits+12, 4);
        gpx->tag   = bits2val(dat_bits+16, 5);
        gpx->std   = bits2val(dat_bits+21, 5);
        gpx->min   = bits2val(dat_bits+26, 6);
    }

    ret = fr_id;
    return ret;
}

// DFM-06 (NXP8)
static float fl20(int d) {  // float20
    int val, p;
    float f;
    p = (d>>16) & 0xF;
    val = d & 0xFFFF;
    f = val/(float)(1<<p);
    return  f;
}
/*
static float flo20(int d) {
    int m, e;
    float f1, f;
    m = d & 0xFFFF;
    e = (d >> 16) & 0xF;
    f =  m / pow(2,e);
    return  f;
}
*/

// DFM-09 (STM32)
static float fl24(int d) {  // float24
    int val, p;
    float f;
    p = (d>>20) & 0xF;
    val = d & 0xFFFFF;
    f = val/(float)(1<<p);
    return  f;
}

// temperature approximation
static float get_Temp(gpx_t *gpx) { // meas[0..4]
// NTC-Thermistor EPCOS B57540G0502
// R/T No 8402, R25=Ro=5k
// B0/100=3450
// 1/T = 1/To + 1/B log(r) , r=R/Ro
// GRAW calibration data -80C..+40C on EEPROM ?
// meas0 = g*(R + Rs)
// meas3 = g*Rs , Rs: dfm6:10k, dfm9:20k
// meas4 = g*Rf , Rf=220k
    float f  = gpx->meas24[0],
          f1 = gpx->meas24[3],
          f2 = gpx->meas24[4];
    if (gpx->ptu_out >= 0xC) {
        f  = gpx->meas24[0+1];
        f1 = gpx->meas24[3+2];
        f2 = gpx->meas24[4+2];
    }
    //float *meas = gpx->meas24;
    float B0 = 3260.0;       // B/Kelvin, fit -55C..+40C
    float T0 = 25 + 273.15;  // t0=25C
    float R0 = 5.0e3;        // R0=R25=5k
    float Rf = 220e3;        // Rf = 220k
    float g = f2/Rf;
    float R = (f-f1) / g; // meas[0,3,4] > 0 ?
    float T = 0;                     // T/Kelvin
    if (f*f1*f2 == 0) R = 0;
    if (R > 0)  T = 1/(1/T0 + 1/B0 * log(R/R0));
    return  T - 273.15; // Celsius
//  DFM-06: meas20 * 16 = meas24
//      -> (meas24[0]-meas24[3])/meas24[4]=(meas20[0]-meas20[3])/meas20[4]
}
static float get_Temp2(gpx_t *gpx) { // meas[0..4]
// NTC-Thermistor EPCOS B57540G0502
// R/T No 8402, R25=Ro=5k
// B0/100=3450
// 1/T = 1/To + 1/B log(r) , r=R/Ro
// GRAW calibration data -80C..+40C on EEPROM ?
// meas0 = g*(R+Rs)+ofs
// meas3 = g*Rs+ofs , Rs: dfm6:10k, dfm9:20k
// meas4 = g*Rf+ofs , Rf=220k
    float f  = gpx->meas24[0],
          f1 = gpx->meas24[3],
          f2 = gpx->meas24[4];
    if (gpx->ptu_out >= 0xC) {
        f  = gpx->meas24[0+1];
        f1 = gpx->meas24[3+2];
        f2 = gpx->meas24[4+2];
    }
    float B0 = 3260.0;      // B/Kelvin, fit -55C..+40C
    float T0 = 25 + 273.15; // t0=25C
    float R0 = 5.0e3;       // R0=R25=5k
    float Rf2 = 220e3;      // Rf2 = Rf = 220k
    float g_o = f2/Rf2;     // approx gain
    float Rs_o = f1/g_o;    // = Rf2 * f1/f2;
    float Rf1 = Rs_o;       // Rf1 = Rs: dfm6:10k, dfm9:20k
    float g = g_o;          // gain
    float Rb = 0.0;         // offset
    float R = 0;            // thermistor
    float T = 0;            // T/Kelvin

    // ptu_out=0xD: Rs_o=13.6, Rf2=?
    if       ( 8e3 < Rs_o && Rs_o < 12e3) Rf1 = 10e3;  // dfm6
    else if  (18e3 < Rs_o && Rs_o < 22e3) Rf1 = 20e3;  // dfm9
    g = (f2 - f1) / (Rf2 - Rf1);
    Rb = (f1*Rf2-f2*Rf1)/(f2-f1); // ofs/g

    R = (f-f1)/g;                    // meas[0,3,4] > 0 ?
    if (R > 0)  T = 1/(1/T0 + 1/B0 * log(R/R0));

    if (gpx->option.ptu && gpx->ptu_out && gpx->option.dbg) {
        printf("  (Rso: %.1f , Rb: %.1f)", Rs_o/1e3, Rb/1e3);
    }

    return  T - 273.15;
//  DFM-06: meas20 * 16 = meas24
}
static float get_Temp4(gpx_t *gpx) { // meas[0..4]
// NTC-Thermistor EPCOS B57540G0502
// [  T/C  ,   R/R25   , alpha ] :
// [ -55.0 ,  51.991   ,   6.4 ]
// [ -50.0 ,  37.989   ,   6.2 ]
// [ -45.0 ,  28.07    ,   5.9 ]
// [ -40.0 ,  20.96    ,   5.7 ]
// [ -35.0 ,  15.809   ,   5.5 ]
// [ -30.0 ,  12.037   ,   5.4 ]
// [ -25.0 ,   9.2484  ,   5.2 ]
// [ -20.0 ,   7.1668  ,   5.0 ]
// [ -15.0 ,   5.5993  ,   4.9 ]
// [ -10.0 ,   4.4087  ,   4.7 ]
// [  -5.0 ,   3.4971  ,   4.6 ]
// [   0.0 ,   2.7936  ,   4.4 ]
// [   5.0 ,   2.2468  ,   4.3 ]
// [  10.0 ,   1.8187  ,   4.2 ]
// [  15.0 ,   1.4813  ,   4.0 ]
// [  20.0 ,   1.2136  ,   3.9 ]
// [  25.0 ,   1.0000  ,   3.8 ]
// [  30.0 ,   0.82845 ,   3.7 ]
// [  35.0 ,   0.68991 ,   3.6 ]
// [  40.0 ,   0.57742 ,   3.5 ]
// -> Steinhart–Hart coefficients (polyfit):
    float p0 = 1.09698417e-03,
          p1 = 2.39564629e-04,
          p2 = 2.48821437e-06,
          p3 = 5.84354921e-08;
// T/K = 1/( p0 + p1*ln(R) + p2*ln(R)^2 + p3*ln(R)^3 )
    float f  = gpx->meas24[0],
          f1 = gpx->meas24[3],
          f2 = gpx->meas24[4];
    if (gpx->ptu_out >= 0xC) {
        f  = gpx->meas24[0+1];
        f1 = gpx->meas24[3+2];
        f2 = gpx->meas24[4+2];
    }
    //float *meas = gpx->meas24;
    float Rf = 220e3;    // Rf = 220k
    float g = f2/Rf;
    float R = (f-f1) / g; // f,f1,f2 > 0 ?
    float T = 0; // T/Kelvin
    if (R > 0)  T = 1/( p0 + p1*log(R) + p2*log(R)*log(R) + p3*log(R)*log(R)*log(R) );
    return  T - 273.15; // Celsius
//  DFM-06: meas20 * 16 = meas24
//      -> (meas24[0]-meas24[3])/meas24[4]=(meas20[0]-meas20[3])/meas20[4]
}


#define SNbit 0x0100
static int conf_out(gpx_t *gpx, ui8_t *conf_bits, int ec) {
    int ret = 0;
    int val;
    ui8_t conf_id;
    ui8_t hl;
    ui32_t SN6, SN;
    ui8_t dfm6typ;
    ui8_t sn2_ch, sn_ch;


    conf_id = bits2val(conf_bits, 4);

    if (conf_id > 4 && bits2val(conf_bits+8, 4*5) == 0) gpx->snc.nul_ch = bits2val(conf_bits, 8);

    dfm6typ = ((gpx->snc.nul_ch & 0xF0)==0x50) && (gpx->snc.nul_ch & 0x0F);
    if (dfm6typ) gpx->ptu_out = 6;
    if (dfm6typ  && (gpx->sonde_typ & 0xF) > 6)
    {   // reset if 0x5A, 0x5B (DFM-06)
        gpx->sonde_typ = 0;
        gpx->snc.max_ch = conf_id;
    }

    if (conf_id > 5 && conf_id > gpx->snc.max_ch && ec == 0) { // mind. 6 Kanaele
        if (bits2val(conf_bits+4, 4) == 0xC) { // 0xsCaaaab
            gpx->snc.max_ch = conf_id; // reset?
        }
/*
        if (bits2val(conf_bits, 8) == 0x70) { // 0x70aaaab
            gpx->snc.max_ch = conf_id; // reset?
        }
*/
    }

    // SN: mind. 6 Kanaele
    if (conf_id > 5 && (conf_id == (gpx->snc.nul_ch>>4)+1 || conf_id == gpx->snc.max_ch))
    {
        sn2_ch = bits2val(conf_bits, 8);
        sn_ch = ((sn2_ch>>4) & 0xF);  // sn_ch == config_id

        if ( (gpx->snc.nul_ch & 0x58) == 0x58 ) { // 0x5A, 0x5B
            SN6 = bits2val(conf_bits+4, 4*6);     // DFM-06: Kanal 6
            if (SN6 == gpx->SN6  &&  SN6 != 0) {  // nur Nibble-Werte 0..9
                gpx->sonde_typ = SNbit | 6;
                gpx->ptu_out = 6;
                sprintf(gpx->sonde_id, "ID06:%6X", gpx->SN6);
                //sprintf(json_sonde_id, "DFM06-%6X", gpx->SN6);
            }
            else { // reset
                gpx->sonde_typ = 0;
                //sprintf(json_sonde_id, "DFMxx-xxxxxxxx"); //json_sonde_id[0] = '\0';
            }
            gpx->SN6 = SN6;
        }
        else if (   (sn2_ch & 0xF) == 0xC    // 0xsCaaaab, s==sn_ch , s: 0xA=DFM-09 , 0xC=DFM-17? 0xD=?
                 || (sn2_ch & 0xF) == 0x0 )  // 0xs0aaaab, s==sn_ch , s: 0x7,0x8: pilotsonde PS-15?
        {
            val = bits2val(conf_bits+8, 4*5);
            hl =  (val & 0xF);
            if (hl < 2)
            {
                if ( gpx->snc.sn_ch != sn_ch ) { // -> sn_ch > 0
                    // reset
                    gpx->snc.chXbit = 0;
                    gpx->snc.chX[0] = 0;
                    gpx->snc.chX[1] = 0;
                }
                gpx->snc.sn_ch = sn_ch;
                gpx->snc.chX[hl] = (val >> 4) & 0xFFFF;
                gpx->snc.chXbit |= 1 << hl;
                if (gpx->snc.chXbit == 3) {
                    SN = (gpx->snc.chX[0] << 16) | gpx->snc.chX[1];
                    if ( SN == gpx->snc.SN_X || gpx->snc.SN_X == 0 ) {

                        gpx->sonde_typ = SNbit | sn_ch;
                        gpx->SN = SN;

                        if (sn_ch == 0xA /*&& (sn2_ch & 0xF) == 0xC*/) gpx->ptu_out = sn_ch; else gpx->ptu_out = 0;
                        if (sn_ch == 0xC) gpx->ptu_out = sn_ch;// DFM-09P, DFM-17 ?
                        if (sn_ch == 0xD && gpx->option.dbg) gpx->ptu_out = sn_ch;// DFM-17 (P?)? test 0xD ...?
                        // PS-15 ? (sn2_ch & 0xF) == 0x0 :  gpx->ptu_out = 0

                        if ( (gpx->sonde_typ & 0xF) == 0xA) {
                            sprintf(gpx->sonde_id, "ID09:%6u", gpx->SN);
                            //sprintf(json_sonde_id, "DFM09-%6u", gpx->SN);
                        }
                        else {
                            sprintf(gpx->sonde_id, "ID-%1X:%6u", gpx->sonde_typ & 0xF, gpx->SN);
                            //sprintf(json_sonde_id, "DFMx%1X-%6u", gpx->sonde_typ & 0xF,gpx->SN);
                        }
                    }
                    else { // reset
                        gpx->sonde_typ = 0;
                        //sprintf(json_sonde_id, "DFMxx-xxxxxxxx"); //json_sonde_id[0] = '\0';
                    }
                    gpx->snc.SN_X = SN;
                    gpx->snc.chXbit = 0;
                }
            }
        }
        ret = (gpx->sonde_typ & 0xF);
    }


    if (conf_id >= 0 && conf_id <= 4) {
        val = bits2val(conf_bits+4, 4*6);
        gpx->meas24[conf_id] = fl24(val);
        // DFM-09 (STM32): 24bit 0exxxxx
        // DFM-06 (NXP8):  20bit 0exxxx0
        //   fl20(bits2val(conf_bits+4, 4*5))
        //       = fl20(exxxx)
        //       = fl24(exxxx0)/2^4
        //   meas20 * 16 = meas24
    }
    if (gpx->ptu_out >= 0xC) { // DFM>=09(P)
        if (conf_id >= 5 && conf_id <= 6) {
            val = bits2val(conf_bits+4, 4*6);
            gpx->meas24[conf_id] = fl24(val);
        }
    }

    // STM32-status: Bat, MCU-Temp
    if (gpx->ptu_out >= 0xA) { // DFM>=09(P) (STM32)
        ui8_t ofs = 0;
        if (gpx->ptu_out >= 0xC) ofs = 2;
        if (conf_id == 0x5+ofs) { // voltage
            val = bits2val(conf_bits+8, 4*4);
            gpx->status[0] = val/1000.0;
        }
        if (conf_id == 0x6+ofs) { // T-intern (STM32)
            val = bits2val(conf_bits+8, 4*4);
            gpx->status[1] = val/100.0;
        }
    }

    return ret;
}

static void print_gpx(gpx_t *gpx) {
    int i, j;
    int contgps = 0;
    int output = 0;
    int jsonout = 0;
    int start = 0;

    if (gpx->frnr > 0) start = 0x1000;

    output |= start;


    for (i = 0; i < 9/*8*/; i++) { // trigger: pck8
        if ( !( (gpx->option.dst || gpx->option.jsn) && gpx->pck[i].ec < 0) )
        {
            if (gpx->pck[8].ts - gpx->pck[i].ts < 6.0)  { output |= (1<<i); }
        }
        //if (gpx->option.dst && gpx->pck[i].ec < 0) { output &= ~(1<<i); }
    }

    jsonout = output;

    contgps = ((output & 0x11F) == 0x11F); // 0,1,2,3,8

    if (gpx->option.dst && !contgps) {
        output = 0;
    }
    if (gpx->option.jsn && !contgps) {
        jsonout = 0;
    }

    if (output & 0xF000) {

        if (gpx->option.raw == 2) {
            for (i = 0; i < 9; i++) {
                printf(" %s", gpx->dat_str[i]);
                if (gpx->option.ecc) printf(" (%1X) ", gpx->pck[i].ec&0xF);
            }
            for (i = 0; i < 9; i++) {
                for (j = 0; j < 13; j++) gpx->dat_str[i][j] = ' ';
            }
        }
        else {
            if (gpx->option.aut && gpx->option.vbs >= 2) printf("<%c> ", gpx->option.inv?'-':'+');
            printf("[%3d] ", gpx->frnr);
            printf("%4d-%02d-%02d ", gpx->jahr, gpx->monat, gpx->tag);
            printf("%02d:%02d:%04.1f ", gpx->std, gpx->min, gpx->sek);
                                                if (gpx->option.vbs >= 2 && gpx->option.ecc) printf("(%1X,%1X,%1X) ", gpx->pck[0].ec&0xF, gpx->pck[8].ec&0xF, gpx->pck[1].ec&0xF);
            printf(" ");
            printf(" lat: %.5f ", gpx->lat);    if (gpx->option.vbs >= 2 && gpx->option.ecc) printf("(%1X)  ", gpx->pck[2].ec&0xF);
            printf(" lon: %.5f ", gpx->lon);    if (gpx->option.vbs >= 2 && gpx->option.ecc) printf("(%1X)  ", gpx->pck[3].ec&0xF);
            printf(" alt: %.1f ", gpx->alt);    if (gpx->option.vbs >= 2 && gpx->option.ecc) printf("(%1X)  ", gpx->pck[4].ec&0xF);
            printf(" vH: %5.2f ", gpx->horiV);
            printf(" D: %5.1f ", gpx->dir);
            printf(" vV: %5.2f ", gpx->vertV);
            if (gpx->option.ptu  &&  gpx->ptu_out) {
                float t = get_Temp(gpx);
                if (t > -270.0) printf("  T=%.1fC ", t);
                if (gpx->option.dbg) {
                    float t2 = get_Temp2(gpx);
                    float t4 = get_Temp4(gpx);
                    if (t2 > -270.0) printf("  T2=%.1fC ", t2);
                    if (t4 > -270.0) printf(" T4=%.1fC  ", t4);
                    printf(" f0: %.2f ", gpx->meas24[0]);
                    printf(" f1: %.2f ", gpx->meas24[1]);
                    printf(" f2: %.2f ", gpx->meas24[2]);
                    printf(" f3: %.2f ", gpx->meas24[3]);
                    printf(" f4: %.2f ", gpx->meas24[4]);
                    if (gpx->ptu_out >= 0xC) {
                        printf(" f5: %.2f ", gpx->meas24[5]);
                        printf(" f6: %.2f ", gpx->meas24[6]);
                    }

                }
            }
            if (gpx->option.vbs == 3  &&  (gpx->ptu_out == 0xA || gpx->ptu_out >= 0xC)) {
                printf("  U: %.2fV ", gpx->status[0]);
                printf("  Ti: %.1fK ", gpx->status[1]);
            }
            if (gpx->option.vbs)
            {
                if (gpx->sonde_typ & SNbit) {
                    printf(" (%s) ", gpx->sonde_id);
                    gpx->sonde_typ ^= SNbit;
                }
            }
        }
        printf("\n");

        if (gpx->option.jsn && jsonout)
        {
            // JSON Buffer to store sonde ID
            char json_sonde_id[] = "DFMxx-xxxxxxxx\0\0";
            switch (gpx->sonde_typ & 0xF) {
                case   0: sprintf(json_sonde_id, "DFMxx-xxxxxxxx"); break; //json_sonde_id[0] = '\0';
                case   6: sprintf(json_sonde_id, "DFM06-%6X", gpx->SN6); break;
                case 0xA: sprintf(json_sonde_id, "DFM09-%6u", gpx->SN); break;
                // 0x7: PS-15?, 0xC: DFM-17? (0xD: DFM-17?p)
                default : sprintf(json_sonde_id, "DFMx%1X-%6u", gpx->sonde_typ & 0xF,gpx->SN);
            }

            // Print JSON blob     // valid sonde_ID?
            printf("{ \"frame\": %d, \"id\": \"%s\", \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f",
                   gpx->frnr, json_sonde_id, gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek, gpx->lat, gpx->lon, gpx->alt, gpx->horiV, gpx->dir, gpx->vertV);
            if (gpx->ptu_out) { // get temperature
                float t = get_Temp(gpx); // ecc-valid temperature?
                if (t > -270.0) printf(", \"temp\": %.1f", t);
            }
            printf(" }\n");
            printf("\n");
        }

    }

    for (i = 0; i < 9; i++) gpx->pck[i].ec = -1;
}

static int print_frame(gpx_t *gpx) {
    int i;
    int nib = 0;
    int frid = -1;
    int ret0, ret1, ret2;
    int ret = 0;

    ui8_t hamming_conf[ 7*B];  //  7*8=56
    ui8_t hamming_dat1[13*B];  // 13*8=104
    ui8_t hamming_dat2[13*B];

    ui8_t block_conf[ 7*S];  //  7*4=28
    ui8_t block_dat1[13*S];  // 13*4=52
    ui8_t block_dat2[13*S];

    deinterleave(gpx->frame_bits+CONF,  7, hamming_conf);
    deinterleave(gpx->frame_bits+DAT1, 13, hamming_dat1);
    deinterleave(gpx->frame_bits+DAT2, 13, hamming_dat2);

    ret0 = hamming(gpx->option.ecc, hamming_conf,  7, block_conf);
    ret1 = hamming(gpx->option.ecc, hamming_dat1, 13, block_dat1);
    ret2 = hamming(gpx->option.ecc, hamming_dat2, 13, block_dat2);
    ret = ret0 | ret1 | ret2;

    if (gpx->option.raw == 1) {

        for (i = 0; i < 7; i++) {
            nib = bits2val(block_conf+S*i, S);
            printf("%01X", nib & 0xFF);
        }
        if (gpx->option.ecc) {
            if      (ret0 == 0) printf(" [OK] ");
            else if (ret0  > 0) printf(" [KO] ");
            else                printf(" [NO] ");
        }
        printf("  ");
        for (i = 0; i < 13; i++) {
            nib = bits2val(block_dat1+S*i, S);
            printf("%01X", nib & 0xFF);
        }
        if (gpx->option.ecc) {
            if      (ret1 == 0) printf(" [OK] ");
            else if (ret1  > 0) printf(" [KO] ");
            else                printf(" [NO] ");
        }
        printf("  ");
        for (i = 0; i < 13; i++) {
            nib = bits2val(block_dat2+S*i, S);
            printf("%01X", nib & 0xFF);
        }
        if (gpx->option.ecc) {
            if      (ret2 == 0) printf(" [OK] ");
            else if (ret2  > 0) printf(" [KO] ");
            else                printf(" [NO] ");
        }

        if (gpx->option.ecc && gpx->option.vbs) {
            if (gpx->option.vbs > 1) printf(" (%1X,%1X,%1X) ", cnt_biterr(ret0), cnt_biterr(ret1), cnt_biterr(ret2));
            printf(" (%d) ", cnt_biterr(ret0)+cnt_biterr(ret1)+cnt_biterr(ret2));
        }

        printf("\n");

    }
    else if (gpx->option.ecc) {

        if (ret0 == 0 || ret0 > 0) {
            conf_out(gpx, block_conf, ret0);
        }
        if (ret1 == 0 || ret1 > 0) {
            frid = dat_out(gpx, block_dat1, ret1);
            if (frid == 8) print_gpx(gpx);
        }
        if (ret2 == 0 || ret2 > 0) {
            frid = dat_out(gpx, block_dat2, ret2);
            if (frid == 8) print_gpx(gpx);
        }

    }
    else {

        conf_out(gpx, block_conf, ret0);
        frid = dat_out(gpx, block_dat1, ret1);
        if (frid == 8) print_gpx(gpx);
        frid = dat_out(gpx, block_dat2, ret2);
        if (frid == 8) print_gpx(gpx);

    }

    return ret;
}

/* -------------------------------------------------------------------------- */

// header bit buffer
typedef struct {
    char *hdr;
    char *buf;
    char len;
    int bufpos;
    float ths;
} hdb_t;

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

static int find_binhead(FILE *fp, hdb_t *hdb, float *score) {
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
        if ( fabs(mv) > hdb->ths ) {
            *score = mv;
            return 1;
        }
    }

    return EOF;
}


int main(int argc, char **argv) {

    int option_verbose = 0;  // ausfuehrliche Anzeige
    int option_raw = 0;      // rohe Frames
    int option_inv = 0;      // invertiert Signal
    int option_ecc = 0;
    int option_ptu = 0;
    int option_dist = 0;     // continuous pcks 0..8
    int option_auto = 0;
    int option_min = 0;
    int option_iq = 0;
    int option_lp = 0;
    int option_dc = 0;
    int option_bin = 0;
    int option_json = 0;     // JSON blob output (for auto_rx)
    int option_pcmraw = 0;
    int wavloaded = 0;
    int sel_wavch = 0;       // audio channel: left
    int spike = 0;

    FILE *fp = NULL;
    char *fpname = NULL;

    int ret = 0;
    int k;

    int bit;
    int bitpos = 0;
    int bitQ;
    int pos;
    int frm = 0, nfrms = 8; // nfrms=1,2,4,8

    int headerlen = 0;

    int header_found = 0;

    float thres = 0.65;
    float _mv = 0.0;

    int symlen = 2;
    int bitofs = 2; // +1 .. +2
    int shift = 0;

    pcm_t pcm = {0};
    dsp_t dsp = {0};

    gpx_t gpx = {0};

    hdb_t hdb = {0};
    ui32_t hdrcnt = 0;


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
            fprintf(stderr, "       -v, -vv\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       -i, --invert\n");
            fprintf(stderr, "       --ecc        (Hamming ECC)\n");
            fprintf(stderr, "       --ths <x>    (peak threshold; default=%.1f)\n", thres);
            fprintf(stderr, "       --json       (JSON output)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "-vv" ) == 0) ) { option_verbose = 2; }
        else if ( (strcmp(*argv, "-vvv") == 0) ) { option_verbose = 3; }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-R") == 0) || (strcmp(*argv, "--RAW") == 0) ) {
            option_raw = 2;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 0x1;
        }
        else if ( (strcmp(*argv, "--ecc") == 0) ) { option_ecc = 1; }
        else if ( (strcmp(*argv, "--ptu") == 0) ) {
            option_ptu = 1;
            //gpx.ptu_out = 1; // force ptu (non PS-15)
        }
        else if ( (strcmp(*argv, "--spike") == 0) ) {
            spike = 1;
        }
        else if ( (strcmp(*argv, "--auto") == 0) ) { option_auto = 1; }
        else if   (strcmp(*argv, "--bin") == 0) { option_bin = 1; }   // bit/byte binary input
        else if ( (strcmp(*argv, "--dist") == 0) ) { option_dist = 1; option_ecc = 1; }
        else if ( (strcmp(*argv, "--json") == 0) ) { option_json = 1; option_ecc = 1; }
        else if ( (strcmp(*argv, "--ch2") == 0) ) { sel_wavch = 1; }  // right channel (default: 0=left)
        else if ( (strcmp(*argv, "--ths") == 0) ) {
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
        else if   (strcmp(*argv, "--dc") == 0) { option_dc = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if   (strcmp(*argv, "--dbg") == 0) { gpx.option.dbg = 1; }
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


    // init gpx
    strcpy(gpx.frame_bits, dfm_header); //, sizeof(dfm_header);
    for (k = 0; k < 9; k++) gpx.pck[k].ec = -1; // init ecc-status

    gpx.option.inv = option_inv;
    gpx.option.vbs = option_verbose;
    gpx.option.raw = option_raw;
    gpx.option.ptu = option_ptu;
    gpx.option.ecc = option_ecc;
    gpx.option.aut = option_auto;
    gpx.option.dst = option_dist;
    gpx.option.jsn = option_json;


    headerlen = strlen(dfm_rawheader);


    if (!option_bin) {

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

        // dfm: BT=1?, h=2.4?
        symlen = 2;

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
        dsp.hdr = dfm_rawheader;
        dsp.hdrlen = strlen(dfm_rawheader);
        dsp.BT = 0.5; // bw/time (ISI) // 0.3..0.5
        dsp.h = 1.8;  // 2.4 modulation index abzgl. BT
        dsp.opt_iq = option_iq;
        dsp.opt_lp = option_lp;
        dsp.lpIQ_bw = 12e3; // IF lowpass bandwidth
        dsp.lpFM_bw = 4e3; // FM audio lowpass
        dsp.opt_dc = option_dc;
        dsp.opt_IFmin = option_min;

        if ( dsp.sps < 8 ) {
            fprintf(stderr, "note: sample rate low\n");
        }
    }
    else {
        // init circular header bit buffer
        hdb.hdr = dfm_rawheader;
        hdb.len = strlen(dfm_rawheader);
        hdb.ths = 1.0 - 2.1/(float)hdb.len; // 1.0-max_bit_errors/hdrlen // max 1.1 !!
        hdb.bufpos = -1;
        hdb.buf = calloc(hdb.len, sizeof(char));
        if (hdb.buf == NULL) {
            fprintf(stderr, "error: malloc\n");
            return -1;
        }
    }


    k = init_buffers(&dsp);
    if ( k < 0 ) {
        fprintf(stderr, "error: init buffers\n");
        return -1;
    };


    bitofs += shift;


        while ( 1 )
        {
            if (option_bin) { // aka find_binrawhead()
                header_found = find_binhead(fp, &hdb, &_mv); // symbols or bits?
                hdrcnt += nfrms;
            }
            else {                                                              // FM-audio:
                header_found = find_header(&dsp, thres, 2, bitofs, dsp.opt_dc); // optional 2nd pass: dc=0
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
                bitpos = 0;
                pos = headerlen;
                pos /= 2;

                //if (fabs(mv) > 0.85) nfrms = 8; else nfrms = 4; // test OK/KO/NO count

                frm = 0;
                while ( frm < nfrms ) { // nfrms=1,2,4,8
                    if (option_bin) {
                        gpx._frmcnt = hdrcnt + frm;
                    }
                    else {
                        gpx._frmcnt = dsp.mv_pos/(2.0*dsp.sps*BITFRAME_LEN) + frm;
                    }
                    while ( pos < BITFRAME_LEN )
                    {
                        if (option_bin) {
                            // symbols or bits?
                            // manchester1 1->10,0->01: 1.bit (DFM-06)
                            // manchester2 0->10,1->01: 2.bit (DFM-09)
                            bitQ = fgetc(fp);
                            if (bitQ != EOF) {
                                bit = bitQ & 0x1;
                                bitQ = fgetc(fp);  // check: rbit0^rbit1=1 (Manchester)
                                if (bitQ != EOF) bit = bitQ & 0x1; // 2.bit (DFM-09)
                            }
                        }
                        else {
                            if (option_iq >= 2) {
                                float bl = -1;
                                if (option_iq > 2) bl = 4.0;
                                bitQ = read_slbit(&dsp, &bit, 0/*gpx.option.inv*/, bitofs, bitpos, bl, 0);
                            }
                            else {
                                bitQ = read_slbit(&dsp, &bit, 0/*gpx.option.inv*/, bitofs, bitpos, -1, spike);
                            }
                        }
                        if ( bitQ == EOF ) { frm = nfrms; break; } // liest 2x EOF

                        if (gpx.option.inv) bit ^= 1;

                        gpx.frame_bits[pos] = 0x30 + bit;
                        pos++;
                        bitpos += 1;
                    }
                    gpx.frame_bits[pos] = '\0';

                    ret = print_frame(&gpx);
                    if (pos < BITFRAME_LEN) break;
                    pos = 0;
                    frm += 1;
                    //if (ret < 0) frms += 1;
                }
            }

            header_found = 0;
            pos = headerlen;
        }

        if (!option_bin) free_buffers(&dsp);
        else {
            if (hdb.buf) { free(hdb.buf); hdb.buf = NULL; }
        }


    fclose(fp);

    return 0;
}

