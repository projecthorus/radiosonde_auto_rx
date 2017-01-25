
#ifndef RS_DATA_H
#define RS_DATA_H


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;
typedef short i16_t;
typedef int   i32_t;


typedef struct {
    int week; int msec;
    double lat; double lon; double alt;
    double vN; double vE; double vU;
    double vH; double vD;
} GPS_t;

typedef struct {
    double P;
    double T;
    double H1;
    double H2;
} PTU_t;

typedef struct {

    char SN[12];
    int frnr;
    int freq;
    int year; int month; int day;
    int wday;
    int hr; int min; float sec;

    GPS_t GPS;
    PTU_t PTU;

    ui32_t crc;
    int    ecc;

    int  header_ofs;
    int  header_len;
    int  bufpos;
    char *buf;
    char *header;

    int   baud;
    int   bits;
    float samples_per_bit;

    char  *frame_rawbits;
    char  *frame_bits;
    ui8_t *frame_bytes;
    ui32_t frame_start;
    ui32_t pos;
    ui32_t pos_min;
    ui32_t frame_len;

    int (*bits2byte)(void *, char *);
    int (*rs_process)(void *, int, int);
    int input;

    void *addData;

} rs_data_t;

typedef struct {
    ui32_t tow;
    int    prn[12];
    double pseudorange[12];
    double doppler[12];
    ui8_t  status[12];
    double pos_ecef[3];
    double vel_ecef[3];
    ui8_t  Nfix;
    double pDOP;
    double sAcc;
} sat_t;

typedef struct {
    char SN[12];
    ui8_t bytes[0x33][16+1];
    sat_t sat;
} addData_Vaisala_t;

typedef struct {
    int typ;
    int msglen;
    int msgpos;
    int parpos;
    int hdrlen;
    int frmlen;
} rs_ecccfg_t;


#define ERROR_MALLOC -1



#endif  /* RS_DATA_H */

