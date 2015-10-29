
/*
  (precise)   ftp://ftp.nga.mil/pub2/gps/pedata/2015pe/  
  (broadcast) ftp://cddis.gsfc.nasa.gov/gnss/data/daily/2015/291/15n/
  (almanac)   https://celestrak.com/GPS/almanac/SEM/2015/
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;


#include "nav_gps.c"


int rollover = 0;

EPHEM_t alm[33];
EPHEM_t eph[33][24];

SAT_t sat_alm[33], sat_eph[33];


int calc_satpos_alm(EPHEM_t alm[], double t, SAT_t *sat) {
    double X, Y, Z;
    int j;
    int week;
    double cl_corr;

    for (j = 1; j < 33; j++) {
        if (alm[j].prn > 0) {  // prn==j

            // Woche hat 604800 sec
            if      (t-alm[j].toa >  604800/2) rollover = +1;
            else if (t-alm[j].toa < -604800/2) rollover = -1;
            else rollover = 0;
            week = alm[j].week - rollover;

            GPS_SatellitePosition_Ephem(
                week, t, alm[j],
                &cl_corr, &X, &Y, &Z
            );
            sat[alm[j].prn].X = X;
            sat[alm[j].prn].Y = Y;
            sat[alm[j].prn].Z = Z;
            sat[alm[j].prn].clock_corr = cl_corr;
        }
    }

    return 0;
}

int calc_satpos_rnx(EPHEM_t eph[][24], double t, SAT_t *sat) {

    double X, Y, Z;
    int j, i, ti;

    int week;

    double cl_corr;

    double tdiff, td;

    for (j = 1; j < 33; j++) {

        // Woche hat 604800 sec
        tdiff = 604800;
        ti = 0;
        for (i = 0; i < 24; i++) {
            if (eph[j][i].prn > 0) {
                if      (t-eph[j][i].toe >  604800/2) rollover = +1;
                else if (t-eph[j][i].toe < -604800/2) rollover = -1;
                else rollover = 0;
                td = t-eph[j][i].toe - rollover*604800;
                if (td < 0) td *= -1;

                if ( td < tdiff ) {
                    tdiff = td;
                    ti = i;
                    week = eph[j][ti].week - rollover;
                }
            }

        }
//printf("prn %02d: ti=%02d (%.1f)  td=%.0f\n", eph[j][ti].prn, ti, eph[j][ti].toe, tdiff);

        GPS_SatellitePosition_Ephem(
            week, t, eph[j][ti],
            &cl_corr, &X, &Y, &Z
        );
        sat[eph[j][ti].prn].X = X;
        sat[eph[j][ti].prn].Y = Y;
        sat[eph[j][ti].prn].Z = Z;
        sat[eph[j][ti].prn].clock_corr = cl_corr;

    }

    return 0;
}


// ftp://ftp.nga.mil/pub2/gps/pedata/2015pe/
// nga18670.Z
double precise201510180000[33][5] = {
{  0, 0, 0, 0, 0 },
{  1, -13396.503088,  16970.854595,  15216.330882,      2.582065 },
{  2,  14263.732177,   7790.652882, -20685.856689,    593.070368 },
{  3, -22205.915807,  13269.761877,  -6063.419651,     19.656736 },
{  4, -15544.326337,   7438.029201,  19792.978577,    -33.725265 },
{  5,  23627.784132,    106.951075, -12385.480993,   -184.039639 },
{  6,   8367.508834,  19482.783055, -16001.912082,     78.618819 },
{  7,  -5782.726114,  25630.989537,  -1813.496023,    482.644186 },
{  8, -19787.879341,    183.638881,  17754.688132,     -9.097048 },
{  9,  -1225.647544,  17186.903817, -20217.354165,     -1.371448 },
{ 10,      0.0     ,      0.0     ,      0.0     ,      0.0      },
{ 11, -12853.596170,  12708.409923,  18888.524237,   -610.793350 },
{ 12,  23820.802471, -10798.624959,  -4014.469346,    337.713996 },
{ 13,  21434.480084,  10098.136853,  11762.516964,   -157.494119 },
{ 14, -12538.553468, -21293.861893,  10189.913434,     27.512184 },
{ 15,  19603.561444,  -2247.081455,  17828.390890,   -277.380526 },
{ 16, -23375.226825,  -1510.183240, -12951.652156,    -75.480117 },
{ 17,  13408.271049,  20849.320095,  10129.984029,   -191.301863 },
{ 18,   5861.005813, -18014.778495,  19020.343167,    443.988849 },
{ 19,  -8773.575811,  12151.257210,  21698.223212,   -521.650415 },
{ 20,  23127.215192, -12950.317089,   2099.622700,    363.931244 },
{ 21,   1813.458929, -26280.230144,   2143.997113,   -487.414006 },
{ 22,  -8635.568795, -13798.497369,  21236.345578,    401.853309 },
{ 23, -14247.163389,   7838.405009, -20901.923478,   -139.410012 },
{ 24,  14174.752473, -14458.950599,  17069.494892,     -6.485548 },
{ 25,  14660.477315, -16439.358633, -14817.632013,    -53.973420 },
{ 26, -16096.651504,  -8238.053093, -19457.243197,    -87.913428 },
{ 27, -22869.968612, -10311.018093,   8885.997685,     28.370732 },
{ 28,   3626.507040,  14437.082373,  22625.094407,    475.137362 },
{ 29,   4371.948677, -15366.892825, -21211.068149,    640.063249 },
{ 30,    925.779039,  24924.887332,   9010.767707,     42.751461 },
{ 31,  -8154.144932, -18631.120730, -16764.082274,    298.816917 },
{ 32, -25500.564581,   4246.936140,   4608.443236,    -11.160977 }};


int main(int argc, char *argv[]) {

    FILE *fp_sem, *fp_eph;

    unsigned gpstime = 0;
    double lat, lon, h;
    int j;


    gpstime = 0; // passend zu den precise-Daten
    fp_sem = fopen("almanac.sem.week0843.061440.txt", "r");
    //fp_eph = fopen("hour2910.15n", "r");
    fp_eph = fopen("brdc2910.15n", "r");

    read_SEMalmanac(fp_sem, alm);
    calc_satpos_alm(alm, gpstime/1000.0, sat_alm);

    read_RNXephemeris(fp_eph, eph);
    calc_satpos_rnx(eph, gpstime/1000.0, sat_eph);

    printf("gpstime/s: %.1f\n", gpstime/1000.0);
    printf("\n");

    for (j = 1; j < 33; j++) {

        printf(" %2d:  ", j);
        printf("( %12.1f , %12.1f , %12.1f )   ", precise201510180000[j][1]*1000.0, precise201510180000[j][2]*1000.0, precise201510180000[j][3]*1000.0);
        ecef2elli(precise201510180000[j][1]*1000.0, precise201510180000[j][2]*1000.0, precise201510180000[j][3]*1000.0, &lat, &lon, &h);
        printf("  lat: %10.6f , lon: %11.6f , h: %11.2f\n", lat, lon, h);

        printf(" %2d:  ", j);
        printf("( %12.1f , %12.1f , %12.1f )   ", sat_eph[j].X, sat_eph[j].Y, sat_eph[j].Z);
        ecef2elli(sat_eph[j].X, sat_eph[j].Y, sat_eph[j].Z, &lat, &lon, &h);
        printf("  lat: %10.6f , lon: %11.6f , h: %11.2f", lat, lon, h);
        printf("  ;  delta = %.1f\n", dist(sat_eph[j].X, sat_eph[j].Y, sat_eph[j].Z, precise201510180000[j][1]*1000.0, precise201510180000[j][2]*1000.0, precise201510180000[j][3]*1000.0));

        printf(" %2d:  ", j);
        printf("( %12.1f , %12.1f , %12.1f )   ", sat_alm[j].X, sat_alm[j].Y, sat_alm[j].Z);
        ecef2elli(sat_alm[j].X, sat_alm[j].Y, sat_alm[j].Z, &lat, &lon, &h);
        printf("  lat: %10.6f , lon: %11.6f , h: %11.2f", lat, lon, h);
        printf("  ;  delta = %.1f\n", dist(sat_eph[j].X, sat_eph[j].Y, sat_eph[j].Z, sat_alm[j].X, sat_alm[j].Y, sat_alm[j].Z));

        printf("\n");
    }


    fclose(fp_sem);
    fclose(fp_eph);

    return 0;
}


