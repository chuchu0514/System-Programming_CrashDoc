#ifndef CRASH_INFO_H
#define CRASH_INFO_H

/* 클라이언트가 수집하고, 서버가 분석기·기록에 넘기는 크래시 정보.
 * 혼자 바꾸지 말기*/
typedef struct {
    char cmd[256];          /* 실행한 명령어 전체, 예: "./cp2 a.txt b.txt" */
    int  signo;             /* 죽인 시그널 번호, 시그널이 아니면 0 */
    int  exit_code;         /* 정상 종료 코드, 시그널로 죽었으면 -1 */
    int  timed_out;         /* 멈춤 감지로 crashdoc이 죽였으면 1 */
    long elapsed_ms;        /* 실행 시간 (밀리초) */
    long maxrss_kb;         /* 최대 메모리 (KB, wait4의 ru_maxrss) */
    char source_code[4096]; /* 실행 파일과 연결된 소스 일부, 없으면 빈 문자열 */
    char last_output[4096]; /* 마지막 출력 N줄, 없으면 빈 문자열 */
} crash_info;

#endif
