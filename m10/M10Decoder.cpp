/*
 * File:   M10Decoder.cpp
 * Author: Viproz
 * Used code from rs1729
 * Created on December 12, 2018, 11:31 PM
 */

#include "M10Decoder.h"
#include "M10GtopParser.h"
#include "M10TrimbleParser.h"

char M10Decoder::header[] = "10011001100110010100110010011001";

M10Decoder::M10Decoder() {
    m10Gtop = new M10GtopParser();
    m10Trimble = new M10TrimbleParser();
    m10Parser = m10Gtop;

    frameSamples = NULL;
    audioFile = NULL;
}

M10Decoder::~M10Decoder() {
    delete m10Gtop;
    delete m10Trimble;

    if (frameSamples)
        delete frameSamples;

    if (audioFile)
        delete audioFile;
}

int M10Decoder::startDecode(std::string fname) {
    filename = fname;
    int error = 0;

    audioFile = new AudioFile(fname, baudRate, &error);

    if (error) {
        return error;
    }

    samplesPerBit = audioFile->getSamplesPerBit();

    samplesBufLength = (DATA_LENGTH * 8 + 100) * samplesPerBit * 2;
    frameSamples = new std::vector<int>(samplesBufLength);

    double res = 1;
    bool supported = true;
    while (res != EOF_INT) {
        res = findFrameStart();
        if (res == EOF_INT)
            break;

        totalFrames++;
        if (decodeMessage(res) == EOF_INT)
            break;

        long sondeType = ((long) frame_bytes[1] << 8) + (long) frame_bytes[2];
        frameLength = frame_bytes[0];
        supported = true;
        switch (sondeType) {
            case 0xAF02:
                m10Parser = m10Gtop;
                break;
            case 0x9F20:
                m10Parser = m10Trimble;
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
            if (correctCRC) {
                correctFrames++;
                lastGoodFrame = frame_bytes;
            }

            m10Parser->changeData(frame_bytes, correctCRC);
            m10Parser->printFrame();

            if (!correctCRC) {
                if (correctFrames == 0 && tryStats) // Add the frame to the record
                    m10Parser->addToStats();
                else if (tryRepair && correctFrames != 0) // Put the last correct to repair
                    m10Parser->changeData(lastGoodFrame, correctFrames > 0);
            }
        }
    }
    if (tryStats && verboseLevel >= 1)
        m10Parser->printStatsFrame();

    if (dispResult)
        fprintf(stderr, "Result of %i/%i decoding\n", correctFrames, totalFrames);
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
    double activeSum = 0;
    for (int j = 0; 1; ++j) {
        v = audioFile->readSignedSample();
        vals[valIndex++ % smallBufLen] = v;
        if (v == EOF_INT)
            return EOF_INT;
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

            // Store the position of the start of the bit
            posData[currentIndex] = (double) j - (1. - (double) i / round((double) len / samplesPerBit))*(double) len;

            char normal = 1;
            char inv = 1;
            int headerIndex = 0;

            // Check if the header is correct
            for (int k = 0; k < headerLength; ++k) {
                if (decoded[(k + currentIndex + 1) % headerLength] == header[headerIndex])
                    inv = 0;
                else
                    normal = 0;
                headerIndex++;
            }

            if (normal || inv) {
                // Calculate the real position of the data averaging over the headerLength samples
                double pos = 0;
                for (int k = 0; k < headerLength; ++k) {
                    pos += posData[(k + currentIndex + 1) % headerLength] + (double) (headerLength - k) * samplesPerBit;
                    //fprintf(stderr, "%.2f\n", posData[(k + currentIndex + 1) % headerLength] + (double) (headerLength - k) * samplesPerBit);
                }

                pos /= (double) headerLength;

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

                // Only the weight of the first bit is useful, they are stored already
                return pos - (int) pos;
            }

            currentIndex = (currentIndex + 1) % headerLength;
        }
        len = 0;
        prevV = v;
    }
}

