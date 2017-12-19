
float read_wav_header(FILE*, float);
int f32buf_sample(FILE*, int, int);
int read_sbit(FILE*, int, int*, int, int, int, int);

int getmaxCorr(float*, unsigned int*, int);
int headcmp(int, char*, int, unsigned int);
float get_bufvar(int);

int init_buffers(char*, int, int);
int free_buffers(void);

unsigned int get_sample(void);

