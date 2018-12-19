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
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            //fprintf(stderr, "       -v, --verbose\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       -c, --color\n");
            //fprintf(stderr, "       -o, --offset\n");
            return 0;
        } else if ((strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0)) {
            decoder.setRaw(true);
        } else if (strcmp(*argv, "-v") == 0) {
            decoder.setVerboseLevel(1);
        } else if (strcmp(*argv, "-b") == 0) {
            decoder.setTryMethodSign(true);
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

