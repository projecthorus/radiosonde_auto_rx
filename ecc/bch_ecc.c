
/*
 *  BCH / Reed-Solomon
 *    encoder()
 *    decoder()  (Euklid. Alg.)
 *
 *
 * author: zilog80
 *

   Vaisala RS92, RS41:
       RS(255, 231), t=12
       f=X^8+X^4+X^3+X^2+1, b=0
       g(X) = (X-alpha^0)...(X-alpha^(2t-1))

   LMS6:
       RS(255, 223), t=16 (CCSDS)
       f=X^8+X^7+X^2+X+1, b=112
       g(X) = (X-(alpha^11)^112)...(X-(alpha^11)^(112+2t-1))

   Meisei:
       bin.BCH(63, 51), t=2
       g(X) = (X^6+X+1)(X^6+X^4+X^2+X+1)
       g(a) = 0 fuer a = alpha^1,...,alpha^4
   Es koennen 2 Fehler korrigiert werden; diese koennen auch direkt mit
       L(x) = 1 + L1 x + L2 x^2, L1=L1(S1), L2=L2(S1,S3)
   gefunden werden. Problem: 3 Fehler und mehr erkennen.
   Auch bei 3 Fehlern ist deg(Lambda)=2 und Lambda hat auch 2 Loesungen.
   Meisei-Bloecke sind auf 46 bit gekuerzt und enthalten 2 parity bits.
   -> Wenn decodierte Bloecke bits in Position 46-63 schalten oder
   einer der parity-checks fehlschlaegt, dann Block nicht korrigierbar.
   Es werden
   - 54% der 3-Fehler-Bloecke erkannt
   - 39% der 3-Fehler-Bloecke werden durch Position/Parity erkannt
   -  7% der 3-Fehler-Bloecke werden falsch korrigiert

 *
 */


/*
//
// --- bch_ecc.h ---
//

typedef unsigned char  ui8_t;
typedef unsigned int  ui32_t;

*/


int rs_init_RS255(void);
int rs_init_BCH64(void);
int rs_encode(ui8_t cw[]);
int rs_decode(ui8_t cw[], ui8_t *err_pos, ui8_t *err_val);
int rs_decode_ErrEra(ui8_t cw[], int nera, ui8_t era_pos[], ui8_t *err_pos, ui8_t *err_val);
int rs_decode_bch_gf2t2(ui8_t cw[], ui8_t *err_pos, ui8_t *err_val);

// ---


#define MAX_DEG 254  // max N-1


typedef struct {
    ui32_t f;
    ui32_t ord;
    ui8_t alpha;
} GF_t;

static GF_t GF256RS = { 0x11D,  // RS-GF(2^8): X^8 + X^4 + X^3 + X^2 + 1 : 0x11D
                        256,    // 2^8
                        0x02 }; // generator: alpha = X

static GF_t GF256RSccsds = { 0x187,  // RS-GF(2^8): X^8 + X^7 + X^2 + X + 1 : 0x187
                             256,    // 2^8
                             0x02 }; // generator: alpha = X

static GF_t GF64BCH = { 0x43,   // BCH-GF(2^6): X^6 + X + 1 : 0x43
                        64,     // 2^6
                        0x02 }; // generator: alpha = X

static GF_t GF16RS = { 0x13,   // RS-GF(2^4): X^4 + X + 1 : 0x13
                       16,     // 2^4
                       0x02 }; // generator: alpha = X

static GF_t GF256AES = { 0x11B,  // AES-GF(2^8): X^8 + X^4 + X^3 + X + 1 : 0x11B
                         256,    // 2^8
                         0x03 }; // generator: alpha = X+1


typedef struct {
    ui8_t N;
    ui8_t t;
    ui8_t R;  // RS: R=2t, BCH: R<=mt
    ui8_t K;  // K=N-R
    ui8_t b;
    ui8_t p; ui8_t ip; // p*ip = 1 mod N
    ui8_t g[MAX_DEG+1];  // ohne g[] eventuell als init_return
} RS_t;


static RS_t RS256 = { 255, 12, 24, 231, 0, 1, 1, {0}};
static RS_t RS256ccsds = { 255, 16, 32, 223, 112, 11, 116, {0}};
static RS_t BCH64 = {  63,  2, 12,  51, 1, 1, 1, {0}};

// static RS_t RS16_0 = { 15, 3, 6,  9, 0, 1, 1, {0}};
static RS_t RS16ccsds = { 15, 2, 4, 11, 6, 1, 1, {0}};


static GF_t GF;
static RS_t RS;

static ui8_t exp_a[256],
             log_a[256];


/* --------------------------------------------------------------------------------------------- */

static
int GF_deg(ui32_t p) {
    ui32_t d = 31;
    if (p == 0) return -1;  /* deg(0) = -infty */
    else {
        while (d && !(p & (1<<d)))  d--;  /* d<32, 1L = 1 */
    }
    return d;
}

static
ui8_t GF2m_mul(ui8_t a, ui8_t b) {
    ui32_t aa = a;
    ui8_t ab = 0;
    int i, m = GF_deg(b);

    if (b & 1) ab = a;

    for (i = 0; i < m; i++) {
        aa = (aa << 1);  // a = a * X
        if (GF_deg(aa) == GF_deg(GF.f))
             aa ^= GF.f; // a = a - GF.f
        b >>= 1;
        if (b & 1) ab ^= (ui8_t)aa;        /* b_{i+1} > 0 ? */
    }
    return ab;
}

static
int GF_genTab(GF_t gf, ui8_t expa[], ui8_t loga[]) {
    int i, j;
    ui8_t b;

//    GF.f = f;
//    GF.ord = 1 << GF_deg(GF.f);

    b = 0x01;
    for (i = 0; i < gf.ord; i++) {
        expa[i] = b;         // b_i = a^i
        b = GF2m_mul(gf.alpha, b);
    }

    loga[0] = -00;  // log(0) = -inf
    for (i = 1; i < gf.ord; i++) {
        b = 0x01; j = 0;
        while (b != i) {
            b = GF2m_mul(gf.alpha, b);
            j++;
            if (j > gf.ord-1) {
                return -1;  // a not primitive
            }
        }   // j = log_a(i)
        loga[i] = j;
    }

    return 0;
}

