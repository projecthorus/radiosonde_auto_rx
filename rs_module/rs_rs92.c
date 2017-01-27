

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


#include "rs_data.h"
#include "rs_datum.h"
#include "rs_bch_ecc.h"

#include "rs_gps.c"



// --- RS92-SGP: 8N1 manchester ---
static                    // 2A                  10
char headerbits_rs92sgp[] = "10100110011001101001"
                            "10100110011001101001"
                            "10100110011001101001"
                            "10100110011001101001"
                            "1010011001100110100110101010100110101001";

static
ui8_t headerbytes_rs92sgp[] = { 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x10};


#define FRAME_LEN 240

#define BAUD 4800
#define BITS (2*(1+8+1))  // 20 (SGP/manchester 8N1)
#define BITFRAME_LEN    (FRAME_LEN*BITS)
#define RAWBITFRAME_LEN (BITFRAME_LEN*2)


static addData_Vaisala_t rs92_addData;


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

static ui32_t rs92_check_CRC(rs_data_t *rs_data, ui32_t pos, ui32_t pck) {
    ui32_t crclen = 0,
           crcdat = 0;
    int ret = 0;

    // ((frame[pos]<<8) | frame[pos+1]) != pck ?  // caution: variable block length (rs92?)
    if ( rs_data->frame_bytes[pos] != ((pck>>8) & 0xFF) ) {
        ret = 0x10000;
    }

    crclen = rs_data->frame_bytes[pos+1] * 2;
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


#define pck_CFG         0x6510
#define pos_CFG         0x06    // 32 byte
#define pos_FrameNb     0x08    // 2 byte
#define pos_SondeID     0x0C    // 8 byte  // oder: 0x0A, 10 byte?
#define pos_CalData     0x17    // 1 byte, counter 0x00..0x1f
#define pos_Calfreq     0x1A    // 2 byte, calfr 0x00

#define pck_PTU         0x690C
#define pos_PTU         0x2A    // 24 byte

#define pck_GPS         0x673D
#define pos_GPS         0x46    // 122 byte
#define pos_GPS_iTOW    0x48    // 4 byte
#define pos_GPS_PRN     0x4E    // 12*5 bit in 8 byte
#define pos_GPS_STATUS  0x56    // 12 byte
#define pos_GPS_DATA    0x62    // 12*8 byte

#define pck_AUX         0x6805
#define pos_AUX         0xC4    // 2+8 byte


#define LEN_CFG (2*(pck_CFG & 0xFF))
#define LEN_GPS (2*(pck_GPS & 0xFF))
#define LEN_PTU (2*(pck_PTU & 0xFF))

#define crc_CFG      (1<<0)
#define crc_PTU      (1<<1)
#define crc_GPS      (1<<2)
#define crc_AUX      (1<<3)



static int rs92_get_FrameNb(rs_data_t *rs_data) {
    ui8_t *frnr_bytes = NULL;

    frnr_bytes = (rs_data->frame_bytes)+pos_FrameNb;
    rs_data->frnr = frnr_bytes[0] | (frnr_bytes[1] << 8);

    return 0;
}

static int rs92_get_SondeID(rs_data_t *rs_data) {
    int i;
    ui8_t byte;
    ui8_t sondeid_bytes[10];

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


static int rs92_get_GPStime(rs_data_t *rs_data) {
    ui8_t *gpstime_bytes;
    ui32_t gpstime = 0, // 32bit
           day, ms;

    gpstime_bytes = (rs_data->frame_bytes)+pos_GPS_iTOW;

    memcpy(&gpstime, gpstime_bytes, 4);
    (rs_data->GPS).msec = gpstime;

    ms = gpstime % 1000;
    gpstime /= 1000;


    day = (gpstime / (24 * 3600)) % 7;        // besser CRC-check, da auch
    //if ((day < 0) || (day > 6)) return -1;  // gpssec=604800,604801 beobachtet

    if (day >=0 && day < 7) rs_data->wday = day;
    gpstime %= (24*3600);

    rs_data->hr  =  gpstime / 3600;
    rs_data->min = (gpstime % 3600) / 60;
    rs_data->sec =  gpstime % 60 + ms/1000.0;

    return 0;
}


#define LEN_CAL 16
static int rs92_get_Cal(rs_data_t *rs_data, int verbose) {
    int i;
    unsigned byte;
    ui8_t calfr = 0;
    //ui8_t burst = 0;
    int freq = 0;
    ui8_t freq_bytes[2];
    int crc = 0;

    ui8_t *frame    = rs_data->frame_bytes;
    ui8_t *calbytes = rs_data->frame_bytes+pos_CalData+1;
    addData_Vaisala_t *rs92_cal = rs_data->addData;

    calfr = frame[pos_CalData];

    crc = rs92_check_CRC(rs_data, pos_CFG, pck_CFG);
    // crc == rs_data->crc & crc_CFG ?

    if (crc==0  &&  strncmp(rs92_cal->SN, rs_data->SN, 8)!=0) {
        memset(rs92_cal, 0, sizeof(*rs92_cal));
        strncpy(rs92_cal->SN, rs_data->SN, 9);
    }

    if (crc == 0) {
        if (rs92_cal->bytes[calfr][LEN_CAL] == 0) {
            for (i = 0; i < LEN_CAL; i++) {
                rs92_cal->bytes[calfr][i] = calbytes[i];
            }
            rs92_cal->bytes[calfr][LEN_CAL] = 1;
        }
    }

    if (calfr == 0x00) {
        for (i = 0; i < 2; i++) {
            freq_bytes[i] = frame[pos_Calfreq + i];
        }
        byte = freq_bytes[0] + (freq_bytes[1] << 8);
        freq = 400000 + 10*byte; // kHz;
        rs_data->freq = freq;
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
            case 0x00: fprintf(stdout, ": fq %d ", freq); break;
        }
        fprintf(stdout, "\n");
    }


    return 0;
}


static int rs92_get_PTUmeas(rs_data_t *rs_data) {
    //double T, P, H1, H2;

    int temp, pres, hum1, hum2, ref1, ref2, ref3, ref4;

    ui8_t *measdata = rs_data->frame_bytes+pos_PTU+2;

    // crc check
    if ( (rs_data->crc & crc_PTU) != 0 ) return -1;

    temp = measdata[ 0] | (measdata[ 1]<<8) | (measdata[ 2]<<16);  // ch1
    hum1 = measdata[ 3] | (measdata[ 4]<<8) | (measdata[ 5]<<16);  // ch2
    hum2 = measdata[ 6] | (measdata[ 7]<<8) | (measdata[ 8]<<16);  // ch3
    ref1 = measdata[ 9] | (measdata[10]<<8) | (measdata[11]<<16);  // ch4
    ref2 = measdata[12] | (measdata[13]<<8) | (measdata[14]<<16);  // ch5
    pres = measdata[15] | (measdata[16]<<8) | (measdata[17]<<16);  // ch6
    ref3 = measdata[18] | (measdata[19]<<8) | (measdata[20]<<16);  // ch7
    ref4 = measdata[21] | (measdata[22]<<8) | (measdata[23]<<16);  // ch8


    // calibration data: float32 poly-coeffs in cal/conf-blocks

    return 0;
}

// ---------------------------------------------------------------------------------------------

//
// GPS ---------------------------------------------------------
//

static double d_err = 10000;
static int option_iter = 0;
static int almanac = 0,
           ephem = 0;

static EPHEM_t alm[33];
static EPHEM_t *ephs = NULL;

static SAT_t Sat[12];
static ui8_t prns[12]; // PRNs in data
static ui8_t sat_status[12];

static int prn32toggle = 0x1, ind_prn32, prn32next;

static int prnbits_le(ui16_t byte16, ui8_t bits[64], int block) {
    int i; /* letztes bit Ueberlauf, wenn 3. PRN = 32 */
    for (i = 0; i < 15; i++) {
        bits[15*block+i] = byte16 & 1;
        byte16 >>= 1;
    }
    bits[60+block] = byte16 & 1;
    return byte16 & 1;
}

static void rs92_prn12(ui8_t *prn_le, ui8_t prns[12]) {
    int i, j, d;
    for (i = 0; i < 12; i++) {
        prns[i] = 0;
        d = 1;
        for (j = 0; j < 5; j++) {
          if (prn_le[5*i+j]) prns[i] += d;
          d <<= 1;
        }
    }
    ind_prn32 = 32;
    for (i = 0; i < 12; i++) {
        // PRN-32 overflow
        if ( (prns[i] == 0) && (sat_status[i] & 0x0F) ) {  // 5 bit: 0..31
            if (  ((i % 3 == 2) && (prn_le[60+i/3] & 1))       // Spalte 2
               || ((i % 3 != 2) && (prn_le[5*(i+1)] & 1)) ) {  // Spalte 0,1
                prns[i] = 32; ind_prn32 = i;
            }
        }
        else if ((sat_status[i] & 0x0F) == 0) {  // erste beiden bits: 0x03 ?
            prns[i] = 0;
        }
    }

    prn32next = 0;
    if (ind_prn32 < 12) {
        // PRN-32 overflow
        if (ind_prn32 % 3 != 2) { // -> ind_prn32<11                            // vorausgesetzt im Block folgt auf PRN-32
            if ((sat_status[ind_prn32+1] & 0x0F)  &&  prns[ind_prn32+1] > 1) {  // entweder PRN-1 oder PRN-gerade
                                               // &&  prns[ind_prn32+1] != 3 ?
                for (j = 0; j < ind_prn32; j++) {
                    if (prns[j] == (prns[ind_prn32+1]^prn32toggle)  &&  (sat_status[j] & 0x0F)) break;
                }
                if (j < ind_prn32) { prn32toggle ^= 0x1; }
                else {
                    for (j = ind_prn32+2; j < 12; j++) {
                        if (prns[j] == (prns[ind_prn32+1]^prn32toggle)  &&  (sat_status[j] & 0x0F)) break;
                    }
                    if (j < 12) { prn32toggle ^= 0x1; }
                }
                prns[ind_prn32+1] ^= prn32toggle;
                /*
                  // nochmal testen
                  for (j = 0; j < ind_prn32; j++) { if (prns[j] == prns[ind_prn32+1]) break; }
                  if (j < ind_prn32) prns[ind_prn32+1] = 0;
                  else {
                      for (j = ind_prn32+2; j < 12; j++) { if (prns[j] == prns[ind_prn32+1]) break; }
                      if (j < 12) prns[ind_prn32+1] = 0;
                  }
                  if (prns[ind_prn32+1] == 0) { prn32toggle ^= 0x1; }
                */
            }
            prn32next = prns[ind_prn32+1];  // ->  ind_prn32<11  &&  ind_prn32 % 3 != 2
        }
    }
}


// pseudo.range = -df*pseudo.chips
//           df = lightspeed/(chips/sec)/2^10
const double df = 299792.458/1023.0/1024.0; //0.286183844 // c=299792458m/s, 1023000chips/s
//           dl = L1/(chips/sec)/4
const double dl = 1575.42/1.023/4.0; //385.0 // GPS L1 1575.42MHz=154*10.23MHz, dl=154*10/4


static int rs92_get_pseudorange(rs_data_t *rs_data) {
    ui32_t gpstime;
    ui8_t *gpstime_bytes;
    ui8_t *pseudobytes;
    int chipbytes, deltabytes; // signed int32
    int i, j, k;
    ui8_t bytes[4];
    ui8_t prn_le[12*5+4];
    ui16_t byte16;

    ui8_t *frame = rs_data->frame_bytes;

    // GPS-TOW in ms
    gpstime_bytes = frame+pos_GPS_iTOW;
    memcpy(&gpstime, gpstime_bytes, 4);

    // Sat Status
    for (i = 0; i < 12; i++) {
        sat_status[i] = frame[pos_GPS_STATUS + i];
    }

    // PRN-Nummern
    memset(prn_le, 0, sizeof(prn_le)); // size=60+4
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 2; j++) {
            bytes[j] = frame[pos_GPS_PRN+2*i+j];
        }
        memcpy(&byte16, bytes, 2);
        prnbits_le(byte16, prn_le, i);
    }
    rs92_prn12(prn_le, prns);


    for (j = 0; j < 12; j++) {

        Sat[j].tow = gpstime;
        Sat[j].prn = prns[j];
        Sat[j].status = sat_status[j];

        if (pos_GPS_DATA+8*(j+1) > rs_data->pos) {
            Sat[j].prn = 0;
        }
        else {

            // Pseudorange/chips
            pseudobytes = frame+pos_GPS_DATA+8*j;
            memcpy(&chipbytes, pseudobytes, 4);

            // delta_pseudochips / 385
            pseudobytes = frame+pos_GPS_DATA+8*j+4;
            deltabytes = 0; // bzw. pseudobytes[3]=0 (24 bit);
            memcpy(&deltabytes, pseudobytes, 3);

            // check deltabytes, status_deltabytes (24..31)
            if ((         chipbytes == 0x7FFFFFFF  ||          chipbytes == 0x55555555 ) ||
                ( (ui32_t)chipbytes  > 0x10000000  &&  (ui32_t)chipbytes  < 0xF0000000 ))
            {
                 chipbytes  = 0;
                 deltabytes = 0;
                 Sat[j].prn = 0;
            }
                                //0x01400000 //0x013FB0A4
            Sat[j].pseudorange = - chipbytes  * df;
            Sat[j].pseudorate  = - deltabytes * df / dl;

            if ((Sat[j].status & 0x0F) != 0xF) Sat[j].prn = 0;
        }
    }


    // GPS Sat Pos & Vel
    if (almanac) gps_satpos_alm(rs_data,  alm, gpstime/1000.0, Sat);
    if (ephem)   gps_satpos_rnx(rs_data, ephs, gpstime/1000.0, Sat);

    k = 0;
    for (j = 0; j < 12; j++) {
        if (Sat[j].prn > 0) k++;
    }

    return k;
}

