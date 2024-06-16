
/*
 *  (unknown (26702) 2021-02-19)
 *  radiosonde "MP3-H1" (WMO translit: "MRZ-N1")
 *  author: zilog80
 *
 *  compile:
 *          gcc -c demod_mod.c
 *          gcc mp3h1mod.c demod_mod.o -lm -o mp3h1mod
 *  usage:
 *          ./mp3h1mod -v fm_audio.wav
 *          (inverse polarity: -i)
 *
 */

#include <stdio.h>
#include <string.h>

#include <math.h>
#include <stdlib.h>

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

#define TIMEOUT_JSN 60


//typedef unsigned char ui8_t;
//typedef unsigned int ui32_t;
//typedef unsigned short ui16_t;
//typedef short i16_t;
//typedef int i32_t;

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
    i8_t col;  // colors
    i8_t jsn;  // JSON output (auto_rx)
    i8_t slt;  // silent
    i8_t dbg;
    i8_t unq;
} option_t;


#define CRCLEN_ECEF   45  // default
#define CRCLEN_LATLON 42

#define BITFRAME_LEN    ((CRCLEN_ECEF+6)*8)  //8=16/2  // ofs=8: 52..53: AA AA (1..5) or 00 00 (6)
#define RAWBITFRAME_LEN (BITFRAME_LEN*2)
#define FRAMESTART      (HEADOFS+HEADLEN)

#define FRAME_LEN       (BITFRAME_LEN/8)


typedef struct {
    ui8_t subcnt1;
    ui8_t subcnt2;
    //int frnr;
    int yr; int mth; int day;
    int hrs; int min; int sec;
    double lat; double lon; double alt;
    double vH; double vD; double vV;
    ui8_t numSats;
    float calA; // A(ntc)
    float calB; // B(ntc)
    float calC; // C(ntc)
    float A_adcT; float B_adcT; float C_adcT;
    float A_adcH; float B_adcH; float C_adcH;
    float Tadc; float RHadc;
    float T; float RH;
    ui8_t frame[FRAME_LEN+16];
    char frame_bits[BITFRAME_LEN+16];
    ui32_t cfg[16];
    ui32_t snC;
    ui32_t snD;
    ui8_t cfg_ntc; ui8_t cfg_T; ui8_t cfg_H;
    ui8_t crcOK;
    //
    int crclen;
    int bitfrm_len;
    //
    int sec_day;
    int sec_day_prev;
    int gps_cnt;
    int gps_cnt_prev;
    int week;
    int jsn_freq;   // freq/kHz (SDR)
    option_t option;
} gpx_t;



#define HEADLEN 44
#define HEADOFS  0
static int bits_ofs = 8;
//Preamble+Header
static char mrz_header[] = "100110011001100110011001100110011001""10101010";

// each frame 6x
// AA BF 35 ........ AA AA
// AA BF 35 ........ AA AA
// AA BF 35 ........ AA AA
// AA BF 35 ........ AA AA
// AA BF 35 ........ AA AA
// AA BF 35 ........ 00 00


/* -------------------------------------------------------------------------- */

#define BAUD_RATE   (2399.0)  // 2400

/* -------------------------------------------------------------------------- */

// manchester1 1->10,0->01: 1.bit
// manchester2 0->10,1->01: 2.bit
static void manchester1(char* frame_rawbits, char *frame_bits, int pos) {
    int i, c, out, buf;
    char bit, bits[2];
    c = 0;

    for (i = 0; i < pos/2; i++) {  // -16
        bits[0] = frame_rawbits[2*i];
        bits[1] = frame_rawbits[2*i+1];

        if ((bits[0] == '0') && (bits[1] == '1')) { bit = '0'; out = 1; }
        else
        if ((bits[0] == '1') && (bits[1] == '0')) { bit = '1'; out = 1; }
        else { //
            if (buf == 0) { c = !c; out = 0; buf = 1; }
            else { bit = 'x'; out = 1; buf = 0; }
        }
        if (out) frame_bits[i] = bit;
    }
}

static int bits2bytes(char *bitstr, ui8_t *bytes, int len) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < len) {

        byteval = 0;
        d = 1;
        for (i = 0; i < 8; i++) {
            //bit = *(bitstr+bitpos+i); /* little endian */
            bit = *(bitstr+bitpos+7-i);  /* big endian */
            if (bit == '\0') goto frame_end;
            if         (bit == '1')     byteval += d;
            else /*if ((bit == '0') */  byteval += 0;
            d <<= 1;
        }
        bitpos += 8;
        bytes[bytepos++] = byteval;

    }
frame_end:
    for (i = bytepos; i < FRAME_LEN; i++) bytes[i] = 0;

    return bytepos;
}

