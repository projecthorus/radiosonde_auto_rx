
#ifndef RS_BCH_ECC_H
#define RS_BCH_ECC_H


int rs_init_RS255(void);
int rs_init_BCH64(void);
int rs_encode(ui8_t *cw);
int rs_decode(ui8_t *cw, ui8_t *, ui8_t *);
int rs_decode_bch_gf2t2(ui8_t *cw, ui8_t *, ui8_t *);


#endif  /* RS_BCH_ECC_H */

