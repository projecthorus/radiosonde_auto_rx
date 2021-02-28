
/*
 *  mXX m18/m20 (test)
 *
 *  (cf. mXX_20180919.c)
 *  sync header: correlation/matched filter
 *  files: mXXmod.c demod_mod.h demod_mod.c
 *  compile:
 *      gcc -c demod_mod.c
 *      gcc mXXmod.c demod_mod.o -lm -o mXXmod
 *
 * 2018-09-19 Ury:  (len=0x43) ./mXX -c -vv --br 9600 mXX_20180919.wav
 * 2019-11-06 Ury:  (len=0x45) ./mXX -c -vv --br 9600 mXX_20191106.wav
 * 2020-02-14 Ury:  (len=0x45) ./mXX -c -vv --br 9600 mXX_20200214.wav
 * 2020-05-11 Wien: (len=0x45) ./mXX -c -vv --br 9603 mXX_20200511.wav
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
    i8_t col;  // colors
    i8_t jsn;  // JSON output (auto_rx)
} option_t;


// ? 9600 baud M20 <-> 9616 baud M10 ?
#define BAUD_RATE   9600  // 9600..9604  // 9614..9616

/* -------------------------------------------------------------------------- */

/*
Header = Sync-Header + Sonde-Header:
1100110011001100 1010011001001100  1101010011010011 0100110101010101 0011010011001100
uudduudduudduudd ududduuddudduudd  uudududduududduu dudduudududududu dduududduudduudd (oder:)
dduudduudduudduu duduudduuduudduu  ddududuudduduudd uduuddududududud uudduduudduudduu (komplement)
 0 0 0 0 0 0 0 0  1 1 - - - 0 0 0   0 1 1 0 0 1 0 0  1 0 0 1 1 1 1 1  0 0 1 0 0 0 0 0
*/

#define BITS 8
#define HEADLEN 32  // HEADLEN+HEADOFS=32 <= strlen(header)
#define HEADOFS  0
                 // Sync-Header (raw)               // Sonde-Header (bits)
//char head[] = "11001100110011001010011001001100"; //"0110010010011111"; // M10: 64 9F , M2K2: 64 8F
                                                    //"0111011010011111"; // M10: 76 9F , w/ aux-data
                                                    //"0110010001001001"; // M10-dop: 64 49 09
                                                    //"0110010010101111"; // M10+: 64 AF w/ gtop-GPS
static char rawheader[] = "10011001100110010100110010011001";

#define FRAME_LEN       (100+1)   // 0x64+1
#define BITFRAME_LEN    (FRAME_LEN*BITS)

#define AUX_LEN          20
#define BITAUX_LEN      (AUX_LEN*BITS)


#define t_M2K2     0x8F
#define t_M10      0x9F
#define t_M10plus  0xAF
#define t_M20      0x20

typedef struct {
    int week; int tow_ms; int gpssec;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double alt;
    double vH; double vD; double vV;
    double vx; double vy; double vD2;
    ui8_t numSV;
    ui8_t utc_ofs;
    char SN[12];
    ui8_t SNraw[3];
    ui8_t frame_bytes[FRAME_LEN+AUX_LEN+4];
    char frame_bits[BITFRAME_LEN+BITAUX_LEN+8];
    int auxlen; // ? 0 .. 0x57-0x45
    int jsn_freq;   // freq/kHz (SDR)
    option_t option;
    ui8_t type;
} gpx_t;


/* -------------------------------------------------------------------------- */
#define SECONDS_IN_WEEK  (604800.0)  // 7*86400
/*
 * Convert GPS Week and Seconds to Modified Julian Day.
 * - Adapted from sci.astro FAQ.
 * - Ignores UTC leap seconds.
 */
static void Gps2Date(long GpsWeek, long GpsSeconds, int *Year, int *Month, int *Day) {

    long GpsDays, Mjd;
    long J, C, Y, M;

    GpsDays = GpsWeek * 7 + (GpsSeconds / 86400);
    Mjd = 44244 + GpsDays;

    J = Mjd + 2468570;
    C = 4 * J / 146097;
    J = J - (146097 * C + 3) / 4;
    Y = 4000 * (J + 1) / 1461001;
    J = J - 1461 * Y / 4 + 31;
    M = 80 * J / 2447;
    *Day = J - 2447 * M / 80;
    J = M / 11;
    *Month = M + 2 - (12 * J);
    *Year = 100 * (C - 49) + Y + J;
}
/* -------------------------------------------------------------------------- */

