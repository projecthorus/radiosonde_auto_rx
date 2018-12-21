/* 
 * File:   M10GTop.cpp
 * Author: Viproz
 * Used code from rs1729
 * Created on December 13, 2018, 4:39 PM
 */

/*
#define stdFLEN        0x64  // pos[0]=0x64
#define pos_GPSTOW     0x0A  // 4 byte
#define pos_GPSlat     0x0E  // 4 byte
#define pos_GPSlon     0x12  // 4 byte
#define pos_GPSalt     0x16  // 4 byte
#define pos_GPSweek    0x20  // 2 byte
//Velocity East-North-Up (ENU)
#define pos_GPSvE      0x04  // 2 byte
#define pos_GPSvN      0x06  // 2 byte
#define pos_GPSvU      0x08  // 2 byte
#define pos_SN         0x5D  // 2+3 byte
#define pos_Check     (stdFLEN-1)  // 2 byte*/

#include "M10PtuParser.h"

M10PtuParser::M10PtuParser() {
}

M10PtuParser::~M10PtuParser() {
}

void M10PtuParser::changeData(std::array<unsigned char, DATA_LENGTH> data, bool good) {
    correctCRC = good;
    frame_bytes = data;

    int i;
    unsigned byte;
    unsigned short gpsweek_bytes[2];

    //Number of weeks
    for (i = 0; i < 2; i++) {
        byte = frame_bytes[0x20 + i];
        gpsweek_bytes[i] = byte;
    }

    week = (gpsweek_bytes[0] << 8) + gpsweek_bytes[1];

    //Time in ms
    unsigned short gpstime_bytes[4];

    for (i = 0; i < 4; i++) {
        byte = frame_bytes[0x0A + i];
        gpstime_bytes[i] = byte;
    }

    time = 0;
    for (i = 0; i < 4; i++) {
        time |= gpstime_bytes[i] << (8 * (3 - i));
    }

    gps2Date(week, time / 1000 % (24 * 3600), &year, &month, &day);
}

double M10PtuParser::getLatitude() {
    int i;
    unsigned byte;
    unsigned short gpslat_bytes[4];
    int gpslat;
    double B60B60 = 0xB60B60;

    for (i = 0; i < 4; i++) {
        byte = frame_bytes[0x0E + i];
        gpslat_bytes[i] = byte;
    }

    gpslat = 0;
    for (i = 0; i < 4; i++) {
        gpslat |= gpslat_bytes[i] << (8 * (3 - i));
    }
    return gpslat / B60B60;
}

double M10PtuParser::getLongitude() {
    int i;
    unsigned byte;
    unsigned short gpslon_bytes[4];
    int gpslon;
    double B60B60 = 0xB60B60;

    for (i = 0; i < 4; i++) {
        byte = frame_bytes[0x12 + i];
        gpslon_bytes[i] = byte;
    }

    gpslon = 0;
    for (i = 0; i < 4; i++) {
        gpslon |= gpslon_bytes[i] << (8 * (3 - i));
    }
    return gpslon / B60B60;
}

double M10PtuParser::getAltitude() {
    int i;
    unsigned byte;
    unsigned short gpsalt_bytes[4];
    int gpsalt;

    for (i = 0; i < 4; i++) {
        byte = frame_bytes[0x16 + i];
        gpsalt_bytes[i] = byte;
    }

    gpsalt = 0;
    for (i = 0; i < 4; i++) {
        gpsalt |= gpsalt_bytes[i] << (8 * (3 - i));
    }
    return gpsalt / 1000.0;
}

int M10PtuParser::getDay() {
    return day;
}

int M10PtuParser::getMonth() {
    return month;
}

int M10PtuParser::getYear() {
    return year;
}

int M10PtuParser::getHours() {
    return (time / 1000 % (24 * 3600)) / 3600;
}

int M10PtuParser::getMinutes() {
    return ((time / 1000 % (24 * 3600)) % 3600) / 60;
}

int M10PtuParser::getSeconds() {
    return (time / 1000 % (24 * 3600)) % 60;
}