static int rs92_get_GPSvel(double lat, double lon, double vel_ecef[3],
                           double *vH, double *vD, double *vU) {
    // ECEF-Velocities
    // ECEF-Vel -> NorthEastUp
    double phi = lat*M_PI/180.0;
    double lam = lon*M_PI/180.0;
    double vN = -vel_ecef[0]*sin(phi)*cos(lam) - vel_ecef[1]*sin(phi)*sin(lam) + vel_ecef[2]*cos(phi);
    double vE = -vel_ecef[0]*sin(lam) + vel_ecef[1]*cos(lam);
    *vU =  vel_ecef[0]*cos(phi)*cos(lam) + vel_ecef[1]*cos(phi)*sin(lam) + vel_ecef[2]*sin(phi);
    // NEU -> HorDirVer
    *vH = sqrt(vN*vN+vE*vE);
    *vD = atan2(vE, vN) * 180.0 / M_PI;
    if (*vD < 0) *vD += 360.0;

    return 0;
}

static int rs92_get_GPSkoord(rs_data_t *rs_data, int opt_gg2) {
    double lat, lon, alt, rx_cl_bias;
    double vH, vD, vU;
    double lat0 , lon0 , alt0 , pos0_ecef[3];
    double vH0, vD0, vU0;
    double pos_ecef[3], dpos_ecef[3],
           vel_ecef[3], dvel_ecef[3];
    double DOP[4] = {0,0,0,0};
    double gdop, gdop0;
    //double hdop, vdop, pdop;
    int j, k, n;
    SAT_t Sat_B[12]; // N <= 12
    SAT_t Sat_C[12]; // 11
    int i0, i1, i2, i3;
    int j0, j1, j2, j3;
    double diter, diter0;
    int exN = -1;
    int N = 0;

    (rs_data->GPS).lat = (rs_data->GPS).lon = (rs_data->GPS).alt = 0;

    k = 0;
    for (j = 0; j < 12; j++) {
        if (Sat[j].prn > 0) Sat_B[k++] = Sat[j];
    }
    for (j = k; j < 12; j++) Sat_B[j].prn = 0;
    N = k;

    // preliminary position
    //
    NAV_bancroft1(N, Sat_B, pos_ecef, &rx_cl_bias);
    ecef2elli(pos_ecef[0], pos_ecef[1], pos_ecef[2], &lat, &lon, &alt);
    gdop = -1;
    if (calc_DOPn(N, Sat_B, pos_ecef, DOP) == 0) {
        gdop = sqrt(DOP[0]+DOP[1]+DOP[2]+DOP[3]);
    }

    NAV_LinP(N, Sat_B, pos_ecef, rx_cl_bias, dpos_ecef, &rx_cl_bias);
    if (option_iter) {
        for (j = 0; j < 3; j++) pos_ecef[j] += dpos_ecef[j];
        ecef2elli(pos_ecef[0], pos_ecef[1], pos_ecef[2], &lat, &lon, &alt);
    }
    diter = dist(0, 0, 0, dpos_ecef[0], dpos_ecef[1], dpos_ecef[2]);


    // Sat mit schlechten Daten suchen
    if (diter >= d_err)
    {
        if (N > 4) {  // N > 5
            for (n = 0; n < N; n++) {
                k = 0;
                for (j = 0; j < N; j++) {
                    if (j != n) {
                        Sat_C[k] = Sat_B[j];
                        k++;
                    }
                }
                for (j = 0; j < 3; j++) pos0_ecef[j] = 0;
                NAV_bancroft1(N-1, Sat_C, pos0_ecef, &rx_cl_bias);

                NAV_LinP(N-1, Sat_C, pos0_ecef, rx_cl_bias, dpos_ecef, &rx_cl_bias);
                diter0 = dist(0, 0, 0, dpos_ecef[0], dpos_ecef[1], dpos_ecef[2]);
                ecef2elli(pos0_ecef[0], pos0_ecef[1], pos0_ecef[2], &lat0, &lon0, &alt0);

                if (diter0 < d_err && diter0 < diter) {
                    diter = diter0;
                    exN = n;
                }
            }
        }

        if (exN >= 0) {

            if (Sat_B[exN].prn == prn32next) {
                prn32toggle ^= 0x1;  // wenn zuvor mit prn32next valider Fix, dann eventuell nicht aendern;
                                     // eventuell gleich testen
            }

            for (k = exN; k < N-1; k++) {
                Sat_B[k] = Sat_B[k+1];
            }
            Sat_B[N-1].prn = 0;
            N = N-1;

        }
        else {
            // 4er-Kombinationen probieren
            gdop = 100.0;
            k = N;
            j0 = j1 = j2 = j3 = 0;
            for (i0=0;i0<k;i0++) { for (i1=i0+1;i1<k;i1++) { for (i2=i1+1;i2<k;i2++) { for (i3=i2+1;i3<k;i3++) {

                Sat_C[0] = Sat_B[i0];
                Sat_C[1] = Sat_B[i1];
                Sat_C[2] = Sat_B[i2];
                Sat_C[3] = Sat_B[i3];

                for (j = 0; j < 3; j++) pos0_ecef[j] = 0;
                NAV_bancroft1(4, Sat_C, pos0_ecef, &rx_cl_bias);

                NAV_LinP(4, Sat_C, pos0_ecef, rx_cl_bias, dpos_ecef, &rx_cl_bias);
                diter0 = dist(0, 0, 0, dpos_ecef[0], dpos_ecef[1], dpos_ecef[2]);
                ecef2elli(pos0_ecef[0], pos0_ecef[1], pos0_ecef[2], &lat0, &lon0, &alt0);

                vel_ecef[0] = vel_ecef[1] = vel_ecef[2] = 0;
                NAV_LinV(4, Sat_C, pos0_ecef, vel_ecef, 0.0, dvel_ecef, &rx_cl_bias);
                for (j=0; j<3; j++) vel_ecef[j] += dvel_ecef[j];
                rs92_get_GPSvel(lat0, lon0, vel_ecef, &vH0, &vD0, &vU0);

                gdop0 = -1;
                if (calc_DOPn(4, Sat_C, pos0_ecef, DOP) == 0) {
                    gdop0 = sqrt(DOP[0]+DOP[1]+DOP[2]+DOP[3]);
                }


                if (   (diter0 < d_err && gdop0 >= 0)
                    && (alt0 > -200 && alt0 < 40000)           // eventuell mit vorherigen
                    && (vH0 < 200.0 && vU0*vU0 < 200.0*200.0)  // validen Positionen vergleichen
                   ) {

                    if ( diter0 < diter  &&  gdop0 < gdop+2.0 ) {
                        diter = diter0;
                        gdop = gdop0;
                        j0 = i0; j1 = i1; j2 = i2; j3 = i3;
                    }
                }
            }}}}

            if (j1 > 0) {
                Sat_C[0] = Sat_B[j0];
                Sat_C[1] = Sat_B[j1];
                Sat_C[2] = Sat_B[j2];
                Sat_C[3] = Sat_B[j3];
                N = 4;
                Sat_B[0] = Sat_C[0];
                Sat_B[1] = Sat_C[1];
                Sat_B[2] = Sat_C[2];
                Sat_B[3] = Sat_C[3];
            }
        }
    }


    // final solution
    //
    // position
    NAV_bancroft1(N, Sat_B, pos_ecef, &rx_cl_bias);
    ecef2elli(pos_ecef[0], pos_ecef[1], pos_ecef[2], &lat, &lon, &alt);
    // velocity
    vel_ecef[0] = vel_ecef[1] = vel_ecef[2] = 0;
    NAV_LinV(N, Sat_B, pos_ecef, vel_ecef, 0.0, dvel_ecef, &rx_cl_bias);
    for (j=0; j<3; j++) vel_ecef[j] += dvel_ecef[j];
    rs92_get_GPSvel(lat, lon, vel_ecef, &vH, &vD, &vU);
    // DOP
    gdop = -1;
    if (calc_DOPn(N, Sat_B, pos_ecef, DOP) == 0) {
        gdop = sqrt(DOP[0]+DOP[1]+DOP[2]+DOP[3]);
    }


    addData_Vaisala_t *rs92_add = rs_data->addData;
    int pDOP = -1;
    if (calc_DOPn(N, Sat_B, pos_ecef, DOP) == 0) {
        pDOP = sqrt(DOP[0]+DOP[1]+DOP[2]);
    }
    (rs92_add->sat).pDOP = pDOP;
    (rs92_add->sat).Nfix = N;
    for (j = 0; j < N; j++) {
        (rs92_add->sat).prn[j]         = Sat_B[j].prn;
        (rs92_add->sat).pseudorange[j] = Sat_B[j].pseudorange;
        (rs92_add->sat).doppler[j]     = Sat_B[j].pseudorate;
    }


    if (diter < d_err) {

        (rs_data->GPS).lat = lat;
        (rs_data->GPS).lon = lon;
        (rs_data->GPS).alt = alt;

        (rs_data->GPS).vH = vH;
        (rs_data->GPS).vD = vD;
        (rs_data->GPS).vU = vU;

    }
    else {
        (rs_data->GPS).lat = (rs_data->GPS).lon = (rs_data->GPS).alt = 0;
        (rs_data->GPS).vH = (rs_data->GPS).vD = (rs_data->GPS).vU = 0;
        N = 0;
    }


    return N;
}


