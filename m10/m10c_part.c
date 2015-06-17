
/*
 * radiosonde M10 (Trimble GPS)
 * author: zilog80
 *
 * big endian forest
 */


typedef struct {
    int week; int gpssec;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; int sek;
    double lat; double lon; double h;
} datum_t;

datum_t datum;


#define BAUD_RATE  4800


unsigned char header_bytes[] = { 0x64, 0x9F, 0x20 };
char header[] = "011001001001111100100000";

#define FRAME_LEN 102
int framebytes[FRAME_LEN];


int bits2bytes(char *bits) {
// big endian
// framebytes[pos] = byteval;
}


/* -------------------------------------------------------------------------- */

#define pos_GPSTOW     0x0A  // 4 byte
#define pos_GPSlat     0x0E  // 4 byte
#define pos_GPSlon     0x12  // 4 byte
#define pos_GPSheight  0x16  // 4 byte
#define pos_GPSweek    0x20  // 2 byte

char weekday[7][3] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};

double B60B60 = 0xB60B60;  // 2^32/360 = 0xB60B60.xxx

void Gps2Date(long GpsWeek, long GpsSeconds, int *Year, int *Month, int *Day) {
//
}

int get_GPSweek() {
//
    for (i = 0; i < 2; i++) {
        gpsweek_bytes[i] = framebytes[pos_GPSweek + i];
    }

    datum.week = (gpsweek_bytes[0] << 8) + gpsweek_bytes[1];

    return 0;
}

int get_GPStime() {
//
    for (i = 0; i < 4; i++) {
        gpstime_bytes[i] = framebytes[pos_GPSTOW + i];
    }

    gpstime = 0;
    for (i = 0; i < 4; i++) {
        gpstime |= gpstime_bytes[i] << (8*(3-i));
    }

    //ms = gpstime % 1000;
    gpstime /= 1000;
    datum.gpssec = gpstime;

    day = gpstime / (24 * 3600);
    gpstime %= (24*3600);

    datum.wday = day;
    datum.std =  gpstime/3600;
    datum.min = (gpstime%3600)/60;
    datum.sek =  gpstime%60;

    return 0;
}

int get_GPSlat() {
//
    for (i = 0; i < 4; i++) {
        gpslat_bytes[i] = framebytes[pos_GPSlat + i];
    }

    gpslat = 0;
    for (i = 0; i < 4; i++) {
        gpslat |= gpslat_bytes[i] << (8*(3-i));
    }
    datum.lat = gpslat / B60B60;

    return 0;
}

int get_GPSlon() {
//
    for (i = 0; i < 4; i++) {
        gpslon_bytes[i] = framebytes[pos_GPSlon + i];
    }

    gpslon = 0;
    for (i = 0; i < 4; i++) {
        gpslon |= gpslon_bytes[i] << (8*(3-i));
    }
    datum.lon = gpslon / B60B60;

    return 0;
}

int get_GPSheight() {
//
    for (i = 0; i < 4; i++) {
        gpsheight_bytes[i] = framebytes[pos_GPSheight + i];
    }

    gpsheight = 0;
    for (i = 0; i < 4; i++) {
        gpsheight |= gpsheight_bytes[i] << (8*(3-i));
    }
    datum.h = gpsheight / 1000.0;

    return 0;
}

/* -------------------------------------------------------------------------- */

int print_pos() {
    int err;

    err = 0;
    err |= get_GPSweek();
    err |= get_GPStime();
    err |= get_GPSlat();
    err |= get_GPSlon();
    err |= get_GPSheight();

    if (!err) {

        Gps2Date(datum.week, datum.gpssec, &datum.jahr, &datum.monat, &datum.tag);

        printf(" (W %d) ", datum.week);
        printf("%s ", weekday[datum.wday]);
        printf("%04d-%02d-%02d (%02d:%02d:%02d) ", 
                datum.jahr, datum.monat, datum.tag, datum.std, datum.min, datum.sek);
        printf(" lat: %.6f ", datum.lat);
        printf(" lon: %.6f ", datum.lon);
        printf(" h: %.2f ", datum.h);
        printf("\n");
 
    }

    return err;
}

void print_frame() {
    int i;
    ui8_t byte;

    for (i = 0; i < FRAME_LEN; i++) {
        byte = framebytes[i];
        fprintf(stdout, "%02x", byte);
    }
    fprintf(stdout, "\n");

}


int main(int argc, char **argv) {


    while ( !read_bits_psk() ) {
//
// read_bit_PSK:
/*
synch: ...,du,ud,du  oder  ...,ud,du,ud
bit 0: ud,du  oder  du,ud
bit 1: du,du  oder  ud,ud  (phase shift)
Header
_uuddu udududduududduud udduudududududud duududduudduuddu (oder:)
_dduud dududuudduduuddu duuddudududududu udduduudduudduud (invers)
  0 0  0 1 1 0 0 1 0 0  1 0 0 1 1 1 1 1  0 0 1 0 0 0 0 0
*/
// header_found
// bits2byte
// framebytes
//
                    if (option_raw) print_frame();
                    else print_pos();

    }


    return 0;
}