static int bits2bytes(char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < FRAME_LEN+AUX_LEN) {

        byteval = 0;
        d = 1;
        for (i = 0; i < BITS; i++) {
            //bit=*(bitstr+bitpos+i); /* little endian */
            bit=*(bitstr+bitpos+7-i);  /* big endian */
            // bit == 'x' ?
            if         (bit == '1')                     byteval += d;
            else /*if ((bit == '0') || (bit == 'x'))*/  byteval += 0;
            d <<= 1;
        }
        bitpos += BITS;
        bytes[bytepos++] = byteval & 0xFF;

    }

    //while (bytepos < FRAME_LEN+AUX_LEN) bytes[bytepos++] = 0;

    return 0;
}

/* -------------------------------------------------------------------------- */

/*
M20

GPS data: Big Endian
PTU/ADC data: little endian

frame[0x0] = framelen        // (0x43,) 0x45
frame[0x1] = 0x20 (type M20)

frame[0x02..0x18]: most important data at beginning (incl. counter + M10check)
frame[0x02..0x03]: ADC
frame[0x04..0x05]: ADC
frame[0x06..0x07]: ADC temperature
frame[0x08..0x0A]: GPS altitude
frame[0x0B..0x0E]: GPS hor.Vel. (velE,velN)
frame[0x0F..0x11]: GPS TOW
frame[0x15]:       counter
frame[0x16..0x17]: block check

frame[0x18..0x19]: GPS ver.Vel. (velU)
frame[0x1A..0x1B]: GPS week
frame[0x1C..0x1F]: GPS latitude
frame[0x20..0x23]: GPS longitude

frame[0x44..0x45]: frame check
*/

#define stdFLEN       0x45  // pos[0]=0x45  // M20: 0x45 (0x43)  M10: 0x64
#define pos_GPSTOW    0x0F  // 3 byte
#define pos_GPSlat    0x1C  // 4 byte
#define pos_GPSlon    0x20  // 4 byte
#define pos_GPSalt    0x08  // 3 byte
//#define pos_GPSsats    0xXX  // 1 byte
//#define pos_GPSutc     0xXX  // 1 byte
#define pos_GPSweek   0x1A  // 2 byte
//Velocity East-North-Up (ENU)
#define pos_GPSvE     0x0B  // 2 byte
#define pos_GPSvN     0x0D  // 2 byte
#define pos_GPSvU     0x18  // 2 byte
#define pos_SN        0x12  // 3 byte
#define pos_CNT       0x15  // 1 byte
#define pos_BlkChk    0x16  // 2 byte
#define pos_Check     (stdFLEN-1)  // 2 byte

#define len_BlkChk    0x16 // frame[0x02..0x17] , incl. chk16


#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define XTERM_COLOR_BROWN   "\x1b[38;5;94m"  // 38;5;{0..255}m

#define col_Mtype      "\x1b[38;5;250m" // 1 byte
#define col_GPSweek    "\x1b[38;5;20m"  // 2 byte
#define col_GPSTOW     "\x1b[38;5;27m"  // 3 byte
#define col_GPSdate    "\x1b[38;5;94m" //111
#define col_GPSlat     "\x1b[38;5;34m"  // 4 byte
#define col_GPSlon     "\x1b[38;5;70m"  // 4 byte
#define col_GPSalt     "\x1b[38;5;82m"  // 3 byte
#define col_GPSvel     "\x1b[38;5;36m"  // 6 byte
#define col_SN         "\x1b[38;5;58m"  // 3 byte
#define col_CNT        "\x1b[38;5;172m" // 1 byte
#define col_Check      "\x1b[38;5;11m"  // 2 byte
#define col_TXT        "\x1b[38;5;244m"
#define col_FRTXT      "\x1b[38;5;244m"
#define col_CSok       "\x1b[38;5;2m"
#define col_CSno       "\x1b[38;5;1m"
#define col_CNST       "\x1b[38;5;58m"  // 3 byte

/*
$ for code in  {0..255}
> do echo -e "\e[38;5;${code}m"'\\e[38;5;'"$code"m"\e[0m"
> done
*/

