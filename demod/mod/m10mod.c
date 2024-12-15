
/*
 *  m10
 *  sync header: correlation/matched filter
 *  files: m10mod.c demod_mod.h demod_mod.c
 *  compile:
 *      gcc -c demod_mod.c
 *      gcc m10mod.c demod_mod.o -lm -o m10mod
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
    i8_t crc;  // CRC check output
    i8_t ecc;  // M10/M20: no ECC
    i8_t sat;  // GPS sat data
    i8_t ptu;  // PTU: temperature
    i8_t inv;
    i8_t aut;
    i8_t col;  // colors
    i8_t jsn;  // JSON output (auto_rx)
    i8_t slt;  // silent (only raw/json)
} option_t;


/*
   9600 baud -> 9616 baud ?
*/
#define BAUD_RATE   9615  // 9614..9616

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
                                                    //"0100010100100000"; // M20: 45 20 (baud=9600)
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
    float T;  float _RH;
    float Ti; float batV;
    ui8_t numSV;
    ui8_t utc_ofs;
    char SN[12];
    ui8_t SNraw[5];
    ui8_t frame_bytes[FRAME_LEN+AUX_LEN+4];
    char frame_bits[BITFRAME_LEN+BITAUX_LEN+8];
    int auxlen; // 0 .. 0x76-0x64
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
M10 w/ trimble GPS

frame[0x0] = framelen
frame[0x1] = 0x9F (type M10)

init/noGPS: frame[0x2]=0x23
GPS: frame[0x2]=0x20 (GPS trimble pck 0x8F-20 sub-id)

frame[0x02..0x21] = GPS trimble pck 0x8F-20 byte 0..31 (sub-id, vel, tow, lat, lon, alt, fix, NumSV, UTC-ofs, week)
frame[0x22..0x2D] = GPS trimble pck 0x8F-20 byte 32..55:2 (PRN 1..12 only)

Trimble Copernicus II
GPS packet 0x8F-20 (p.138)
byte
0      sub-pck id (always 0x20)
2-3    velE (i16) 0.005m/s
4-5    velN (i16) 0.005m/s
6-7    velU (i16) 0.005m/s
8-11   TOW (ms)
12-15  lat (scale 2^32/360) (i32) -90..90
16-19  lon (scale 2^32/360) (ui32) 0..360 <-> (i32) -180..180
20-23  alt (i32) mm above ellipsoid)
24     bit0: vel-scale (0: 0.005m/s)
26     datum (1: WGS-84)
27     fix: bit0(0:valid fix, 1:invalid fix), bit2(0:3D, 1:2D)
28     numSVs
29     UTC offset = (GPS - UTC) sec
30-31  GPS week
32+2*n PRN_(n+1), bit0-5

frame[0x32..0x5C] sensors (rel.hum., temp.)
frame[0x5D..0x61] SN
frame[0x62] counter
frame[0x63..0x64] check  (AUX len=0x76: frame[0x63..0x74], frame[0x75..0x76])


6449/10sec-frame:
GPS trimble pck 0x47 (signal levels): numSats sat1 lev1 sat2 lev2 ..
frame[0x0] = framelen
frame[0x1] = 0x49
frame[0x2] = numSats (max 12)
frame[0x3+2*n] = PRN_(n+1)
frame[0x4+2*n] = signal level (float32 -> i8-byte level)

*/
/*
M10 w/  Sierra Wireless  Airprime X1110
 -> Trimble Copernicus II
*/


#define stdFLEN        0x64  // pos[0]=0x64
// Trimble GPS
#define pos_GPSTOW     0x0A  // 4 byte
#define pos_GPSlat     0x0E  // 4 byte
#define pos_GPSlon     0x12  // 4 byte
#define pos_GPSalt     0x16  // 4 byte
#define pos_GPSsats    0x1E  // 1 byte
#define pos_GPSutc     0x1F  // 1 byte
#define pos_GPSweek    0x20  // 2 byte
//Velocity East-North-Up (ENU)
#define pos_GPSvE      0x04  // 2 byte
#define pos_GPSvN      0x06  // 2 byte
#define pos_GPSvU      0x08  // 2 byte

#define pos_SN         0x5D  // 2+3 byte
#define pos_CNT        0x62  // 1 byte
#define pos_Check     (stdFLEN-1)  // 2 byte

// Gtop GPS
#define pos_gtopGPSlat    0x04  // 4 byte
#define pos_gtopGPSlon    0x08  // 4 byte
#define pos_gtopGPSalt    0x0C  // 3 byte
#define pos_gtopGPSvE     0x0F  // 2 byte
#define pos_gtopGPSvN     0x11  // 2 byte
#define pos_gtopGPSvU     0x13  // 2 byte
#define pos_gtopGPStime   0x15  // 3 byte
#define pos_gtopGPSdate   0x18  // 3 byte


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
#define col_GPSTOW     "\x1b[38;5;27m"  // 4 byte
#define col_GPSdate    "\x1b[38;5;94m" //111
#define col_GPSlat     "\x1b[38;5;34m"  // 4 byte
#define col_GPSlon     "\x1b[38;5;70m"  // 4 byte
#define col_GPSalt     "\x1b[38;5;82m"  // 4 byte
#define col_GPSvel     "\x1b[38;5;36m"  // 6 byte
#define col_SN         "\x1b[38;5;58m"  // 3 byte
#define col_CNT        "\x1b[38;5;172m" // 1 byte
#define col_Check      "\x1b[38;5;11m"  // 2 byte
#define col_CSok       "\x1b[38;5;2m"
#define col_CSno       "\x1b[38;5;1m"
#define col_TXT        "\x1b[38;5;244m"
#define col_FRTXT      "\x1b[38;5;244m"

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

    gpx->numSV   = gpx->frame_bytes[pos_GPSsats];
    gpx->utc_ofs = gpx->frame_bytes[pos_GPSutc];

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

    for (i = 0; i < 4; i++) {
        byte = gpx->frame_bytes[pos_GPSTOW + i];
        gpstime_bytes[i] = byte;
    }

    gpstime = 0;
    for (i = 0; i < 4; i++) {
        gpstime |= gpstime_bytes[i] << (8*(3-i));
    }

    gpx->tow_ms = gpstime;
    ms = gpstime % 1000;
    gpstime /= 1000;
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

