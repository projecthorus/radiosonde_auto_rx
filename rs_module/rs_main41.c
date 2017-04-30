

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "rs_data.h"
#include "rs_demod.h"
#include "rs_datum.h"
#include "rs_rs41.h"


int option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 0,      // rohe Frames
    option_inv = 0,      // invertiert Signal
    option_crc = 0,      // check CRC
    option_sat = 0,      // GPS sat data
    wavloaded = 0;
    option_csv = 0; 


ui8_t *frame = NULL;

int print_position(rs_data_t *rs_data) {

// option_crc: check block-crc

            fprintf(stdout, "[%5d] ", rs_data->frnr);
            fprintf(stdout, "(%s) ", rs_data->SN);

            fprintf(stdout, "%s ", weekday[rs_data->wday]);
            fprintf(stdout, "%04d-%02d-%02d %02d:%02d:%06.3f",
                             rs_data->year, rs_data->month, rs_data->day,
                             rs_data->hr, rs_data->min, rs_data->sec);
            if (option_verbose == 3) fprintf(stdout, " (W %d)", (rs_data->GPS).week);

            fprintf(stdout, " ");
            fprintf(stdout, " lat: %.5f ", (rs_data->GPS).lat);
            fprintf(stdout, " lon: %.5f ", (rs_data->GPS).lon);
            fprintf(stdout, " alt: %.2f ", (rs_data->GPS).alt);
            fprintf(stdout,"  vH: %4.1f  D: %5.1f°  vV: %3.1f ", (rs_data->GPS).vH, (rs_data->GPS).vD, (rs_data->GPS).vU);

            int i;
            fprintf(stdout, " # [");
            for (i=0; i<5; i++) fprintf(stdout, "%d", (rs_data->crc>>i)&1);
            fprintf(stdout, "]");

            if (rs_data->ecc >= 0) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
            if (rs_data->ecc >  0) fprintf(stdout, " (%d)", rs_data->ecc);

    fprintf(stdout, "\n");

    return 0;
}

// VK5QI Addition - Print data as easily parseable CSV.
int print_position_csv(rs_data_t *rs_data) {

// option_crc: check block-crc

if ( option_crc==0  || ( option_crc && (rs_data->crc & 0x7)==0 ) )
{
            fprintf(stdout, "%5d,", rs_data->frnr);
            fprintf(stdout, "%s,", rs_data->SN);

            //fprintf(stdout, "%s,", weekday[rs_data->wday]);
            fprintf(stdout, "%04d-%02d-%02d,%02d:%02d:%06.3f,",
                             rs_data->year, rs_data->month, rs_data->day,
                             rs_data->hr, rs_data->min, rs_data->sec);
            //if (option_verbose) fprintf(stdout, " (W %d)", (rs_data->GPS).week);
            fprintf(stdout, "%.5f,", (rs_data->GPS).lat);
            fprintf(stdout, "%.5f,", (rs_data->GPS).lon);
            fprintf(stdout, "%.2f,", (rs_data->GPS).alt);
            fprintf(stdout,"%4.1f,%5.1f,%3.1f,", (rs_data->GPS).vH, (rs_data->GPS).vD, (rs_data->GPS).vU);
            if (rs_data->ecc >= 0) fprintf(stdout, "OK"); else fprintf(stdout, "FAIL");
            //if (rs_data->ecc >  0) fprintf(stdout, " (%d)", rs_data->ecc);

    fprintf(stdout, "\n");
}
    return 0;
}

void print_frame(rs_data_t *rs_data) {
    int i;

    if (!option_raw && option_verbose) fprintf(stdout, "\n");  // fflush(stdout);

    (rs_data->rs_process)(rs_data, option_raw, option_verbose);

    if (option_raw) {
        for (i = 0; i < rs_data->pos; i++) {
            fprintf(stdout, "%02x", rs_data->frame_bytes[i]);
        }

        if (rs_data->ecc >= 0) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
        if (rs_data->ecc >  0) fprintf(stdout, " (%d)", rs_data->ecc);

        fprintf(stdout, "\n");
    }
    else if (option_csv){
        print_position_csv(rs_data);
    }else{
        print_position(rs_data);
    }
}