static int get_GPSweek(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t gpsweek_bytes[2];
    int gpsweek;

    //gpx->numSV   = gpx->frame_bytes[pos_GPSsats];
    //gpx->utc_ofs = gpx->frame_bytes[pos_GPSutc];

    for (i = 0; i < 2; i++) {
        byte = gpx->frame_bytes[pos_GPSweek + i];
        gpsweek_bytes[i] = byte;
    }

    gpsweek = (gpsweek_bytes[0] << 8) + gpsweek_bytes[1];

    if (gpsweek > 4000) return -1;

    // Trimble Copernicus II WNRO  (AirPrime XM1110 OK)
    if (gpsweek < 1304 /*2005-01-02*/ ) gpsweek += 1024;

    gpx->week = gpsweek;

    return 0;
}

//char weekday[7][3] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
static char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static int get_GPStime(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t gpstime_bytes[4];
    int gpstime, day;
    int ms;

    for (i = 0; i < 3; i++) {
        byte = gpx->frame_bytes[pos_GPSTOW + i];
        gpstime_bytes[i] = byte;
    }

    gpstime = 0;
    for (i = 0; i < 3; i++) {
        gpstime |= gpstime_bytes[i] << (8*(2-i));
    }

    gpx->tow_ms = gpstime*1000;
    ms = 0;//gpstime % 1000;
    //gpstime /= 1000;
    gpx->gpssec = gpstime;

    day = gpstime / (24 * 3600);
    if ((day < 0) || (day > 6)) return -1;

    gpstime %= (24*3600);

    gpx->wday = day;
    gpx->std =  gpstime/3600;
    gpx->min = (gpstime%3600)/60;
    gpx->sek =  gpstime%60 + ms/1000.0;

    return 0;
}

//static double B60B60 = (1<<30)/90.0; // 2^32/360 = 2^30/90 = 0xB60B60.711x // M10
static double B60B60 = 1e6; // M20

static int get_GPSlat(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t gpslat_bytes[4];
    int gpslat;
    double lat;

    for (i = 0; i < 4; i++) {
        byte = gpx->frame_bytes[pos_GPSlat + i];
        gpslat_bytes[i] = byte;
    }

    gpslat = 0;
    for (i = 0; i < 4; i++) {
        gpslat |= gpslat_bytes[i] << (8*(3-i));
    }
    lat = gpslat / B60B60;
    gpx->lat = lat;

    return 0;
}

static int get_GPSlon(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t gpslon_bytes[4];
    int gpslon;
    double lon;

    for (i = 0; i < 4; i++) {
        byte = gpx->frame_bytes[pos_GPSlon + i];
        gpslon_bytes[i] = byte;
    }

    gpslon = 0;
    for (i = 0; i < 4; i++) {
        gpslon |= gpslon_bytes[i] << (8*(3-i));
    }
    lon = gpslon / B60B60;
    gpx->lon = lon;

    return 0;
}

static int get_GPSalt(gpx_t *gpx) { // 24 bit
    int i;
    unsigned byte;
    ui8_t gpsalt_bytes[4];
    int gpsalt;
    double alt;

    for (i = 0; i < 3; i++) {
        byte = gpx->frame_bytes[pos_GPSalt + i];
        gpsalt_bytes[i] = byte;
    }

    gpsalt = 0;
    for (i = 0; i < 3; i++) {
        gpsalt |= gpsalt_bytes[i] << (8*(2-i));
    }
    alt = gpsalt / 100.0;
    gpx->alt = alt;

    return 0;
}