/* ------------------------------------------------------------------------------------ */
static int datetime2GPSweek(int yy, int mm, int dd,
                            int hr, int min, int sec,
                            int *week, int *tow) {
    int ww = 0;
    int tt = 0;
    int gpsDays = 0;

    if ( mm < 3 ) { yy -= 1; mm += 12; }

    gpsDays = (int)(365.25*yy) + (int)(30.6001*(mm+1.0)) + dd - 723263; // 1980-01-06

    ww = gpsDays / 7;
    tt = gpsDays % 7;
    tt = tt*86400 + hr*3600 + min*60 + sec;

    *week = ww;
    *tow  = tt;

    return 0;
}
/* ------------------------------------------------------------------------------------ */

static ui32_t u4(ui8_t *bytes) {  // 32bit unsigned int
    ui32_t val = 0; // le: p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24)
    memcpy(&val, bytes, 4);
    return val;
}

static i32_t i4(ui8_t *bytes) {  // 32bit signed int
    i32_t val = 0; // le: p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24)
    memcpy(&val, bytes, 4);
    return val;
}

static ui32_t u3(ui8_t *bytes) {  // 24bit unsigned int
    int val24 = 0;
    val24 = bytes[0] | (bytes[1]<<8) | (bytes[2]<<16);
    // = memcpy(&val, bytes, 3), val &= 0x00FFFFFF;
    return val24;
}

static int i3(ui8_t *bytes) {  // 24bit signed int
    int val = 0,
        val24 = 0;
    val = bytes[0] | (bytes[1]<<8) | (bytes[2]<<16);
    val24 = val & 0xFFFFFF; if (val24 & 0x800000) val24 = val24 - 0x1000000;
    return val24;
}

static ui16_t u2(ui8_t *bytes) {  // 16bit unsigned int
    return  bytes[0] | (bytes[1]<<8);
}

static i16_t i2(ui8_t *bytes) { // 16bit signed int
    //return (i16_t)u2(bytes);
    int val = bytes[0] | (bytes[1]<<8);
    if (val & 0x8000) val -= 0x10000;
    return val;
}

// -----------------------------------------------------------------------------

// AA BF 35 .... crc AA AA
// "BF" header/subtype?
// "35" frame length?

#define OFS 0
#define pos_CNT1        (OFS+ 3)  //   1 nibble (0x80..0x8F ?)
#define pos_TIME        (OFS+ 4)  // 3*1 byte
#define pos_GPSecefX    (OFS+ 8)  //   4 byte
#define pos_GPSecefY    (OFS+12)  //   4 byte
#define pos_GPSecefZ    (OFS+16)  //   4 byte
#define pos_GPSecefV    (OFS+20)  // 3*2 byte
#define pos_GPSnSats    (OFS+26)  //   1 byte (num Sats ?)
#define pos_T16         (OFS+29)  //   2 byte
#define pos_H16         (OFS+31)  //   2 byte
#define pos_FFFF        (OFS+33)  //   2 byte
#define pos_ADCT        (OFS+35)  //   4 byte
#define pos_ADCH        (OFS+39)  //   4 byte
#define pos_CNT2        (OFS+43)  //   1 byte   (0x01..0x10 ?)
#define pos_CFG         (OFS+44)  // 2/4 byte
#define pos_CRC_ECEF    (OFS+CRCLEN_ECEF+1)   //   2 byte
#define pos_CRC_LATLON  (OFS+CRCLEN_LATLON+1) //   2 byte

#define pos_GPSlat      (OFS+ 7)  //   4 byte
#define pos_GPSlon      (OFS+11)  //   4 byte
#define pos_GPSalt      (OFS+15)  //   4 byte
#define pos_GPSvH       (OFS+19)  //   2 byte
#define pos_GPSvD       (OFS+21)  //   2 byte


// -----------------------------------------------------------------------------

static int crc16rev(gpx_t *gpx, int start, int len) {
    int crc16poly = 0xA001; // rev 0x8005
    int rem = 0xFFFF, i, j;
    int byte;

    if (start+len+2 > FRAME_LEN) return -1;

    for (i = 0; i < len; i++) {
        byte = gpx->frame[start+i];
        rem ^= byte;
        for (j = 0; j < 8; j++) {
            if (rem & 0x0001) {
                rem = (rem >> 1) ^ crc16poly;
            }
            else {
                rem = (rem >> 1);
            }
            rem &= 0xFFFF;
        }
    }
    return rem;
}
static int check_CRC(gpx_t *gpx, ui32_t crclen) {
    //ui32_t crclen = 45; // 45/42
    ui32_t crcdat = 0;
    crcdat = u2(gpx->frame+crclen+3);
    if ( crcdat != crc16rev(gpx, pos_CNT1, crclen) ) {
        return 1;  // CRC NO
    }
    else return 0; // CRC OK
}

// -----------------------------------------------------------------------------
// WGS84/GRS80 Ellipsoid
#define EARTH_a  6378137.0
#define EARTH_b  6356752.31424518
#define EARTH_a2_b2  (EARTH_a*EARTH_a - EARTH_b*EARTH_b)

