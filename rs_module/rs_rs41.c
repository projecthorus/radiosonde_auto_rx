
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "rs_data.h"
#include "rs_datum.h"
#include "rs_bch_ecc.h"


/*
   Vaisala data whitening

   LFSR: ab i=8 (mod 64):
   m[16+i] = m[i] ^ m[i+2] ^ m[i+4] ^ m[i+6]
   ________________3205590EF944C6262160C2EA795D6DA15469470CDCE85CF1
   F776827F0799A22C937C3063F5102E61D0BCB4B606AAF423786E3BAEBF7B4CC196833E51B1490898

   uint16 y[]:
   y[i+8] = y[i] ^ y[i+1] ^ y[i+2] ^ y[i+3]
*/

#define MASK_LEN 64
static
ui8_t mask[MASK_LEN] = { 0x96, 0x83, 0x3E, 0x51, 0xB1, 0x49, 0x08, 0x98,
                         0x32, 0x05, 0x59, 0x0E, 0xF9, 0x44, 0xC6, 0x26,
                         0x21, 0x60, 0xC2, 0xEA, 0x79, 0x5D, 0x6D, 0xA1,
                         0x54, 0x69, 0x47, 0x0C, 0xDC, 0xE8, 0x5C, 0xF1,
                         0xF7, 0x76, 0x82, 0x7F, 0x07, 0x99, 0xA2, 0x2C,
                         0x93, 0x7C, 0x30, 0x63, 0xF5, 0x10, 0x2E, 0x61,
                         0xD0, 0xBC, 0xB4, 0xB6, 0x06, 0xAA, 0xF4, 0x23,
                         0x78, 0x6E, 0x3B, 0xAE, 0xBF, 0x7B, 0x4C, 0xC1};

static                 // 10      B6      CA      11      22      96      12      F8      |
char headerbits_rs41[] = "0000100001101101010100111000100001000100011010010100100000011111";

static
ui8_t headerbytes_rs41[] = { 0x86, 0x35, 0xf4, 0x40, 0x93, 0xdf, 0x1a, 0x60}; // = xorbyte(xframe)
              //xframe[] = { 0x10, 0xB6, 0xCA, 0x11, 0x22, 0x96, 0x12, 0xF8}     = xorbyte( frame)

#define NDATA_LEN 320                    // std framelen 320
#define XDATA_LEN 198
#define FRAME_LEN (NDATA_LEN+XDATA_LEN)  // max framelen 518

#define BAUD            4800
#define BITS            8
#define BITFRAME_LEN    (FRAME_LEN*BITS)
#define RAWBITFRAME_LEN (BITFRAME_LEN)


// -------------------------------------------------------------

static ui32_t u4(ui8_t *bytes) {  // 32bit unsigned int
    ui32_t val = 0;
    memcpy(&val, bytes, 4);
    // val = bytes[0] | (bytes[1]<<8) | (bytes[2]<<16) | (bytes[3]<<24);
    return val;
}

static ui32_t u3(ui8_t *bytes) {  // 24bit unsigned int
    int val24 = 0;
    val24 = bytes[0] | (bytes[1]<<8) | (bytes[2]<<16);
    // = memcpy(&val, bytes, 3), val &= 0x00FFFFFF;
    return val24;
}

static ui32_t u2(ui8_t *bytes) {  // 16bit unsigned int
    return  bytes[0] | (bytes[1]<<8);
}

static int i3(ui8_t *bytes) {  // 24bit signed int
    int val = 0,
        val24 = 0;
    val = bytes[0] | (bytes[1]<<8) | (bytes[2]<<16);
    val24 = val & 0xFFFFFF; if (val24 & 0x800000) val24 -= 0x1000000;
    return val24;
}

static int crc16(ui8_t bytes[], int len) {
    int crc16poly = 0x1021;
    int rem = 0xFFFF, i, j;
    int byte;

    //if (start+len >= FRAME_LEN) return -1;

    for (i = 0; i < len; i++) {
        byte = bytes[i];
        rem = rem ^ (byte << 8);
        for (j = 0; j < 8; j++) {
            if (rem & 0x8000) {
                rem = (rem << 1) ^ crc16poly;
            }
            else {
                rem = (rem << 1);
            }
            rem &= 0xFFFF;
        }
    }
    return rem;
}

// -------------------------------------------------------------