// ---------------------------------------------------------------------------------------------


static int rs92_get_FrameConf(rs_data_t *rs_data, int verbose) {
    int err;

    err = rs92_check_CRC(rs_data, pos_CFG, pck_CFG);
    if (err) rs_data->crc |= crc_CFG;

    rs92_get_FrameNb(rs_data);
    rs92_get_SondeID(rs_data);
    rs92_get_Cal(rs_data, verbose);

    return err;
}

static int rs92_get_PTU(rs_data_t *rs_data) {
    int err;

    err = rs92_check_CRC(rs_data, pos_PTU, pck_PTU);
    if (err) rs_data->crc |= crc_PTU;
    //else
    {
        rs92_get_PTUmeas(rs_data);
    }
    return err;
}


static int rs92_get_GPS(rs_data_t *rs_data, int verbose) {
    int err;
    int k, n;

    err = rs92_check_CRC(rs_data, pos_GPS, pck_GPS);
    if (err) rs_data->crc |= crc_GPS;

    rs92_get_GPStime(rs_data);
    // if alm||eph:
        k = rs92_get_pseudorange(rs_data);
        if (k >= 4) {
            n = rs92_get_GPSkoord(rs_data, verbose);
        }
    Gps2Date(rs_data);

    return err;
}


static int rs92_get_Aux(rs_data_t *rs_data, int verbose) {
//
    int err;
    int i;
    ui32_t aux;
    ui8_t *frame = rs_data->frame_bytes;

    err = rs92_check_CRC(rs_data, pos_AUX, pck_AUX);
    if (err) rs_data->crc |= crc_AUX;

    if (verbose) {
        fprintf(stdout, "AUX #");
        for (i = 0; i < 4; i++) {
            aux = frame[pos_AUX+4+2*i] | (frame[pos_AUX+4+2*i+1]<<8);
            fprintf(stdout, " %04x", aux);
        }
        fprintf(stdout, "\n");
    }

    return err;
}


