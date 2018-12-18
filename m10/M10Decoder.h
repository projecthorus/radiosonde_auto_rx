/* 
 * File:   M10Decoder.h
 * Author: Viproz
 * Used code from rs1729
 * Created on December 12, 2018, 11:31 PM
 */

#ifndef M10DECODER_H
#define M10DECODER_H
#define EOF_INT  0x1000000
#define FRAME_LEN       (100+1)   // 0x64+1
#define AUX_LEN         20
#define DATA_LENGTH     FRAME_LEN + AUX_LEN + 2

#define stdFLEN        0x64  // pos[0]=0x64
#define pos_Check     (stdFLEN-1)  // 2 byte

#include <stdio.h>
#include <string>
#include <cstring>
#include <cmath>
#include "M10GeneralParser.h"

class M10Decoder {
public:
    M10Decoder();
    virtual ~M10Decoder();
    virtual int startDecode(std::string fname);
    virtual double findFrameStart();
    virtual int decodeMessage(double initialPos);
    
    void setRaw(bool b) {dispRaw = b;}
    void setDispResult(bool b) {dispResult = b;}
    void setChannel(int c) {targetedChannel = c;}
    void setTryMethodSign(bool b) {trySign = b;}
private:
    int decodeMethodCompare(double initialPos);
    int decodeMethodSign(double initialPos);
    int read_wav_header();
    int readSignedSample();
    int readSignedSampleNormalized();
    int findstr(char *buf, const char *str, int pos);
    bool checkCRC();
    int update_checkM10(int c, unsigned short b);
    void bits2bytes();

    M10GeneralParser *m10Parser;
    M10GeneralParser *m10GTop;
    M10GeneralParser *m10Ptu;
    FILE *fp;
    bool dispResult = false;
    bool dispRaw = false;
    bool trySign = false;
    int targetedChannel = 0;
    int sample_rate = 0;
    int bits_sample = 0;
    int channels = 0;
    double samplesPerBit = 0;
    double baudRate = 9615;
    double activeSum = 0;
    static char header[];
    std::string filename;
    
    std::array<unsigned char, DATA_LENGTH> frame_bytes;
    std::array<unsigned char, (DATA_LENGTH)*8> frame_bits;
};

#endif /* M10DECODER_H */

