

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// CRC16
// Generatorpolynom x^16+x^12+x^5+1 = (1) 0001 0000 0010 0001 = 0x(1)1021
#define CRC 16
#define CODELEN_CRC (2*0xDF)
unsigned char msg[CODELEN_CRC],
              par[8];
char bitstr[CODELEN_CRC*8+1+32*8];
char g16[] = "10001000000100001"; // big endian
unsigned int crc16poly = 0x1021;
unsigned int crc16init = 0xFFFF; // 16bit


// BCH-Code (63,51)
// Generatorpolynom x^12+x^10+x^8+x^5+x^4+x^3+1
#define BCH 46
#define CODELEN_BCH 46
char g12[] = "1010100111001"; // big endian


char *g = NULL;
int msglen, parlen;

int option = 0;


int check(char *str) {
    while (*str) {
        //printf("%02X ", *str);
        if (*str < 0x30 || *str > 0x31) return -1;
        str++;
    }
    //printf("\n");
    return 0;
}

int deg(char *x) {
    int i;
    i = strlen(x);
    while (*x && *x == '0') {
        x++;
        i--;
    }
    return i-1;
}

char *shortstr(char *x) {
    if (strlen(x) <= 1) return x;
    while (strlen(x) > 1 && *x == '0') x++;
    return x;
}

int polymodcheck(char *x) {
    int i, j, d;
    char *t = malloc(strlen(x)+1);
    char *taddr = t;
    t[0] = '\0';
    for (i = 0; i < strlen(x)+1; i++) t[i] = x[i];
    t = shortstr(t);
    d = strlen(t);
    printf("%s\n", t);
    printf("%s\n", g);
    while (deg(t) >= deg(g)) {
        for (i = 0; i < strlen(g); i++) {
            if (t[i] == g[i]) t[i] = '0'; else t[i] = '1';
        }
        t = shortstr(t);
        for (j = 0; j < d-strlen(t); j++) printf(" ");
        printf("%s\n", t);
        if (t[0] == '0') return 0;
        for (j = 0; j < d-strlen(t); j++) printf(" ");
        printf("%s\n", g);
    }
    printf("%s\n", t);
    free(taddr);
    return 1;
}

unsigned int crc16(unsigned char bytes[], int len) {
    unsigned int rem = crc16init; // init value 
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


int main(int argc, char *argv[]) {
    char *str;
    int i, j, rem, byt;

    if (argv[1]) {
        if (strcmp(argv[1], "--crc") == 0) option = CRC;
        if (strcmp(argv[1], "--bch") == 0) option = BCH;
    }
    if (!argv[1] || !option) {
        fprintf(stderr, "%s --crc <hexstring> [crc]\n", argv[0]);
        fprintf(stderr, "%s --bch <bitstring> \n", argv[0]);
        return 0;
    }

    str = argv[2];

    if (option == CRC) {

        msglen = strlen(str)/2;
        if (msglen > 2*CODELEN_CRC) {
            fprintf(stderr, "len > %d\n", CODELEN_CRC);
            return -3;
        }

        for (i = 0; i < msglen; i++) sscanf(argv[2]+2*i, "%2hhx", msg+i);
        rem = crc16(msg, msglen);
        printf("%04X\n", rem);

        g = g16;
        parlen = (strlen(g)-1)/8;

        for (i = 0; i < msglen; i++) {
            byt = msg[i];
            if (i < 2) byt ^= (crc16init >> (8*(1-i))) & 0xFF;  // init value 0xFFFF
            for (j = 0; j < 8; j++) {
                bitstr[8*i+7-j] = (byt & 0x1) + 0x30;
                byt >>= 1;
            }
        }
        // wenn crc nicht angehaengt, dann x^n shiften:
        for (i = 0; i < 8*parlen; i++) bitstr[8*msglen+i] = 0x30;
        if (argv[3]  &&  strlen(argv[3])/2 == parlen) {
            for (i = 0; i < parlen; i++) {
                sscanf(argv[3]+2*i, "%2hhx", par+i);
                byt = par[i];
                for (j = 0; j < 8; j++) {
                    bitstr[8*(msglen+parlen-1-i)+7-j] = (byt & 0x1) + 0x30;
                    byt >>= 1;
                }
            }
        }
/*
        for (i = 0; i < msglen; i++) printf("%02X ", msg[i]);
        printf("  "); for (i = 0; i < parlen; i++) printf("%02X ", par[i]);
        printf("\n");
*/
        str = shortstr(bitstr);
    }
    else if (option == BCH) {

        if (strlen(str) > CODELEN_BCH) {
            printf("CODELEN\n");
            return -3;
        }

        str = shortstr(str);
        if (check(str)) {
            printf("nicht in GF(2)[X]\n");
            return -1;
        }
        //printf("f = %s\n", str);
        //printf("deg(f) = %d\n", deg(str));
        g = g12;

    }

    //if (option != CRC)
    polymodcheck(str);


    return 0;
}