/* --------------------------------------------------------------------------------------------- */
/*

static ui32_t f256  = 0x11D;
static ui8_t  f256FF = 0x1D;

static int gf256_deg(ui8_t p) {
    int d = 7;  // sizeof(p)*8 - 1 = 7 fuer ui8_t

    if (p == 0) return -0xFF;  // deg(0) = -infty
    else {
        while (d && !(p & (1<<d)))  d--;  // d<8, 1L = 1
    }
    return d;
}

static ui8_t gf256_mul(ui8_t p, ui8_t q) {
    ui8_t h = 0;
    int i, m = gf256_deg(q);
    if (q & 1) h = p;
    for (i = 0; i < m; i++) {
        if (gf256_deg(p) == 7)        // deg(f256)-1 = 7
             p = (p << 1) ^ f256FF;   // p = p * X - f256FF
        else p = (p << 1);            // p = p * X
        q >>= 1;
        if (q & 1) h ^= p;            // q_{i+1} > 0 ?
    }
    return h;
}

static int gf256_divmod(ui8_t p, ui8_t q, ui8_t *s, ui8_t *r) {
    int deg_p, deg_q = gf256_deg(q);           // p = s*q + r
    *s = 0;

    if (q == 0) { *s = -1; *r = -1; return -1;} // DIV_BY_ZERO

    if (q == 1) { *s = p; *r = 0; }
    else {
        deg_p = gf256_deg(p);
        if (p == 0) {
            p = f256FF;  // (ui8_t) f256 = f256 & 0xFF = f256FF
            deg_p = 8;   // deg(f256) = 8
        }
        while (deg_p >= deg_q) {
            *s |= 1 << (deg_p-deg_q);
            p  ^= q << (deg_p-deg_q);
            deg_p = gf256_deg(p);
        }
        *r = p;
    }
    return 0;
}

static ui8_t gf256_inv(ui8_t a) { // 1 = x*a + y*f , ggT(a, f) = 1
    ui8_t rem, rem1, rem2, aux, aux1, aux2, quo;

    if (a == 0) return 0; // nicht definiert; DIV_BY_ZERO
    if (a == 1) return 1;

    rem1 = a;
    rem2 = 0;  // = f256
    aux1 = 0x1;
    aux2 = 0x0;
    rem = rem1;
    aux = aux1;

    while (rem > 0x1) {
        gf256_divmod(rem2, rem1, &quo, &rem);
        aux = gf256_mul(quo, aux1) ^ aux2;  // aux = aux2 - quo*aux1
        rem2 = rem1;
        rem1 = rem;
        aux2 = aux1;
        aux1 = aux;
    }
    return aux;
}
*/

