#include "protocol.h"
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>   /* htonl, ntohl */
#include <string.h>      /* memcpy */
#include <stdio.h>
#include <stdlib.h>


/* buf의 n바이트를 fd에 전부 쓴다. 성공 0, 실패 -1 */
static int write_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;     
    size_t left = n;        

    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR)   /* 시그널 때문에 끊긴 거면 */
                continue;         
            return -1;            
        }
        p += w;      
        left -= w;  
    }
    return 0;
}

int send_msg(int fd, uint8_t type, const char *body, uint32_t len)
{
    unsigned char hdr[5];      /* [길이 4][종류 1] */
    uint32_t netlen;

    if (len > MSG_MAX_BODY)            /* 약속보다 큰 본문 거절 */
        return -1;
    if (len > 0 && body == NULL)       /* 본문 있다면서 포인터가 없음 */
        return -1;

    netlen = htonl(len);               /* 네트워크 순서로 */
    memcpy(hdr, &netlen, 4);           /* 앞 4바이트에  htonl돌린 길이 복사 */
    hdr[4] = type;                     /* 마지막 1바이트는 종류 */

    if (write_all(fd, hdr, 5) < 0)     /* 송장 보내기 */
        return -1;
    if (len > 0 && write_all(fd, body, len) < 0)   /* 본문 보내기 */
        return -1;
    return 0;
}

/* fd에서 정확히 n바이트를 buf에 읽는다. 성공 0, 실패·끊김 -1 */
static int read_all(int fd, void *buf, size_t n)
{
    char *p = buf;
    size_t left = n;

    while (left > 0) {
        ssize_t r = read(fd, p, left);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)         
            return -1;
        p += r;
        left -= r;
    }
    return 0;
}

int recv_msg(int fd, uint8_t *type, char *body, uint32_t bodysz)
{
    unsigned char hdr[5];
    uint32_t netlen, len;

    if (read_all(fd, hdr, 5) < 0)      /* 송장 읽기 */
        return -1;

    memcpy(&netlen, hdr, 4);           /* 앞 4바이트 꺼내서 */
    len = ntohl(netlen);               /* 내 기계 순서로 */
    *type = hdr[4];

    if (len > MSG_MAX_BODY)            /* 길이 검사 */
        return -1;
    if (len >= bodysz)                 /* '\0' 자리까지 들어가야 함 */
        return -1;

    if (len > 0 && read_all(fd, body, len) < 0)   /* 본문 읽기 */
        return -1;
    body[len] = '\0';
    return (int)len;
}

int crash_info_encode(const crash_info *in, char *out, size_t outsz)
{
    int n = snprintf(out, outsz,
                     "cmd=%s\n"
                     "signal=%d\n"
                     "exit_code=%d\n"
                     "timed_out=%d\n"
                     "elapsed_ms=%ld\n"
                     "maxrss_kb=%ld\n"
                     "\n"                  /* 빈 줄 = 헤더 끝 */
                     "%s",                 /* 마지막 출력 원문 */
                     in->cmd, in->signo, in->exit_code, in->timed_out,
                     in->elapsed_ms, in->maxrss_kb, in->last_output);

    if (n < 0 || (size_t)n >= outsz)   /* 에러거나 잘림 */
        return -1;
    return n;
}

int crash_info_decode(const char *body, crash_info *out)
{
    const char *sep;
    char hdr[1024];                 
    size_t hlen;                    
    char *line, *save, *eq;         

    memset(out, 0, sizeof(*out));          /* 전부 0으로 */

    sep = strstr(body, "\n\n");            /* 빈 줄 찾기, 여기가 헤더 끝*/
    if (sep == NULL)
        return -1;                         /* 없으면 형식 오류 */

    snprintf(out->last_output, sizeof(out->last_output),
             "%s", sep + 2);               /* 마지막 출력 */

    hlen = (size_t)(sep - body);           /* 헤더 길이 */
    if (hlen >= sizeof(hdr))
        return -1;
    memcpy(hdr, body, hlen);
    hdr[hlen] = '\0';


    for (line = strtok_r(hdr, "\n", &save); line != NULL;
         line = strtok_r(NULL, "\n", &save)) {

        eq = strchr(line, '=');
        if (eq == NULL)
            continue;               /* '=' 없는 줄은 무시 */
        *eq = '\0';                 /* line = 키, eq + 1 = 값 */

        if (strcmp(line, "cmd") == 0)
            snprintf(out->cmd, sizeof(out->cmd), "%s", eq + 1);
        else if (strcmp(line, "signal") == 0)
            out->signo = atoi(eq + 1);
        else if (strcmp(line, "exit_code") == 0)
            out->exit_code = atoi(eq + 1);
        else if (strcmp(line, "timed_out") == 0)
            out->timed_out = atoi(eq + 1);
        else if (strcmp(line, "elapsed_ms") == 0)
            out->elapsed_ms = atol(eq + 1);
        else if (strcmp(line, "maxrss_kb") == 0)
            out->maxrss_kb = atol(eq + 1);
      
    }
    return 0;
}