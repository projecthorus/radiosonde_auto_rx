
#ifndef RS_RS41_H
#define RS_RS41_H


int rs41_process(void *, int, int);
//int rs41_xbits2byte(void *, char *);

int init_rs41data(rs_data_t *);
int free_rs41data(rs_data_t *);


#endif  /* RS_RS41_H */

