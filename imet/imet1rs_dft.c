
/*
 *  iMet-1-RS
 *  Bell202 8N1
 *  (empfohlen: sample rate 48kHz)
*/

#include <stdio.h>
#include <string.h>
#include <complex.h>
#include <math.h>

typedef  unsigned char  ui8_t;

int option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 0,      // rohe Frames
    option_rawbits = 0,
    option_dft = 0,
    option_json = 0,
    wavloaded = 0;

// Bell202, 1200 baud (1200Hz/2200Hz), 8N1
#define BAUD_RATE 1200


typedef struct {
    int frame;
    int hour;
    int min;
    int sec;
    float lat;
    float lon;
    int alt;
    int sats;

    float temp;
    float pressure;
    float humidity;
    float batt;

    int gps_valid;
    int ptu_valid;
} json_output_data_t;

json_output_data_t json_data;

/* ------------------------------------------------------------------------------------ */

int sample_rate = 0, bits_sample = 0, channels = 0;
//float samples_per_bit = 0;

int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}

int read_wav_header(FILE *fp) {
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;

    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "RIFF", 4)) return -1;
    if (fread(txt, 1, 4, fp) < 4) return -1;
    // pos_WAVE = 8L
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "WAVE", 4)) return -1;
    // pos_fmt = 12L
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "fmt ", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;
    if (fread(dat, 1, 2, fp) < 2) return -1;

    if (fread(dat, 1, 2, fp) < 2) return -1;
    channels = dat[0] + (dat[1] << 8);

    if (fread(dat, 1, 4, fp) < 4) return -1;
    memcpy(&sample_rate, dat, 4); //sample_rate = dat[0]|(dat[1]<<8)|(dat[2]<<16)|(dat[3]<<24);

    if (fread(dat, 1, 4, fp) < 4) return -1;
    if (fread(dat, 1, 2, fp) < 2) return -1;
    //byte = dat[0] + (dat[1] << 8);

    if (fread(dat, 1, 2, fp) < 2) return -1;
    bits_sample = dat[0] + (dat[1] << 8);

    // pos_dat = 36L + info
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "data", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;


    fprintf(stderr, "sample_rate: %d\n", sample_rate);
    fprintf(stderr, "bits       : %d\n", bits_sample);
    fprintf(stderr, "channels   : %d\n", channels);

    if ((bits_sample != 8) && (bits_sample != 16)) return -1;

    //samples_per_bit = sample_rate/(float)BAUD_RATE;
    //fprintf(stderr, "samples/bit: %.2f\n", samples_per_bit);

    return 0;
}

#define EOF_INT  0x1000000

int read_signed_sample(FILE *fp) {  // int = i32_t
    int byte, i, ret;         //  EOF -> 0x1000000

    for (i = 0; i < channels; i++) {
                           // i = 0: links bzw. mono
        byte = fgetc(fp);
        if (byte == EOF) return EOF_INT;
        if (i == 0) ret = byte;
    
        if (bits_sample == 16) {
            byte = fgetc(fp);
            if (byte == EOF) return EOF_INT;
            if (i == 0) ret +=  byte << 8;
        }

    }

    if (bits_sample ==  8) return ret-128;
    if (bits_sample == 16) return (short)ret;

    return ret;
}

/* ------------------------------------------------------------------------------------ */


#define LOG2N    7  // 2^7 = 128 = N
#define N      128  // 128  Vielfaches von 22 oder 10 unten
#define WLEN    80  // (2*(48000/BAUDRATE))

#define BITS (10)
#define LEN_BITFRAME  BAUD_RATE
#define LEN_BYTEFRAME  (LEN_BITFRAME/BITS)
#define HEADLEN 30

char header[] = "1111111111111111111""10""10000000""1";
char buf[HEADLEN+1] = "x";
int bufpos = -1;

int    bitpos;
ui8_t  bitframe[LEN_BITFRAME+1] = { 0, 1, 0, 0, 0, 0, 0, 0, 0, 1};
ui8_t  byteframe[LEN_BYTEFRAME+1];