static ui32_t rs41_check_CRC(rs_data_t *rs_data, ui32_t pos, ui32_t pck) {
    ui32_t crclen = 0,
           crcdat = 0;
    int ret = 0;

    // ((frame[pos]<<8) | frame[pos+1]) != pck ?  // caution: variable block length
    if ( rs_data->frame_bytes[pos] != ((pck>>8) & 0xFF) ) {
        ret = 0x10000;
    }

    crclen = rs_data->frame_bytes[pos+1];
    if (pos + crclen + 4 > rs_data->frame_len) ret |= 1;
    else {
        crcdat = u2((rs_data->frame_bytes)+pos+2+crclen);
        if ( crcdat != crc16((rs_data->frame_bytes)+pos+2, crclen) ) {
            ret |= 1;  // CRC NO
        }
        //else { };    // CRC OK
    }

    return ret;
}

// -------------------------------------------------------------

/*
  Pos: SubHeader, 1+1 byte (ID+LEN)
0x039: 7928   FrameNumber+SondeID
              +(0x050: 0732  CalFrames 0x00..0x32)
0x065: 7A2A   PTU
0x093: 7C1E   GPS1: RXM-RAW (0x02 0x10) Week, TOW, Sats
0x0B5: 7D59   GPS2: RXM-RAW (0x02 0x10) pseudorange, doppler
0x112: 7B15   GPS3: NAV-SOL (0x01 0x06) ECEF-POS, ECEF-VEL
0x12B: 7611   00
0x12B: 7Exx   AUX-xdata
*/

#define crc_FRAME    (1<<0)
#define xor_FRAME    0x1713  // ^0x6E3B=0x7928
#define pck_FRAME    0x7928
#define pos_FRAME     0x039
#define pos_FrameNb   0x03B  // 2 byte
#define pos_SondeID   0x03D  // 8 byte
#define pos_CalData   0x052  // 1 byte, counter 0x00..0x32
#define pos_Calfreq   0x055  // 2 byte, calfr 0x00
#define pos_Calburst  0x05E  // 1 byte, calfr 0x02
// ? #define pos_Caltimer  0x05A  // 2 byte, calfr 0x02 ?
#define pos_CalRSTyp  0x05B  // 8 byte, calfr 0x21 (+2 byte in 0x22?)
        // weitere chars in calfr 0x22/0x23; weitere ID

#define crc_PTU      (1<<1)
#define pck_PTU      0x7A2A  // PTU
#define pos_PTU       0x065

#define crc_GPS1     (1<<2)
#define xor_GPS1     0x9667  // ^0xEA79=0x7C1E
#define pck_GPS1     0x7C1E  // RXM-RAW (0x02 0x10)
#define pos_GPS1      0x093
#define pos_GPSweek   0x095  // 2 byte
#define pos_GPSiTOW   0x097  // 4 byte
#define pos_satsN     0x09B  // 12x2 byte (1: SV, 1: quality,strength)

#define crc_GPS2     (1<<3)
#define pck_GPS2     0x7D59  // RXM-RAW (0x02 0x10)
#define pos_GPS2      0x0B5
#define pos_minPR     0x0B7  //        4 byte
#define pos_FF        0x0BB  //        1 byte
#define pos_dataSats  0x0BC  // 12x(4+3) byte (4: pseudorange, 3: doppler)

#define crc_GPS3     (1<<4)
#define xor_GPS3     0xB9FF  // ^0xC2EA=0x7B15
#define pck_GPS3     0x7B15  // NAV-SOL (0x01 0x06)
#define pos_GPS3      0x112
#define pos_GPSecefP  0x114  // 3*4 byte ecefX,ecefY,ecefZ
#define pos_GPSecefV  0x120  // 3*2 byte
#define pos_numSats   0x126  //   1 byte
#define pos_sAcc      0x127  //   1 byte
#define pos_pDOP      0x128  //   1 byte

#define crc_AUX      (1<<5)
#define pck_AUX      0x7E00  // LEN variable
#define pos_AUX       0x12B

#define crc_ZERO     (1<<6)  // LEN variable
#define pck_ZERO     0x7600


static addData_Vaisala_t rs41_addData;


static double c = 299.792458e6;
static double L1 = 1575.42e6;