const
double a = EARTH_a,
       b = EARTH_b,
       a_b = EARTH_a2_b2,
       e2  = EARTH_a2_b2 / (EARTH_a*EARTH_a),
       ee2 = EARTH_a2_b2 / (EARTH_b*EARTH_b);

static void ecef2elli(double X[], double *lat, double *lon, double *alt) {
    double phi, lam, R, p, t;

    lam = atan2( X[1] , X[0] );

    p = sqrt( X[0]*X[0] + X[1]*X[1] );
    t = atan2( X[2]*a , p*b );

    phi = atan2( X[2] + ee2 * b * sin(t)*sin(t)*sin(t) ,
                 p - e2 * a * cos(t)*cos(t)*cos(t) );

    R = a / sqrt( 1 - e2*sin(phi)*sin(phi) );
    *alt = p / cos(phi) - R;

    *lat = phi*180/M_PI;
    *lon = lam*180/M_PI;
}

static int get_GPSkoord_ecef(gpx_t *gpx) {
    int k;
    int XYZ; // 32bit
    double X[3], lat, lon, alt;
    ui8_t *gpsVel;
    short vel16; // 16bit
    double V[3];
    double phi, lam, dir;
    double vN; double vE; double vU;


    for (k = 0; k < 3; k++)
    {
        memcpy(&XYZ, gpx->frame+(pos_GPSecefX+4*k), 4);
        X[k] = XYZ / 100.0;

        gpsVel = gpx->frame+(pos_GPSecefV+2*k);
        vel16 = gpsVel[0] | gpsVel[1] << 8;
        V[k] = vel16 / 100.0;
    }


    // ECEF-Position
    ecef2elli(X, &lat, &lon, &alt);
    gpx->lat = lat;
    gpx->lon = lon;
    gpx->alt = alt;
    if (alt < -1000.0 || alt > 80000.0) return -3; // plausibility-check: altitude, if ecef=(0,0,0)


    // ECEF-Velocities
    // ECEF-Vel -> NorthEastUp
    phi = lat*M_PI/180.0;
    lam = lon*M_PI/180.0;
    vN = -V[0]*sin(phi)*cos(lam) - V[1]*sin(phi)*sin(lam) + V[2]*cos(phi);
    vE = -V[0]*sin(lam) + V[1]*cos(lam);
    vU =  V[0]*cos(phi)*cos(lam) + V[1]*cos(phi)*sin(lam) + V[2]*sin(phi);

    // NEU -> HorDirVer
    gpx->vH = sqrt(vN*vN+vE*vE);

    dir = atan2(vE, vN) * 180.0 / M_PI;
    if (dir < 0) dir += 360.0;
    gpx->vD = dir;

    gpx->vV = vU;

    // num Sats solution ? GLONASS + GPS ?
    gpx->numSats = gpx->frame[pos_GPSnSats];

    return 0;
}

static int get_GPSkoord_latlon(gpx_t *gpx) {
    int XYZ; // 32bit
    short vH, vV; // 16bit
    unsigned short vD;


    memcpy(&XYZ, gpx->frame+pos_GPSlat, 4);
    gpx->lat = XYZ * 1e-6;

    memcpy(&XYZ, gpx->frame+pos_GPSlon, 4);
    gpx->lon = XYZ * 1e-6;

    memcpy(&XYZ, gpx->frame+pos_GPSalt, 4);
    gpx->alt = XYZ * 1e-2;

    if (gpx->alt < -1000.0 || gpx->alt > 80000.0) return -3; // plausibility-check: altitude

    vH = gpx->frame[pos_GPSvH] | (gpx->frame[pos_GPSvH+1] << 8);
    vD = gpx->frame[pos_GPSvD] | (gpx->frame[pos_GPSvD+1] << 8);

    gpx->vH = vH / 100.0;
    gpx->vD = vD / 100.0;
    gpx->vV = 0;

    //TODO: Sats
    // num Sats solution ? GLONASS + GPS ?
    gpx->numSats = gpx->frame[pos_GPSnSats-3]; // ?


    return 0;
}

static int reset_time(gpx_t *gpx) {

    gpx->gps_cnt = 0;
    gpx->yr = 0;
    gpx->week = 0;

    return 0;
}

