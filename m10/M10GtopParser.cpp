/*
 * File:   M10Gtop.cpp
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

#include "M10GtopParser.h"
#include <ctime>
#include <string>
#include <sstream>
#include <iostream>

M10GtopParser::M10GtopParser() {
}

M10GtopParser::~M10GtopParser() {
}

void M10GtopParser::changeData(std::array<unsigned char, DATA_LENGTH> data, bool good) {
    M10GeneralParser::changeData(data, good);

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

double M10GtopParser::getLatitude() {
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

double M10GtopParser::getLongitude() {
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

double M10GtopParser::getAltitude() {
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

int M10GtopParser::getDay() {
    return date / 10000;
}

int M10GtopParser::getMonth() {
    return (date % 10000) / 100;
}

int M10GtopParser::getYear() {
    return date % 100 + 2000;
}

int M10GtopParser::getHours() {
    return time / 10000;
}

int M10GtopParser::getMinutes() {
    return (time % 10000) / 100;
}

int M10GtopParser::getSeconds() {
    return time % 100;
}

double M10GtopParser::getVerticalSpeed() {
    int i;
    unsigned short bytes[2];
    short vel16;

    for (i = 0; i < 2; i++)
        bytes[i] = frame_bytes[0x13 + i];
    vel16 = bytes[0] << 8 | bytes[1];
    return vel16 / 1e2; // up
}

double M10GtopParser::getHorizontalSpeed() {
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

double M10GtopParser::getDirection() {
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

double M10GtopParser::getTemperature() {
    return 0;
}

double M10GtopParser::getHumidity() {
    return 0;
}

double M10GtopParser::getDp() {
    return 0;
}

std::string M10GtopParser::getSerialNumber() {
    int i;
    unsigned byte;
    unsigned short sn_bytes[5];
    char SN[18];

    for (i = 0; i < 17; i++)
        SN[i] = ' ';
    SN[17] = '\0';

    for (i = 0; i < 5; i++) {
        byte = frame_bytes[0x5D + i];
        sn_bytes[i] = byte;
    }

    /*
     * The serial number is in the form M10-A-BCC-D-EEEE
     * - A is the frame type, T for Trimble, the original GPS used for this modulation
     * G for Gtop GPS
     * - B is the year of fabrication (8 = 2018)
     * - CC is the month of fabrication
     * - D is the product type, 2 is production type
     * - EEEE is the RS serial number
     */

    byte = sn_bytes[2];
    sprintf(SN, "M10-G-%1X%02u", (byte >> 4)&0xF, byte & 0xF);
    byte = sn_bytes[3] | (sn_bytes[4] << 8);
    sprintf(SN + 9, "-%1X-%1u%04u", sn_bytes[0]&0xF, (byte >> 13)&0x7, byte & 0x1FFF);

    return SN;
}

std::string M10GtopParser::getdxlSerialNumber() {
    int i;
    unsigned byte;
    unsigned short sn_bytes[5];

    for (i = 0; i < 5; i++) {
        byte = frame_bytes[0x5D + i];
        sn_bytes[i] = byte;
    }

    // The way used by dxlARPS used for compatibility.
    uint32_t id;
    char ids[9];

    id = (uint32_t) (((uint32_t) ((uint32_t) (uint8_t)
            sn_bytes[4] + 256UL * (uint32_t) (uint8_t)
            sn_bytes[3] + 65536UL * (uint32_t) (uint8_t)
            sn_bytes[2])^(uint32_t) ((uint32_t) (uint8_t)
            sn_bytes[0] / 16UL + 16UL * (uint32_t) (uint8_t)
            sn_bytes[1] + 4096UL * (uint32_t) (uint8_t)
            sn_bytes[2]))&0xFFFFFUL);
    i = 8UL;
    ids[8U] = 0;
    --i;
    do {
        ids[i] = (char) (id % 10UL + 48UL);
        id = id / 10UL;
        --i;
    } while (i != 1UL);
    ids[i] = 'E';
    --i;
    ids[i] = 'M';

    return ids;
}

void M10GtopParser::printFrame() {
    if (dispRaw) {
        for (int i = 0; i < frameLength + 1; ++i) {
            printf("%02X", frame_bytes[i]);
        }
        if (correctCRC)
            printf(" [OK]");
        else
            printf(" [NO]");
        printf("\n");
    } else {
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
                "\"dxlid\": \"%s\", "
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
                "Gtop", frame, getSerialNumber().c_str(), getdxlSerialNumber().c_str(), getYear(), getMonth(), getDay(), getHours(), getMinutes(), getSeconds(), getLatitude(), getLongitude(),
                getAltitude(), getHorizontalSpeed(), getDirection(), getVerticalSpeed()/*, getTemperature()*/, correctCRC);
    }
    fflush(stdout);
}