// -------------------------------------------------------------

//
// Reed-Solomon error correction -------------------------------
//

rs_ecccfg_t cfg_rs92ecc = {
              .typ=     92,
              .msglen=  240-6-24, // 210 <= rs_K=231
              .msgpos=  6,
              .parpos=  240-24,
              .hdrlen=  6,
              .frmlen=  240
            };

#define rs_N 255
#define rs_R 24
#define rs_K (rs_N-rs_R)

static int rs92_ecc(rs_data_t *rs_data) {
// richtige framelen wichtig fuer 0-padding

    int i, ret = 0;
    int errors;
    ui8_t cw[rs_N];
    ui8_t err_pos[rs_R],
          err_val[rs_R];

    ui32_t frmlen = rs_data->pos;
    ui8_t *frame = rs_data->frame_bytes;

    // frmlen <= cfg_rs92ecc.frmlen; // = 240
    int msglen = cfg_rs92ecc.msglen; // = 240-6-24=231 // msgpos=6 rs_R=24;
    int msgpos = cfg_rs92ecc.msgpos; // = 6
    int parpos = cfg_rs92ecc.parpos; // = 240-24

    while (frmlen < parpos) frmlen++;
    while (frmlen < rs_data->frame_len) frame[frmlen++] = 0xFF; // besser bei 00-error-frames

    if (frmlen > rs_data->frame_len) frmlen = rs_data->frame_len;

    memset(cw, 0, rs_N);
    for (i = 0; i < rs_R;   i++)  cw[i]      = frame[parpos+i];
    for (i = 0; i < msglen; i++)  cw[rs_R+i] = frame[msgpos+i];
    // for (i = msglen; i < rs_K; i++)  cw[rs_R+i] = 0;

    errors = rs_decode(cw, err_pos, err_val);

    // Wenn Fehler im 00-padding korrigiert wurden,
    // war entweder der frame zu kurz, oder
    // Fehler wurden falsch korrigiert;
    // allerdings ist bei t=12 die Wahrscheinlichkeit,
    // dass falsch korrigiert wurde mit 1/t! sehr gering.

    // check CRC32
    // CRC32 OK:
    //for (i = 0; i < cfg_rs92ecc.hdrlen; i++) frame[i] = data[i];
    for (i = 0; i < rs_R; i++) {
        frame[parpos+i] = cw[i];
    }
    for (i = 0; i < msglen; i++) { // msglen <= rs_K
        frame[msgpos+i] = cw[rs_R+i];
    }

    ret = errors;
    if (errors < 0) ret = -1;
    else rs_data->pos = frmlen;

    return ret;
}