static int get_GPSvel(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t gpsVel_bytes[2];
    short vel16;
    double vx, vy, dir, alpha;

    for (i = 0; i < 2; i++) {
        byte = gpx->frame_bytes[pos_GPSvE + i];
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    vx = vel16 / 1e2; // ost

    for (i = 0; i < 2; i++) {
        byte = gpx->frame_bytes[pos_GPSvN + i];
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    vy= vel16 / 1e2; // nord

    gpx->vx = vx;
    gpx->vy = vy;
    gpx->vH = sqrt(vx*vx+vy*vy);
///*
    alpha = atan2(vy, vx)*180/M_PI;  // ComplexPlane (von x-Achse nach links) - GeoMeteo (von y-Achse nach rechts)
    dir = 90-alpha;                  // z=x+iy= -> i*conj(z)=y+ix=re(i(pi/2-t)), Achsen und Drehsinn vertauscht
    if (dir < 0) dir += 360;         // atan2(y,x)=atan(y/x)=pi/2-atan(x/y) , atan(1/t) = pi/2 - atan(t)
    gpx->vD2 = dir;
//*/
    dir = atan2(vx, vy) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx->vD = dir;

    for (i = 0; i < 2; i++) {
        byte = gpx->frame_bytes[pos_GPSvU + i];
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    gpx->vV = vel16 / 1e2;

    return 0;
}

static int get_SN(gpx_t *gpx) {
    int i;
    ui8_t  b0 = gpx->frame_bytes[pos_SN]; //0x12
    ui32_t s2 = (gpx->frame_bytes[pos_SN+2]<<8) | gpx->frame_bytes[pos_SN+1];
    ui8_t ym = b0 & 0x7F;  // #{0x0,..,0x77}=120=10*12
    ui8_t y = ym / 12;
    ui8_t m = (ym % 12)+1; // there is b0=0x69<0x80 from 2018-09-19 ...

    for (i = 0; i < 11; i++) gpx->SN[i] = ' '; gpx->SN[11] = '\0';

    for (i = 0; i < 3; i++) {
        gpx->SNraw[i] = gpx->frame_bytes[pos_SN + i];
    }

    sprintf(gpx->SN, "%u%02u", y, m);           // more samples needed
    sprintf(gpx->SN+3, " %u ", (s2&0x3)+2);     // (b0>>7)+1? (s2&0x3)+2?
    sprintf(gpx->SN+6, "%u", (s2>>(2+13))&0x1); // ?(s2>>(2+13))&0x1 ?? (s2&0x3)?
    sprintf(gpx->SN+7, "%04u", (s2>>2)&0x1FFF);

    return 0;
}

/* -------------------------------------------------------------------------- */
/*
g : F^n -> F^16      // checksum, linear
g(m||b) = f(g(m),b)

// update checksum
f : F^16 x F^8 -> F^16 linear

010100001000000101000000
001010000100000010100000
000101000010000001010000
000010100001000000101000
000001010000100000010100
100000100000010000001010
000000011010100000000100
100000000101010000000010
000000001000000000000000
000000000100000000000000
000000000010000000000000
000000000001000000000000
000000000000100000000000
000000000000010000000000
000000000000001000000000
000000000000000100000000
*/

static int update_checkM10(int c, ui8_t b) {
    int c0, c1, t, t6, t7, s;

    c1 = c & 0xFF;

    // B
    b  = (b >> 1) | ((b & 1) << 7);
    b ^= (b >> 2) & 0xFF;

    // A1
    t6 = ( c     & 1) ^ ((c>>2) & 1) ^ ((c>>4) & 1);
    t7 = ((c>>1) & 1) ^ ((c>>3) & 1) ^ ((c>>5) & 1);
    t = (c & 0x3F) | (t6 << 6) | (t7 << 7);

    // A2
    s  = (c >> 7) & 0xFF;
    s ^= (s >> 2) & 0xFF;


    c0 = b ^ t ^ s;

    return ((c1<<8) | c0) & 0xFFFF;
}

static int checkM10(ui8_t *msg, int len) {
    int i, cs;  // msg[0] = len+1

    cs = 0;
    for (i = 0; i < len; i++) {
        cs = update_checkM10(cs, msg[i]);
    }

    return cs & 0xFFFF;
}
// checkM10(frame, frame[0]-1) = blk_checkM10(frame[0], frame+1)
static int blk_checkM10(int len, ui8_t *msg) {
    int i, cs;
    ui8_t pre = len & 0xFF; // len(block+chk16)
    cs = 0;

    cs = update_checkM10(cs, pre);

    for (i = 0; i < len-2; i++) {
        cs = update_checkM10(cs, msg[i]);
    }

    return cs & 0xFFFF;
}

/* -------------------------------------------------------------------------- */

static float get_Tntc0(gpx_t *gpx) {
// SMD ntc
    float Rs = 22.1e3;          // P5.6=Vcc
  float R25 = 2.2e3;// 0.119e3; //2.2e3;
  float b = 3650.0;           // B/Kelvin
  float T25 = 25.0 + 273.15;  // T0=25C, R0=R25=5k
// -> Steinhart–Hart coefficients (polyfit):
    float p0 =  4.42606809e-03,
          p1 = -6.58184309e-04,
          p2 =  8.95735557e-05,
          p3 = -2.84347503e-06;
    float T = 0.0;              // T/Kelvin
    ui16_t ADC_ntc0;            // M10: ADC12 P6.4(A4)
    float x, R;

    ADC_ntc0  = (gpx->frame_bytes[0x07] << 8) | gpx->frame_bytes[0x06]; // M10: 0x40,0x3F
    x = (4095.0 - ADC_ntc0)/ADC_ntc0;  // (Vcc-Vout)/Vout
    R = Rs / x;
    if (R > 0)  T = 1/(1/T25 + 1/b * log(R/R25));
    //if (R > 0)  T =  1/( p0 + p1*log(R) + p2*log(R)*log(R) + p3*log(R)*log(R)*log(R) );

    return T - 273.15;
}

/* -------------------------------------------------------------------------- */

static int print_pos(gpx_t *gpx, int bcOK, int csOK) {
    int err, err2;

    if (1 || gpx->type == t_M20)
    {
        err = 0;
        err |= get_GPSweek(gpx);
        err |= get_GPStime(gpx);
        err |= get_GPSlat(gpx);
        err |= get_GPSlon(gpx);
        err |= get_GPSalt(gpx);
        err2 = get_GPSvel(gpx);
    }
    else err = 0xFF;

    if (!err) {

        Gps2Date(gpx->week, gpx->gpssec, &gpx->jahr, &gpx->monat, &gpx->tag);
        get_SN(gpx);

        if (gpx->option.col) {
            fprintf(stdout, col_TXT);
            if (gpx->option.vbs >= 3) {
                fprintf(stdout, "[%3d]", gpx->frame_bytes[pos_CNT]);
                fprintf(stdout, " (W "col_GPSweek"%d"col_TXT") ", gpx->week);
            }
            fprintf(stdout, col_GPSTOW"%s"col_TXT" ", weekday[gpx->wday]);
            fprintf(stdout, col_GPSdate"%04d-%02d-%02d"col_TXT" "col_GPSTOW"%02d:%02d:%06.3f"col_TXT" ",
                    gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek);
            fprintf(stdout, " lat: "col_GPSlat"%.5f"col_TXT" ", gpx->lat);
            fprintf(stdout, " lon: "col_GPSlon"%.5f"col_TXT" ", gpx->lon);
            fprintf(stdout, " alt: "col_GPSalt"%.2f"col_TXT" ", gpx->alt);
            if (!err2) {
                fprintf(stdout, "  vH: "col_GPSvel"%.1f"col_TXT"  D: "col_GPSvel"%.1f"col_TXT"  vV: "col_GPSvel"%.1f"col_TXT" ", gpx->vH, gpx->vD, gpx->vV);
            }
            if (gpx->option.vbs >= 2 && (bcOK || csOK)) { // SN
                fprintf(stdout, "  SN: "col_SN"%s"col_TXT, gpx->SN);
            }
            if (gpx->option.vbs >= 2) {
                fprintf(stdout, "  # ");
                if (bcOK) fprintf(stdout, " "col_CSok"(ok)"col_TXT);
                else      fprintf(stdout, " "col_CSno"(no)"col_TXT);
                if (csOK) fprintf(stdout, " "col_CSok"[OK]"col_TXT);
                else      fprintf(stdout, " "col_CSno"[NO]"col_TXT);
            }
            if (gpx->option.ptu && csOK) {
                if (gpx->option.vbs >= 3) {
                    float t0 = get_Tntc0(gpx);
                    if (t0 > -270.0) fprintf(stdout, " (T0:%.1fC) ", t0);
                }
            }
            fprintf(stdout, ANSI_COLOR_RESET"");
        }
        else {
            if (gpx->option.vbs >= 3) {
                fprintf(stdout, "[%3d]", gpx->frame_bytes[pos_CNT]);
                fprintf(stdout, " (W %d) ", gpx->week);
            }
            fprintf(stdout, "%s ", weekday[gpx->wday]);
            fprintf(stdout, "%04d-%02d-%02d %02d:%02d:%06.3f ",
                    gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek);
            fprintf(stdout, " lat: %.5f ", gpx->lat);
            fprintf(stdout, " lon: %.5f ", gpx->lon);
            fprintf(stdout, " alt: %.2f ", gpx->alt);
            if (!err2) {
                fprintf(stdout, "  vH: %.1f  D: %.1f  vV: %.1f ", gpx->vH, gpx->vD, gpx->vV);
            }
            if (gpx->option.vbs >= 2 && (bcOK || csOK)) { // SN
                fprintf(stdout, "  SN: %s", gpx->SN);
            }
            if (gpx->option.vbs >= 2) {
                fprintf(stdout, "  # ");
                if (bcOK) fprintf(stdout, " (ok)"); else fprintf(stdout, " (no)");
                if (csOK) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
            }
            if (gpx->option.ptu && csOK) {
                if (gpx->option.vbs >= 3) {
                    float t0 = get_Tntc0(gpx);
                    if (t0 > -270.0) fprintf(stdout, " (T0:%.1fC) ", t0);
                }
            }
        }
        fprintf(stdout, "\n");

        if (gpx->option.jsn) {
            // Print out telemetry data as JSON
            if (csOK) {
                int j;
                char sn_id[4+12] = "M20-";
                double sec_gps0 = (double)gpx->week*SECONDS_IN_WEEK + gpx->tow_ms/1e3;

                strncpy(sn_id+4, gpx->SN, 12);
                sn_id[15] = '\0';
                for (j = 0; sn_id[j]; j++) { if (sn_id[j] == ' ') sn_id[j] = '-'; }

                fprintf(stdout, "{ \"type\": \"%s\"", "M20");
                fprintf(stdout, ", \"frame\": %lu, ", (unsigned long)(sec_gps0+0.5));
                fprintf(stdout, "\"id\": \"%s\", \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f",
                               sn_id, gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek, gpx->lat, gpx->lon, gpx->alt, gpx->vH, gpx->vD, gpx->vV);
                fprintf(stdout, ", \"rawid\": \"M20_%02X%02X%02X\"", gpx->frame_bytes[pos_SN], gpx->frame_bytes[pos_SN+1], gpx->frame_bytes[pos_SN+2]); // gpx->type
                fprintf(stdout, ", \"subtype\": \"0x%02X\"", gpx->type);
                if (gpx->jsn_freq > 0) {
                    fprintf(stdout, ", \"freq\": %d", gpx->jsn_freq);
                }
                fprintf(stdout, " }\n");
                fprintf(stdout, "\n");
            }
        }

    }

    return err;
}

static int print_frame(gpx_t *gpx, int pos) {
    int i;
    ui8_t byte;
    int cs1, cs2;
    int bc1, bc2;
    int flen = stdFLEN; // stdFLEN=0x64, auxFLEN=0x76; M20:0x45 ?

    bits2bytes(gpx->frame_bits, gpx->frame_bytes);
    flen = gpx->frame_bytes[0];
    if (flen == stdFLEN) gpx->auxlen = 0;
    else {
        gpx->auxlen = flen - stdFLEN;
        //if (gpx->auxlen < 0 || gpx->auxlen > AUX_LEN) gpx->auxlen = 0; // 0x43,0x45
    }

    cs1 = (gpx->frame_bytes[pos_Check+gpx->auxlen] << 8) | gpx->frame_bytes[pos_Check+gpx->auxlen+1];
    cs2 = checkM10(gpx->frame_bytes, pos_Check+gpx->auxlen);

    bc1 = (gpx->frame_bytes[pos_BlkChk] << 8) | gpx->frame_bytes[pos_BlkChk+1];
    bc2 = blk_checkM10(len_BlkChk, gpx->frame_bytes+2); // len(essentialBlock+chk16) = 0x16

    switch (gpx->frame_bytes[1]) {
        case 0x8F: gpx->type = t_M2K2;    break;
        case 0x9F: gpx->type = t_M10;     break;
        case 0xAF: gpx->type = t_M10plus; break;
        case 0x20: gpx->type = t_M20;     break;
        default  : gpx->type = t_M10;
    }

    if (gpx->option.raw) {

        if (gpx->option.col /* &&  gpx->frame_bytes[1] != 0x49 */) {
            fprintf(stdout, col_FRTXT);
            for (i = 0; i < flen+1; i++) {
                byte = gpx->frame_bytes[i];
                if  (i == 1) fprintf(stdout, col_Mtype);
                if ((i >= pos_GPSTOW)   &&  (i < pos_GPSTOW+3))   fprintf(stdout, col_GPSTOW);
                if ((i >= pos_GPSlat)   &&  (i < pos_GPSlat+4))   fprintf(stdout, col_GPSlat);
                if ((i >= pos_GPSlon)   &&  (i < pos_GPSlon+4))   fprintf(stdout, col_GPSlon);
                if ((i >= pos_GPSalt)   &&  (i < pos_GPSalt+3))   fprintf(stdout, col_GPSalt);
                if ((i >= pos_GPSweek)  &&  (i < pos_GPSweek+2))  fprintf(stdout, col_GPSweek);
                if ((i >= pos_GPSvE)    &&  (i < pos_GPSvE+2))    fprintf(stdout, col_GPSvel);
                if ((i >= pos_GPSvN)    &&  (i < pos_GPSvN+2))    fprintf(stdout, col_GPSvel);
                if ((i >= pos_GPSvU)    &&  (i < pos_GPSvU+2))    fprintf(stdout, col_GPSvel);
                if ((i >= pos_SN)       &&  (i < pos_SN+3))       fprintf(stdout, col_SN);
                if  (i == pos_CNT) fprintf(stdout, col_CNT);
                if ((i >= pos_BlkChk)   &&  (i < pos_BlkChk+2))   fprintf(stdout, col_Check);
                if ((i >= pos_Check+gpx->auxlen)  &&  (i < pos_Check+gpx->auxlen+2))  fprintf(stdout, col_Check);
                fprintf(stdout, "%02x", byte);
                fprintf(stdout, col_FRTXT);
            }
            if (gpx->option.vbs) {
                fprintf(stdout, " # "col_Check"%04x"col_FRTXT, cs2);
                if (bc1 == bc2) fprintf(stdout, " "col_CSok"(ok)"col_TXT);
                else            fprintf(stdout, " "col_CSno"(no)"col_TXT);
                if (cs1 == cs2) fprintf(stdout, " "col_CSok"[OK]"col_TXT);
                else            fprintf(stdout, " "col_CSno"[NO]"col_TXT);
            }
            fprintf(stdout, ANSI_COLOR_RESET"\n");
        }
        else {
            for (i = 0; i < flen+1; i++) {
                byte = gpx->frame_bytes[i];
                fprintf(stdout, "%02x", byte);
            }
            if (gpx->option.vbs) {
                fprintf(stdout, " # %04x", cs2);
                if (bc1 == bc2) fprintf(stdout, " (ok)"); else fprintf(stdout, " (no)");
                if (cs1 == cs2) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
            }
            fprintf(stdout, "\n");
        }

    }
    /*
    else if (gpx->frame_bytes[1] == 0x49) {
        if (gpx->option.vbs == 3) {
            for (i = 0; i < FRAME_LEN+gpx->auxlen; i++) {
                byte = gpx->frame_bytes[i];
                fprintf(stdout, "%02x", byte);
            }
            fprintf(stdout, "\n");
        }
    }
    */
    else print_pos(gpx, bc1 == bc2, cs1 == cs2);

    return (gpx->frame_bytes[0]<<8)|gpx->frame_bytes[1];
}

/* -------------------------------------------------------------------------- */


int main(int argc, char **argv) {

    int option_verbose = 0;  // ausfuehrliche Anzeige
    int option_raw = 0;      // rohe Frames
    int option_inv = 0;      // invertiert Signal
    //int option_res = 0;      // genauere Bitmessung
    int option_color = 0;
    int option_ptu = 0;
    int option_min = 0;
    int option_iq = 0;
    int option_iqdc = 0;
    int option_lp = 0;
    int option_dc = 0;
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

    int bit, bit0;
    int bitpos = 0;
    int bitQ;
    int pos;
    hsbit_t hsbit, hsbit1;

    //int headerlen = 0;

    int header_found = 0;

    float thres = 0.76;
    float _mv = 0.0;

    int symlen = 2;
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
            //fprintf(stderr, "       -v, --verbose\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       -c, --color\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "-vv" ) == 0) ) option_verbose = 2;
        else if ( (strcmp(*argv, "-vvv") == 0) ) option_verbose = 3;
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;  // nicht noetig
        }
        else if ( (strcmp(*argv, "-c") == 0) || (strcmp(*argv, "--color") == 0) ) {
            option_color = 1;
        }
        else if ( (strcmp(*argv, "--br") == 0) ) {
            ++argv;
            if (*argv) {
                baudrate = atof(*argv);
                if (baudrate < 9000 || baudrate > 10000) baudrate = BAUD_RATE; // default: M20:9600, M10:9615
            }
            else return -1;
        }
        //else if   (strcmp(*argv, "--res") == 0) { option_res = 1; }
        else if ( (strcmp(*argv, "--ptu") == 0) ) {
            option_ptu = 1;
        }
        else if ( (strcmp(*argv, "--spike") == 0) ) {
            spike = 1;
        }
        else if ( (strcmp(*argv, "--ch2") == 0) ) { sel_wavch = 1; }  // right channel (default: 0=left)
        else if   (strcmp(*argv, "--softin") == 0) { option_softin = 1; }  // float32 soft input
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
        else if   (strcmp(*argv, "--dc") == 0) { option_dc = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if   (strcmp(*argv, "--json") == 0) { gpx.option.jsn = 1; }
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


    // init gpx
    gpx.option.inv = option_inv; // irrelevant
    gpx.option.vbs = option_verbose;
    gpx.option.raw = option_raw;
    gpx.option.ptu = option_ptu;
    gpx.option.col = option_color;

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

        if (cfreq > 0) {
            int fq_kHz = (cfreq - dsp.xlt_fq*pcm.sr + 500)/1e3;
            gpx.jsn_freq = fq_kHz;
        }

        // m10: BT>1?, h=1.2 ?
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
        dsp.symhd = 1; // M10!header
        dsp._spb = dsp.sps*symlen;
        dsp.hdr = rawheader;
        dsp.hdrlen = strlen(rawheader);
        dsp.BT = 1.8; // bw/time (ISI) // 1.0..2.0  // M20 ?
        dsp.h = 0.9;  // 1.2 modulation index       // M20 ?
        dsp.opt_iq = option_iq;
        dsp.opt_iqdc = option_iqdc;
        dsp.opt_lp = option_lp;
        dsp.lpIQ_bw = 24e3; // IF lowpass bandwidth
        dsp.lpFM_bw = 10e3; // FM audio lowpass
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
        hdb.ths = 0.8; // caution 0.7: false positive / offset
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
            pos /= 2;
            bit0 = '0'; // oder: _mv[j] > 0

            while ( pos < BITFRAME_LEN+BITAUX_LEN ) {

                if (option_softin) {
                    float s1 = 0.0;
                    float s2 = 0.0;
                    float s = 0.0;
                    bitQ = f32soft_read(fp, &s1);
                    if (bitQ != EOF) {
                        bitQ = f32soft_read(fp, &s2);
                        if (bitQ != EOF) {
                            s = s2-s1; // integrate both symbols  // only 2nd Manchester symbol: s2
                            bit = (s>=0.0); // no soft decoding
                        }
                    }
                }
                else {
                    float bl = -1;
                    if (option_iq >= 2) spike = 0;
                    if (option_iq > 2)  bl = 4.0;
                    //bitQ = read_slbit(&dsp, &bit, 0, bitofs, bitpos, bl, spike); // symlen=2
                    bitQ = read_softbit2p(&dsp, &hsbit, 0, bitofs, bitpos, bl, spike, &hsbit1); // symlen=2
                    bit = hsbit.hb;
                }
                if ( bitQ == EOF ) { break; }

                gpx.frame_bits[pos] = 0x31 ^ (bit0 ^ bit);
                pos++;
                bit0 = bit;
                bitpos += 1;
            }
            gpx.frame_bits[pos] = '\0';
            print_frame(&gpx, pos);
            if (pos < BITFRAME_LEN) break;

            header_found = 0;

            // bis Ende der Sekunde vorspulen; allerdings Doppel-Frame alle 10 sek
            if (gpx.option.vbs < 3) { // && (regulare frame) // print_frame-return?
                while ( bitpos < 5*BITFRAME_LEN ) {
                    bitQ = read_slbit(&dsp, &bit, 0, bitofs, bitpos, -1, 0); // symlen=2
                    if ( bitQ == EOF) break;
                    bitpos++;
                }
            }

            pos = 0;
        }
    }


    if (!option_softin) free_buffers(&dsp);
    else {
        if (hdb.buf) { free(hdb.buf); hdb.buf = NULL; }
    }

    fclose(fp);

    return 0;
}

