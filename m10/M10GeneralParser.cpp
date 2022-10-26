/*
 * File:   M10GeneralParser.cpp
 * Author: Viproz
 * Used code from rs1729
 * Created on December 12, 2018, 10:52 PM
 */

#include <string>
#include <sstream>
#include <iostream>
#include "M10GeneralParser.h"
#include "M10TrimbleParser.h"

M10GeneralParser::M10GeneralParser() {
}

M10GeneralParser::~M10GeneralParser() {
}

void M10GeneralParser::changeData(std::array<unsigned char, DATA_LENGTH> data, bool good) {
    correctCRC = good;
    frame_bytes = data;
    frameLength = frame_bytes[0];
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

std::array<unsigned char, DATA_LENGTH> M10GeneralParser::replaceWithPrevious(std::array<unsigned char, DATA_LENGTH> data) {
    return data;
}

void M10GeneralParser::addToStats() {
    for (int i = 0; i < DATA_LENGTH; ++i) {
        ++statValues[i][frame_bytes[i]];
    }
}

void M10GeneralParser::printStatsFrame() {
    unsigned short valMax;
    unsigned short posMax;

    for (int i = 0; i < FRAME_LEN; ++i) {
        valMax = 0;
        posMax = 0;
        for (unsigned short k = 0; k < 0xFF+1; ++k) { // Find maximum
            if (statValues[i][k] > valMax) {
                valMax = statValues[i][k];
                posMax = k;
            }
        }
        frame_bytes[i] = posMax;
    }

    changeData(frame_bytes, false);

    printf("Stats frame:\n");
    printFrame();
}