// -------------------------------------------------------------

//
// process bits/bytes
//

static int rs92_framebits2bytes(rs_data_t *rs_data) {

    char  *rawframebits = rs_data->frame_rawbits;
    ui8_t *frame        = rs_data->frame_bytes;
    ui32_t n;

    for (n = 0; n < rs_data->pos; n++) {
        frame[n] = rs_data->bits2byte(rs_data, rawframebits+(BITS*n));
    }

    return 0;
}


int rs92_process(void *data, int raw, int options) {
    rs_data_t *rs_data = data;
    int err=0, ret=0;
    ui32_t n;

    if (rs_data->input < 8) {
        rs92_framebits2bytes(rs_data);
    }

    for (n = rs_data->pos; n < rs_data->frame_len; n++) {
        rs_data->frame_bytes[n] = 0;
    }

    rs_data->ecc = rs92_ecc(rs_data);
    rs_data->crc = 0;

    if ( !raw ) {

        err = 0;
        ret = 0;

        ret  = rs92_get_FrameConf(rs_data, options & 0x1);
        err |= ret<<0;

        ret  = rs92_get_PTU(rs_data);
        err |= ret<<1;

        ret = rs92_get_GPS(rs_data, options & 0);
        err |= ret<<2;

        ret = rs92_get_Aux(rs_data, options & 0x2);
        err |= ret<<3;

    }

    return err;
}


