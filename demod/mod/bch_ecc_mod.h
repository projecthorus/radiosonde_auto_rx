
/*
 *  BCH / Reed-Solomon
 *    encoder()
 *    decoder()  (Euklid. Alg.)
 *
 *
 * author: zilog80
 *
 *
 */

#ifdef INCLUDESTATIC
    #define INCSTAT static
#else
    #ifndef INTTYPES
    #define INTTYPES
    typedef unsigned char  ui8_t;
    typedef unsigned short ui16_t;
    typedef unsigned int   ui32_t;
    typedef char  i8_t;
    typedef short i16_t;
    typedef int   i32_t;
    #endif
    //typedef unsigned char  ui8_t;
    //typedef unsigned int  ui32_t;
    #define INCSTAT
#endif


#define MAX_DEG 254  // max N-1


typedef struct {
    ui32_t f;
    ui32_t ord;
    ui8_t alpha;
    ui8_t exp_a[256];
    ui8_t log_a[256];
} GF_t;

typedef struct {
    ui8_t N;
    ui8_t t;
    ui8_t R;  // RS: R=2t, BCH: R<=mt
    ui8_t K;  // K=N-R
    ui8_t b;
    ui8_t p; ui8_t ip; // p*ip = 1 mod N
    ui8_t g[MAX_DEG+1];  // ohne g[] eventuell als init_return
    GF_t GF;
} RS_t;


static GF_t GF256RS = { .f     = 0x11D,  // RS-GF(2^8): X^8 + X^4 + X^3 + X^2 + 1 : 0x11D
                        .ord   = 256,    // 2^8
                        .alpha = 0x02,   // generator: alpha = X
                        .exp_a = {0},
                        .log_a = {0} };

static GF_t GF256RSccsds = { .f     = 0x187,  // RS-GF(2^8): X^8 + X^7 + X^2 + X + 1 : 0x187
                             .ord   = 256,    // 2^8
                             .alpha = 0x02,   // generator: alpha = X
                             .exp_a = {0},
                             .log_a = {0} };

static GF_t GF64BCH = { .f     = 0x43,   // BCH-GF(2^6): X^6 + X + 1 : 0x43
                        .ord   = 64,     // 2^6
                        .alpha = 0x02,   // generator: alpha = X
                        .exp_a = {0},
                        .log_a = {0} };

static GF_t GF16RS = { .f     = 0x13,   // RS-GF(2^4): X^4 + X + 1 : 0x13
                       .ord   = 16,     // 2^4
                       .alpha = 0x02,   // generator: alpha = X
                       .exp_a = {0},
                       .log_a = {0} };

static GF_t GF256AES = { .f     = 0x11B,  // AES-GF(2^8): X^8 + X^4 + X^3 + X + 1 : 0x11B
                         .ord   = 256,    // 2^8
                         .alpha = 0x03,   // generator: alpha = X+1
                         .exp_a = {0},
                         .log_a = {0} };


static RS_t RS256 = { 255, 12, 24, 231, 0, 1, 1, {0}, {0} };
static RS_t RS256ccsds = { 255, 16, 32, 223, 112, 11, 116, {0}, {0} };
static RS_t BCH64 = {  63,  2, 12,  51, 1, 1, 1, {0}, {0} };

// static RS_t RS16_0 = { 15, 3, 6,  9, 0, 1, 1, {0}, {0} };
static RS_t RS16ccsds = { 15, 2, 4, 11, 6, 1, 1, {0}, {0} };


#ifndef INCLUDESTATIC

int rs_init_RS(RS_t *RS);
int rs_init_RS255(RS_t *RS); // RS(255, 231)
int rs_init_RS255ccsds(RS_t *RS); // RS(255, 223)
int rs_init_RS15ccsds(RS_t *RS);
int rs_init_BCH64(RS_t *RS);

int rs_encode(RS_t *RS, ui8_t cw[]);
int rs_decode(RS_t *RS, ui8_t cw[], ui8_t *err_pos, ui8_t *err_val);
int rs_decode_ErrEra(RS_t *RS, ui8_t cw[], int nera, ui8_t era_pos[], ui8_t *err_pos, ui8_t *err_val);
int rs_decode_bch_gf2t2(RS_t *RS, ui8_t cw[], ui8_t *err_pos, ui8_t *err_val);

#endif