static int rs41_get_SatData(rs_data_t *rs_data, int verbose) {
    int i, n;
    int sv;
    ui32_t minPR;
    int Nfix;
    double pDOP, sAcc;
    ui32_t tow;
    ui32_t ecefP[3];
    i16_t  ecefV[3];

    ui8_t *frame = rs_data->frame_bytes;
    addData_Vaisala_t *rs41_add = rs_data->addData;

    tow = u4(frame+pos_GPSiTOW);
    minPR = u4(frame+pos_minPR);

    (rs41_add->sat).tow = tow;
    for (i = 0; i < 12; i++) {
        n = i*7;
        sv = frame[pos_satsN+2*i];
        if (sv == 0xFF) break;
        (rs41_add->sat).prn[i]         = sv;
        (rs41_add->sat).pseudorange[i] = u4(frame+pos_dataSats+n)/100.0 + minPR;
        (rs41_add->sat).doppler[i]     = -i3(frame+pos_dataSats+n+4)/100.0*L1/c;
    }
    n = i;
    for (i = n; i < 12; i++) {
        (rs41_add->sat).prn[i]         = 0;
        (rs41_add->sat).pseudorange[i] = 0.0;
        (rs41_add->sat).doppler[i]     = 0.0;
        i++;
    }

    // ECEF-pos
    for (i = 0; i < 3; i++) {
        ecefP[i] = (i32_t)u4(frame+pos_GPSecefP+4*i);
        (rs41_add->sat).pos_ecef[i] = ecefP[i] / 100.0;
    }
    // ECEF-vel
    for (i = 0; i < 3; i++) {
        ecefV[i] = (i16_t)u2(frame+pos_GPSecefV+2*i);
        (rs41_add->sat).vel_ecef[i] = ecefV[i] / 100.0;
    }

    Nfix = frame[pos_numSats];
    sAcc = frame[pos_sAcc]/10.0;
    pDOP = frame[pos_pDOP]/10.0;

    (rs41_add->sat).Nfix = Nfix;
    (rs41_add->sat).pDOP = pDOP;
    (rs41_add->sat).sAcc = sAcc;


    if (verbose) {

        fprintf(stdout, "[%5d]\n", u2(frame+pos_FrameNb));

        fprintf(stdout, "iTOW: 0x%08X", tow);
        fprintf(stdout, "  week: 0x%04X", u2(frame+pos_GPSweek));
        fprintf(stdout, "\n");
        fprintf(stdout, "minPR: %d", minPR);
        fprintf(stdout, "\n");

        for (i = 0; i < n; i++) {
            fprintf(stdout, "    SV: %2d #  ", (rs41_add->sat).prn[i]);
            fprintf(stdout, "prMes: %.1f", (rs41_add->sat).pseudorange[i]);
            fprintf(stdout, "  ");
            fprintf(stdout, "doMes: %.1f", (rs41_add->sat).doppler[i]);
            fprintf(stdout, "\n");
        }

        fprintf(stdout, "ECEF-POS: (%d,%d,%d)\n", ecefP[0], ecefP[1], ecefP[2]);
        fprintf(stdout, "ECEF-VEL: (%d,%d,%d)\n", ecefV[0], ecefV[1], ecefV[2]);

        fprintf(stdout, "numSatsFix: %2d  sAcc: %.1f  pDOP: %.1f\n", Nfix, sAcc, pDOP);

        fprintf(stdout, "CRC: ");
        fprintf(stdout, " %04X", pck_GPS1);
        if (rs41_check_CRC(rs_data, pos_GPS1, pck_GPS1)==0) fprintf(stdout, "[OK]"); else fprintf(stdout, "[NO]");
        fprintf(stdout, " %04X", pck_GPS2);
        if (rs41_check_CRC(rs_data, pos_GPS2, pck_GPS2)==0) fprintf(stdout, "[OK]"); else fprintf(stdout, "[NO]");
        fprintf(stdout, " %04X", pck_GPS3);
        if (rs41_check_CRC(rs_data, pos_GPS3, pck_GPS3)==0) fprintf(stdout, "[OK]"); else fprintf(stdout, "[NO]");

        fprintf(stdout, "\n");
    }

    return 0;
}


static int rs41_get_FrameNb(rs_data_t *rs_data) {
    ui8_t *frnr_bytes = NULL;

    frnr_bytes = (rs_data->frame_bytes)+pos_FrameNb;
    rs_data->frnr = frnr_bytes[0] | (frnr_bytes[1] << 8);

    return 0;
}

