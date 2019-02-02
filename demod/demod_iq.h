
float read_wav_header(FILE*, float, int);
int f32buf_sample(FILE*, int, int);
int read_sbit(FILE*, int, int*, int, int, int, int);
int read_IDsbit(FILE*, int, int*, int, int, int, int);
int read_softbit(FILE *fp, int symlen, int *bit, float *sb, float level, int inv, int ofs, int reset, int cm);
float header_level(char hdr[], int hLen, unsigned int pos, int inv);

int getCorrDFT(int, int, unsigned int, float *, unsigned int *);
int headcmp(int, char*, int, unsigned int, int, int);
int get_fqofs(int, unsigned int, float *, float *);
float get_bufvar(int);
float get_bufmu(int);

int init_buffers(char*, int, float, int);
int free_buffers(void);

unsigned int get_sample(void);

