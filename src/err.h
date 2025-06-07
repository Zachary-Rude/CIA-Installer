#ifndef ERR_H
#define ERR_H

void err_show(char file_name[], int line, char message[]);
void err_show_res(Result result, char task[]);
void err_show_errno(int err, char task[]);

#endif