/* --------------------------------------------------------------------------------------------- */
/*

// F2[X] mod X^8 + X^4 + X^3 + X + 1

static ui8_t exp_11B[256] = {  // 0x11B: a^n , a = 0x03 = X+1
  0x01, 0x03, 0x05, 0x0F, 0x11, 0x33, 0x55, 0xFF, 0x1A, 0x2E, 0x72, 0x96, 0xA1, 0xF8, 0x13, 0x35,
  0x5F, 0xE1, 0x38, 0x48, 0xD8, 0x73, 0x95, 0xA4, 0xF7, 0x02, 0x06, 0x0A, 0x1E, 0x22, 0x66, 0xAA,
  0xE5, 0x34, 0x5C, 0xE4, 0x37, 0x59, 0xEB, 0x26, 0x6A, 0xBE, 0xD9, 0x70, 0x90, 0xAB, 0xE6, 0x31,
  0x53, 0xF5, 0x04, 0x0C, 0x14, 0x3C, 0x44, 0xCC, 0x4F, 0xD1, 0x68, 0xB8, 0xD3, 0x6E, 0xB2, 0xCD,
  0x4C, 0xD4, 0x67, 0xA9, 0xE0, 0x3B, 0x4D, 0xD7, 0x62, 0xA6, 0xF1, 0x08, 0x18, 0x28, 0x78, 0x88,
  0x83, 0x9E, 0xB9, 0xD0, 0x6B, 0xBD, 0xDC, 0x7F, 0x81, 0x98, 0xB3, 0xCE, 0x49, 0xDB, 0x76, 0x9A,
  0xB5, 0xC4, 0x57, 0xF9, 0x10, 0x30, 0x50, 0xF0, 0x0B, 0x1D, 0x27, 0x69, 0xBB, 0xD6, 0x61, 0xA3,
  0xFE, 0x19, 0x2B, 0x7D, 0x87, 0x92, 0xAD, 0xEC, 0x2F, 0x71, 0x93, 0xAE, 0xE9, 0x20, 0x60, 0xA0,
  0xFB, 0x16, 0x3A, 0x4E, 0xD2, 0x6D, 0xB7, 0xC2, 0x5D, 0xE7, 0x32, 0x56, 0xFA, 0x15, 0x3F, 0x41,
  0xC3, 0x5E, 0xE2, 0x3D, 0x47, 0xC9, 0x40, 0xC0, 0x5B, 0xED, 0x2C, 0x74, 0x9C, 0xBF, 0xDA, 0x75,
  0x9F, 0xBA, 0xD5, 0x64, 0xAC, 0xEF, 0x2A, 0x7E, 0x82, 0x9D, 0xBC, 0xDF, 0x7A, 0x8E, 0x89, 0x80,
  0x9B, 0xB6, 0xC1, 0x58, 0xE8, 0x23, 0x65, 0xAF, 0xEA, 0x25, 0x6F, 0xB1, 0xC8, 0x43, 0xC5, 0x54,
  0xFC, 0x1F, 0x21, 0x63, 0xA5, 0xF4, 0x07, 0x09, 0x1B, 0x2D, 0x77, 0x99, 0xB0, 0xCB, 0x46, 0xCA,
  0x45, 0xCF, 0x4A, 0xDE, 0x79, 0x8B, 0x86, 0x91, 0xA8, 0xE3, 0x3E, 0x42, 0xC6, 0x51, 0xF3, 0x0E,
  0x12, 0x36, 0x5A, 0xEE, 0x29, 0x7B, 0x8D, 0x8C, 0x8F, 0x8A, 0x85, 0x94, 0xA7, 0xF2, 0x0D, 0x17,
  0x39, 0x4B, 0xDD, 0x7C, 0x84, 0x97, 0xA2, 0xFD, 0x1C, 0x24, 0x6C, 0xB4, 0xC7, 0x52, 0xF6, 0x01};

static ui8_t log_11B[256] = {  // 0x11B: log_a , a = 0x03 = X+1
  -00 , 0x00, 0x19, 0x01, 0x32, 0x02, 0x1A, 0xC6, 0x4B, 0xC7, 0x1B, 0x68, 0x33, 0xEE, 0xDF, 0x03,
  0x64, 0x04, 0xE0, 0x0E, 0x34, 0x8D, 0x81, 0xEF, 0x4C, 0x71, 0x08, 0xC8, 0xF8, 0x69, 0x1C, 0xC1,
  0x7D, 0xC2, 0x1D, 0xB5, 0xF9, 0xB9, 0x27, 0x6A, 0x4D, 0xE4, 0xA6, 0x72, 0x9A, 0xC9, 0x09, 0x78,
  0x65, 0x2F, 0x8A, 0x05, 0x21, 0x0F, 0xE1, 0x24, 0x12, 0xF0, 0x82, 0x45, 0x35, 0x93, 0xDA, 0x8E,
  0x96, 0x8F, 0xDB, 0xBD, 0x36, 0xD0, 0xCE, 0x94, 0x13, 0x5C, 0xD2, 0xF1, 0x40, 0x46, 0x83, 0x38,
  0x66, 0xDD, 0xFD, 0x30, 0xBF, 0x06, 0x8B, 0x62, 0xB3, 0x25, 0xE2, 0x98, 0x22, 0x88, 0x91, 0x10,
  0x7E, 0x6E, 0x48, 0xC3, 0xA3, 0xB6, 0x1E, 0x42, 0x3A, 0x6B, 0x28, 0x54, 0xFA, 0x85, 0x3D, 0xBA,
  0x2B, 0x79, 0x0A, 0x15, 0x9B, 0x9F, 0x5E, 0xCA, 0x4E, 0xD4, 0xAC, 0xE5, 0xF3, 0x73, 0xA7, 0x57,
  0xAF, 0x58, 0xA8, 0x50, 0xF4, 0xEA, 0xD6, 0x74, 0x4F, 0xAE, 0xE9, 0xD5, 0xE7, 0xE6, 0xAD, 0xE8,
  0x2C, 0xD7, 0x75, 0x7A, 0xEB, 0x16, 0x0B, 0xF5, 0x59, 0xCB, 0x5F, 0xB0, 0x9C, 0xA9, 0x51, 0xA0,
  0x7F, 0x0C, 0xF6, 0x6F, 0x17, 0xC4, 0x49, 0xEC, 0xD8, 0x43, 0x1F, 0x2D, 0xA4, 0x76, 0x7B, 0xB7,
  0xCC, 0xBB, 0x3E, 0x5A, 0xFB, 0x60, 0xB1, 0x86, 0x3B, 0x52, 0xA1, 0x6C, 0xAA, 0x55, 0x29, 0x9D,
  0x97, 0xB2, 0x87, 0x90, 0x61, 0xBE, 0xDC, 0xFC, 0xBC, 0x95, 0xCF, 0xCD, 0x37, 0x3F, 0x5B, 0xD1,
  0x53, 0x39, 0x84, 0x3C, 0x41, 0xA2, 0x6D, 0x47, 0x14, 0x2A, 0x9E, 0x5D, 0x56, 0xF2, 0xD3, 0xAB,
  0x44, 0x11, 0x92, 0xD9, 0x23, 0x20, 0x2E, 0x89, 0xB4, 0x7C, 0xB8, 0x26, 0x77, 0x99, 0xE3, 0xA5,
  0x67, 0x4A, 0xED, 0xDE, 0xC5, 0x31, 0xFE, 0x18, 0x0D, 0x63, 0x8C, 0x80, 0xC0, 0xF7, 0x70, 0x07};

// ------------------------------------------------------------------------------------------------

// F2[X] mod X^8 + X^4 + X^3 + X^2 + 1

static ui8_t exp_11D[256] = {  // 0x11D: a^n , a = 0x02 = X
  0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1D, 0x3A, 0x74, 0xE8, 0xCD, 0x87, 0x13, 0x26,
  0x4C, 0x98, 0x2D, 0x5A, 0xB4, 0x75, 0xEA, 0xC9, 0x8F, 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0,
  0x9D, 0x27, 0x4E, 0x9C, 0x25, 0x4A, 0x94, 0x35, 0x6A, 0xD4, 0xB5, 0x77, 0xEE, 0xC1, 0x9F, 0x23,
  0x46, 0x8C, 0x05, 0x0A, 0x14, 0x28, 0x50, 0xA0, 0x5D, 0xBA, 0x69, 0xD2, 0xB9, 0x6F, 0xDE, 0xA1,
  0x5F, 0xBE, 0x61, 0xC2, 0x99, 0x2F, 0x5E, 0xBC, 0x65, 0xCA, 0x89, 0x0F, 0x1E, 0x3C, 0x78, 0xF0,
  0xFD, 0xE7, 0xD3, 0xBB, 0x6B, 0xD6, 0xB1, 0x7F, 0xFE, 0xE1, 0xDF, 0xA3, 0x5B, 0xB6, 0x71, 0xE2,
  0xD9, 0xAF, 0x43, 0x86, 0x11, 0x22, 0x44, 0x88, 0x0D, 0x1A, 0x34, 0x68, 0xD0, 0xBD, 0x67, 0xCE,
  0x81, 0x1F, 0x3E, 0x7C, 0xF8, 0xED, 0xC7, 0x93, 0x3B, 0x76, 0xEC, 0xC5, 0x97, 0x33, 0x66, 0xCC,
  0x85, 0x17, 0x2E, 0x5C, 0xB8, 0x6D, 0xDA, 0xA9, 0x4F, 0x9E, 0x21, 0x42, 0x84, 0x15, 0x2A, 0x54,
  0xA8, 0x4D, 0x9A, 0x29, 0x52, 0xA4, 0x55, 0xAA, 0x49, 0x92, 0x39, 0x72, 0xE4, 0xD5, 0xB7, 0x73,
  0xE6, 0xD1, 0xBF, 0x63, 0xC6, 0x91, 0x3F, 0x7E, 0xFC, 0xE5, 0xD7, 0xB3, 0x7B, 0xF6, 0xF1, 0xFF,
  0xE3, 0xDB, 0xAB, 0x4B, 0x96, 0x31, 0x62, 0xC4, 0x95, 0x37, 0x6E, 0xDC, 0xA5, 0x57, 0xAE, 0x41,
  0x82, 0x19, 0x32, 0x64, 0xC8, 0x8D, 0x07, 0x0E, 0x1C, 0x38, 0x70, 0xE0, 0xDD, 0xA7, 0x53, 0xA6,
  0x51, 0xA2, 0x59, 0xB2, 0x79, 0xF2, 0xF9, 0xEF, 0xC3, 0x9B, 0x2B, 0x56, 0xAC, 0x45, 0x8A, 0x09,
  0x12, 0x24, 0x48, 0x90, 0x3D, 0x7A, 0xF4, 0xF5, 0xF7, 0xF3, 0xFB, 0xEB, 0xCB, 0x8B, 0x0B, 0x16,
  0x2C, 0x58, 0xB0, 0x7D, 0xFA, 0xE9, 0xCF, 0x83, 0x1B, 0x36, 0x6C, 0xD8, 0xAD, 0x47, 0x8E, 0x01};

static ui8_t log_11D[256] = {  // 0x11D: log_a , a = 0x02 = X
  -00 , 0x00, 0x01, 0x19, 0x02, 0x32, 0x1A, 0xC6, 0x03, 0xDF, 0x33, 0xEE, 0x1B, 0x68, 0xC7, 0x4B,
  0x04, 0x64, 0xE0, 0x0E, 0x34, 0x8D, 0xEF, 0x81, 0x1C, 0xC1, 0x69, 0xF8, 0xC8, 0x08, 0x4C, 0x71,
  0x05, 0x8A, 0x65, 0x2F, 0xE1, 0x24, 0x0F, 0x21, 0x35, 0x93, 0x8E, 0xDA, 0xF0, 0x12, 0x82, 0x45,
  0x1D, 0xB5, 0xC2, 0x7D, 0x6A, 0x27, 0xF9, 0xB9, 0xC9, 0x9A, 0x09, 0x78, 0x4D, 0xE4, 0x72, 0xA6,
  0x06, 0xBF, 0x8B, 0x62, 0x66, 0xDD, 0x30, 0xFD, 0xE2, 0x98, 0x25, 0xB3, 0x10, 0x91, 0x22, 0x88,
  0x36, 0xD0, 0x94, 0xCE, 0x8F, 0x96, 0xDB, 0xBD, 0xF1, 0xD2, 0x13, 0x5C, 0x83, 0x38, 0x46, 0x40,
  0x1E, 0x42, 0xB6, 0xA3, 0xC3, 0x48, 0x7E, 0x6E, 0x6B, 0x3A, 0x28, 0x54, 0xFA, 0x85, 0xBA, 0x3D,
  0xCA, 0x5E, 0x9B, 0x9F, 0x0A, 0x15, 0x79, 0x2B, 0x4E, 0xD4, 0xE5, 0xAC, 0x73, 0xF3, 0xA7, 0x57,
  0x07, 0x70, 0xC0, 0xF7, 0x8C, 0x80, 0x63, 0x0D, 0x67, 0x4A, 0xDE, 0xED, 0x31, 0xC5, 0xFE, 0x18,
  0xE3, 0xA5, 0x99, 0x77, 0x26, 0xB8, 0xB4, 0x7C, 0x11, 0x44, 0x92, 0xD9, 0x23, 0x20, 0x89, 0x2E,
  0x37, 0x3F, 0xD1, 0x5B, 0x95, 0xBC, 0xCF, 0xCD, 0x90, 0x87, 0x97, 0xB2, 0xDC, 0xFC, 0xBE, 0x61,
  0xF2, 0x56, 0xD3, 0xAB, 0x14, 0x2A, 0x5D, 0x9E, 0x84, 0x3C, 0x39, 0x53, 0x47, 0x6D, 0x41, 0xA2,
  0x1F, 0x2D, 0x43, 0xD8, 0xB7, 0x7B, 0xA4, 0x76, 0xC4, 0x17, 0x49, 0xEC, 0x7F, 0x0C, 0x6F, 0xF6,
  0x6C, 0xA1, 0x3B, 0x52, 0x29, 0x9D, 0x55, 0xAA, 0xFB, 0x60, 0x86, 0xB1, 0xBB, 0xCC, 0x3E, 0x5A,
  0xCB, 0x59, 0x5F, 0xB0, 0x9C, 0xA9, 0xA0, 0x51, 0x0B, 0xF5, 0x16, 0xEB, 0x7A, 0x75, 0x2C, 0xD7,
  0x4F, 0xAE, 0xD5, 0xE9, 0xE6, 0xE7, 0xAD, 0xE8, 0x74, 0xD6, 0xF4, 0xEA, 0xA8, 0x50, 0x58, 0xAF};

// ------------------------------------------------------------------------------------------------

// F2[X] mod X^6 + X + 1 : 0x43

static ui8_t exp64[64] = {  // 0x43: a^n , a = 0x02 = X
  0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x03, 0x06, 0x0C, 0x18, 0x30, 0x23, 0x05, 0x0A, 0x14, 0x28,
  0x13, 0x26, 0x0F, 0x1E, 0x3C, 0x3B, 0x35, 0x29, 0x11, 0x22, 0x07, 0x0E, 0x1C, 0x38, 0x33, 0x25,
  0x09, 0x12, 0x24, 0x0B, 0x16, 0x2C, 0x1B, 0x36, 0x2F, 0x1D, 0x3A, 0x37, 0x2D, 0x19, 0x32, 0x27,
  0x0D, 0x1A, 0x34, 0x2B, 0x15, 0x2A, 0x17, 0x2E, 0x1F, 0x3E, 0x3F, 0x3D, 0x39, 0x31, 0x21, 0x01};

static ui8_t log64[64] = {
  -00 , 0x00, 0x01, 0x06, 0x02, 0x0C, 0x07, 0x1A, 0x03, 0x20, 0x0D, 0x23, 0x08, 0x30, 0x1B, 0x12,
  0x04, 0x18, 0x21, 0x10, 0x0E, 0x34, 0x24, 0x36, 0x09, 0x2D, 0x31, 0x26, 0x1C, 0x29, 0x13, 0x38,
  0x05, 0x3E, 0x19, 0x0B, 0x22, 0x1F, 0x11, 0x2F, 0x0F, 0x17, 0x35, 0x33, 0x25, 0x2C, 0x37, 0x28,
  0x0A, 0x3D, 0x2E, 0x1E, 0x32, 0x16, 0x27, 0x2B, 0x1D, 0x3C, 0x2A, 0x15, 0x14, 0x3B, 0x39, 0x3A};

// ------------------------------------------------------------------------------------------------

// F2[X] mod X^4 + X + 1 : 0x13

static ui8_t exp16[16] = {  // 0x43: a^n , a = 0x02 = X
  0x1, 0x2, 0x4, 0x8, 0x3, 0x6, 0xC, 0xB,
  0x5, 0xA, 0x7, 0xE, 0xF, 0xD, 0x9, 0x1};

static ui8_t log16[16] = {
  -00, 0x0, 0x1, 0x4, 0x2, 0x8, 0x5, 0xA,
  0x3, 0xE, 0x9, 0x7, 0x6, 0xD, 0xB, 0xC};

*/
/* --------------------------------------------------------------------------------------------- */