static int rs41_get_SondeID(rs_data_t *rs_data) {
    int i;
    ui8_t byte;
    ui8_t sondeid_bytes[8];

    for (i = 0; i < 8; i++) {
        byte = rs_data->frame_bytes[pos_SondeID + i];
        if ((byte < 0x20) || (byte > 0x7E)) return -1;
        sondeid_bytes[i] = byte;
    }

    for (i = 0; i < 8; i++) {
        rs_data->SN[i] = sondeid_bytes[i];
    }
    rs_data->SN[8] = '\0';

    return 0;
}

#define LEN_CAL 16
static int rs41_get_Cal(rs_data_t *rs_data, int verbose) {
    int i;
    unsigned byte;
    ui8_t calfr = 0;
    ui8_t burst = 0;
    ui16_t fw = 0;
    int freq = 0, f0 = 0, f1 = 0;
    char sondetyp[9];
    int crc = 0;

    ui8_t *frame    = rs_data->frame_bytes;
    ui8_t *calbytes = rs_data->frame_bytes+pos_CalData+1;
    addData_Vaisala_t *rs41_cal = rs_data->addData;

    calfr = frame[pos_CalData];

    crc = rs41_check_CRC(rs_data, pos_FRAME, pck_FRAME);

    if (crc==0  &&  strncmp(rs41_cal->SN, rs_data->SN, 8)!=0) {
        memset(rs41_cal, 0, sizeof(*rs41_cal));
        strncpy(rs41_cal->SN, rs_data->SN, 9);
    }

    if (crc == 0) {
        if (rs41_cal->bytes[calfr][LEN_CAL] == 0) {
            for (i = 0; i < LEN_CAL; i++) {
                rs41_cal->bytes[calfr][i] = calbytes[i];
            }
            rs41_cal->bytes[calfr][LEN_CAL] = 1;
        }
    }


    if (calfr == 0x00) {
        byte = frame[pos_Calfreq] & 0xC0;  // erstmal nur oberste beiden bits
        f0 = (byte * 10) / 64;  // 0x80 -> 1/2, 0x40 -> 1/4 ; dann mal 40
        byte = frame[pos_Calfreq+1];
        f1 = 40 * byte;
        freq = 400000 + f1+f0; // kHz;
        if (crc == 0) rs_data->freq = freq;  // crc == rs_data->crc & crc_FRAME
    }

    if (calfr == 0x01) {
        fw = frame[pos_CalData+6] | (frame[pos_CalData+7]<<8);
    }

    if (calfr == 0x02) {
        byte = frame[pos_Calburst];
        burst = byte;   // fw >= 0x4ef5, BK irrelevant? (killtimer in 0x31?)
    }

    if (calfr == 0x21) {  // eventuell noch zwei bytes in 0x22
        for (i = 0; i < 9; i++) sondetyp[i] = 0;
        for (i = 0; i < 8; i++) {
            byte = frame[pos_CalRSTyp + i];
            if ((byte >= 0x20) && (byte < 0x7F)) sondetyp[i] = byte;
            else if (byte == 0x00) sondetyp[i] = '\0';
        }
    }

    if (verbose) {
        fprintf(stdout, "[%5d] ", rs_data->frnr);
        fprintf(stdout, "0x%02x: ", calfr);
        for (i = 0; i < LEN_CAL; i++) {
            fprintf(stdout, "%02x ", calbytes[i]);
        }
        if (crc == 0) fprintf(stdout, "[OK]");
        else          fprintf(stdout, "[NO]");
        fprintf(stdout, " ");
        switch (calfr) {
            case 0x00: fprintf(stdout, ": fq %d ", freq);    break;
            case 0x01: fprintf(stdout, ": fw 0x%04x ", fw);  break;
            case 0x02: fprintf(stdout, ": BK %02X ", burst); break;
            case 0x21: fprintf(stdout, ": %s ", sondetyp);   break;
        }
        fprintf(stdout, "\n");
    }

    return 0;
}


