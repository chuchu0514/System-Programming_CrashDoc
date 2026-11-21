#include <stdio.h>
#include <stdarg.h>      
#include <time.h>        
#include <pthread.h>
#include <sys/stat.h>    
#include "log.h"

#define LOG_DIR  "logs"
#define LOG_PATH "logs/server.log"

static FILE *log_fp = NULL;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

int log_open(void)
{
    mkdir(LOG_DIR, 0755);             
    log_fp = fopen(LOG_PATH, "a");
    return (log_fp == NULL) ? -1 : 0;
}

void log_msg(const char *fmt, ...)
{
    va_list ap;
    time_t now;
    struct tm tm;
    char tbuf[32];

    if (log_fp == NULL)                 
        return;

    now = time(NULL);                    
    localtime_r(&now, &tm);              /* 년/월/일/시/분/초 */
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm);

    pthread_mutex_lock(&log_lock);
    fprintf(log_fp, "%s ", tbuf);        /* 시각 */
    va_start(ap, fmt);
    vfprintf(log_fp, fmt, ap);           /* 내용 */
    va_end(ap);
    fputc('\n', log_fp);                 
    fflush(log_fp);
    pthread_mutex_unlock(&log_lock);
}

void log_close(void)
{
    if (log_fp != NULL) {
        fclose(log_fp);
        log_fp = NULL;
    }
}