static ui8_t GF_mul(ui8_t p, ui8_t q) {
  ui32_t x;
  if ((p == 0) || (q == 0)) return 0;
  x = (ui32_t)log_a[p] + log_a[q];
  return exp_a[x % (GF.ord-1)];     // a^(ord-1) = 1
}

static ui8_t GF_inv(ui8_t p) {
  if (p == 0) return 0;             // DIV_BY_ZERO
  return exp_a[GF.ord-1-log_a[p]];  // a^(ord-1) = 1
}

/* --------------------------------------------------------------------------------------------- */

/*
 *  p(x) = p[0] + p[1]x + ... + p[N-1]x^(N-1)
 */

static
ui8_t poly_eval(ui8_t poly[], ui8_t x) {
    int n;
    ui8_t xn, y;

    y = poly[0];
    if (x != 0) {
        for (n = 1; n < GF.ord-1; n++) {
            xn = exp_a[(n*log_a[x]) % (GF.ord-1)];
            y ^= GF_mul(poly[n], xn);
        }
    }
    return y;
}

static
ui8_t poly_evalH(ui8_t poly[], ui8_t x) {
    int n;
    ui8_t y;
    y = poly[GF.ord-1];
    for (n = GF.ord-2; n >= 0; n--) {
        y = GF_mul(y, x) ^ poly[n];
    }
    return y;
}