static int rs41_get_PTUmeas(rs_data_t *rs_data) {
    int i;
    ui32_t measdata[12];
    ui8_t *frame = rs_data->frame_bytes;

    // 4*3 (u)int24
    for (i = 0; i < 12; i++) {
        measdata[i] = u3(frame+pos_PTU+2+3*i);
    }

    if (0) {
        printf("\n");
        printf("1: %8d %8d %8d", measdata[ 0], measdata[ 1], measdata[ 2]);  // T?
        printf("   #   ");
        printf("2: %8d %8d %8d", measdata[ 3], measdata[ 4], measdata[ 5]);  // H1?
        printf("   #   ");
        printf("3: %8d %8d %8d", measdata[ 6], measdata[ 7], measdata[ 8]);  // H2?
        printf("   #   ");
        printf("4: %8d %8d %8d", measdata[ 9], measdata[10], measdata[11]);  // P?
        printf("\n");
    }


    // calibration data: float32 poly-coeffs in cal/conf-blocks

    return 0;
}


static int rs41_get_GPSweek(rs_data_t *rs_data) {
    ui8_t *gpsweek_bytes;
    int gpsweek;

    gpsweek_bytes = (rs_data->frame_bytes)+pos_GPSweek;

    gpsweek = gpsweek_bytes[0] + (gpsweek_bytes[1] << 8);
    //if (gpsweek < 0) { rs_data->week = -1; return -1; } // (short int)
    (rs_data->GPS).week = gpsweek;

    return 0;
}


static int rs41_get_GPStime(rs_data_t *rs_data) {
    ui8_t *gpstime_bytes;
    ui32_t gpstime = 0, // 32bit
           day, ms;

    gpstime_bytes = (rs_data->frame_bytes)+pos_GPSiTOW;

    memcpy(&gpstime, gpstime_bytes, 4);
    (rs_data->GPS).msec = gpstime;

    ms = gpstime % 1000;
    gpstime /= 1000;


    day = (gpstime / (24 * 3600)) % 7;
    rs_data->wday = day;

    gpstime %= (24*3600);

    rs_data->hr  =  gpstime / 3600;
    rs_data->min = (gpstime % 3600) / 60;
    rs_data->sec =  gpstime % 60 + ms/1000.0;

    return 0;
}


#define EARTH_a  6378137.0
#define EARTH_b  6356752.31424518
#define EARTH_a2_b2  (EARTH_a*EARTH_a - EARTH_b*EARTH_b)

static double a = EARTH_a,
              b = EARTH_b,
              //a_b = EARTH_a2_b2,
              e2  = EARTH_a2_b2 / (EARTH_a*EARTH_a),
              ee2 = EARTH_a2_b2 / (EARTH_b*EARTH_b);

static int ecef2elli(double X[], double *lat, double *lon, double *alt) {
    double phi, lam, R, p, t;

    lam = atan2( X[1] , X[0] );

    p = sqrt( X[0]*X[0] + X[1]*X[1] );
    t = atan2( X[2]*a , p*b );

    phi = atan2( X[2] + ee2 * b * sin(t)*sin(t)*sin(t) ,
                 p - e2 * a * cos(t)*cos(t)*cos(t) );

    R = a / sqrt( 1 - e2*sin(phi)*sin(phi) );
    *alt = p / cos(phi) - R;

    *lat = phi*180.0/M_PI;
    *lon = lam*180.0/M_PI;

    return 0;
}