int M10Decoder::decodeMessage(double initialPos) {
    std::array<unsigned char, DATA_LENGTH> frameBackup;
    int v;
    for (; curIndex < samplesBufLength; ++curIndex) {
        v = audioFile->readSignedSample();
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

    if ((tryRepair && correctFrames != 0) || (correctFrames == 0 && tryStats)) {
        frameBackup = frame_bytes;
        frame_bytes = m10Parser->replaceWithPrevious(frame_bytes);
        if (checkCRC())
            return 0;
        frame_bytes = frameBackup;
    }

    if (trySign) {
        // Reset the index
        curIndex = 0;
        ret = decodeMethodSign(initialPos);
        if (ret == 0)
            return 0;
        if (ret == EOF_INT)
            return EOF_INT;

        if ((tryRepair && correctFrames != 0) || (correctFrames == 0 && tryStats)) {
            frameBackup = frame_bytes;
            frame_bytes = m10Parser->replaceWithPrevious(frame_bytes);
            if (checkCRC())
                return 0;
            frame_bytes = frameBackup;
        }
    }

    return 1;
}

int M10Decoder::decodeMethodCompare(double initialPos) {
    char bit0 = 2;

    double j = initialPos;
    double sum = 0;
    int val = getNextBufferValue(); // Read the first value

    if (val == EOF_INT)
        return EOF_INT;
    for (int k = 0; k < FRAME_LEN * 8 + AUX_LEN * 8 + 8; ++k) { // Iterate through needed bits
        // val is set in the previous round
        // Add the first part of the value weighted correctly
        sum = (1. - (j - floor(j)))*(double) val;
        for (int i = ceil(j); i < j + samplesPerBit - 1; ++i) { // Full vals in the middle
            val = getNextBufferValue();
            if (val == EOF_INT)
                return EOF_INT;
            sum += (double) val;
        }
        val = getNextBufferValue();
        if (val == EOF_INT)
            return EOF_INT;
        // Add the rest of the value
        sum += (j + samplesPerBit - floor(j + samplesPerBit))*(double) val;

        double moy1 = sum / samplesPerBit;

        j += samplesPerBit;

        // Same for the second bit
        sum = (1 - (j - floor(j)))*(double) val;
        for (int i = ceil(j); i < j + samplesPerBit - 1; ++i) { // Full vals in the middle
            val = getNextBufferValue();
            if (val == EOF_INT)
                return EOF_INT;
            sum += (double) val;
        }
        val = getNextBufferValue();
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
    bits2bytes();
    return !checkCRC();
}

int M10Decoder::decodeMethodSign(double initialPos) {
    char bit0 = 2;

    double j = initialPos;
    double sum = 0;
    int val = audioFile->averageNormalizeSample(getNextBufferValue()); // Read the first value

    if (val == EOF_INT)
        return EOF_INT;
    for (int k = 0; k < FRAME_LEN * 8 + AUX_LEN * 8 + 8; ++k) { // Iterate through needed bits
        // val is set in the previous round
        // Add the first part of the value weighted correctly
        sum = (1. - (j - floor(j)))*(double) val;
        for (int i = ceil(j); i < j + samplesPerBit - 1; ++i) { // Full vals in the middle
            val = audioFile->averageNormalizeSample(getNextBufferValue());
            if (val == EOF_INT)
                return EOF_INT;
            sum += val;
        }

        val = audioFile->averageNormalizeSample(getNextBufferValue());
        if (val == EOF_INT)
            return EOF_INT;

        // Add the rest of the value
        sum += (j + samplesPerBit - floor(j + samplesPerBit))*(double) val;

        double moy1 = sum / samplesPerBit;

        j += samplesPerBit;

        // Same for the second bit
        sum = (1 - (j - floor(j)))*(double) val;
        for (int i = ceil(j); i < j + samplesPerBit - 1; ++i) { // Full vals in the middle
            val = audioFile->averageNormalizeSample(getNextBufferValue());
            if (val == EOF_INT)
                return EOF_INT;
            sum += (double) val;
        }
        val = audioFile->averageNormalizeSample(getNextBufferValue());
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
    bits2bytes();
    return !checkCRC();
}

void M10Decoder::setRaw(bool b) {
    dispRaw = b;
    m10Gtop->setRaw(b);
    m10Trimble->setRaw(b);
}

int M10Decoder::getNextBufferValue() {
    if (curIndex < samplesBufLength)
        return frameSamples->at(curIndex++);
    else {
        fprintf(stderr, "Error, end of buffer.\n");
        return EOF_INT;
    }
}

bool M10Decoder::checkCRC() {
    int i, cs;

    cs = 0;
    for (i = 0; i < frameLength-1; i++) {
        cs = update_checkM10(cs, frame_bytes[i]);
    }

    return ((cs & 0xFFFF) != 0) && ((cs & 0xFFFF) == ((frame_bytes[frameLength-1] << 8) | frame_bytes[frameLength]));
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