double x[N];
double complex  Z[N], w[N], expw[N][N], ew[N*N];

int    ptr;
double Hann[N], buffer[N+1], xn[N];


void init_dft() {
    int i, k, n;

    for (i = 0; i < N; i++)     Hann[i] = 0;
    for (i = 0; i < WLEN; i++)  Hann[i] = 0.5 * (1 - cos( 2 * M_PI * i / (double)(WLEN-1) ) );
                              //Hann[i+(N-1-WLEN)/2] = 0.5 * (1 - cos( 2 * M_PI * i / (double)(WLEN-1) ) );

    for (k = 0; k < N; k++) {
        w[k] = -I*2*M_PI * k / (double)N;
        for (n = 0; n < N; n++) {
            expw[k][n] = cexp( w[k] * n );
            ew[k*n] = expw[k][n];
        }
    }
}


double dft_k(int k) {
    int n;
    double complex  Zk;

    Zk = 0;
    for (n = 0; n < N; n++) {
        Zk += xn[n] * ew[k*n];
    }
    return cabs(Zk);
}

void dft() {
    int k, n;

    for (k = 0; k < N/2; k++) {  // xn reell, brauche nur N/2 unten
        Z[k] = 0;
        for (n = 0; n < N; n++) {
            Z[k] += xn[n] * ew[k*n];
        }
    }
}

void dft2() {
    int s, l, l2, i, j, k;
    double complex  w1, w2, T;

    for (i = 0; i < N; i++) {
        Z[i] = (double complex)xn[i];
    }

    j = 1;
    for (i = 1; i < N; i++) {
        if (i < j) {
            T = Z[j-1];
            Z[j-1] = Z[i-1];
            Z[i-1] = T;
        }
        k = N/2;
        while (k < j) {
            j = j - k;
            k = k/2;
        }
        j = j + k;
    }

    for (s = 0; s < LOG2N; s++) {
        l2 = 1 << s;
        l  = l2 << 1;
        w1 = (double complex)1.0;
        w2 = cexp(-I*M_PI/(double)l2);
        for (j = 1; j <= l2; j++) {
            for (i = j; i <= N; i += l) {
                k = i + l2;
                T = Z[k-1] * w1;
                Z[k-1] = Z[i-1] - T;
                Z[i-1] = Z[i-1] + T;
            }
            w1 = w1 * w2;
        }
    }
}

int max_bin() {
    int k, kmax;
    double max;

    max = 0; kmax = 0;
    for (k = 0; k < N/2-1; k++) {
        if (cabs(Z[k]) > max) {
            max = cabs(Z[k]);
            kmax = k;
        }
    }

    return kmax;
}

double freq2bin(int f) {
    return  f * N / (double)sample_rate;
}

int bin2freq(int k) {
    return  sample_rate * k / N;
}


/* ------------------------------------------------------------------------------------ */


void inc_bufpos() {
  bufpos = (bufpos+1) % HEADLEN;
}

int compare() {
    int i=0, j = bufpos;

    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != header[HEADLEN-1-i]) break;
        j--;
        i++;
    }
    return i;
}

int bits2byte(ui8_t *bits) {
    int i, d = 1, byte = 0;

    if ( bits[0]+bits[1]+bits[2]+bits[3]+bits[4] // 1 11111111 1 (sync)
        +bits[5]+bits[6]+bits[7]+bits[8]+bits[9] == 10 ) return 0xFFFF;

    for (i = 1; i < BITS-1; i++) {  // little endian
        if      (bits[i] == 1)  byte += d;
        else if (bits[i] == 0)  byte += 0;
        d <<= 1;
    }
    return byte & 0xFF;
}


int bits2bytes(ui8_t *bits, ui8_t *bytes, int len) {
    int i;
    int byte;
    for (i = 0; i < len; i++) {
        byte = bits2byte(bits+BITS*i);
        bytes[i] = byte & 0xFF;
        if (byte == 0xFFFF) break;
    }
    return i;
}