static
int poly_deg(ui8_t p[]) {
    int n = MAX_DEG;
    while (p[n] == 0 && n > 0) n--;
    if (p[n] == 0) n--;  // deg(0) = -inf
    return n;
}

static
int poly_divmod(ui8_t p[], ui8_t q[], ui8_t *d, ui8_t *r) {
  int deg_p, deg_q;            // p(x) = q(x)d(x) + r(x)
  int i;                       //        deg(r) < deg(q)
  ui8_t c;

  deg_p = poly_deg(p);
  deg_q = poly_deg(q);

  if (deg_q < 0) return -1;  // q=0: DIV_BY_ZERO

  for (i = 0; i <= MAX_DEG; i++) d[i] = 0;
  for (i = 0; i <= MAX_DEG; i++) r[i] = 0;


  c = GF_mul( p[deg_p], GF_inv(q[deg_q]));

  if (deg_q == 0) {
      for (i = 0; i <= deg_p;   i++) d[i] = GF_mul(p[i], c);
      for (i = 0; i <= MAX_DEG; i++) r[i] = 0;
  }
  else if (deg_p < 0) {  // p=0
      for (i = 0; i <= MAX_DEG; i++) {
          d[i] = 0;
          r[i] = 0;
      }
  }
  else if (deg_p < deg_q) {
      for (i = 0; i <= MAX_DEG; i++) d[i] = 0;
      for (i = 0; i <= deg_p; i++) r[i] = p[i]; // r(x)=p(x), deg(r)<deg(q)
      for (i = deg_p+1; i <= MAX_DEG; i++) r[i] = 0;
  }
  else {
      for (i = 0; i <= deg_p; i++) r[i] = p[i];
      while (deg_p >= deg_q) {
          d[deg_p-deg_q] = c;
          for (i = 0; i <= deg_q; i++) {
              r[deg_p-i] ^= GF_mul( q[deg_q-i], c);
          }
          while (r[deg_p] == 0 && deg_p > 0) deg_p--;
          if (r[deg_p] == 0) deg_p--;
          if (deg_p >= 0) c = GF_mul( r[deg_p], GF_inv(q[deg_q]));
      }
  }
  return 0;
}

static
int poly_add(ui8_t a[], ui8_t b[], ui8_t *sum) {
    int i;
    ui8_t c[MAX_DEG+1];

    for (i = 0; i <= MAX_DEG; i++) {
            c[i] = a[i] ^ b[i];
    }

    for (i = 0; i <= MAX_DEG; i++) { sum[i] = c[i]; }

    return 0;
}

static
int poly_mul(ui8_t a[], ui8_t b[], ui8_t *ab) {
    int i, j;
    ui8_t c[MAX_DEG+1];

    if (poly_deg(a)+poly_deg(b) > MAX_DEG) {
       return -1;
    }

    for (i = 0; i <= MAX_DEG; i++) { c[i] = 0; }

    for (i = 0; i <= poly_deg(a); i++) {
        for (j = 0; j <= poly_deg(b); j++) {
            c[i+j] ^= GF_mul(a[i], b[j]);
        }
    }

    for (i = 0; i <= MAX_DEG; i++) { ab[i] = c[i]; }

    return 0;
}


