
#include "rs_data.h"
#include "rs_datum.h"

char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
//char weekday[7][3] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};

/*
 * Convert GPS Week and Seconds to Modified Julian Day.
 * - Adapted from sci.astro FAQ.
 * - Ignores UTC leap seconds.
 */
//void Gps2Date(long GpsWeek, long GpsSeconds, int *Year, int *Month, int *Day) {
void Gps2Date(rs_data_t *rs_data) {

    long GpsSeconds, GpsDays, Mjd;
    long J, C, Y, M;

    GpsSeconds = (rs_data->GPS).msec / 1000;
    GpsDays = (rs_data->GPS).week * 7 + (GpsSeconds / 86400);
    Mjd = 44244 + GpsDays;

    J = Mjd + 2468570;
    C = 4 * J / 146097;
    J = J - (146097 * C + 3) / 4;
    Y = 4000 * (J + 1) / 1461001;
    J = J - 1461 * Y / 4 + 31;
    M = 80 * J / 2447;
    rs_data->day = J - 2447 * M / 80;
    J = M / 11;
    rs_data->month = M + 2 - (12 * J);
    rs_data->year = 100 * (C - 49) + Y + J;
}


