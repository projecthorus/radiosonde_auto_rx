/* 
 * File:   M10Decoder.cpp
 * Author: Viproz
 * Used code from rs1729
 * Created on December 12, 2018, 11:31 PM
 */

#include "M10Decoder.h"
#include "M10GTopParser.h"
#include "M10PtuParser.h"

char M10Decoder::header[] = "10011001100110010100110010011001";

M10Decoder::M10Decoder() {
    m10GTop = new M10GTopParser();
    m10Ptu = new M10PtuParser();
    m10Parser = m10GTop;
}

M10Decoder::~M10Decoder() {
    delete m10GTop;
    delete m10Ptu;

    if (samplesPerBit != 0)
        delete frameSamples;
}

int M10Decoder::startDecode(std::string fname) {
    filename = fname;
    if (filename == "" || filename == "-")
        fp = stdin;
    else
        fp = fopen(filename.c_str(), "rb");

    if (fp == NULL) {
        fprintf(stderr, "%s could not be opened.\n", filename.c_str());
        return -1;
    }

    if (read_wav_header()) {
        fprintf(stderr, "Could not read the header.\n");
        return -1;
    }

    samplesBufLength = (DATA_LENGTH * 8 + 100) * samplesPerBit * 2;
    frameSamples = new std::vector<int>(samplesBufLength);

    double res = 1;
    int c = 0;
    int correct = 0;
    bool supported = true;
    while (res) {
        res = findFrameStart();
        if (res == 0)
            break;
        c++;
        if (decodeMessage(res) == EOF_INT)
            break;
        bits2bytes();

        long sondeType = ((long) frame_bytes[0] << 16) + ((long) frame_bytes[1] << 8) + (long) frame_bytes[2];
        supported = true;
        switch (sondeType) {
            case 0x64AF02:
                m10Parser = m10GTop;
                break;
            case 0x649F20:
                m10Parser = m10Ptu;
                break;
            default:
                supported = false;
        }

        bool correctCRC = checkCRC();
        if (correctCRC || verboseLevel >= 1) {
            if (!supported) {
                fprintf(stderr, "Not supported : %#06x\n", (unsigned int) sondeType);
                continue;
            }
            if (correctCRC)
                correct++;
            m10Parser->changeData(frame_bytes, correctCRC);
            m10Parser->printFrame();
        }
    }
    if (dispResult)
        fprintf(stderr, "Result of %i/%i decoding\n", correct, c);
    return 0;
}

