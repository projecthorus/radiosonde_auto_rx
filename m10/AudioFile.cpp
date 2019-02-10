/* 
 * File:   AudioFile.cpp
 * Author: Viproz
 * Used code from rs1729
 * Created on January 6, 2019, 10:23 AM
 */

#include "AudioFile.h"

AudioFile::AudioFile(std::string filename, int baudrate, int* errors) {
    baudRate = baudrate;
    if (filename == "" || filename == "-")
        fp = stdin;
    else
        fp = fopen(filename.c_str(), "rb");

    if (!fp) {
        fprintf(stderr, "%s could not be opened.\n", filename.c_str());
        *errors = -1;
        return;
    }

    if (read_wav_header()) {
        fprintf(stderr, "Could not read the header.\n");
        *errors = -1;
        return;
    }

    *errors = 0;
}

AudioFile::~AudioFile() {
    if (fp)
        fclose(fp);
}

int AudioFile::read_wav_header() {
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

    if ((bits_sample != 8) && (bits_sample != 16))
        return -1;

    samplesPerBit = sample_rate / (double) baudRate;

    fprintf(stderr, "samples/bit: %.2f\n", samplesPerBit);

    return 0;
}

int AudioFile::findstr(char* buf, const char* str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buf[(pos + i) % 4] != str[i]) break;
    }
    return i;
}

int AudioFile::readSignedSample() {
    int byte, i, sample = 0, s = 0; // EOF -> 0x1000000

    for (i = 0; i < channels; i++) {
        // i = 0: links bzw. mono
        byte = fgetc(fp);
        if (byte == EOF)
            return EOF_INT;
        if (i == targetedChannel)
            sample = byte;

        if (bits_sample == 16) {
            byte = fgetc(fp);
            if (byte == EOF)
                return EOF_INT;
            if (i == targetedChannel)
                sample += byte << 8;
        }

    }

    if (bits_sample == 8) s = sample - 128; // 8bit: 00..FF, centerpoint 0x80=128
    if (bits_sample == 16) s = (short) sample;

    return s;
}

int AudioFile::readSignedSampleAveraged() {
    return averageSample(readSignedSample());
}

int AudioFile::readSignedSampleNormalized() {
    return normalizeSample(readSignedSample());
}

int AudioFile::readSignedSampleAveragedNormalized() {
    return averageNormalizeSample(readSignedSample());
}

int AudioFile::averageSample(int sample) {
    if (sample == EOF_INT)
        return EOF_INT;
    // Average over the last AVG_NUM samples to comply with a global offset
    activeSum = (activeSum + (double) sample)*(samplesPerBit * AVG_NUM) / (samplesPerBit * AVG_NUM + 1.);
    return sample - activeSum / (samplesPerBit * AVG_NUM);
}

int AudioFile::normalizeSample(int sample) {
    if (sample == EOF_INT)
        return EOF_INT;
    return sample > 0 ? 1 : -1;
}

int AudioFile::averageNormalizeSample(int sample) {
    if (sample == EOF_INT)
        return EOF_INT;
    // Average over the last AVG_NUM samples to comply with a global offset
    activeSum = (activeSum + (double) sample)*(samplesPerBit * AVG_NUM) / (samplesPerBit * AVG_NUM + 1.);
    sample = sample - activeSum / (samplesPerBit * AVG_NUM);
    return sample > 0 ? 1 : -1;
}

void AudioFile::resetActiveSum() {
    activeSum = 0;
}