static int rs41_get_GPSkoord(rs_data_t *rs_data) {
    int k;
    ui8_t *gpsPos = NULL;
    int XYZ; // signed 32bit
    double P[3], lat, lon, alt;
    ui8_t *gpsVel = NULL;
    short vel16; // signed 16bit
    double V[3], phi, lam, dir;
    int ret = 0;

    ui8_t *frame = rs_data->frame_bytes;

    for (k = 0; k < 3; k++) {

        gpsPos = frame + pos_GPSecefP + 4*k;
        memcpy(&XYZ, gpsPos, 4);
        P[k] = XYZ / 100.0;

        gpsVel = frame + pos_GPSecefV + 2*k;
        vel16 = gpsVel[0] | gpsVel[1] << 8;
        V[k] = vel16 / 100.0;

    }


    // ECEF-Position
    ecef2elli(P, &lat, &lon, &alt);
    (rs_data->GPS).lat = lat;
    (rs_data->GPS).lon = lon;
    (rs_data->GPS).alt = alt;
    if ((alt < -1000) || (alt > 80000)) ret = -3;


    // ECEF-Velocities
    // ECEF-Vel -> NorthEastUp
    phi = lat*M_PI/180.0;
    lam = lon*M_PI/180.0;
    (rs_data->GPS).vN = -V[0]*sin(phi)*cos(lam) - V[1]*sin(phi)*sin(lam) + V[2]*cos(phi);
    (rs_data->GPS).vE = -V[0]*sin(lam) + V[1]*cos(lam);
    (rs_data->GPS).vU =  V[0]*cos(phi)*cos(lam) + V[1]*cos(phi)*sin(lam) + V[2]*sin(phi);

    // NEU -> HorDirVer
    (rs_data->GPS).vH = sqrt((rs_data->GPS).vN*(rs_data->GPS).vN + (rs_data->GPS).vE*(rs_data->GPS).vE);
/*
    double alpha;
    alpha = atan2(gpx.vN, gpx.vE)*180/M_PI;  // ComplexPlane (von x-Achse nach links) - GeoMeteo (von y-Achse nach rechts)
    dir = 90-alpha;                          // z=x+iy= -> i*conj(z)=y+ix=re(i(pi/2-t)), Achsen und Drehsinn vertauscht
    if (dir < 0) dir += 360;                 // atan2(y,x)=atan(y/x)=pi/2-atan(x/y) , atan(1/t) = pi/2 - atan(t)
    gpx.vD2 = dir;
*/
    dir = atan2((rs_data->GPS).vE, (rs_data->GPS).vN) * 180.0 / M_PI;
    if (dir < 0) dir += 360.0;
    (rs_data->GPS).vD = dir;

    return ret;
}

// -------------------------------------------------------------


static int rs41_get_FrameConf(rs_data_t *rs_data, int verbose) {
    int err;

    err = rs41_check_CRC(rs_data, pos_FRAME, pck_FRAME);
    if (err) rs_data->crc |= crc_FRAME;

    rs41_get_FrameNb(rs_data);
    rs41_get_SondeID(rs_data);
    rs41_get_Cal(rs_data, verbose);

    return err;
}

static int rs41_get_PTU(rs_data_t *rs_data) {
    int err;

    err = rs41_check_CRC(rs_data, pos_PTU, pck_PTU);
    if (err) rs_data->crc |= crc_PTU;
    //else
    {
        rs41_get_PTUmeas(rs_data);
    }
    return err;
}

static int rs41_get_GPS1(rs_data_t *rs_data) {
    int err;

    err = rs41_check_CRC(rs_data, pos_GPS1, pck_GPS1);
    if (err) rs_data->crc |= crc_GPS1;
    //else
    {
        rs41_get_GPSweek(rs_data);
        rs41_get_GPStime(rs_data);
    }

    Gps2Date(rs_data);

    return err;
}

static int rs41_get_GPS2(rs_data_t *rs_data, int verbose) {
    int err;

    err = rs41_check_CRC(rs_data, pos_GPS2, pck_GPS2);
    if (err) rs_data->crc |= crc_GPS2;

    rs41_get_SatData(rs_data, verbose);

    return err;
}

static int rs41_get_GPS3(rs_data_t *rs_data) {
    int err;

    err = rs41_check_CRC(rs_data, pos_GPS3, pck_GPS3);
    if (err) rs_data->crc |= crc_GPS3;

    rs41_get_GPSkoord(rs_data);

    return err;
}


static int rs41_get_Aux(rs_data_t *rs_data) {
//
// "Ozone Sounding with Vaisala Radiosonde RS41" user's guide
//
    int i, auxlen, auxcrc, count7E, pos7E, err;
    ui8_t *frame = rs_data->frame_bytes;

    count7E = 0;
    pos7E = pos_AUX;

    // 7Exx: xdata
    while ( pos7E < rs_data->frame_len  &&  frame[pos7E] == 0x7E ) {

        auxlen = frame[pos7E+1];
        auxcrc = frame[pos7E+2+auxlen] | (frame[pos7E+2+auxlen+1]<<8);

        if (count7E == 0) fprintf(stdout, "# xdata = ");
        else              fprintf(stdout, " # ");

        err = rs41_check_CRC(rs_data, pos7E, frame[pos7E]);
        if (err) rs_data->crc |= crc_AUX;

        if ( auxcrc == crc16(frame+pos7E+2, auxlen) ) {
            //fprintf(stdout, " # %02x : ", frame[pos_AUX+2]);
            for (i = 1; i < auxlen; i++) {
                fprintf(stdout, "%c", frame[pos7E+2+i]);
            }
            count7E++;
            pos7E += 2+auxlen+2;
        }
        else pos7E = rs_data->frame_len;
    }
    if (count7E > 0) fprintf(stdout, "\n");


    err = rs41_check_CRC(rs_data, pos7E, 0x7600);
    if (err) rs_data->crc |= crc_ZERO;


    return count7E;
}


