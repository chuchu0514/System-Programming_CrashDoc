#ifndef STATS_H
#define STATS_H

#define STATS_MAX_SIG 65            

typedef struct {
    long total;                     /* 처리 끝난 요청 수 */
    long active;                    /* 지금 처리 중인 요청 수 */
    long errors;                    /* ERROR로 답한 수 */
    long sig_count[STATS_MAX_SIG];  /* 시그널별 크래시 수 */
} server_stats;

void stats_begin(void);                    /* 요청 처리 시작 */
void stats_end(int ok);                    /* 요청 처리 끝. 1 성공, 0 실패 */
void stats_crash(int signo);               /* ANALYZE로 들어온 시그널 하나 기록 */
void stats_snapshot(server_stats *out);    /* 지금 통계를 out에 통째로 복사 (대시보드용) */

#endif