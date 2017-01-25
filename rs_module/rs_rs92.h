
#ifndef RS_RS92_H
#define RS_RS92_H


int rs92_process(void *, int, int);
//int rs92_bits2byte(void *, char *);

int init_rs92data(rs_data_t *, int, char *);
int free_rs92data(rs_data_t *);


#endif  /* RS_RS92_H */

