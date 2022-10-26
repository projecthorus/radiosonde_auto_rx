/*
 * File:   M10Gtop.h
 * Author: Viproz
 * Used code from rs1729
 * Created on December 13, 2018, 4:39 PM
 */

#ifndef M10GTOP_H
#define M10GTOP_H
#define FRAME_LEN       (100+1)   // 0x64+1
#define AUX_LEN         20
#define DATA_LENGTH     (FRAME_LEN + AUX_LEN + 2)

#include <math.h>
#include "M10GeneralParser.h"

class M10GtopParser : public M10GeneralParser {
public:
    M10GtopParser();
    virtual ~M10GtopParser();
    virtual void changeData(std::array<unsigned char, DATA_LENGTH> data, bool good);
    virtual double getLatitude();
    virtual double getLongitude();
    virtual double getAltitude();
    virtual int getDay();
    virtual int getMonth();
    virtual int getYear();
    virtual int getHours();
    virtual int getMinutes();
    virtual int getSeconds();
    virtual double getVerticalSpeed();
    virtual double getHorizontalSpeed();
    virtual double getDirection();
    virtual double getTemperature();
    virtual double getHumidity();
    virtual double getDp();
    virtual std::string getSerialNumber();
    virtual std::string getdxlSerialNumber();

    void printFrame();
private:
    int getNumberOfDays(int month, int year);
    int time;
    int date;

};

#endif /* M10GTOP_H */