double M10Decoder::findFrameStart() {
    int headerLength = strlen(header);
    double posData[headerLength];
    char decoded[headerLength];
    int currentIndex = 0;

    for (int i = 0; i < headerLength; ++i)
        decoded[i] = '0'; // Fill in decoded to compare later

    int prevV = 1;
    int len = 0;
    int v;
    int smallBufLen = (int) (samplesPerBit * 5.);
    std::vector<int> vals(smallBufLen);
    int valIndex = 0;
    for (int j = 0; 1; ++j) {
        v = readSignedSample(false);
        vals[valIndex++ % smallBufLen] = v;
        if (v == EOF_INT)
            return 0;
        // Average over the last 6 samples to comply with a global offset
        activeSum = (activeSum + (double) v)*(samplesPerBit * AVG_NUM) / (samplesPerBit * AVG_NUM + 1.);
        v = v - activeSum / (samplesPerBit * AVG_NUM);

        len++;
        if (v * prevV > 0) // If the signs are the same
            continue;

        for (int i = 0; i < round((double) len / samplesPerBit); ++i) { // Multiple bits in one detection
            if (prevV < 0)
                decoded[currentIndex] = '1';
            else
                decoded[currentIndex] = '0';

            posData[currentIndex] = (double) j - (1. - (double) i / round((double) len / samplesPerBit))*(double) len;

            char normal = 1;
            char inv = 1;
            int headerIndex = 0;
            // currentIndex to the end
            for (int l = currentIndex + 1; l < headerLength; ++l) {
                if (decoded[l] == header[headerIndex])
                    inv = 0;
                else
                    normal = 0;
                headerIndex++;
            }
            // The start to currentIndex
            for (int l = 0; l < currentIndex; ++l) {
                if (decoded[l] == header[headerIndex])
                    inv = 0;
                else
                    normal = 0;
                headerIndex++;
            }
            if (normal || inv) {
                // Calculate the real position of the data averaging over the 16 samples
                double pos = 0;
                for (int k = currentIndex; k < headerLength; ++k)
                    pos += posData[k] + (double) (headerLength - k) * samplesPerBit;
                for (int k = 0; k < currentIndex; ++k)
                    pos += posData[k] + (double) (headerLength - k) * samplesPerBit;

                pos /= headerLength;

                int tmpIndex = 0;
                valIndex--;
                valIndex %= smallBufLen;
                if (j - (int) pos > smallBufLen) // If absurdly long way back
                    continue;
                // Store the previous values in the buffer
                for (curIndex = 0; curIndex < (j - (int) pos); ++curIndex) {
                    tmpIndex = valIndex + curIndex - (j - (int) pos);
                    if (tmpIndex < 0)
                        tmpIndex += smallBufLen;
                    frameSamples->at(curIndex) = vals.at(tmpIndex);
                }

                pos = pos - (int) pos;
                return pos;
            }

            currentIndex = (currentIndex + 1) % headerLength;
        }
        len = 0;
        prevV = v;
    }
    return 0;
}

int M10Decoder::decodeMessage(double initialPos) {
    int v;
    for (; curIndex < samplesBufLength; ++curIndex) {
        v = readSignedSample(false);
        if (v == EOF_INT)
            return EOF_INT;
        frameSamples->at(curIndex) = v;
    }

    // Reset the index
    curIndex = 0;

    int ret;

    ret = decodeMethodCompare(initialPos);
    if (ret == 0)
        return 0;
    if (ret == EOF_INT)
        return EOF_INT;

    if (trySign) {
        // Reset the index
        curIndex = 0;
        ret = decodeMethodSign(initialPos);
        if (ret == 0)
            return 0;
        if (ret == EOF_INT)
            return EOF_INT;
    }

    return 0;
}

int M10Decoder::decodeMethodCompare(double initialPos) {
    char bit0 = 2;

    double j = initialPos;
    double sum = 0;
    int val = readSignedSample(); // Read the first value
    fpos_t tmp;
    fgetpos(fp, &tmp);
    if (val == EOF_INT)
        return EOF_INT;
    for (int k = 0; k < FRAME_LEN * 8 + AUX_LEN * 8 + 8; ++k) { // Iterate through needed bits
        // val is set in the previous round
        // Add the first part of the value weighted correctly
        sum = (1. - (j - floor(j)))*(double) val;
        for (int i = ceil(j); i < j + samplesPerBit - 1; ++i) { // Full vals in the middle
            val = readSignedSample();
            if (val == EOF_INT)
                return EOF_INT;
            sum += (double) val;
        }
        val = readSignedSample();
        if (val == EOF_INT)
            return EOF_INT;
        // Add the rest of the value
        sum += (j + samplesPerBit - floor(j + samplesPerBit))*(double) val;

        double moy1 = sum / samplesPerBit;

        j += samplesPerBit;

        // Same for the second bit
        sum = (1 - (j - floor(j)))*(double) val;
        for (int i = ceil(j); i < j + samplesPerBit - 1; ++i) { // Full vals in the middle
            val = readSignedSample();
            if (val == EOF_INT)
                return EOF_INT;
            sum += (double) val;
        }
        val = readSignedSample();
        if (val == EOF_INT)
            return EOF_INT;
        sum += (j + samplesPerBit - floor(j + samplesPerBit))*(double) val;

        double moy2 = sum / samplesPerBit;

        j += samplesPerBit;

        // Determination of a combination of raw bits or 10 or 01.
        if (moy1 > moy2) {
            if (bit0 == 0)
                frame_bits[k] = '1';
            else
                frame_bits[k] = '0';
            bit0 = 0;
        } else {
            if (bit0 == 1)
                frame_bits[k] = '1';
            else
                frame_bits[k] = '0';
            bit0 = 1;
        }
    }
    return !checkCRC();
}

