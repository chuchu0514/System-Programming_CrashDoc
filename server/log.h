#ifndef LOG_H
#define LOG_H

int  log_open(void);                 /* 성공 0, 실패 -1 */
void log_msg(const char *fmt, ...);  
void log_close(void);                

#endif