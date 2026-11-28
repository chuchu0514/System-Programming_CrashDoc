#include <pthread.h>
#include "stats.h"

static server_stats g_stats;                             
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

void stats_begin(void)
{
    pthread_mutex_lock(&g_lock);
    g_stats.active++;
    pthread_mutex_unlock(&g_lock);
}

void stats_end(int ok)
{
    pthread_mutex_lock(&g_lock);
    g_stats.active--;
    g_stats.total++;
    if (!ok)
        g_stats.errors++;
    pthread_mutex_unlock(&g_lock);
}

void stats_crash(int signo)
{
    if (signo < 0 || signo >= STATS_MAX_SIG)   
        return;
    pthread_mutex_lock(&g_lock);
    g_stats.sig_count[signo]++;
    pthread_mutex_unlock(&g_lock);
}

void stats_snapshot(server_stats *out)
{
    pthread_mutex_lock(&g_lock);
    *out = g_stats;                           
    pthread_mutex_unlock(&g_lock);
}