static
int polyGF_eggT(int deg, ui8_t a[], ui8_t b[],  // in
                ui8_t *u, ui8_t *v, ui8_t *ggt  // out
               ) {
// deg = 0:
// a(x)u(x)+b(x)v(x) = ggt(x)
// RS:
// deg=t, a(x)=S(x), b(x)=x^2t

    int i;
    ui8_t r0[MAX_DEG+1], r1[MAX_DEG+1], r2[MAX_DEG+1],
          s0[MAX_DEG+1], s1[MAX_DEG+1], s2[MAX_DEG+1],
          t0[MAX_DEG+1], t1[MAX_DEG+1], t2[MAX_DEG+1],
          quo[MAX_DEG+1];

    for (i = 0; i <= MAX_DEG; i++) { u[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { v[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { ggt[i] = 0; }

    for (i = 0; i <= MAX_DEG; i++) { r0[i] = a[i]; }
    for (i = 0; i <= MAX_DEG; i++) { r1[i] = b[i]; }
    s0[0] = 1; for (i = 1; i <= MAX_DEG; i++) { s0[i] = 0; } // s0 = 1
    s1[0] = 0; for (i = 1; i <= MAX_DEG; i++) { s1[i] = 0; } // s1 = 0
    t0[0] = 0; for (i = 1; i <= MAX_DEG; i++) { t0[i] = 0; } // t0 = 0
    t1[0] = 1; for (i = 1; i <= MAX_DEG; i++) { t1[i] = 0; } // t1 = 1
    for (i = 0; i <= MAX_DEG; i++) { r2[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { s2[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { t2[i] = 0; }

    while ( poly_deg(r1) >= deg ) {
        poly_divmod(r0, r1, quo, r2);
        for (i = 0; i <= MAX_DEG; i++) { r0[i] = r1[i]; }
        for (i = 0; i <= MAX_DEG; i++) { r1[i] = r2[i]; }
        poly_mul(quo, s1, s2);
        poly_add(s0, s2, s2);
        for (i = 0; i <= MAX_DEG; i++) { s0[i] = s1[i]; }
        for (i = 0; i <= MAX_DEG; i++) { s1[i] = s2[i]; }
        poly_mul(quo, t1, t2);
        poly_add(t0, t2, t2);
        for (i = 0; i <= MAX_DEG; i++) { t0[i] = t1[i]; }
        for (i = 0; i <= MAX_DEG; i++) { t1[i] = t2[i]; }
    }

    if (deg > 0) {
        for (i = 0; i <= MAX_DEG; i++) { ggt[i] = r1[i]; } // deg=0: r0
        for (i = 0; i <= MAX_DEG; i++) { u[i] = s1[i]; }   // deg=0: s0
        for (i = 0; i <= MAX_DEG; i++) { v[i] = t1[i]; }
    }
    else {
        for (i = 0; i <= MAX_DEG; i++) { ggt[i] = r0[i]; }
        for (i = 0; i <= MAX_DEG; i++) { u[i] = s0[i]; }
        for (i = 0; i <= MAX_DEG; i++) { v[i] = t0[i]; }
    }

    return 0;
}

static
int polyGF_lfsr(int deg, int x2t, ui8_t S[],
                ui8_t *Lambda, ui8_t *Omega ) {
// BCH/RS/LFSR: deg=t+e/2, e=#erasures
// S(x)Lambda(x) = Omega(x) mod x^2t
    int i;
    ui8_t r0[MAX_DEG+1], r1[MAX_DEG+1], r2[MAX_DEG+1],
          s0[MAX_DEG+1], s1[MAX_DEG+1], s2[MAX_DEG+1],
          quo[MAX_DEG+1];

    for (i = 0; i <= MAX_DEG; i++) { Lambda[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { Omega[i] = 0; }

    for (i = 0; i <= MAX_DEG; i++) { r0[i] = S[i]; }
    for (i = 0; i <= MAX_DEG; i++) { r1[i] = 0; } r1[x2t] = 1; //x^2t
    s0[0] = 1; for (i = 1; i <= MAX_DEG; i++) { s0[i] = 0; }
    s1[0] = 0; for (i = 1; i <= MAX_DEG; i++) { s1[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { r2[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { s2[i] = 0; }

    while ( poly_deg(r1) >= deg ) { // deg=t+e/2
        poly_divmod(r0, r1, quo, r2);
        for (i = 0; i <= MAX_DEG; i++) { r0[i] = r1[i]; }
        for (i = 0; i <= MAX_DEG; i++) { r1[i] = r2[i]; }

        poly_mul(quo, s1, s2);
        poly_add(s0, s2, s2);
        for (i = 0; i <= MAX_DEG; i++) { s0[i] = s1[i]; }
        for (i = 0; i <= MAX_DEG; i++) { s1[i] = s2[i]; }
    }

// deg > 0:
    for (i = 0; i <= MAX_DEG; i++) { Omega[i]  = r1[i]; }
    for (i = 0; i <= MAX_DEG; i++) { Lambda[i] = s1[i]; }

    return 0;
}

static
int poly_D(ui8_t a[], ui8_t *Da) {
    int i;

    for (i = 0; i <= MAX_DEG; i++) { Da[i] = 0; } // unten werden nicht immer
                                                  // alle Koeffizienten gesetzt
    for (i = 1; i <= poly_deg(a); i++) {
        if (i % 2) Da[i-1] = a[i];   // GF(2^n): b+b=0
    }

    return 0;
}

static
ui8_t forney(ui8_t x, ui8_t Omega[], ui8_t Lambda[]) {
    ui8_t DLam[MAX_DEG+1];
    ui8_t w, z, Y;         //  x=X^(-1), Y = x^(b-1) * Omega(x)/Lambda'(x)
                           //            Y = X^(1-b) * Omega(X^(-1))/Lambda'(X^(-1))
    poly_D(Lambda, DLam);
    w = poly_eval(Omega, x);
    z = poly_eval(DLam, x); if (z == 0) { return -00; }
    Y = GF_mul(w, GF_inv(z));
    if (RS.b == 0) Y = GF_mul(GF_inv(x), Y);
    else if (RS.b > 1) {
        ui8_t xb1 = exp_a[((RS.b-1)*log_a[x]) % (GF.ord-1)];
        Y = GF_mul(xb1, Y);
    }

    return Y;
}

static
int era_sigma(int n, ui8_t era_pos[], ui8_t *sigma) {
    int i;
    ui8_t Xa[MAX_DEG+1], sig[MAX_DEG+1];
    ui8_t a_i;

    for (i = 0; i <= MAX_DEG; i++) sig[i] = 0;
    for (i = 0; i <= MAX_DEG; i++) Xa[i] = 0;

    // sigma(X)=(1 - alpha^j1 X)...(1 - alpha^jn X)
    // j_{i+1} = era_pos[i]
    sig[0] = 1;
    Xa[0] = 1;
    for (i = 0; i < n; i++) { // n <= 2*RS.t
        a_i = exp_a[(RS.p*era_pos[i]) % (GF.ord-1)];
        Xa[1] = a_i;  // Xalp[0..1]: (1-alpha^(j_)X)
        poly_mul(sig, Xa, sig);
    }

    for (i = 0; i <= MAX_DEG; i++) sigma[i] = sig[i];

    return 0;
}

static
int syndromes(ui8_t cw[], ui8_t *S) {
    int i, errors = 0;
    ui8_t a_i;

    // syndromes: e_j=S((alpha^p)^(b+i))  (wie in g(X))
    for (i = 0; i < 2*RS.t; i++) {
        a_i = exp_a[(RS.p*(RS.b+i)) % (GF.ord-1)];  // (alpha^p)^(b+i)
        S[i] = poly_eval(cw, a_i);
        if (S[i]) errors = 1;
    }
    return errors;
}


static
int prn_GFpoly(ui32_t p) {
  int i, s;
  s = 0;
  if (p == 0) printf("0");
  else {
    if (p != (p & 0x1FF)) printf(" (& 0x1FF) ");
    for (i=8; i>1; i--) {
      if ( (p>>i) & 1 ) {
        if (s) printf(" + ");
        printf("X^%d", i);
        s = 1;
      }
    }
    if ( (p>>1) & 1 ) {
      if (s) printf(" + ");
      printf("X");
      s = 1;
    }
    if ( p & 1 ) {
      if (s) printf(" + ");
      printf("1");
    }
  }
  return 0;
}

static
void prn_table(void) {
    int i;

    printf("F2[X] mod ");
    prn_GFpoly(GF.f);
    printf(" : 0x%X\n", GF.f);
    printf("\n");

    printf("a^n[%d] = {\n", GF.ord);
    printf(" ");
    for (i=0; i<GF.ord; i++) {
        printf(" 0x");
        printf("%02X", exp_a[i]);
        if (i<GF.ord-1) printf(","); else printf("}");
        if (i%16 == 15) printf("\n ");
    }
    printf("\n");

    printf("log_a(n)[%d] = {\n", GF.ord);
    printf(" ");
    printf(" -oo ,"); // log(0)
    for (i=1; i<GF.ord; i++) {
        printf(" 0x");
        printf("%02X", log_a[i]);
        if (i<GF.ord-1) printf(","); else printf("}");
        if (i%16 == 15) printf("\n ");
    }
    printf("\n");
}


int rs_init_RS255() {
    int i, check_gen;
    ui8_t Xalp[MAX_DEG+1];

    GF = GF256RS;
    check_gen = GF_genTab( GF, exp_a, log_a);

    RS = RS256; // N=255, t=12, b=0, p=1
    for (i = 0; i <= MAX_DEG; i++) RS.g[i] = 0;
    for (i = 0; i <= MAX_DEG; i++) Xalp[i] = 0;

    // g(X)=(X-alpha^b)...(X-alpha^(b+2t-1)), b=0
    RS.g[0] = 0x01;
    Xalp[1] = 0x01; // X
    for (i = 0; i < 2*RS.t; i++) {
        Xalp[0] = exp_a[(RS.b+i) % (GF.ord-1)];  // Xalp[0..1]: X - alpha^(b+i)
        poly_mul(RS.g, Xalp, RS.g);
    }

    return check_gen;
}

int rs_init_RS255ccsds() {
    int i, check_gen;
    ui8_t Xalp[MAX_DEG+1];

    GF = GF256RSccsds;
    check_gen = GF_genTab( GF, exp_a, log_a);

    RS = RS256ccsds; // N=255, t=16, b=112, p=11
    for (i = 0; i <= MAX_DEG; i++) RS.g[i] = 0;
    for (i = 0; i <= MAX_DEG; i++) Xalp[i] = 0;

    // beta=alpha^p primitive root of g(X)
    // beta^ip=alpha  // N=255, p=11 -> ip=116
    for (i = 1; i < GF.ord-1; i++) {
        if ( (RS.p * i) % (GF.ord-1) == 1 ) {
            RS.ip = i;
            break;
        }
    }

    // g(X)=(X-(alpha^p)^b)...(X-(alpha^p)^(b+2t-1)), b=112
    RS.g[0] = 0x01;
    Xalp[1] = 0x01; // X
    for (i = 0; i < 2*RS.t; i++) {
        Xalp[0] = exp_a[(RS.p*(RS.b+i)) % (GF.ord-1)];  // Xalp[0..1]: X - (alpha^p)^(b+i)
        poly_mul(RS.g, Xalp, RS.g);
    }
/*
    RS.g[ 0] = RS.g[32] = exp_a[0];
    RS.g[ 1] = RS.g[31] = exp_a[249];
    RS.g[ 2] = RS.g[30] = exp_a[59];
    RS.g[ 3] = RS.g[29] = exp_a[66];
    RS.g[ 4] = RS.g[28] = exp_a[4];
    RS.g[ 5] = RS.g[27] = exp_a[43];
    RS.g[ 6] = RS.g[26] = exp_a[126];
    RS.g[ 7] = RS.g[25] = exp_a[251];
    RS.g[ 8] = RS.g[24] = exp_a[97];
    RS.g[ 9] = RS.g[23] = exp_a[30];
    RS.g[10] = RS.g[22] = exp_a[3];
    RS.g[11] = RS.g[21] = exp_a[213];
    RS.g[12] = RS.g[20] = exp_a[50];
    RS.g[13] = RS.g[19] = exp_a[66];
    RS.g[14] = RS.g[18] = exp_a[170];
    RS.g[15] = RS.g[17] = exp_a[5];
    RS.g[16] = exp_a[24];
*/
    return check_gen;
}

int rs_init_BCH64() {
    int i, check_gen;

    GF = GF64BCH;
    check_gen = GF_genTab( GF, exp_a, log_a);

    RS = BCH64; // N=63, t=2, b=1
    for (i = 0; i <= MAX_DEG; i++) RS.g[i] = 0;

    // g(X)=X^12+X^10+X^8+X^5+X^4+X^3+1
    //     =(X^6+X+1)(X^6+X^4+X^2+X+1)
    RS.g[0] = RS.g[3] = RS.g[4] = RS.g[5] = RS.g[8] = RS.g[10] = RS.g[12] = 1;

    return check_gen;
}

int rs_init_RS15ccsds() {
    int i, check_gen;
    ui8_t Xalp[MAX_DEG+1];

    GF = GF16RS;
    check_gen = GF_genTab( GF, exp_a, log_a);

    //RS = RS16_0; // N=15, t=3, b=0, p=1
    RS = RS16ccsds; // N=15, t=2, b=6, p=1
    for (i = 0; i <= MAX_DEG; i++) RS.g[i] = 0;
    for (i = 0; i <= MAX_DEG; i++) Xalp[i] = 0;

    // g(X)=(X-alpha^b)...(X-alpha^(b+2t-1))
    RS.g[0] = 0x01;
    Xalp[1] = 0x01; // X
    for (i = 0; i < 2*RS.t; i++) {
        Xalp[0] = exp_a[(RS.b+i) % (GF.ord-1)];  // Xalp[0..1]: X - alpha^(b+i)
        poly_mul(RS.g, Xalp, RS.g);
    }

    return check_gen;
}

int rs_encode(ui8_t cw[]) {
    int j;
    ui8_t __cw[MAX_DEG+1],
          parity[MAX_DEG+1],
          d[MAX_DEG+1];
    for (j = 0; j <= MAX_DEG; j++) parity[j] = 0;
    for (j = 0; j <= MAX_DEG; j++) __cw[j] = 0;
    for (j = RS.R; j < RS.N; j++) __cw[j] = cw[j];
    poly_divmod(__cw, RS.g, d, parity);
    //if (poly_deg(parity) >= RS.R) return -1;
    for (j = 0; j < RS.R; j++) cw[j] = parity[j];
    return 0;
}

// 2*Errors + Erasure <= 2*t
int rs_decode_ErrEra(ui8_t cw[], int nera, ui8_t era_pos[],
                     ui8_t *err_pos, ui8_t *err_val) {
    ui8_t x, gamma;
    ui8_t S[MAX_DEG+1],
          Lambda[MAX_DEG+1],
          Omega[MAX_DEG+1],
          sigma[MAX_DEG+1],
          sigLam[MAX_DEG+1];
    int deg_sigLam, deg_Lambda, deg_Omega;
    int i, nerr, errera = 0;

    if (nera > 2*RS.t) { return -4; }

    for (i = 0; i < 2*RS.t; i++) { err_pos[i] = 0; }
    for (i = 0; i < 2*RS.t; i++) { err_val[i] = 0; }

    // IF: erasures set 0
    //    for (i = 0; i < nera; i++) cw[era_pos[i]] = 0x00; // erasures
    // THEN: restore cw[era_pos[i]], if errera < 0

    for (i = 0; i <= MAX_DEG; i++) { S[i] = 0; }
    errera = syndromes(cw, S);
    // wenn  S(x)=0 ,  dann poly_divmod(cw, RS.g, d, rem): rem=0

    for (i = 0; i <= MAX_DEG; i++) { sigma[i] = 0; }
    sigma[0] = 1;


    if (nera > 0) {
        era_sigma(nera, era_pos, sigma);
        poly_mul(sigma, S, S);
        for (i = 2*RS.t; i <= MAX_DEG; i++) S[i] = 0; // S = sig*S mod x^2t
    }

    if (errera)
    {
        polyGF_lfsr(RS.t+nera/2, 2*RS.t, S, Lambda, Omega);

        deg_Lambda = poly_deg(Lambda);
        deg_Omega  = poly_deg(Omega);
        if (deg_Omega >= deg_Lambda + nera) {
            errera = -3;
            return errera;
        }
        gamma = Lambda[0];
        if (gamma) {
            for (i = deg_Lambda; i >= 0; i--) Lambda[i] = GF_mul(Lambda[i], GF_inv(gamma));
            for (i = deg_Omega ; i >= 0; i--)  Omega[i] = GF_mul( Omega[i], GF_inv(gamma));
            poly_mul(sigma, Lambda, sigLam);
            deg_sigLam = poly_deg(sigLam);
        }
        else {
            errera = -2;
            return errera;
        }

        nerr = 0; // Errors + Erasures (erasure-pos bereits bekannt)
        for (i = 1; i < GF.ord ; i++) { // Lambda(0)=1
            x = (ui8_t)i;    // roll-over
            if (poly_eval(sigLam, x) == 0) { // Lambda(x)=0 fuer x in erasures[] moeglich
                // error location index
                ui8_t x1 = GF_inv(x);
                err_pos[nerr] = (log_a[x1]*RS.ip) % (GF.ord-1);
                // error value;   bin-BCH: err_val=1
                err_val[nerr] = forney(x, Omega, sigLam);
                //err_val[nerr] == 0, wenn era_val[pos]=0, d.h. cw[pos] schon korrekt
                nerr++;
            }
            if (nerr >= deg_sigLam) break;
        }

        // 2*Errors + Erasure <= 2*t
        if (nerr < deg_sigLam) errera = -1; // uncorrectable errors
        else {
            errera = nerr;
            for (i = 0; i < errera; i++) cw[err_pos[i]] ^= err_val[i];
        }
    }

    return errera;
}

// Errors <= t
int rs_decode(ui8_t cw[], ui8_t *err_pos, ui8_t *err_val) {
    ui8_t tmp[1] = {0};
    return rs_decode_ErrEra(cw, 0, tmp, err_pos, err_val);
}

int rs_decode_bch_gf2t2(ui8_t cw[], ui8_t *err_pos, ui8_t *err_val) {
// binary 2-error correcting BCH

    ui8_t x, gamma,
          S[MAX_DEG+1],
          L[MAX_DEG+1], L2,
          Lambda[MAX_DEG+1],
          Omega[MAX_DEG+1];
    int i, n, errors = 0;


    for (i = 0; i < RS.t; i++) { err_pos[i] = 0; }
    for (i = 0; i < RS.t; i++) { err_val[i] = 0; }

    for (i = 0; i <= MAX_DEG; i++) { S[i] = 0; }
    errors = syndromes(cw, S);
    // wenn  S(x)=0 ,  dann poly_divmod(cw, RS.g, d, rem): rem=0

    if (errors) {
        polyGF_lfsr(RS.t, 2*RS.t, S, Lambda, Omega);
        gamma = Lambda[0];
        if (gamma) {
            for (i = poly_deg(Lambda); i >= 0; i--) Lambda[i] = GF_mul(Lambda[i], GF_inv(gamma));
            for (i = poly_deg(Omega) ; i >= 0; i--)  Omega[i] = GF_mul( Omega[i], GF_inv(gamma));
        }
        else {
            errors = -2;
            return errors;
        }

        // GF(2)-BCH, t=2:
        // S1 = S[0]
        //   S1^2 = S2 , S2^2 = S4
        // L(x) = 1 + L1 x + L2 x^2  (1-2 errors)
        //   L1 = S1 , L2 = (S3 + S1^3)/S1
        if ( RS.t == 2 ) {

            for (i = 0; i <= MAX_DEG; i++) { L[i] = 0; }
            L[0] = 1;
            L[1] = S[0];
            L2 = GF_mul(S[0], S[0]); L2 = GF_mul(L2, S[0]); L2 ^= S[2];
            L2 = GF_mul(L2, GF_inv(S[0]));
            L[2] = L2;

            if (S[1] != GF_mul(S[0],S[0]) || S[3] != GF_mul(S[1],S[1])) {
                errors = -2;
                return errors;
            }
            if (L[1] != Lambda[1] || L[2] != Lambda[2] ) {
                errors = -2;
                return errors;
            }
        }

        n = 0;
        for (i = 1; i < GF.ord ; i++) { // Lambda(0)=1
            x = (ui8_t)i;    // roll-over
            if (poly_eval(Lambda, x) == 0) {
                // error location index
                err_pos[n] = log_a[GF_inv(x)];
                // error value;   bin-BCH: err_val=1
                err_val[n] = 1; // = forney(x, Omega, Lambda);
                n++;
            }
            if (n >= poly_deg(Lambda)) break;
        }

        if (n < poly_deg(Lambda)) errors = -1; // uncorrectable errors
        else {
            errors = n;
            for (i = 0; i < errors; i++) cw[err_pos[i]] ^= err_val[i];
        }
    }

    return errors;
}

