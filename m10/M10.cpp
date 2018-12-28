/* 
 * File:   main.cpp
 * Author: Viproz
 * Used code from rs1729
 * Created on December 12, 2018, 10:49 PM
 */

#include <cstdlib>
#include "M10Decoder.h"

using namespace std;

/*
 * 
 */
int main(int argc, char** argv) {
    M10Decoder decoder;
    std::string filename = "";
    char *fpname;

    fpname = argv[0];
    ++argv;
    while (*argv) {
        if ((strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0)) {
            fprintf(stderr, "%s [options] filename\n", fpname);
            fprintf(stderr, "  filename needs to be in wav format and blank or - for stdin\n");
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v, --verbose Display even when CRC is wrong\n");
            fprintf(stderr, "       -R Show result at the end decoded/total\n");
            fprintf(stderr, "       -r Display raw information\n");
            fprintf(stderr, "       -b Try alternative method after main method if it failed\n");
            fprintf(stderr, "       -b2 Try to repair data with the previous line\n");
            fprintf(stderr, "       --ch2 Decode the second channel\n");
            
            return 0;
        } else if ((strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0)) {
            decoder.setRaw(true);
        } else if (strcmp(*argv, "-v") == 0 || strcmp(*argv, "--verbose") == 0) {
            decoder.setVerboseLevel(1);
        } else if (strcmp(*argv, "-b") == 0) {
            decoder.setTryMethodSign(true);
        } else if (strcmp(*argv, "-b2") == 0) {
            decoder.setTryMethodRepair(true);
        } else if (strcmp(*argv, "-R") == 0) {
            decoder.setDispResult(true);
        } else if (strcmp(*argv, "--ch2") == 0) {
            decoder.setChannel(1);
        }// right channel (default: 0=left)
        else {
            filename = *argv;
        }
        ++argv;
    }

    return decoder.startDecode(filename);
}

