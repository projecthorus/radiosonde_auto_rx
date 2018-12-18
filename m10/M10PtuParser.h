/* 
 * File:   M10GTop.h
 * Author: Viproz
 * Used code from rs1729
 * Created on December 13, 2018, 4:39 PM
 */

#ifndef M10PTU_H
#define M10PTU_H
#define FRAME_LEN       (100+1)   // 0x64+1
#define AUX_LEN         20
#define DATA_LENGTH     FRAME_LEN + AUX_LEN + 2

#include <math.h>
#include "M10GeneralParser.h"

class M10PtuParser : public M10GeneralParser {
public:
    M10PtuParser();
    virtual ~M10PtuParser();
    virtual void changeData(std::array<unsigned char, DATA_LENGTH> data);
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
    
    void printFrame();
private:
    void gps2Date(long GpsWeek, long GpsSeconds, int *Year, int *Month, int *Day);
    int week;
    int time;
    int year;
    int month;
    int day;
    
};

#endif /* M10GTOP_H */