// -------------------------------------------------------------

//
// Reed-Solomon error correction -------------------------------
//

rs_ecccfg_t cfg_rs41ecc = {
              .typ=     41,
              .msglen=  (320-56)/2, // 132..231 <= rs_K=231
              .msgpos=  56,
              .parpos=  8,
              .hdrlen=  8,
              .frmlen=  320 // 320..518
            };

#define rs_N 255
#define rs_R 24
#define rs_K (rs_N-rs_R)

static int rs41_ecc(rs_data_t *rs_data) {
// richtige framelen wichtig fuer 0-padding

    int i, leak, ret = 0;
    int errors1, errors2;
    ui8_t cw1[rs_N], cw2[rs_N];
    ui8_t err_pos1[rs_R], err_pos2[rs_R],
          err_val1[rs_R], err_val2[rs_R];

    ui32_t frmlen = rs_data->pos;
    ui8_t *frame = rs_data->frame_bytes;

    // frmlen <= cfg_rs41ecc.frmlen; // = 518

    if (frmlen > rs_data->frame_len) frmlen = rs_data->frame_len;
    cfg_rs41ecc.frmlen = frmlen;
    cfg_rs41ecc.msglen = (frmlen-cfg_rs41ecc.msgpos)/2; // msgpos=56;
    leak = frmlen % 2;

    for (i = frmlen; i < rs_data->frame_len; i++) frame[i] = 0;  // FRAME_LEN-HDR = 510 = 2*255
    // memset(cw1/2, 0, rs_N);

    for (i = 0; i < rs_R; i++) cw1[i] = frame[cfg_rs41ecc.parpos+i     ];
    for (i = 0; i < rs_R; i++) cw2[i] = frame[cfg_rs41ecc.parpos+i+rs_R];
    for (i = 0; i < rs_K; i++) cw1[rs_R+i] = frame[cfg_rs41ecc.msgpos+2*i  ];
    for (i = 0; i < rs_K; i++) cw2[rs_R+i] = frame[cfg_rs41ecc.msgpos+2*i+1];

    errors1 = rs_decode(cw1, err_pos1, err_val1);
    errors2 = rs_decode(cw2, err_pos2, err_val2);

    // Wenn Fehler im 00-padding korrigiert wurden,
    // war entweder der frame zu kurz, oder
    // Fehler wurden falsch korrigiert;
    // allerdings ist bei t=12 die Wahrscheinlichkeit,
    // dass falsch korrigiert wurde mit 1/t! sehr gering.

    // check CRC32
    // CRC32 OK:
    //for (i = 0; i < cfg_rs41ecc.hdrlen; i++) frame[i] = data[i];
    for (i = 0; i < rs_R; i++) {
        frame[cfg_rs41ecc.parpos+     i] = cw1[i];
        frame[cfg_rs41ecc.parpos+rs_R+i] = cw2[i];
    }
    for (i = 0; i < rs_K; i++) { // cfg_rs41ecc.msglen <= rs_K
        frame[cfg_rs41ecc.msgpos+  2*i] = cw1[rs_R+i];
        frame[cfg_rs41ecc.msgpos+1+2*i] = cw2[rs_R+i];
    }
    if (leak) {
        frame[cfg_rs41ecc.msgpos+2*i] = cw1[rs_R+i];
    }


    ret = errors1 + errors2;
    if (errors1 < 0 || errors2 < 0) ret = -1;

    return ret;
}

// -------------------------------------------------------------

//
// process bits/bytes
//

static int rs41_framebits2bytes(rs_data_t *rs_data) {

    char  *rawframebits = rs_data->frame_rawbits;
    ui8_t *frame        = rs_data->frame_bytes;
    ui32_t n;

    for (n = 0; n < rs_data->pos; n++) {
        frame[n] = rs_data->bits2byte(rs_data, rawframebits+(BITS*n));
    }

    return 0;
}