static double B60B60 = (1<<30)/90.0; // 2^32/360 = 2^30/90 = 0xB60B60.711x

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

static int get_GPSalt(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t gpsalt_bytes[4];
    int gpsalt;
    double alt;

    for (i = 0; i < 4; i++) {
        byte = gpx->frame_bytes[pos_GPSalt + i];
        gpsalt_bytes[i] = byte;
    }

    gpsalt = 0;
    for (i = 0; i < 4; i++) {
        gpsalt |= gpsalt_bytes[i] << (8*(3-i));
    }
    alt = gpsalt / 1000.0;
    gpx->alt = alt;

    return 0;
}

static int get_GPSvel(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui8_t gpsVel_bytes[2];
    short vel16;
    double vx, vy, dir, alpha;
    const double ms2kn100 = 2e2;  // m/s -> knots: 1 m/s = 3.6/1.852 kn = 1.94 kn

    for (i = 0; i < 2; i++) {
        byte = gpx->frame_bytes[pos_GPSvE + i];
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    vx = vel16 / ms2kn100; // ost

    for (i = 0; i < 2; i++) {
        byte = gpx->frame_bytes[pos_GPSvN + i];
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    vy= vel16 / ms2kn100; // nord

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
    gpx->vV = vel16 / ms2kn100;

    return 0;
}

static int get_SN(gpx_t *gpx) {
    int i;
    unsigned byte;

    for (i = 0; i < 11; i++) gpx->SN[i] = ' '; gpx->SN[11] = '\0';

    for (i = 0; i < 5; i++) {
        gpx->SNraw[i] = gpx->frame_bytes[pos_SN + i];
    }

    byte = gpx->SNraw[2];
    sprintf(gpx->SN, "%1X%02u", (byte>>4)&0xF, byte&0xF);
    byte = gpx->SNraw[3] | (gpx->SNraw[4]<<8);
    sprintf(gpx->SN+3, " %1X %1u%04u", gpx->SNraw[0]&0xF, (byte>>13)&0x7, byte&0x1FFF);

    return 0;
}

/* -------------------------------------------------------------------------- */
//
// M10+ w/ Gtop

static int get_gtopGPSpos(gpx_t *gpx) {
    int i;
    ui8_t bytes[4];
    int val;

    for (i = 0; i < 4; i++)  bytes[i] = gpx->frame_bytes[pos_gtopGPSlat + i];
    val = 0;
    for (i = 0; i < 4; i++)  val |= bytes[i] << (8*(3-i));
    gpx->lat = val/1e6;

    for (i = 0; i < 4; i++)  bytes[i] = gpx->frame_bytes[pos_gtopGPSlon + i];
    val = 0;
    for (i = 0; i < 4; i++)  val |= bytes[i] << (8*(3-i));
    gpx->lon = val/1e6;

    for (i = 0; i < 3; i++)  bytes[i] = gpx->frame_bytes[pos_gtopGPSalt + i];
    val = 0;
    for (i = 0; i < 3; i++)  val |= bytes[i] << (8*(2-i));
    if (val & 0x800000) val -= 0x1000000; // alt: signed 24bit?
    gpx->alt = val/1e2;

    return 0;
}

static int get_gtopGPSvel(gpx_t *gpx) {
    int i;
    ui8_t bytes[2];
    short vel16;
    double vx, vy, vz, dir;

    for (i = 0; i < 2; i++)  bytes[i] = gpx->frame_bytes[pos_gtopGPSvE + i];
    vel16 = bytes[0] << 8 | bytes[1];
    vx = vel16 / 1e2; // east

    for (i = 0; i < 2; i++)  bytes[i] = gpx->frame_bytes[pos_gtopGPSvN + i];
    vel16 = bytes[0] << 8 | bytes[1];
    vy= vel16 / 1e2; // north

    for (i = 0; i < 2; i++)  bytes[i] = gpx->frame_bytes[pos_gtopGPSvU + i];
    vel16 = bytes[0] << 8 | bytes[1];
    vz = vel16 / 1e2; // up

    gpx->vx = vx;
    gpx->vy = vy;
    gpx->vH = sqrt(vx*vx+vy*vy);
    dir = atan2(vx, vy) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx->vD = dir;
    gpx->vV = vz;

    return 0;
}

static int get_gtopGPStime(gpx_t *gpx) {
    int i;
    ui8_t bytes[4];
    int time;

    for (i = 0; i < 3; i++)  bytes[i] = gpx->frame_bytes[pos_gtopGPStime + i];
    time = 0;
    for (i = 0; i < 3; i++)  time |= bytes[i] << (8*(2-i));

    gpx->std =  time/10000;
    gpx->min = (time%10000)/100;
    gpx->sek = (time%100)/1.0;

    return 0;
}

static int get_gtopGPSdate(gpx_t *gpx) {
    int i;
    ui8_t bytes[4];
    int date;

    for (i = 0; i < 3; i++)  bytes[i] = gpx->frame_bytes[pos_gtopGPSdate + i];
    date = 0;
    for (i = 0; i < 3; i++)  date |= bytes[i] << (8*(2-i));

    gpx->jahr  = 2000 + date%100;
    gpx->monat = (date%10000)/100;
    gpx->tag   =  date/10000;

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
    int i, cs;

    cs = 0;
    for (i = 0; i < len; i++) {
        cs = update_checkM10(cs, msg[i]);
    }

    return cs & 0xFFFF;
}

/* -------------------------------------------------------------------------- */

// Temperatur Sensor
// NTC-Thermistor Shibaura PB5-41E
//
static float get_Temp(gpx_t *gpx) {
// NTC-Thermistor Shibaura PB5-41E
// T00 = 273.15 +  0.0 , R00 = 15e3
// T25 = 273.15 + 25.0 , R25 = 5.369e3
// B00 = 3450.0 Kelvin // 0C..100C, poor fit low temps
// [  T/C  , R/1e3 ] ( [P__-43]/2.0 ):
// [ -50.0 , 204.0 ]
// [ -45.0 , 150.7 ]
// [ -40.0 , 112.6 ]
// [ -35.0 , 84.90 ]
// [ -30.0 , 64.65 ]
// [ -25.0 , 49.66 ]
// [ -20.0 , 38.48 ]
// [ -15.0 , 30.06 ]
// [ -10.0 , 23.67 ]
// [  -5.0 , 18.78 ]
// [   0.0 , 15.00 ]
// [   5.0 , 12.06 ]
// [  10.0 , 9.765 ]
// [  15.0 , 7.955 ]
// [  20.0 , 6.515 ]
// [  25.0 , 5.370 ]
// [  30.0 , 4.448 ]
// [  35.0 , 3.704 ]
// [  40.0 , 3.100 ]
// -> Steinhart-Hart coefficients (polyfit):
    float p0 = 1.07303516e-03,
          p1 = 2.41296733e-04,
          p2 = 2.26744154e-06,
          p3 = 6.52855181e-08;
// T/K = 1/( p0 + p1*ln(R) + p2*ln(R)^2 + p3*ln(R)^3 )

    // range/scale 0, 1, 2:                        // M10-pcb
    float Rs[3] = { 12.1e3 ,  36.5e3 ,  475.0e3 }; // bias/series
    float Rp[3] = { 1e20   , 330.0e3 , 2000.0e3 }; // parallel, Rp[0]=inf

    ui8_t  scT;     // {0,1,2}, range/scale voltage divider
    ui16_t ADC_RT;  // ADC12 P6.7(A7) , adr_0377h,adr_0376h
    ui16_t Tcal[2]; // adr_1000h[scT*4]

    float adc_max = 4095.0; // ADC12
    float x, R;
    float T = 0;    // T/Kelvin

    scT     =  gpx->frame_bytes[0x3E]; // adr_0455h
    ADC_RT  = (gpx->frame_bytes[0x40] << 8) | gpx->frame_bytes[0x3F];
    ADC_RT -= 0xA000;
    Tcal[0] = (gpx->frame_bytes[0x42] << 8) | gpx->frame_bytes[0x41];
    Tcal[1] = (gpx->frame_bytes[0x44] << 8) | gpx->frame_bytes[0x43];

    x = (adc_max-ADC_RT)/ADC_RT;  // (Vcc-Vout)/Vout
    if (scT < 3) R =  Rs[scT] /( x - Rs[scT]/Rp[scT] );
    else         R = -1;

    if (R > 0)  T =  1/( p0 + p1*log(R) + p2*log(R)*log(R) + p3*log(R)*log(R)*log(R) );

    return  T - 273.15; // Celsius
}

static float get_intTemp(gpx_t *gpx) {
// on-chip temperature
    ui16_t ADC_Ti_raw = (gpx->frame_bytes[0x49] << 8) | gpx->frame_bytes[0x48]; // int.temp.diode, ref: 4095->1.5V
    float vti, ti;
    // INCH1A (temp.diode), slau144
    vti = ADC_Ti_raw/4095.0 * 1.5; // V_REF+ = 1.5V, no calibration
    ti = (vti-0.986)/0.00355;      // 0.986/0.00355=277.75, 1.5/4095/0.00355=0.1032
    gpx->Ti = ti;
    // SegmentA-Calibration:
    //ui16_t T30 = adr_10e2h; // CAL_ADC_15T30
    //ui16_t T85 = adr_10e4h; // CAL_ADC_15T85
    //float  tic = (ADC_Ti_raw-T30)*(85.0-30.0)/(T85-T30) + 30.0;
}

/*
frame[0x32]: adr_1074h
frame[0x33]: adr_1075h
frame[0x34]: adr_1076h

frame[0x35..0x37]: TBCCR1 ; relHumCap-freq

frame[0x38]: adr_1078h
frame[0x39]: adr_1079h
frame[0x3A]: adr_1077h
frame[0x3B]: adr_100Ch
frame[0x3C..3D]: 0


frame[0x3E]: scale_index ; scale/range-index
frame[0x3F..40] = ADC12_A7 | 0xA000, V_R+=AVcc ; Thermistor

frame[0x41]: adr_1000h[scale_index*4]
frame[0x42]: adr_1000h[scale_index*4+1]
frame[0x43]: adr_1000h[scale_index*4+2]
frame[0x44]: adr_1000h[scale_index*4+3]

frame[0x45..46]: ADC12_A5/4, V_R+=2.5V
frame[0x47]: ADC12_A2/16 , V_R+=2.5V
frame[0x48..49]: ADC12_iT, V_R+=1.5V (int.Temp.diode)
frame[0x4C..4D]: ADC12_A6, V_R+=2.5V
frame[0x4E..4F]: ADC12_A3, V_R+=AVcc
frame[0x50..54]: 0;
frame[0x55..56]: ADC12_A1, V_R+=AVcc
frame[0x57..58]: ADC12_A0, V_R+=AVcc
frame[0x59..5A]: ADC12_A4, V_R+=AVcc  // ntc2: R(25C)=2.2k, Rs=22.1e3 (relHumCap-Temp)

frame[0x5B]:
frame[0x5C]: adr_108Eh


frame[0x5D]: adr_1082h (SN)
frame[0x5E]: adr_1083h (SN)
frame[0x5F]: adr_1084h (SN)
frame[0x60]: adr_1080h (SN)
frame[0x61]: adr_1081h (SN)
*/
static float get_Tntc2(gpx_t *gpx) {
// SMD ntc
    float Rs = 22.1e3;          // P5.6=Vcc
//  float R25 = 2.2e3;
//  float b = 3650.0;           // B/Kelvin
//  float T25 = 25.0 + 273.15;  // T0=25C, R0=R25=5k
// -> Steinhart-Hart coefficients (polyfit):
    float p0 =  4.42606809e-03,
          p1 = -6.58184309e-04,
          p2 =  8.95735557e-05,
          p3 = -2.84347503e-06;
    float T = 0.0;              // T/Kelvin
    ui16_t ADC_ntc2;            // ADC12 P6.4(A4)
    float x, R;

    ADC_ntc2  = (gpx->frame_bytes[0x5A] << 8) | gpx->frame_bytes[0x59];
    x = (4095.0 - ADC_ntc2)/ADC_ntc2;  // (Vcc-Vout)/Vout
    R = Rs / x;
    //if (R > 0)  T = 1/(1/T25 + 1/b * log(R/R25));
    if (R > 0)  T =  1/( p0 + p1*log(R) + p2*log(R)*log(R) + p3*log(R)*log(R)*log(R) );

    return T - 273.15;
}

// Humidity Sensor
// U.P.S.I.
//
#define FREQ_CAPCLK (8e6/2)      // 8 MHz XT2 crystal, InputDivider IDx=01 (/2)
#define LN2         0.693147181
#define ADR_108A    1000.0       // 0x3E8=1000

static float get_count_55(gpx_t *gpx) { // CalRef 55%RH , T=20C ?
    ui32_t TBCREF_1000 = gpx->frame_bytes[0x32] | (gpx->frame_bytes[0x33]<<8) | (gpx->frame_bytes[0x34]<<16);
    return TBCREF_1000 / ADR_108A;
}

static float get_count_RH(gpx_t *gpx) {  // capture 1000 rising edges
    ui32_t TBCCR1_1000 = gpx->frame_bytes[0x35] | (gpx->frame_bytes[0x36]<<8) | (gpx->frame_bytes[0x37]<<16);
    return TBCCR1_1000 / ADR_108A;
}
static float get_TLC555freq(gpx_t *gpx) {
    return FREQ_CAPCLK / get_count_RH(gpx);
}
static float cRHc55_RH(gpx_t *gpx, float cRHc55) {  // C_RH / C_55
// U.P.S.I.
// C_RH/C_55 = 0.8955 + 0.002*RH , T=20C
// C_RH = C_RH(RH,T) , RH = RH(C_RH,T)
// C_RH/C_55 approx.eq. count_RH/count_ref
    float TH = get_Tntc2(gpx);
    float Tc = get_Temp(gpx);
    float rh = (cRHc55-0.8955)/0.002; // UPSI linear transfer function
    // temperature compensation
    float T0 = 0.0, T1 = -30.0; // T/C
    float T = Tc; // TH, TH-Tc (sensorT - T)
    if (T < T0) rh += T0 - T/5.5;        // approx/empirical
    if (T < T1) rh *= 1.0 + (T1-T)/75.0; // approx/empirical
    if (rh < 0.0) rh = 0.0;
    if (rh > 100.0) rh = 100.0;
    return rh;
}

static float get_RH(gpx_t *gpx) {
    //ui32_t TBCREF_1000 = frame_bytes[0x32] | (frame_bytes[0x33]<<8) | (frame_bytes[0x34]<<16); // CalRef 55%RH , T=20C ?
    //ui32_t TBCCR1_1000 = frame_bytes[0x35] | (frame_bytes[0x36]<<8) | (frame_bytes[0x37]<<16); // FrqCnt TLC555
    //float  cRHc55 = TBCCR1_1000 / (float)TBCREF_1000; // CalRef 55%RH , T=20C ?
    float  cRHc55 = get_count_RH(gpx) / get_count_55(gpx); // CalRef 55%RH , T=20C ?
    return cRHc55_RH(gpx, cRHc55);
}

/*
static float get_C_RH() {  // TLC555 astable: R_A=3.65k, R_B=338k
    double R_B = 338e3;
    double R_A = 3.65e3;
    double C_RH = 1/get_TLC555freq() / (LN2 * (R_A + 2*R_B));
    return C_RH;
}
*/

// Battery Voltage
// M10 batteries: 4xAA
static double get_BatV(gpx_t *gpx) {
    float  batV = 0.0;
    ui32_t batADC = 0;

    // ADC12_A5/4, V_R+=2.5V : 4096/4
    // 0..1023 <-> 0V .. 2.5V
    batADC = (gpx->frame_bytes[0x46] << 8) | gpx->frame_bytes[0x45];
    // R1=[06D]=113kOhm
    // R2=[30D]=200kOhm
    // f=(R1+R2)/R2=2.77

    //batV = 6.62*batADC/1000.0;
    batV = 2.709 * batADC*2.5/1023.0;

    //fprintf(stdout, "  (bat0:%d/1023=%.2f)", batADC, batADC/1023.0);

    return batV;
}

/* -------------------------------------------------------------------------- */

static int print_pos(gpx_t *gpx, int csOK) {
    int err, err2;

    err = 0;

    if (gpx->type == t_M10) {
        err |= get_GPSweek(gpx);
        err |= get_GPStime(gpx);
        err |= get_GPSlat(gpx);
        err |= get_GPSlon(gpx);
        err |= get_GPSalt(gpx);
        err2 = get_GPSvel(gpx);
    }
    else if (gpx->type == t_M10plus) {
        err |= get_gtopGPStime(gpx);
        err |= get_gtopGPSdate(gpx);
        err |= get_gtopGPSpos(gpx);
        err2 = get_gtopGPSvel(gpx);
    }
    else err = 0xFF;

    if (!err) {

        if (gpx->type == t_M10) {
            Gps2Date(gpx->week, gpx->gpssec, &gpx->jahr, &gpx->monat, &gpx->tag);
        }
        gpx->T    = get_Temp(gpx);
        gpx->_RH  = get_RH(gpx);
        gpx->Ti   = get_intTemp(gpx);
        gpx->batV = get_BatV(gpx);
        get_SN(gpx);


        if ( !gpx->option.slt )
        {
            if (gpx->option.col) {
                fprintf(stdout, col_TXT);
                if (gpx->type == t_M10)
                {
                    if (gpx->option.vbs >= 3) fprintf(stdout, " (W "col_GPSweek"%d"col_TXT") ", gpx->week);
                    fprintf(stdout, col_GPSTOW"%s"col_TXT" ", weekday[gpx->wday]);
                }
                fprintf(stdout, col_GPSdate"%04d-%02d-%02d"col_TXT" "col_GPSTOW"%02d:%02d:%06.3f"col_TXT" ",
                        gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek);
                fprintf(stdout, " lat: "col_GPSlat"%.5f"col_TXT" ", gpx->lat);
                fprintf(stdout, " lon: "col_GPSlon"%.5f"col_TXT" ", gpx->lon);
                fprintf(stdout, " alt: "col_GPSalt"%.2f"col_TXT" ", gpx->alt);
                if (!err2) {
                    //if (gpx->option.vbs == 2) fprintf(stdout, "  "col_GPSvel"(%.1f , %.1f : %.1f)"col_TXT" ", gpx->vx, gpx->vy, gpx->vD2);
                    fprintf(stdout, "  vH: "col_GPSvel"%.1f"col_TXT"  D: "col_GPSvel"%.1f"col_TXT"  vV: "col_GPSvel"%.1f"col_TXT" ", gpx->vH, gpx->vD, gpx->vV);
                }
                if (gpx->option.vbs >= 2) {
                    fprintf(stdout, "  SN: "col_SN"%s"col_TXT, gpx->SN);
                }
                if (gpx->option.vbs >= 2) {
                    fprintf(stdout, "  # ");
                    if (csOK) fprintf(stdout, " "col_CSok"[OK]"col_TXT);
                    else      fprintf(stdout, " "col_CSno"[NO]"col_TXT);
                }
                if (gpx->option.ptu && csOK) {
                    if (gpx->T > -270.0) fprintf(stdout, "  T=%.1fC", gpx->T);
                    if (gpx->option.vbs >= 2) { if (gpx->_RH > -0.5) fprintf(stdout, " _RH=%.0f%%", gpx->_RH); }
                    if (gpx->option.vbs >= 3) {
                        float t2 = get_Tntc2(gpx);
                        float fq555 = get_TLC555freq(gpx);
                        fprintf(stdout, "  (Ti:%.1fC)", gpx->Ti);
                        if (t2 > -270.0) fprintf(stdout, " (T2:%.1fC) (%.3fkHz)", t2, fq555/1e3);
                    }
                }
                if (gpx->option.vbs >= 3 && csOK) {
                    fprintf(stdout, " (bat:%.2fV)", gpx->batV);
                }
                fprintf(stdout, ANSI_COLOR_RESET"");
            }
            else {
                if (gpx->type == t_M10)
                {
                    if (gpx->option.vbs >= 3) fprintf(stdout, " (W %d) ", gpx->week);
                    fprintf(stdout, "%s ", weekday[gpx->wday]);
                }
                fprintf(stdout, "%04d-%02d-%02d %02d:%02d:%06.3f ",
                        gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek);
                fprintf(stdout, " lat: %.5f ", gpx->lat);
                fprintf(stdout, " lon: %.5f ", gpx->lon);
                fprintf(stdout, " alt: %.2f ", gpx->alt);
                if (!err2) {
                    //if (gpx->option.vbs == 2) fprintf(stdout, "  (%.1f , %.1f : %.1f) ", gpx->vx, gpx->vy, gpx->vD2);
                    fprintf(stdout, "  vH: %.1f  D: %.1f  vV: %.1f ", gpx->vH, gpx->vD, gpx->vV);
                }
                if (gpx->option.vbs >= 2) {
                    fprintf(stdout, "  SN: %s", gpx->SN);
                }
                if (gpx->option.vbs >= 2) {
                    fprintf(stdout, "  # ");
                    if (csOK) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
                }
                if (gpx->option.ptu && csOK) {
                    if (gpx->T > -270.0) fprintf(stdout, "  T=%.1fC", gpx->T);
                    if (gpx->option.vbs >= 2) { if (gpx->_RH > -0.5) fprintf(stdout, " _RH=%.0f%%", gpx->_RH); }
                    if (gpx->option.vbs >= 3) {
                        float t2 = get_Tntc2(gpx);
                        float fq555 = get_TLC555freq(gpx);
                        fprintf(stdout, "  (Ti:%.1fC)", gpx->Ti);
                        if (t2 > -270.0) fprintf(stdout, " (T2:%.1fC) (%.3fkHz)", t2, fq555/1e3);
                    }
                }
                if (gpx->option.vbs >= 3 && csOK) {
                    fprintf(stdout, " (bat:%.2fV)", gpx->batV);
                }
            }
            fprintf(stdout, "\n");
        }


        if (gpx->option.jsn) {
            // Print out telemetry data as JSON
            if (csOK) {
                char *ver_jsn = NULL;
                int j;
                char sn_id[4+12] = "M10-";
                ui8_t aprs_id[4];
                double sec_gps0 = (double)gpx->week*SECONDS_IN_WEEK + gpx->tow_ms/1e3;
                // UTC = GPS - UTC_OFS  (ab 1.1.2017: UTC_OFS=18sec)
                int utc_s = gpx->gpssec - gpx->utc_ofs;
                int utc_week = gpx->week;
                int utc_jahr; int utc_monat; int utc_tag;
                int utc_std; int utc_min; float utc_sek;
                if (utc_s < 0) {
                    utc_week -= 1;
                    utc_s += 604800; // 604800sec = 1week
                }
                if (gpx->type == t_M10) {
                    Gps2Date(utc_week, utc_s, &utc_jahr, &utc_monat, &utc_tag);
                    utc_s  %= (24*3600); // 86400sec = 1day
                    utc_std =  utc_s/3600;
                    utc_min = (utc_s%3600)/60;
                    utc_sek =  utc_s%60 + (gpx->tow_ms % 1000)/1000.0;
                }
                else {
                    utc_jahr  = gpx->jahr;
                    utc_monat = gpx->monat;
                    utc_tag   = gpx->tag;
                    utc_std = gpx->std;
                    utc_min = gpx->min;
                    utc_sek = gpx->sek;
                }

                strncpy(sn_id+4, gpx->SN, 12);
                sn_id[15] = '\0';
                for (j = 0; sn_id[j]; j++) { if (sn_id[j] == ' ') sn_id[j] = '-'; }

                fprintf(stdout, "{ \"type\": \"%s\"", "M10");
                fprintf(stdout, ", \"frame\": %lu, ", (unsigned long)(sec_gps0+0.5));
                fprintf(stdout, "\"id\": \"%s\", \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f",
                               sn_id, utc_jahr, utc_monat, utc_tag, utc_std, utc_min, utc_sek, gpx->lat, gpx->lon, gpx->alt, gpx->vH, gpx->vD, gpx->vV);
                if (gpx->type == t_M10) {
                    fprintf(stdout, ", \"sats\": %d", gpx->numSV);
                }
                // APRS id, 9 characters
                aprs_id[0] = gpx->frame_bytes[pos_SN+2];
                aprs_id[1] = gpx->frame_bytes[pos_SN] & 0xF;
                aprs_id[2] = gpx->frame_bytes[pos_SN+4];
                aprs_id[3] = gpx->frame_bytes[pos_SN+3];
                fprintf(stdout, ", \"aprsid\": \"ME%02X%1X%02X%02X\"", aprs_id[0], aprs_id[1], aprs_id[2], aprs_id[3]);
                fprintf(stdout, ", \"batt\": %.2f", gpx->batV);
                // temperature (and humidity)
                if (gpx->option.ptu) {
                    if (gpx->T > -273.0) fprintf(stdout, ", \"temp\": %.1f", gpx->T);
                    if (gpx->option.vbs >= 2) {
                        if (gpx->_RH > -0.5) fprintf(stdout, ", \"humidity\": %.1f", gpx->_RH);
                    }
                }
                fprintf(stdout, ", \"rawid\": \"M10_%02X%02X%02X%02X%02X\"", gpx->frame_bytes[pos_SN], gpx->frame_bytes[pos_SN+1],
                                               gpx->frame_bytes[pos_SN+2], gpx->frame_bytes[pos_SN+3], gpx->frame_bytes[pos_SN+4]); // gpx->type
                fprintf(stdout, ", \"subtype\": \"0x%02X\"", gpx->type);
                if (gpx->jsn_freq > 0) {
                    fprintf(stdout, ", \"freq\": %d", gpx->jsn_freq);
                }

                // Reference time/position       (M10 time ref UTC only for json)
                fprintf(stdout, ", \"ref_datetime\": \"%s\"", "UTC" ); // {"GPS", "UTC"} GPS-UTC=leap_sec
                fprintf(stdout, ", \"ref_position\": \"%s\"", "GPS" ); // {"GPS", "MSL"} GPS=ellipsoid , MSL=geoid
                fprintf(stdout, ", \"gpsutc_leapsec\": %d", gpx->utc_ofs); // GPS-UTC offset, utc_s = gpx->gpssec - gpx->utc_ofs;

                #ifdef VER_JSN_STR
                    ver_jsn = VER_JSN_STR;
                #endif
                if (ver_jsn && *ver_jsn != '\0') fprintf(stdout, ", \"version\": \"%s\"", ver_jsn);
                fprintf(stdout, " }\n");
                fprintf(stdout, "\n");
            }
        }

    }

    return err;
}

static int print_frame(gpx_t *gpx, int pos, int b2B) {
    int i;
    ui8_t byte;
    int cs1, cs2;
    int flen = stdFLEN; // stdFLEN=0x64, auxFLEN=0x76

    if (b2B) {
        bits2bytes(gpx->frame_bits, gpx->frame_bytes);
    }
    flen = gpx->frame_bytes[0];
    if (flen == stdFLEN) gpx->auxlen = 0;
    else {
        gpx->auxlen = flen - stdFLEN;
        if (gpx->auxlen < 0 || gpx->auxlen > AUX_LEN) gpx->auxlen = 0;
    }

    cs1 = (gpx->frame_bytes[pos_Check+gpx->auxlen] << 8) | gpx->frame_bytes[pos_Check+gpx->auxlen+1];
    cs2 = checkM10(gpx->frame_bytes, pos_Check+gpx->auxlen);

    switch (gpx->frame_bytes[1]) {
        case 0x8F: gpx->type = t_M2K2;    break;
        case 0x9F: gpx->type = t_M10;     break;
        case 0xAF: gpx->type = t_M10plus; break;
        case 0x20: gpx->type = t_M20;     break;
        default  : gpx->type = t_M10;
    }

    if (gpx->option.raw) {

        if (gpx->option.col  &&  gpx->frame_bytes[1] != 0x49 && (gpx->type == t_M10 || gpx->type == t_M10plus)) {
            fprintf(stdout, col_FRTXT);
            for (i = 0; i < FRAME_LEN+gpx->auxlen; i++) {
                byte = gpx->frame_bytes[i];
                if (i == 1) fprintf(stdout, col_Mtype);
                if (gpx->type == t_M10) {
                    if ((i >= pos_GPSTOW)   &&  (i < pos_GPSTOW+4))   fprintf(stdout, col_GPSTOW);
                    if ((i >= pos_GPSlat)   &&  (i < pos_GPSlat+4))   fprintf(stdout, col_GPSlat);
                    if ((i >= pos_GPSlon)   &&  (i < pos_GPSlon+4))   fprintf(stdout, col_GPSlon);
                    if ((i >= pos_GPSalt)   &&  (i < pos_GPSalt+4))   fprintf(stdout, col_GPSalt);
                    if ((i >= pos_GPSweek)  &&  (i < pos_GPSweek+2))  fprintf(stdout, col_GPSweek);
                    if ((i >= pos_GPSvE)    &&  (i < pos_GPSvE+6))    fprintf(stdout, col_GPSvel);
                }
                else {
                    if ((i >= pos_gtopGPSlat)   &&  (i < pos_gtopGPSlat+4))   fprintf(stdout, col_GPSlat);
                    if ((i >= pos_gtopGPSlon)   &&  (i < pos_gtopGPSlon+4))   fprintf(stdout, col_GPSlon);
                    if ((i >= pos_gtopGPSalt)   &&  (i < pos_gtopGPSalt+3))   fprintf(stdout, col_GPSalt);
                    if ((i >= pos_gtopGPSvE)    &&  (i < pos_gtopGPSvE+6))    fprintf(stdout, col_GPSvel);
                    if ((i >= pos_gtopGPStime)  &&  (i < pos_gtopGPStime+3))  fprintf(stdout, col_GPSTOW);
                    if ((i >= pos_gtopGPSdate)  &&  (i < pos_gtopGPSdate+3))  fprintf(stdout, col_GPSweek);
                }
                if ((i >= pos_SN) && (i < pos_SN+5))  fprintf(stdout, col_SN);
                if (i == pos_CNT) fprintf(stdout, col_CNT);
                if ((i >= pos_Check+gpx->auxlen) && (i < pos_Check+gpx->auxlen+2))  fprintf(stdout, col_Check);
                fprintf(stdout, "%02x", byte);
                fprintf(stdout, col_FRTXT);
            }
            if (gpx->option.vbs) {
                fprintf(stdout, " # "col_Check"%04x"col_FRTXT, cs2);
                if (cs1 == cs2) fprintf(stdout, " "col_CSok"[OK]"col_TXT);
                else            fprintf(stdout, " "col_CSno"[NO]"col_TXT);
            }
            fprintf(stdout, ANSI_COLOR_RESET"\n");
        }
        else {
            for (i = 0; i < FRAME_LEN+gpx->auxlen; i++) {
                byte = gpx->frame_bytes[i];
                fprintf(stdout, "%02x", byte);
            }
            if (gpx->option.vbs) {
                fprintf(stdout, " # %04x", cs2);
                if (cs1 == cs2) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
            }
            fprintf(stdout, "\n");
        }
        if (gpx->frame_bytes[1] != 0x49 && gpx->option.slt /*&& gpx->option.jsn*/) {
            print_pos(gpx, cs1 == cs2);
        }
    }
    else if (gpx->frame_bytes[1] == 0x49) {
        if (gpx->option.vbs == 3) {
            for (i = 0; i < FRAME_LEN+gpx->auxlen; i++) {
                byte = gpx->frame_bytes[i];
                fprintf(stdout, "%02x", byte);
            }
            if (cs1 == cs2) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
            fprintf(stdout, "\n");
        }
    }
    else print_pos(gpx, cs1 == cs2);

    return (gpx->frame_bytes[0]<<8)|gpx->frame_bytes[1];
}

/* -------------------------------------------------------------------------- */


int main(int argc, char **argv) {

    //int option_res = 0;      // genauere Bitmessung
    int option_min = 0;
    int option_iq = 0;
    int option_iqdc = 0;
    int option_lp = 0;
    int option_dc = 0;
    int option_noLUT = 0;
    int option_chk = 0;
    int option_softin = 0;
    int option_pcmraw = 0;
    int wavloaded = 0;
    int sel_wavch = 0;     // audio channel: left
    int spike = 0;
    int rawhex = 0;
    int cfreq = -1;

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

    float lpIQ_bw = 24e3;

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
            gpx.option.vbs = 1;
        }
        else if ( (strcmp(*argv, "-vv" ) == 0) ) gpx.option.vbs = 2;
        else if ( (strcmp(*argv, "-vvv") == 0) ) gpx.option.vbs = 3;
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            gpx.option.raw = 1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            gpx.option.inv = 1;  // nicht noetig
        }
        else if ( (strcmp(*argv, "-c") == 0) || (strcmp(*argv, "--color") == 0) ) {
            gpx.option.col = 1;
        }
        //else if   (strcmp(*argv, "--res") == 0) { option_res = 1; }
        else if ( (strcmp(*argv, "--ptu") == 0) ) {
            gpx.option.ptu = 1;
        }
        else if ( (strcmp(*argv, "--spike") == 0) ) {
            spike = 1;
        }
        else if   (strcmp(*argv, "--chk3") == 0) { option_chk = 3; }
        else if   (strcmp(*argv, "--ch2") == 0) { sel_wavch = 1; }  // right channel (default: 0=left)
        else if   (strcmp(*argv, "--softin") == 0)  { option_softin = 1; }  // float32 soft input
        else if   (strcmp(*argv, "--softinv") == 0) { option_softin = 2; }  // float32 inverted soft input
        else if   (strcmp(*argv, "--silent") == 0) { gpx.option.slt = 1; }
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
        else if   (strcmp(*argv, "--json") == 0) { gpx.option.jsn = 1; }
        else if   (strcmp(*argv, "--jsn_cfq") == 0) {
            int frq = -1;  // center frequency / Hz
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
        }
        else if (strcmp(*argv, "--rawhex") == 0) { rawhex = 2; }  // raw hex input
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
    // LUT recommonded if decM > 2
    //
    if (option_noLUT && option_iq == 5) dsp.opt_nolut = 1; else dsp.opt_nolut = 0;


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
            dsp.BT = 1.8; // bw/time (ISI) // 1.0..2.0
            dsp.h = 0.9;  // 1.2 modulation index
            dsp.opt_iq = option_iq;
            dsp.opt_iqdc = option_iqdc;
            dsp.opt_lp = option_lp;
            dsp.lpIQ_bw = lpIQ_bw; //24e3; // IF lowpass bandwidth
            dsp.lpFM_bw = 10e3; // FM audio lowpass
            dsp.opt_dc = option_dc;
            dsp.opt_IFmin = option_min;

            if ( dsp.sps < 8 ) {
                fprintf(stderr, "note: sample rate low (%.1f sps)\n", dsp.sps);
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
                pos /= 2;
                bit0 = '0'; // oder: _mv[j] > 0

                while ( pos < BITFRAME_LEN+BITAUX_LEN ) {

                    if (option_softin) {
                        float s1 = 0.0;
                        float s2 = 0.0;
                        float s = 0.0;
                        bitQ = f32soft_read(fp, &s1, option_softin == 2);
                        if (bitQ != EOF) {
                            bitQ = f32soft_read(fp, &s2, option_softin == 2);
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
                        if (option_chk == 3 && option_iq) {
                        //if (hsbit.sb*hsbit1.sb < 0)
                            bit = (hsbit.sb+0.25*hsbit1.sb)>=0;
                        }
                    }
                    if ( bitQ == EOF ) { break; }

                    gpx.frame_bits[pos] = 0x31 ^ (bit0 ^ bit);
                    pos++;
                    bit0 = bit;
                    bitpos += 1;
                }
                gpx.frame_bits[pos] = '\0';
                print_frame(&gpx, pos, 1);
                if (pos < BITFRAME_LEN) break;

                header_found = 0;

                // bis Ende der Sekunde vorspulen; allerdings Doppel-Frame alle 10 sek
                if (gpx.option.vbs < 3) { // && (regulare frame) // print_frame-return?
                    while ( bitpos < 5*BITFRAME_LEN ) {
                        if (option_softin) {
                            float s = 0.0;
                            bitQ = f32soft_read(fp, &s, option_softin == 2);
                        }
                        else {
                            bitQ = read_slbit(&dsp, &bit, 0, bitofs, bitpos, -1, 0); // symlen=2
                        }
                        if (bitQ == EOF) break;
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
    }
    else //if (rawhex)
    {
        char buffer_rawhex[2*(FRAME_LEN+AUX_LEN)+12];
        char *pbuf = NULL, *buf_sp = NULL;
        ui8_t frmbyte;
        int frameofs = 0, len, i;

        while (1 > 0) {

            memset(buffer_rawhex, 0, 2*(FRAME_LEN+AUX_LEN)+12);
            pbuf = fgets(buffer_rawhex, 2*(FRAME_LEN+AUX_LEN)+12, fp);
            if (pbuf == NULL) break;
            buffer_rawhex[2*(FRAME_LEN+AUX_LEN)] = '\0';
            buf_sp = strchr(buffer_rawhex, ' ');
            if (buf_sp != NULL && buf_sp-buffer_rawhex < 2*(FRAME_LEN+AUX_LEN)) {
                buffer_rawhex[buf_sp-buffer_rawhex] = '\0';
                for (i = buf_sp-buffer_rawhex+1; i < 2*(FRAME_LEN+AUX_LEN); i++) buffer_rawhex[i] = '\0';
            }
            len = strlen(buffer_rawhex) / 2;
            if (len > pos_GPSweek+2) {
                for (i = 0; i < len; i++) { //%2x  SCNx8=%hhx(inttypes.h)
                    sscanf(buffer_rawhex+2*i, "%2hhx", &frmbyte);
                    // wenn ohne %hhx: sscanf(buffer_rawhex+rawhex*i, "%2x", &byte); frame[frameofs+i] = (ui8_t)byte;
                    gpx.frame_bytes[frameofs+i] = frmbyte;
                }
                print_frame(&gpx, len*8, 0);
            }
        }
    }

    fclose(fp);

    return 0;
}

