/* 
 * File:   M10GeneralParser.h
 * Author: Viproz
 * Used code from rs1729
 * Created on December 12, 2018, 10:52 PM
 */

#ifndef M10GENERALDECODER_H
#define M10GENERALDECODER_H
#define FRAME_LEN       (100+1)   // 0x64+1
#define AUX_LEN         20
#define DATA_LENGTH     (FRAME_LEN + AUX_LEN + 2)

#include <array>

class M10GeneralParser {
public:
    M10GeneralParser();
    virtual ~M10GeneralParser();
    virtual void changeData(std::array<unsigned char, DATA_LENGTH> data, bool good);
    void setRaw(bool b) {dispRaw = b;}
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
    virtual std::string getSerialNumber();
    std::array<unsigned char, DATA_LENGTH> getFrameBytes() {return frame_bytes;}
    
    virtual std::array<unsigned char, DATA_LENGTH> replaceWithPrevious(std::array<unsigned char, DATA_LENGTH> data);
    virtual void addToStats();
    virtual void printStatsFrame();
    
    virtual void printFrame() = 0;
protected:
    std::array<unsigned char, DATA_LENGTH> frame_bytes;
    std::array<std::array<unsigned short, 0xFF+1>, DATA_LENGTH> statValues = {};
    bool correctCRC;
    bool dispRaw = false;
    int frameLength = 0;
};

#endif /* M10GENERALDECODER_H */