int main(int argc, char *argv[]) {

    FILE *fp = NULL;
    char *fpname = NULL;
    char *bitbuf = NULL;
    int bit_count = 0,
        header_found = 0,
        frmlen = 0;
    int i, bit, len;
    int err = 0;


    setbuf(stdout, NULL);

    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v, -vx, -vv  (info, aux, info/conf)\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       -i, --invert\n");
            fprintf(stderr, "       --crc        (check CRC)\n");
            fprintf(stderr, "       --std        (std framelen)\n");
            fprintf(stderr, "       --csv        (output as CSV)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose |= 0x1;
        }
        else if   (strcmp(*argv, "-vx") == 0) { option_verbose |= 0x2; }
        else if   (strcmp(*argv, "-vv") == 0) { option_verbose |= 0x3; }
        else if   (strcmp(*argv, "--crc") == 0) { option_crc = 1; }
        else if   (strcmp(*argv, "--csv") == 0) { option_csv = 1; }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
        }
        else if   (strcmp(*argv, "--std" ) == 0) { frmlen = 320; }  // NDATA_LEN
        else if   (strcmp(*argv, "--std2") == 0) { frmlen = 518; }  // FRAME_LEN
        else if   (strcmp(*argv, "--sat") == 0) { option_sat = 1; option_verbose |= 0x100; }
        else {
            fp = fopen(*argv, "rb");
            if (fp == NULL) {
                fprintf(stderr, "%s konnte nicht geoeffnet werden\n", *argv);
                return -1;
            }
            wavloaded = 1;
        }
        ++argv;
    }
    if (!wavloaded) fp = stdin;


    rs_data_t rs41data = {{0}};
    rs_data_t *rs_data = &rs41data;
    rs_data->input = 8;
    err = init_rs41data(rs_data);
    if (err) goto error_tag;
    frame = rs_data->frame_bytes;
    if (frmlen == 0) frmlen = rs_data->frame_len;

    err = read_wav_header(fp, rs_data);
    if (err) goto error_tag;

    bitbuf = calloc(rs_data->bits, 1);
    if (bitbuf == NULL)  {
        err = -1;
        goto error_tag;
    }

    rs_data->pos = rs_data->frame_start;

    while (!read_bits_fsk(fp, &bit, &len, option_inv)) {

        if (len == 0) { // reset_frame();
            if (rs_data->pos > rs_data->pos_min) {
                print_frame(rs_data);
                bit_count = 0;
                rs_data->pos = rs_data->frame_start;
                header_found = 0;
            }
            //inc_bufpos();
            //buf[bufpos] = 'x';
            continue;   // ...
        }

        for (i = 0; i < len; i++) {

            inc_bufpos(rs_data);
            rs_data->buf[rs_data->bufpos] = 0x30 + bit;  // Ascii

            if (!header_found) {
                if (compare(rs_data) >= rs_data->header_len) header_found = 1;
            }
            else {

                if (rs_data->input < 8) {
                    rs_data->frame_rawbits[rs_data->bits*rs_data->pos + bit_count] = 0x30 + bit;
                }

                bitbuf[bit_count] = bit;
                bit_count++;

                if (bit_count == rs_data->bits) {
                    bit_count = 0;
                    if (rs_data->input == 8) {
                        frame[rs_data->pos] = rs_data->bits2byte(rs_data, bitbuf);
                    }
                    rs_data->pos++;
                    if (rs_data->pos == frmlen) {
                        print_frame(rs_data);
                        rs_data->pos = rs_data->frame_start;
                        header_found = 0;
                    }
                }
            }

        }
    }

    free(bitbuf);
    bitbuf = NULL;

error_tag:
    fclose(fp);
    free_rs41data(rs_data);

    return err;
}

