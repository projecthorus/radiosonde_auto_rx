
#ifndef RS_DEMOD_H
#define RS_DEMOD_H


int read_wav_header(FILE *, rs_data_t *);
int read_bits_fsk(FILE *, int *, int *, int);

void inc_bufpos(rs_data_t *);
int compare(rs_data_t *);


#endif  /* RS_DEMOD_H */

