/* 
 * File:   M10GTop.cpp
 * Author: Viproz
 * Used code from rs1729
 * Created on December 13, 2018, 4:39 PM
 */

/*
#define stdFLEN        0x64  // pos[0]=0x64
#define pos_GPSlat     0x04  // 4 byte
#define pos_GPSlon     0x08  // 4 byte
#define pos_GPSalt     0x0C  // 3 byte
#define pos_GPSvE      0x0F  // 2 byte
#define pos_GPSvN      0x11  // 2 byte
#define pos_GPSvU      0x13  // 2 byte
#define pos_GPStime    0x15  // 3 byte
#define pos_GPSdate    0x18  // 3 byte

#define pos_SN         0x5D  // 2+3 byte
#define pos_Check     (stdFLEN-1)  // 2 byte*/

#include "M10GTopParser.h"
#include <ctime>

M10GTopParser::M10GTopParser() {
}

M10GTopParser::~M10GTopParser() {
}

void M10GTopParser::changeData(std::array<unsigned char, DATA_LENGTH> data, bool good) {
    correctCRC = good;
    frame_bytes = data;

    int i;
    unsigned short bytes[4];
    int val;

    for (i = 0; i < 3; i++) bytes[i] = frame_bytes[0x15 + i];
    val = 0;
    for (i = 0; i < 3; i++) val |= bytes[i] << (8 * (2 - i));
    time = val;

    for (i = 0; i < 3; i++) bytes[i] = frame_bytes[0x18 + i];
    val = 0;
    for (i = 0; i < 3; i++) val |= bytes[i] << (8 * (2 - i));
    date = val;
}

double M10GTopParser::getLatitude() {
    int i;
    unsigned short bytes[4];
    int val;

    for (i = 0; i < 4; i++)
        bytes[i] = frame_bytes[0x04 + i];
    val = 0;
    for (i = 0; i < 4; i++)
        val |= bytes[i] << (8 * (3 - i));
    return val / 1e6;
}

double M10GTopParser::getLongitude() {
    int i;
    unsigned short bytes[4];
    int val;

    for (i = 0; i < 4; i++)
        bytes[i] = frame_bytes[0x08 + i];
    val = 0;
    for (i = 0; i < 4; i++)
        val |= bytes[i] << (8 * (3 - i));
    return val / 1e6;
}

double M10GTopParser::getAltitude() {
    int i;
    unsigned short bytes[4];
    int val;

    for (i = 0; i < 3; i++)
        bytes[i] = frame_bytes[0x0C + i];
    val = 0;
    for (i = 0; i < 3; i++)
        val |= bytes[i] << (8 * (2 - i));
    if (val & 0x800000) val -= 0x1000000; // alt: signed 24bit?
    return val / 1e2;
}

int M10GTopParser::getDay() {
    return date / 10000;
}

int M10GTopParser::getMonth() {
    return (date % 10000) / 100;
}

int M10GTopParser::getYear() {
    return date % 100 + 2000;
}

int M10GTopParser::getHours() {
    return time / 10000;
}

int M10GTopParser::getMinutes() {
    return (time % 10000) / 100;
}

int M10GTopParser::getSeconds() {
    return time % 100;
}

double M10GTopParser::getVerticalSpeed() {
    int i;
    unsigned short bytes[2];
    short vel16;

    for (i = 0; i < 2; i++)
        bytes[i] = frame_bytes[0x13 + i];
    vel16 = bytes[0] << 8 | bytes[1];
    return vel16 / 1e2; // up
}

double M10GTopParser::getHorizontalSpeed() {
    int i;
    unsigned short bytes[2];
    short vel16;
    double vx, vy;

    for (i = 0; i < 2; i++)
        bytes[i] = frame_bytes[0x0F + i];
    vel16 = bytes[0] << 8 | bytes[1];
    vx = vel16 / 1e2; // east

    for (i = 0; i < 2; i++)
        bytes[i] = frame_bytes[0x11 + i];
    vel16 = bytes[0] << 8 | bytes[1];
    vy = vel16 / 1e2; // north

    return sqrt(vx * vx + vy * vy);
}

double M10GTopParser::getDirection() {
    int i;
    unsigned short bytes[2];
    short vel16;
    double vx, vy, dir;

    for (i = 0; i < 2; i++)
        bytes[i] = frame_bytes[0x0F + i];
    vel16 = bytes[0] << 8 | bytes[1];
    vx = vel16 / 1e2; // east

    for (i = 0; i < 2; i++)
        bytes[i] = frame_bytes[0x11 + i];
    vel16 = bytes[0] << 8 | bytes[1];
    vy = vel16 / 1e2; // north

    return sqrt(vx * vx + vy * vy);
    dir = atan2(vx, vy) * 180 / M_PI;
    if (dir < 0) dir += 360;
    return dir;
}

double M10GTopParser::getTemperature() {
    return 0;
}

double M10GTopParser::getHumidity() {
    return 0;
}

double M10GTopParser::getDp() {
    return 0;
}

std::string M10GTopParser::getSerialNumber() {
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

void M10GTopParser::printFrame() {
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
            "\"crc\": %d, "
            "}\n",
            "GTop", frame, getSerialNumber().c_str(), getYear(), getMonth(), getDay(), getHours(), getMinutes(), getSeconds(), getLatitude(), getLongitude(),
            getAltitude(), getHorizontalSpeed(), getDirection(), getVerticalSpeed()/*, getTemperature()*/, correctCRC);
}