int M10Decoder::decodeMethodSign(double initialPos) {
    char bit0 = 2;

    double j = initialPos;
    double sum = 0;
    int val = readSignedSampleNormalized(); // Read the first value
    fpos_t tmp;
    fgetpos(fp, &tmp);
    if (val == EOF_INT)
        return EOF_INT;
    for (int k = 0; k < FRAME_LEN * 8 + AUX_LEN * 8 + 8; ++k) { // Iterate through needed bits
        // val is set in the previous round
        // Add the first part of the value weighted correctly
        sum = (1. - (j - floor(j)))*(double) val;
        for (int i = ceil(j); i < j + samplesPerBit - 1; ++i) { // Full vals in the middle
            val = readSignedSampleNormalized();
            if (val == EOF_INT)
                return EOF_INT;
            sum += val;
        }

        val = readSignedSampleNormalized();
        if (val == EOF_INT)
            return EOF_INT;

        // Add the rest of the value
        sum += (j + samplesPerBit - floor(j + samplesPerBit))*(double) val;

        double moy1 = sum / samplesPerBit;

        j += samplesPerBit;

        // Same for the second bit
        sum = (1 - (j - floor(j)))*(double) val;
        for (int i = ceil(j); i < j + samplesPerBit - 1; ++i) { // Full vals in the middle
            val = readSignedSampleNormalized();
            if (val == EOF_INT)
                return EOF_INT;
            sum += (double) val;
        }
        val = readSignedSampleNormalized();
        if (val == EOF_INT)
            return EOF_INT;
        sum += (j + samplesPerBit - floor(j + samplesPerBit))*(double) val;

        double moy2 = sum / samplesPerBit;

        j += samplesPerBit;

        // Determination of a combination of raw bits or 10 or 01.
        if (moy1 > moy2) {
            if (bit0 == 0)
                frame_bits[k] = '1';
            else
                frame_bits[k] = '0';
            bit0 = 0;
        } else {
            if (bit0 == 1)
                frame_bits[k] = '1';
            else
                frame_bits[k] = '0';
            bit0 = 1;
        }
    }
    return !checkCRC();
}