int rs41_process(void *data, int raw, int options) {
    rs_data_t *rs_data = data;
    int err=0, ret=0;
    ui32_t n;

    if (rs_data->input < 8) {
        rs41_framebits2bytes(rs_data);
    }

    for (n = rs_data->pos; n < rs_data->frame_len; n++) {
        rs_data->frame_bytes[n] = 0;
    }

    rs_data->ecc = rs41_ecc(rs_data);
    rs_data->crc = 0;

    if ( !raw ) {

        err = 0;
        ret = 0;

        ret  = rs41_get_FrameConf(rs_data, options & 0x1);
        err |= ret<<0;

        ret  = rs41_get_PTU(rs_data);
        err |= ret<<1;

        ret = rs41_get_GPS1(rs_data);
        err |= ret<<2;

        ret = rs41_get_GPS2(rs_data, (options>>8) & 0xFF);
        err |= ret<<3;

        ret = rs41_get_GPS3(rs_data);
        err |= ret<<4;

        if (options & 0x2) {
            ret = rs41_get_Aux(rs_data);
            // ret = count7E; // bei crc: err |= ret<<4;
        }

    }

    return err;
}


int rs41_xbits2byte(void *data, char bits[]) {
// 0 eq '0'=0x30
// 1 eq '1'=0x31
    rs_data_t *rs_data = data;
    int i, byte=0, d=1;
    for (i = 0; i < 8; i++) {     // little endian
    /* for (i = 7; i >= 0; i--) { // big endian */
        if      ((bits[i]&1) == 1)  byte += d;
        else if ((bits[i]&1) == 0)  byte += 0;
        //else return 0x100;
        d <<= 1;
    }

    return  byte ^ mask[rs_data->pos % MASK_LEN];
}


int init_rs41data(rs_data_t *rs_data) {

    rs_init_RS255();

    // int in = rs_data->input;
    // memset(rs_data, 0, sizeof(rs_data_t));
    // rs_data->input = in

    rs_data->baud = BAUD;
    rs_data->bits = BITS;

    rs_data->header = calloc(sizeof(headerbits_rs41), 1);
    if (rs_data->header == NULL) return ERROR_MALLOC;
    strcpy(rs_data->header, headerbits_rs41);
    rs_data->header_ofs = 24;
    rs_data->header_len = 32;

    rs_data->bufpos = -1;
    rs_data->buf = calloc((rs_data->header_len)+1, 1);
    if (rs_data->buf == NULL) return ERROR_MALLOC;

    if (rs_data->input < 8) {
        rs_data->frame_rawbits = calloc(RAWBITFRAME_LEN, 1);
        if (rs_data->frame_rawbits == NULL) return ERROR_MALLOC;
        strncpy(rs_data->frame_rawbits, headerbits_rs41, strlen(headerbits_rs41));

        //rs_data->frame_bits = rs_data->frame_rawbits;
    }

    rs_data->frame_bytes = calloc(FRAME_LEN, 1);
    if (rs_data->frame_bytes == NULL) return ERROR_MALLOC;
    memcpy(rs_data->frame_bytes, headerbytes_rs41, sizeof(headerbytes_rs41));

    rs_data->frame_start = (rs_data->header_ofs + rs_data->header_len) / rs_data->bits;
    rs_data->pos_min = pos_AUX;
    rs_data->frame_len = FRAME_LEN;

    rs_data->bits2byte = rs41_xbits2byte;
    rs_data->rs_process = rs41_process;

    rs_data->addData = &rs41_addData;

    return 0;
}

int free_rs41data(rs_data_t *rs_data) {

    rs_data->header = NULL;

    free(rs_data->buf);
    rs_data->buf = NULL;

    if (rs_data->input < 8) {
        // memset(rs_data->frame_rawbits, 0, rs_data->RAWBITFRAME_LEN) ...
        free(rs_data->frame_rawbits);
        rs_data->frame_rawbits = NULL;

        //rs_data->frame_bits = NULL;
    }

    // memset(rs_data->frame_bytes, 0, rs_data->FRAME_LEN) ...
    free(rs_data->frame_bytes);
    rs_data->frame_bytes = NULL;

    //memset(rs_data, 0, sizeof(rs_data_t));

    return 0;
}