static int get_time(gpx_t *gpx) {

    gpx->hrs = gpx->frame[pos_TIME];
    gpx->min = gpx->frame[pos_TIME+1];
    gpx->sec = gpx->frame[pos_TIME+2];


    if (gpx->crcOK)
    {
        int week = 0;
        int tow = 0;
        int sec_gps = 0;

        gpx->gps_cnt_prev = gpx->gps_cnt;
        gpx->sec_day_prev = gpx->sec_day;

        gpx->sec_day = gpx->hrs*60*60 + gpx->min*60 + gpx->sec;


        // JSON frame counter: seconds since GPS (ignoring leap seconds)
        //
        if (gpx->yr == 0) { // 1980-01-06
            week = 0;
            tow  = gpx->sec_day; // yr=mth=day=0
        }
        else {
            datetime2GPSweek(gpx->yr, gpx->mth, gpx->day, gpx->hrs, gpx->min, (int)(gpx->sec+0.5), &week, &tow);
        }
        sec_gps = week*604800 + tow; // SECONDS_IN_WEEK=7*86400=604800
        gpx->week = week;

        if (sec_gps > gpx->gps_cnt_prev) { // skip day roll-over until date update
            gpx->gps_cnt = sec_gps;
        }
    }

    return 0;
}

static float f32(ui32_t w) {
    float f = 0.0f;
    memcpy(&f, &w, 4);
    return f;
}

static int get_ptu(gpx_t *gpx, int ofs) {
    // cf. МРЗ-3МК documentation

    float t = -273.15f;
    float rh = -1.0f;

    float ADC_MAX = 32767.0; //32767=(1<<15)? 32767?

    int ADCT = u4(gpx->frame+pos_ADCT+ofs); // u3?
    float adc_t = ADCT/100.0;

    int ADCH = u4(gpx->frame+pos_ADCH+ofs); // u3?
    float adc_h = ADCH/100.0;


    if (gpx->cfg_ntc == 0x7)
    {
        if (gpx->cfg_T == 0x7) {
            float poly1 = adc_t*adc_t * gpx->A_adcT + adc_t * gpx->B_adcT + gpx->C_adcT;
            float Rt = 100000.0*poly1 / (ADC_MAX - poly1);
            if (Rt > 0.0) {
                t = gpx->calB/log(Rt/gpx->calA) - gpx->calC - 273.15f;
                if (t < -120.0f || t > 120.0f) t = -273.15f;
            }
        }
    }
    gpx->Tadc = t;

    if (gpx->Tadc > -273.0f)
    {
        if (gpx->cfg_H == 0x7) {
            float poly2 = adc_h*adc_h * gpx->A_adcH + adc_h * gpx->B_adcH + gpx->A_adcH;
            float K = poly2/ADC_MAX;

            rh = (K - 0.1515) / (0.00636*(1.05460 - 0.00216*gpx->Tadc)); // if T = 273.15, set T=0 ?
            if (rh < -10.0f || rh > 120.0f) rh = -1.0f;
            else {
                if (rh < 0.0f) rh = 0.0f;
                if (rh > 100.0f) rh = 100.0f;
            }
        }
    }
    gpx->RHadc = rh;


    gpx->T  = i2(gpx->frame+pos_T16+ofs) / 100.0;
    gpx->RH = i2(gpx->frame+pos_H16+ofs) / 100.0;

    return 0;
}

