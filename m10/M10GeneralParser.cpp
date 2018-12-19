/* 
 * File:   M10GeneralParser.cpp
 * Author: Viproz
 * Used code from rs1729
 * Created on December 12, 2018, 10:52 PM
 */

#include "M10GeneralParser.h"

M10GeneralParser::M10GeneralParser() {
}

M10GeneralParser::~M10GeneralParser() {
}

void M10GeneralParser::changeData(std::array<unsigned char, DATA_LENGTH> data, bool good) {
    correctCRC = good;
    frame_bytes = data;
}

double M10GeneralParser::getLatitude() {
    return 0;
}

double M10GeneralParser::getLongitude() {
    return 0;
}

double M10GeneralParser::getAltitude() {
    return 0;
}

int M10GeneralParser::getDay() {
    return 0;
}

int M10GeneralParser::getMonth() {
    return 0;
}

int M10GeneralParser::getYear() {
    return 0;
}

int M10GeneralParser::getHours() {
    return 0;
}

int M10GeneralParser::getMinutes() {
    return 0;
}

int M10GeneralParser::getSeconds() {
    return 0;
}

double M10GeneralParser::getVerticalSpeed() {
    return 0;
}

double M10GeneralParser::getHorizontalSpeed() {
    return 0;
}

double M10GeneralParser::getDirection() {
    return 0;
}

std::string M10GeneralParser::getSerialNumber() {
    return "";
}



