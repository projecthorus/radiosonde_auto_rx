/*
   tsrc.c
   David Rowe
   Sat Nov 3 2012

   Unit test for libresample code.

   build: gcc tsrc.c -o tsrc -lm -lsamplerate

  */

#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define N    10000                   /* processing buffer size */

void display_help(void) {
    fprintf(stderr, "\nusage: tsrc inputRawFile OutputRawFile OutSampleRatio [-l] [-c]\n");
    fprintf(stderr, "\nUse - for stdin/stdout\n\n");
    fprintf(stderr, "-l fast linear resampler\n");
    fprintf(stderr, "-c complex (two channel) resampling\n\n");
}

int main(int argc, char *argv[]) {
    FILE       *fin, *fout;
    short       in_short[N], out_short[N];
    float       in[N], out[N];
    SRC_DATA    data;
    int         error, nin, nremaining, i, output_rate, symbol_rate, samples_per_symbol;

    if (argc < 3) {
	display_help();
	exit(1);
    }

    if (strcmp(argv[1], "-") == 0) 
        fin = stdin;
    else
        fin = fopen(argv[1], "rb");
    assert(fin != NULL);

    if (strcmp(argv[2], "-") == 0) 
        fout = stdout;
    else
        fout = fopen(argv[2], "wb");
    assert(fout != NULL);

    output_rate = atof(argv[3]);
    symbol_rate = atof(argv[4]);
    samples_per_symbol = output_rate / symbol_rate;

    char in_char;

    while(fread(&in_char, 1, 1, fin) == 1) {

        if(in_char == 0){
            for (i=0; i<samples_per_symbol; i++){
                fprintf('\0')
            }
        }

	fwrite(out_short, sizeof(short), data.output_frames_gen*channels, fout);
        if (fout == stdout) fflush(stdout);




    }

    //fprintf(stderr, "total_in: %d total_out: %d\n", total_in, total_out);

    fclose(fout);
    fclose(fin);

    return 0;
}