double M10PtuParser::getVerticalSpeed() {
    int i;
    unsigned byte;
    unsigned short gpsVel_bytes[2];
    short vel16;
    const double ms2kn100 = 2e2; // m/s -> knots: 1 m/s = 3.6/1.852 kn = 1.94 kn

    for (i = 0; i < 2; i++) {
        byte = frame_bytes[0x08 + i];
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    return vel16 / ms2kn100;
}

double M10PtuParser::getHorizontalSpeed() {
    int i;
    unsigned byte;
    unsigned short gpsVel_bytes[2];
    short vel16;
    double vx, vy;
    const double ms2kn100 = 2e2; // m/s -> knots: 1 m/s = 3.6/1.852 kn = 1.94 kn

    for (i = 0; i < 2; i++) {
        byte = frame_bytes[0x04 + i];
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    vx = vel16 / ms2kn100; // est

    for (i = 0; i < 2; i++) {
        byte = frame_bytes[0x06 + i];
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    vy = vel16 / ms2kn100; // nord

    return sqrt(vx * vx + vy * vy);
}

double M10PtuParser::getDirection() {
    int i;
    unsigned byte;
    unsigned short gpsVel_bytes[2];
    short vel16;
    double vx, vy, dir;
    const double ms2kn100 = 2e2; // m/s -> knots: 1 m/s = 3.6/1.852 kn = 1.94 kn

    for (i = 0; i < 2; i++) {
        byte = frame_bytes[0x04 + i];
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    vx = vel16 / ms2kn100; // est

    for (i = 0; i < 2; i++) {
        byte = frame_bytes[0x06 + i];
        gpsVel_bytes[i] = byte;
    }
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    vy = vel16 / ms2kn100; // nord

    ///*
    dir = atan2(vx, vy)*180 / M_PI;
    if (dir < 0) dir += 360;
    return dir;
}

double M10PtuParser::getTemperature() {
    return 0;
}

double M10PtuParser::getHumidity() {
    return 0;
}

double M10PtuParser::getDp() {
    return 0;
}

std::string M10PtuParser::getSerialNumber() {
    int i;
    unsigned byte;
    unsigned short sn_bytes[5];
    char SN[12];

    for (i = 0; i < 11; i++)
        SN[i] = ' ';
    SN[11] = '\0';

    for (i = 0; i < 5; i++) {
        byte = frame_bytes[0x5D + i];
        sn_bytes[i] = byte;
    }

    byte = sn_bytes[2];
    sprintf(SN, "%1X%02u", (byte >> 4)&0xF, byte & 0xF);
    byte = sn_bytes[3] | (sn_bytes[4] << 8);
    sprintf(SN + 3, " %1X %1u%04u", sn_bytes[0]&0xF, (byte >> 13)&0x7, byte & 0x1FFF);

    return SN;
}

void M10PtuParser::printFrame() {
    setenv("TZ", "", 1); // Set local timezone to UTC
    time_t frame = 0;
    struct tm timeinfo;

    timeinfo.tm_hour = getHours();
    timeinfo.tm_min = getMinutes();
    timeinfo.tm_sec = getSeconds();
    timeinfo.tm_mday = getDay();
    timeinfo.tm_mon = getMonth() - 1;
    timeinfo.tm_year = getYear() - 1900;
    timeinfo.tm_isdst = 0;

    frame = mktime(&timeinfo);

    printf("{ "
            "\"sub_type\": \"%s\", "
            "\"frame\": %ld, "
            "\"id\": \"%s\", "
            "\"datetime\": \"%04d-%02d-%02dT%02d:%02d:%02dZ\", "
            "\"lat\": %.5f, "
            "\"lon\": %.5f, "
            "\"alt\": %.2f, "
            "\"vel_h\": %.5f, "
            "\"heading\": %.5f, "
            "\"vel_v\": %.2f, "
            //"\"temp\": %.1f "
            "\"crc\": %d "
            "}\n",
            "Ptu", frame, getSerialNumber().c_str(), getYear(), getMonth(), getDay(), getHours(), getMinutes(), getSeconds(), getLatitude(), getLongitude(),
            getAltitude(), getHorizontalSpeed(), getDirection(), getVerticalSpeed()/*, getTemperature()*/, correctCRC);
}

/*
 * Convert GPS Week and Seconds to Modified Julian Day.
 * - Adapted from sci.astro FAQ.
 * - Ignores UTC leap seconds.
 */
void M10PtuParser::gps2Date(long GpsWeek, long GpsSeconds, int *Year, int *Month, int *Day) {

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