void print_rawbits(int len) {
    int i;
    for (i = 0; i < len; i++) {
        if ((i % BITS == 1) || (i % BITS == BITS-1)) fprintf(stdout, " ");
        fprintf(stdout, "%d", bitframe[i]);
    }
    fprintf(stdout, "\n");
}


/* -------------------------------------------------------------------------- */

int crc16poly = 0x1021; // CRC16-CCITT
int crc16(ui8_t bytes[], int len) {
    int rem = 0x1D0F;   // initial value
    int i, j;
    for (i = 0; i < len; i++) {
        rem = rem ^ (bytes[i] << 8);
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

/* -------------------------------------------------------------------------- */

#define LEN_GPSePTU   (18+20)


/*
GPS Data Packet
offset bytes description
 0     1     SOH = 0x01
 1     1     PKT_ID = 0x02
 2     4     Latitude, +/- deg (float)
 6     4     Longitude, +/- deg (float)
10     2     Altitude, meters (Alt = n-5000)
12     1     nSat (0 - 12)
13     3     Time (hr,min,sec)
16     2     CRC (16-bit)
packet size = 18 bytes
*/
#define pos_GPSlat  0x02  // 4 byte float
#define pos_GPSlon  0x06  // 4 byte float
#define pos_GPSsats 0x0C  // 1 byte
#define pos_GPSalt  0x0A  // 2 byte int
#define pos_GPStim  0x0D  // 3 byte
#define pos_GPScrc  0x10  // 2 byte

void print_GPS(int pos) {
    float lat, lon;
    int alt, sats;
    int std, min, sek;
    int crc1, crc2;

    crc1 = ((byteframe+pos)[pos_GPScrc] << 8) | (byteframe+pos)[pos_GPScrc+1];
    crc2 = crc16(byteframe+pos, pos_GPScrc); // len=pos

    lat = *(float*)(byteframe+pos+pos_GPSlat);
    lon = *(float*)(byteframe+pos+pos_GPSlon);
    alt = ((byteframe+pos)[pos_GPSalt+1]<<8)+(byteframe+pos)[pos_GPSalt] - 5000;
    sats = (byteframe+pos)[pos_GPSsats+0];
    std = (byteframe+pos)[pos_GPStim+0];
    min = (byteframe+pos)[pos_GPStim+1];
    sek = (byteframe+pos)[pos_GPStim+2];

    fprintf(stdout, "(%02d:%02d:%02d) ", std, min, sek);
    fprintf(stdout, " lat: %.6f° ", lat);
    fprintf(stdout, " lon: %.6f° ", lon);
    fprintf(stdout, " alt: %dm ", alt);
    fprintf(stdout, " sats: %d ", sats);

    fprintf(stdout, " # ");
    fprintf(stdout, " CRC: %04X ", crc1);
    fprintf(stdout, "- %04X ", crc2);
    if (crc1 == crc2){ 
        fprintf(stdout, "[OK]");
        json_data.gps_valid = 1;
        json_data.lat = lat;
        json_data.lon = lon;
        json_data.alt = alt;
        json_data.sats = sats;
        json_data.hour = std;
        json_data.min = min;
        json_data.sec = sek;
    }else{ 
        fprintf(stdout, "[NO]");
        json_data.gps_valid = 0;
    }
}


/*
PTU (enhanced) Data Packet
offset bytes description
 0     1     SOH = 0x01
 1     1     PKT_ID = 0x04
 2     2     PKT = packet number
 4     3     P, mbs (P = n/100)
 7     2     T, °C (T = n/100)
 9     2     U, % (U = n/100)
11     1     Vbat, V (V = n/10)
12     2     Tint, °C (Tint = n/100)
14     2     Tpr, °C (Tpr = n/100)
16     2     Tu, °C (Tu = n/100)
18     2     CRC (16-bit)
packet size = 20 bytes
*/
#define pos_PCKnum  0x02  // 2 byte
#define pos_PTUprs  0x04  // 3 byte
#define pos_PTUtem  0x07  // 2 byte int
#define pos_PTUhum  0x09  // 2 byte
#define pos_PTUbat  0x0B  // 1 byte
#define pos_PTUcrc  0x12  // 2 byte

void print_ePTU(int pos) {
    int P, U;
    short T;
    int bat, pcknum;
    int crc1, crc2;

    crc1 = ((byteframe+pos)[pos_PTUcrc] << 8) | (byteframe+pos)[pos_PTUcrc+1];
    crc2 = crc16(byteframe+pos, pos_PTUcrc); // len=pos

    P   = (byteframe+pos)[pos_PTUprs] | ((byteframe+pos)[pos_PTUprs+1]<<8) | ((byteframe+pos)[pos_PTUprs+2]<<16);
    T   = (byteframe+pos)[pos_PTUtem] | ((byteframe+pos)[pos_PTUtem+1]<<8);
    U   = (byteframe+pos)[pos_PTUhum] | ((byteframe+pos)[pos_PTUhum+1]<<8);
    bat = (byteframe+pos)[pos_PTUbat];

    pcknum = (byteframe+pos)[pos_PCKnum] | ((byteframe+pos)[pos_PCKnum+1]<<8);
    fprintf(stdout, "[%d] ", pcknum);

    fprintf(stdout, " P:%.2fmb ", P/100.0);
    fprintf(stdout, " T:%.2f°C ", T/100.0);
    fprintf(stdout, " U:%.2f%% ", U/100.0);
    fprintf(stdout, " bat:%.1fV ", bat/10.0);

    fprintf(stdout, " # ");
    fprintf(stdout, " CRC: %04X ", crc1);
    fprintf(stdout, "- %04X ", crc2);
    if (crc1 == crc2){
        fprintf(stdout, "[OK]"); 
        json_data.frame = pcknum;
        json_data.ptu_valid = 1;
        json_data.temp = T/100.0;
        json_data.humidity = U/100.0;
        json_data.batt = bat/10.0;
        json_data.pressure = P/100.0;
    }else{
        fprintf(stdout, "[NO]");
        json_data.ptu_valid = 0;
    }

}


void print_JSON(){
    if(json_data.gps_valid && json_data.ptu_valid){
        printf("{ \"frame\": %d, \"id\": \"iMet\", \"datetime\": \"%02d:%02d:%02dZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %d, \"sats\": %d, \"temp\":%.2f, \"humidity\":%.2f, \"pressure\":%.2f, \"batt\":%.1f}\n",  json_data.frame, json_data.hour, json_data.min, json_data.sec, json_data.lat, json_data.lon, json_data.alt, json_data.sats, json_data.temp, json_data.humidity, json_data.pressure, json_data.batt);
            
    }

}


/* -------------------------------------------------------------------------- */

int print_frame(int len) {
    int i;
    int framelen;

    if ( len < 2 || len > LEN_BYTEFRAME) return -1;
    for (i = len; i < LEN_BYTEFRAME; i++) byteframe[i] = 0;

    if (option_rawbits)
    {
        print_rawbits((LEN_GPSePTU+2)*BITS);
    }
    else
    {
        framelen = bits2bytes(bitframe, byteframe, len);

        if (option_raw) {
            for (i = 0; i < framelen; i++) { // LEN_GPSePTU
                fprintf(stdout, "%02X ", byteframe[i]);
            }
            fprintf(stdout, "\n");
        }
        //else
        {
            if ((byteframe[0] == 0x01) && (byteframe[1] == 0x02)) { // GPS Data Packet
                print_GPS(0x00);  // packet offset in byteframe
                fprintf(stdout, "\n");
            }
            if ((byteframe[pos_GPScrc+2+0] == 0x01) && (byteframe[pos_GPScrc+2+1] == 0x04)) { // PTU Data Packet
                print_ePTU(pos_GPScrc+2);  // packet offset in byteframe
                fprintf(stdout, "\n");

                if(option_json) print_JSON();
            }
/*
            if ((byteframe[0] == 0x01) && (byteframe[1] == 0x04)) { // PTU Data Packet
                print_ePTU(0x00);  // packet offset in byteframe
                fprintf(stdout, "\n");
            }
*/
            fprintf(stdout, "\n");
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------- */


int main(int argc, char *argv[]) {

    FILE *fp;
    char *fpname;
    int  sample;
    unsigned int  sample_count;
    int i, j, kmax, k0, k1;
    int bit = 8, bit0 = 8;
    int pos = 0, pos0 = 0;
    int header_found = 0;
    int bitlen; // sample_rate/BAUD_RATE
    int len;
    double k_f0, k_f1, k_df;
    double cb0, cb1;


    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v, --verbose\n");
            fprintf(stderr, "       -r, --raw\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "--rawbits") == 0) ) {
            option_rawbits = 1;
        }
        else if ( (strcmp(*argv, "--json") == 0) ) {
            option_json = 1;
        }
        else if ( (strcmp(*argv, "-d1") == 0) || (strcmp(*argv, "--dft1") == 0) ) {
            option_dft = 1;
        }
        else if ( (strcmp(*argv, "-d2") == 0) || (strcmp(*argv, "--dft2") == 0) ) {
            option_dft = 2;
        }
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


    i = read_wav_header(fp);
    if (i) {
        fclose(fp);
        return -1;
    }


    bitlen = sample_rate/BAUD_RATE;
    k_f0 = freq2bin(2200);  // bit0: 2200Hz
    k_f1 = freq2bin(1200);  // bit1: 1200Hz
    k_df = fabs(k_f0-k_f1)/2.5;
    k0 = (int)(k_f0+.5);
    k1 = (int)(k_f1+.5);

    init_dft();

    ptr = -1; sample_count = -1;
    while ((sample=read_signed_sample(fp)) < EOF_INT) {

        ptr++; 
        sample_count++;
        if (ptr == N) ptr = 0;
        buffer[ptr] = sample / (double)(1<<bits_sample);

        if (sample_count < N) continue;


            for (j = 0; j < N; j++) {
                xn[j] = Hann[j]*buffer[(ptr + j + 1)%N];
            }

            if (option_dft) {
                if (option_dft == 2) dft2();
                else                 dft();
                kmax = max_bin();
                if      (kmax > k_f0-k_df  &&  kmax < k_f0+k_df)  bit = 0;  // kmax = freq2bin(2200): 2200Hz
                else if (kmax > k_f1-k_df  &&  kmax < k_f1+k_df)  bit = 1;  // kmax = freq2bin(1200): 1200Hz
            }
            else {
                cb0 = dft_k(k0);
                cb1 = dft_k(k1);
                if      ( cb0 > cb1 )  bit = 0;  // freq2bin(2200): 2200Hz
                else                   bit = 1;  // freq2bin(1200): 1200Hz
            }

            if (bit != bit0) {

                pos0 = pos;
                pos = sample_count;  //sample_count-(N-1)/2

                len = (pos-pos0+bitlen/2)/bitlen; //(pos-pos0)/bitlen + 0.5;
                for (i = 0; i < len; i++) {
                    inc_bufpos();
                    buf[bufpos] = 0x30 + bit0;

                    if (!header_found) {
                        if (compare() >= HEADLEN) {
                            header_found = 1;
                            bitpos = 10;
                        }
                    }
                    else {
                        bitframe[bitpos] = bit0;
                        bitpos++;
                        if (bitpos >= LEN_BITFRAME-200) {  // LEN_GPSePTU*BITS+40

                            print_frame(bitpos/BITS);

                            bitpos = 0;
                            header_found = 0;
                        }
                    }
                }
                bit0 = bit;
            }

    }
    fprintf(stdout, "\n");

    fclose(fp);

    return 0;
}