static int get_cfg(gpx_t *gpx, int ofs) {

    gpx->subcnt1 = (gpx->frame[pos_CNT1] & 0xF);
    gpx->subcnt2 =  gpx->frame[pos_CNT2+ofs] ; // ? subcnt2 == subcnt1 + 1 ?

    if (gpx->crcOK)
    {
        ui32_t cfg32 = u4(gpx->frame+pos_CFG+ofs);
        gpx->cfg[gpx->subcnt1] = cfg32;

        switch (gpx->subcnt1) { // or use subcnt2 ?
            // T-ntc A, B, C
            case 0x0: //sub2=0x01:
                        // TODO: reset if changed?
                        gpx->calA = f32(cfg32); //memcpy(&gpx->calA, &cfg32, 4);
                        gpx->cfg_ntc |= 0x1;
                    break;
            case 0x1: //sub2=0x02:
                        gpx->calB = f32(cfg32); //memcpy(&gpx->calB, &cfg32, 4);
                        gpx->cfg_ntc |= 0x2;
                    break;
            case 0x2: //sub2=0x03:
                        gpx->calC = f32(cfg32); //memcpy(&gpx->calC, &cfg32, 4);
                        gpx->cfg_ntc |= 0x4;
                    break;
            // ADC1/ADC_T calib ?
            case 0x3: //sub2=0x04:
                        gpx->A_adcT = f32(cfg32);
                        gpx->cfg_T |= 0x1;
                    break;
            case 0x4: //sub2=0x05:
                        gpx->B_adcT = f32(cfg32);
                        gpx->cfg_T |= 0x2;
                    break;
            case 0x5: //sub2=0x06:
                        gpx->C_adcT = f32(cfg32);
                        gpx->cfg_T |= 0x4;
                    break;
            // ADC2/ADC_H calib ?
            case 0x6: //sub2=0x07:
                        gpx->A_adcH = f32(cfg32);
                        gpx->cfg_H |= 0x1;
                    break;
            case 0x7: //sub2=0x08:
                        gpx->B_adcH = f32(cfg32);
                        gpx->cfg_H |= 0x2;
                    break;
            case 0x8: //sub2=0x09:
                        gpx->C_adcH = f32(cfg32);
                        gpx->cfg_H |= 0x4;
                    break;
            // radiosonde/GNSS SN
            case 0xC: //sub2=0x0D: SN GLONASS/GPS ?
                        if (cfg32 != gpx->snC && gpx->snC > 0) {
                            //reset_cfg
                            gpx->snD = 0;
                            reset_time(gpx);
                        }
                        gpx->snC = cfg32; // 16 or 32 bit ?
                    break;
            // sensor SN
            case 0xD: //sub2=0x0E: SN sensor boom ?
                        if (cfg32 != gpx->snD && gpx->snD > 0) {
                            //reset_cfg
                            gpx->snC = 0;
                            reset_time(gpx);
                        }
                        gpx->snD = cfg32; // 16 or 32 bit ?
                    break;
            // sensor date
            case 0xE: //sub2=0x0F: calib date ?
                    break;
            // date
            case 0xF: //sub2=0x10: date
                        gpx->yr = cfg32 % 100;
                        gpx->yr += 2000;
                        cfg32 /= 100;
                        gpx->mth = cfg32 % 100;
                        cfg32 /= 100;
                        gpx->day = cfg32 % 100;
                    break;
            default:
                    break;
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------

#define ANSI_COLOR_RESET    "\x1b[0m"
#define col_CSok            "\x1b[38;5;2m"
#define col_CSno            "\x1b[38;5;1m"
#define col_back            "\x1b[38;5;244m"


static void print_gpx(gpx_t *gpx, int crcOK) {

    //printf(" :%6.1f: ", sample_count/(double)sample_rate);
    //

    int ofs_ptucfg = (gpx->crclen == CRCLEN_ECEF) ? 0 : -3;

    gpx->crcOK = crcOK;

    get_cfg(gpx, ofs_ptucfg);
    get_time(gpx);
    if (ofs_ptucfg) get_GPSkoord_latlon(gpx);
    else            get_GPSkoord_ecef(gpx);

    get_ptu(gpx, ofs_ptucfg);

    if (gpx->sec_day != gpx->sec_day_prev || !gpx->option.unq)
    {
        printf(" [%2d] ", gpx->subcnt1);

        printf(" (%02d:%02d:%02d) ", gpx->hrs, gpx->min, gpx->sec);

        printf(" lat: %.5f ", gpx->lat);
        printf(" lon: %.5f ", gpx->lon);
        printf(" alt: %.2f ", gpx->alt);

        printf("  vH: %4.1f  D: %5.1f ", gpx->vH, gpx->vD);
        if ( !ofs_ptucfg ) {
            printf(" vV: %3.1f ", gpx->vV);
        }

        if (gpx->option.vbs > 1) printf("  sats: %d ", gpx->numSats);

        if (gpx->option.vbs > 1 && ofs_ptucfg < 0)
        {
            static float alt0;
            static int t0;
            if (gpx->crcOK && gpx->sec_day > t0) {
                if (t0 > 0 && gpx->sec_day < t0+10) {
                    printf(" (d_alt: %+4.1f) ", (gpx->alt - alt0)/(float)(gpx->sec_day - t0) );
                }
                alt0 = gpx->alt;
                t0 = gpx->sec_day;
            }
        }

        if (gpx->option.ptu) {
            if (gpx->T > -273.0f || gpx->RH > -0.5f) printf(" ");
            if (gpx->T > -273.0f) printf(" T=%.2fC", gpx->T);
            if (gpx->RH > -0.5f)  printf(" RH=%.2f%%", gpx->RH);
            if (gpx->T > -273.0f || gpx->RH > -0.5f) printf(" ");
            if (gpx->option.vbs > 1) {
                if (gpx->Tadc > -273.0f || gpx->RHadc > -0.5f) printf("  (");
                if (gpx->Tadc > -273.0f) printf(" T0=%.1fC", gpx->Tadc);
                if (gpx->RHadc > -0.5f)  printf(" RH0=%.0f%%", gpx->RHadc);
                if (gpx->Tadc > -273.0f || gpx->RHadc > -0.5f) printf(" ) ");
            }
        }

        if (gpx->option.col) {
                if (gpx->crcOK) printf("  "col_CSok"[OK]"ANSI_COLOR_RESET);
                else            printf("  "col_CSno"[NO]"ANSI_COLOR_RESET);
        }
        else {
            printf("  %s", gpx->crcOK ? "[OK]" : "[NO]");
        }


        if (gpx->crcOK)
        {
            if (gpx->option.vbs)
            {
                //printf("  <%2d>", gpx->subcnt2);
                // subcnt2 == subcnt1 + 1 ?
                switch (gpx->subcnt1) {
                    case 0x0: if (gpx->option.vbs > 1) printf("  <%d> A: %.5f", gpx->subcnt2, gpx->calA); break;
                    case 0x1: if (gpx->option.vbs > 1) printf("  <%d> B: %.2f", gpx->subcnt2, gpx->calB); break;
                    case 0x2: if (gpx->option.vbs > 1) printf("  <%d> C: %.3f", gpx->subcnt2, gpx->calC); break;
                    case 0xC: printf("  <%d> snC: %d", gpx->subcnt2, gpx->snC); break;
                    case 0xD: printf("  <%d> snD: %d", gpx->subcnt2, gpx->snD); break;
                    case 0xE: printf("  <%d> calDate: %06d", gpx->subcnt2, gpx->cfg[gpx->subcnt1]); break;
                    case 0xF: printf("  <%d> %04d-%02d-%02d", gpx->subcnt2, gpx->yr, gpx->mth, gpx->day); break;
                    default:  if (gpx->option.vbs > 1) printf("  <%d>", gpx->subcnt2); break;
                }
            }

            if (gpx->option.dbg)
            {
                printf("    : ");
                //printf(" [0x%X:0x%02X]", gpx->subcnt1, gpx->subcnt2);
                printf("  0x%08X =", gpx->cfg[gpx->subcnt1]);
                if (gpx->subcnt1 > 0x8) printf(" %u ", gpx->cfg[gpx->subcnt1]); // 0x9,0xA not const
                else {
                    float *f = (float*)(gpx->cfg+gpx->subcnt1);
                    printf(" %g ", *f);
                }
            }
        }

        printf("\n");
    }


    if (gpx->option.jsn && gpx->crcOK) {
        // sonde SN change remains undetected until next SN update
        if (gpx->week > 0 && gpx->gps_cnt > gpx->gps_cnt_prev && gpx->snC > 0 && gpx->snD > 0)
        {
            if (gpx->gps_cnt - gpx->gps_cnt_prev > TIMEOUT_JSN && gpx->gps_cnt_prev > gpx->sec_day_prev) {
                // reset SN after TIMEOUT_JSN sec gap;
                // if new signal replaces old one within timeout limit,
                // new positions might still be transmitted with old SN
                //reset_cfg
                gpx->snC = 0;
                gpx->snD = 0;
                reset_time(gpx);
            }
            else {
                char *ver_jsn = NULL;
                printf("{ \"type\": \"%s\"", "MRZ");
                printf(", \"frame\": %lu, ", (unsigned long)gpx->gps_cnt); // sec_gps0+0.5
                printf("\"id\": \"MRZ-%d-%d\", \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%02dZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f",
                        gpx->snC, gpx->snD, gpx->yr, gpx->mth, gpx->day, gpx->hrs, gpx->min, gpx->sec, gpx->lat, gpx->lon, gpx->alt);
                printf(", \"vel_h\": %.5f, \"heading\": %.5f", gpx->vH, gpx->vD);
                if ( !ofs_ptucfg ) {
                    printf(", \"vel_v\": %.5f", gpx->vV);
                }
                printf(", \"sats\": %d", gpx->numSats);

                if (gpx->option.ptu) {
                    if (gpx->T > -273.0f) {
                        fprintf(stdout, ", \"temp\": %.1f",  gpx->T );
                    }
                    if (gpx->RH > -0.5f) {
                        fprintf(stdout, ", \"humidity\": %.1f",  gpx->RH );
                    }
                }
                if (gpx->jsn_freq > 0) {
                    printf(", \"freq\": %d", gpx->jsn_freq);
                }

                // Reference time/position
                printf(", \"ref_datetime\": \"%s\"", "UTC" ); // {"GPS", "UTC"} GPS-UTC=leap_sec
                printf(", \"ref_position\": \"%s\"", !ofs_ptucfg ? "GPS" : "MSL" ); // {"GPS", "MSL"} GPS=ellipsoid , MSL=geoid

                #ifdef VER_JSN_STR
                    ver_jsn = VER_JSN_STR;
                #endif
                if (ver_jsn && *ver_jsn != '\0') printf(", \"version\": \"%s\"", ver_jsn);
                printf(" }\n");
            }
        }
    }

}

static void print_frame(gpx_t *gpx, int pos, int b2B) {
    int j;
    int crcOK = 0;

    static int frame_count = 0;


    if (b2B)
    {
        if (gpx->option.raw == 2) {
            //printf(" :%6.1f: ", sample_count/(double)sample_rate);
            //
            for (j = 0; j < pos; j++) {
                printf("%c", gpx->frame_bits[j]);
            }
            //if (frame_count % 3 == 2)
            {
                printf("\n");
            }
        }
        else {
            int frmlen = (pos-bits_ofs)/8;
            bits2bytes(gpx->frame_bits+bits_ofs, gpx->frame, frmlen);

            if (u2(gpx->frame+30) == 0xFFFF) gpx->crclen = CRCLEN_LATLON;
            else gpx->crclen = CRCLEN_ECEF;

            crcOK = (check_CRC(gpx, gpx->crclen) == 0);

            if (crcOK) {
                gpx->bitfrm_len = (gpx->crclen+6)*8;
            }

            if (gpx->option.raw == 1) {
                //printf(" :%6.1f: ", sample_count/(double)sample_rate);
                //
                for (j = 0; j < frmlen; j++) {
                    printf("%02X ", gpx->frame[j]);
                }
                printf(" %s", crcOK ? "[OK]" : "[NO]");
                printf("\n");
            }
            else {

                //if (frame_count % 3 == 0)
                {
                    if (pos/8 > pos_GPSecefV+6) print_gpx(gpx, crcOK);
                }
            }
        }
    }
    else
    {
        int frmlen = pos;

        if (u2(gpx->frame+30) == 0xFFFF) gpx->crclen = CRCLEN_LATLON;
        else gpx->crclen = CRCLEN_ECEF;

        crcOK = (check_CRC(gpx, gpx->crclen) == 0);

        if (crcOK) {
            gpx->bitfrm_len = (gpx->crclen+6)*8;
        }

        if (gpx->option.raw) {
            //printf(" :%6.1f: ", sample_count/(double)sample_rate);
            //
            for (j = 0; j < frmlen; j++) {
                printf("%02X ", gpx->frame[j]);
            }
            printf(" %s", crcOK ? "[OK]" : "[NO]");
            printf("\n");
        }
        else {
            if (pos > pos_GPSecefV+6) print_gpx(gpx, crcOK);
        }
    }

    frame_count++;
}

/* -------------------------------------------------------------------------- */


int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;
    int pos, bit;
    int cfreq = -1;

    float baudrate = -1;

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
    int rawhex = 0;
    int k;

    int bitQ;
    hsbit_t hsbit, hsbit1;

    int header_found = 0;

    float thres = 0.76; // dsp.mv threshold
    float _mv = 0.0;

    float lpIQ_bw = 9.0e3;

    int bitpos = 0;
    int symlen = 2;
    int bitofs = 2; // +0 .. +3
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
            fprintf(stderr, "       -v, --verbose\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       -i, --invert\n");
            return 0;
        }
        else if ( (strcmp(*argv, "--ofs") == 0) ) {
            ++argv;
            if (*argv) {
                bits_ofs = atoi(*argv);
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "--dbg" ) == 0) ) { gpx.option.dbg = 1; }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            gpx.option.vbs = 1;
        }
        else if ( (strcmp(*argv, "-vv" ) == 0) ) gpx.option.vbs = 2;
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            gpx.option.raw = 1;
        }
        else if ( (strcmp(*argv, "-R") == 0) || (strcmp(*argv, "--RAW") == 0) ) {
            gpx.option.raw = 2;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            gpx.option.inv = 1;
        }
        else if   (strcmp(*argv, "--auto") == 0) { gpx.option.aut = 1; }
        else if ( (strcmp(*argv, "--uniq") == 0) ) {
            gpx.option.unq = 1;
        }
        else if ( (strcmp(*argv, "-c") == 0) || (strcmp(*argv, "--color") == 0) ) {
            gpx.option.col = 1;
        }
        else if ( (strcmp(*argv, "--br") == 0) ) {
            ++argv;
            if (*argv) {
                baudrate = atof(*argv);
                if (baudrate < 2000 || baudrate > 3000) baudrate = BAUD_RATE; // 2399..2400
            }
            else return -1;
        }
        else if (strcmp(*argv, "--ecc" ) == 0) { gpx.option.ecc = 1; }
        else if (strcmp(*argv, "--ptu") == 0) {
            gpx.option.ptu = 1;
        }
        else if (strcmp(*argv, "--ch2") == 0) { sel_wavch = 1; }  // right channel (default: 0=left)
        else if (strcmp(*argv, "--softin") == 0) { option_softin = 1; }  // float32 soft input
        else if (strcmp(*argv, "-d") == 0) {
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
        else if (strcmp(*argv, "--json") == 0) {
            gpx.option.jsn = 1;
        }
        else if ( (strcmp(*argv, "--jsn_cfq") == 0) ) {
            int frq = -1;  // center frequency / Hz
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
        }
        else if   (strcmp(*argv, "--rawhex") == 0) { rawhex = 3; }  // raw hex input
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
                fprintf(stderr, "error open %s\n", *argv);
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


    gpx.jsn_freq = 0;
    if (cfreq > 0) gpx.jsn_freq = (cfreq+500)/1000;


    // init frame/type
    if ((CRCLEN_ECEF+6)*8 > BITFRAME_LEN) {
        if (fp) fclose(fp);
        fprintf(stderr, "error: int frame\n");
        return -1;
    }
    gpx.crclen = CRCLEN_ECEF;
    gpx.bitfrm_len = (gpx.crclen+6)*8;


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

            // mrz-n1: BT=1.0, h=2.0 ?
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
            dsp.hdr = mrz_header;
            dsp.hdrlen = strlen(mrz_header);
            dsp.BT = 1.0; // bw/time (ISI) // 1.0..2.0  // TODO
            dsp.h = 2.0; //  // 1.5..2.5? modulation index abzgl. BT  // TODO
            dsp.opt_iq = option_iq;
            dsp.opt_iqdc = option_iqdc;
            dsp.opt_lp = option_lp;
            dsp.lpIQ_bw = lpIQ_bw;  // 9.0e3 (8e3..10e3) // IF lowpass bandwidth
            dsp.lpFM_bw = 6e3; // FM audio lowpass
            dsp.opt_dc = option_dc;
            dsp.opt_IFmin = option_min;

            if ( dsp.sps < 5 ) {
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

            //if (option_iq >= 2) bitofs += 1; // FM: +1 , IQ: +2
            bitofs += shift;
        }
        else {
            // init circular header bit buffer
            hdb.hdr = mrz_header;
            hdb.len = strlen(mrz_header);
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
            hdb.ths = 0.82; // caution 0.8: false positive / offset
            hdb.sbuf = calloc(hdb.len, sizeof(float));
            if (hdb.sbuf == NULL) {
                fprintf(stderr, "error: malloc\n");
                return -1;
            }
        }


        manchester1(mrz_header, gpx.frame_bits, HEADLEN); // HEADLEN==FRAMESTART

        while ( 1 )
        {
            if (option_softin) {
                header_found = find_softbinhead(fp, &hdb, &_mv);
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
                pos = FRAMESTART/2;

                while ( pos < gpx.bitfrm_len )
                {
                    if (option_softin) {
                        float s1 = 0.0;
                        float s2 = 0.0;
                        float s = 0.0;
                        bitQ = f32soft_read(fp, &s1);
                        if (bitQ != EOF) {
                            bitQ = f32soft_read(fp, &s2);
                            if (bitQ != EOF) {
                                s = s2-s1; // integrate both symbols  // Manchester2=s2 (invert to Manchester1=s1 below)
                                bit = (s>=0.0); // no soft decoding
                                hsbit.hb = bit;
                                hsbit.sb = s;
                            }
                        }
                    }
                    else {
                        float bl = -1;
                        if (option_iq > 2) bl = 2.0;
                        //bitQ = read_slbit(&dsp, &bit, 0, bitofs, bitpos, bl, 0); // symlen=2
                        bitQ = read_softbit2p(&dsp, &hsbit, 0, bitofs, bitpos, bl, 0, &hsbit1); // symlen=2
                        bit = hsbit.hb;
                    }
                    if ( bitQ == EOF ) break; // liest 2x EOF

                    if (!gpx.option.inv) { // Manchester1
                        hsbit.hb ^= 1;
                        hsbit.sb = -hsbit.sb;
                        bit ^= 1;
                    }

                    gpx.frame_bits[pos] = 0x30 + (hsbit.hb & 1);

                    bitpos += 1;
                    pos++;
                }
                gpx.frame_bits[pos] = '\0';

                print_frame(&gpx, pos, 1);
                if (pos < gpx.bitfrm_len) break;
                header_found = 0;

            }
        }

        if (!option_softin) free_buffers(&dsp);
        else {
            if (hdb.buf) { free(hdb.buf); hdb.buf = NULL; }
        }
    }
    else //if (rawhex)
    {
        char buffer_rawhex[3*FRAME_LEN+12];
        char *pbuf = NULL, *buf_sp = NULL;
        ui8_t frmbyte;
        int frameofs = 0, len, i;

        while (1 > 0) {

            pbuf = fgets(buffer_rawhex, 3*FRAME_LEN+12, fp);
            if (pbuf == NULL) break;
            buffer_rawhex[3*FRAME_LEN] = '\0';
            buf_sp = strchr(buffer_rawhex, '['); // # (%d) ecc-info?
            if (buf_sp != NULL && buf_sp-buffer_rawhex < 3*FRAME_LEN) {
                buffer_rawhex[buf_sp-buffer_rawhex] = '\0';
            }
            len = strlen(buffer_rawhex) / 3;
            if (len > 20) {
                for (i = 0; i < len; i++) { //%2x  SCNx8=%hhx(inttypes.h)
                    sscanf(buffer_rawhex+3*i, "%2hhx", &frmbyte);
                    // wenn ohne %hhx: sscanf(buffer_rawhex+rawhex*i, "%2x", &byte); frame[frameofs+i] = (ui8_t)byte;
                    gpx.frame[frameofs+i] = frmbyte;
                }
                print_frame(&gpx, len, 0);
            }
        }
    }


    fclose(fp);

    return 0;
}