// manchester1 1->10,0->01: 1.bit
// manchester2 0->10,1->01: 2.bit
// RS92-SGP: 8N1 manchester2
char manch(char *mbits) {
   if      (((mbits[0]&1) == 1) && ((mbits[1]&1) == 0)) return 0;
   else if (((mbits[0]&1) == 0) && ((mbits[1]&1) == 1)) return 1;
   else return -1;
}
int rs92_mbits2byte(void *data, char mbits[]) {
// 0 eq '0'=0x30
// 1 eq '1'=0x31
    int i, byte=0, d=1;
    int bit8[8];

    if (manch(mbits+0) != 0) return 0x100;     // hier error-00-frames mit pos==frame_len moeglich;
    for (i = 0; i < 8; i++) {                  // eventuell hier reync, oder in demod
        bit8[i] = manch(mbits+2*(i+1));
    }

    for (i = 0; i < 8; i++) {     // little endian
    /* for (i = 7; i >= 0; i--) { // big endian */
        if      ((bit8[i]&1) == 1)  byte += d;
        else if ((bit8[i]&1) == 0)  byte += 0;
        //else return 0x100;
        d <<= 1;
    }

    return  byte;
}


int init_rs92data(rs_data_t *rs_data, int orbdata, char *eph_file) {

    FILE *fp = NULL;

    rs_init_RS255();

    // int in = rs_data->input;
    // memset(rs_data, 0, sizeof(rs_data_t));
    // rs_data->input = in

    rs_data->baud = BAUD;
    rs_data->bits = BITS;

    rs_data->header = calloc(sizeof(headerbits_rs92sgp), 1);
    if (rs_data->header == NULL) return ERROR_MALLOC;
    strcpy(rs_data->header, headerbits_rs92sgp);
    rs_data->header_ofs = 40;
    rs_data->header_len = 80;

    rs_data->bufpos = -1;
    rs_data->buf = calloc((rs_data->header_len)+1, 1);
    if (rs_data->buf == NULL) return ERROR_MALLOC;

    if (rs_data->input < 8) {
        rs_data->frame_rawbits = calloc(RAWBITFRAME_LEN, 1);
        if (rs_data->frame_rawbits == NULL) return ERROR_MALLOC;
        strncpy(rs_data->frame_rawbits, headerbits_rs92sgp, strlen(headerbits_rs92sgp));

        //rs_data->frame_bits = rs_data->frame_rawbits;
    }

    rs_data->frame_bytes = calloc(FRAME_LEN, 1);
    if (rs_data->frame_bytes == NULL) return ERROR_MALLOC;
    memcpy(rs_data->frame_bytes, headerbytes_rs92sgp, sizeof(headerbytes_rs92sgp));

    rs_data->frame_start = (rs_data->header_ofs + rs_data->header_len) / rs_data->bits;
    rs_data->pos_min = pos_PTU;
    rs_data->frame_len = FRAME_LEN;

    rs_data->bits2byte = rs92_mbits2byte;
    rs_data->rs_process = rs92_process;

    rs_data->addData = &rs92_addData;


    if (orbdata) {
        fp = fopen(eph_file, "r"); // txt-mode
        if (fp == NULL) orbdata = 0;
    }

    if (orbdata == 1) {
        if (read_SEMalmanac(fp, alm) == 0) {
            almanac = 1;
        }
        fclose(fp);
        d_err = 4000;
    }
    if (orbdata == 2) {
        ephs = read_RNXpephs(fp);
        if (ephs) {
            ephem = 1;
            almanac = 0;
        }
        fclose(fp);
        d_err = 1000;
    }


    return 0;
}

int free_rs92data(rs_data_t *rs_data) {

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