int M10Decoder::read_wav_header() {
    char txt[4 + 1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p = 0;

    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "RIFF", 4)) return -1;
    if (fread(txt, 1, 4, fp) < 4) return -1;
    // pos_WAVE = 8L
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "WAVE", 4)) return -1;
    // pos_fmt = 12L
    for (;;) {
        if ((byte = fgetc(fp)) == EOF) return -1;
        txt[p % 4] = byte;
        p++;
        if (p == 4) p = 0;
        if (findstr(txt, "fmt ", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;
    if (fread(dat, 1, 2, fp) < 2) return -1;

    if (fread(dat, 1, 2, fp) < 2) return -1;
    channels = dat[0] + (dat[1] << 8);

    if (fread(dat, 1, 4, fp) < 4) return -1;
    memcpy(&sample_rate, dat, 4); //sample_rate = dat[0]|(dat[1]<<8)|(dat[2]<<16)|(dat[3]<<24);

    if (fread(dat, 1, 4, fp) < 4) return -1;
    if (fread(dat, 1, 2, fp) < 2) return -1;
    //byte = dat[0] + (dat[1] << 8);

    if (fread(dat, 1, 2, fp) < 2) return -1;
    bits_sample = dat[0] + (dat[1] << 8);

    // pos_dat = 36L + info
    for (;;) {
        if ((byte = fgetc(fp)) == EOF) return -1;
        txt[p % 4] = byte;
        p++;
        if (p == 4) p = 0;
        if (findstr(txt, "data", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;


    fprintf(stderr, "sample_rate: %d\n", sample_rate);
    fprintf(stderr, "bits       : %d\n", bits_sample);
    fprintf(stderr, "channels   : %d\n", channels);

    if ((bits_sample != 8) && (bits_sample != 16)) return -1;

    samplesPerBit = sample_rate / (double) baudRate;

    fprintf(stderr, "samples/bit: %.2f\n", samplesPerBit);

    return 0;
}

int M10Decoder::readSignedSample(bool buffer) {
    if (buffer) {
        if (curIndex < samplesBufLength)
            return frameSamples->at(curIndex++);
        else {
            fprintf(stderr, "Error, end of buffer.\n");
            return EOF_INT;
        }
    }

    int byte, i, sample = 0, s = 0; // EOF -> 0x1000000

    for (i = 0; i < channels; i++) {
        // i = 0: links bzw. mono
        byte = fgetc(fp);
        if (byte == EOF) return EOF_INT;
        if (i == targetedChannel) sample = byte;

        if (bits_sample == 16) {
            byte = fgetc(fp);
            if (byte == EOF) return EOF_INT;
            if (i == targetedChannel) sample += byte << 8;
        }

    }

    if (bits_sample == 8) s = sample - 128; // 8bit: 00..FF, centerpoint 0x80=128
    if (bits_sample == 16) s = (short) sample;

    return s;
}

int M10Decoder::readSignedSampleNormalized(bool buffer) {
    int v = readSignedSample(buffer);
    if (v == EOF_INT)
        return EOF_INT;
    // Average over the last 6 samples to comply with a global offset
    activeSum = (activeSum + (double) v)*(samplesPerBit * AVG_NUM) / (samplesPerBit * AVG_NUM + 1.);
    v = v - activeSum / (samplesPerBit * AVG_NUM);
    return v > 0 ? 1 : -1;

}

int M10Decoder::findstr(char* buf, const char* str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buf[(pos + i) % 4] != str[i]) break;
    }
    return i;
}

bool M10Decoder::checkCRC() {
    int i, cs;

    cs = 0;
    for (i = 0; i < pos_Check; i++) {
        cs = update_checkM10(cs, frame_bytes[i]);
    }

    return ((cs & 0xFFFF) != 0) && ((cs & 0xFFFF) == ((frame_bytes[pos_Check] << 8) | frame_bytes[pos_Check + 1]));
}

int M10Decoder::update_checkM10(int c, unsigned short b) {
    int c0, c1, t, t6, t7, s;

    c1 = c & 0xFF;

    // B
    b = (b >> 1) | ((b & 1) << 7);
    b ^= (b >> 2) & 0xFF;

    // A1
    t6 = (c & 1) ^ ((c >> 2) & 1) ^ ((c >> 4) & 1);
    t7 = ((c >> 1) & 1) ^ ((c >> 3) & 1) ^ ((c >> 5) & 1);
    t = (c & 0x3F) | (t6 << 6) | (t7 << 7);

    // A2
    s = (c >> 7) & 0xFF;
    s ^= (s >> 2) & 0xFF;


    c0 = b ^ t ^ s;

    return ((c1 << 8) | c0) & 0xFFFF;
}

void M10Decoder::bits2bytes() {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < FRAME_LEN + AUX_LEN) {

        byteval = 0;
        d = 1;
        for (i = 0; i < 8; i++) {
            //bit=*(bitstr+bitpos+i); /* little endian */
            bit = frame_bits[bitpos + 7 - i]; /* big endian */
            if (bit == '1')
                byteval += d;
            else
                byteval += 0;
            d <<= 1;
        }
        bitpos += 8;
        frame_bytes[bytepos++] = byteval & 0xFF;

    }
}
