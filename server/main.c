#include <stdio.h>
#include <stdlib.h>       /* atoi */
#include <string.h>       /* memset */
#include <unistd.h>       /* close */
#include <arpa/inet.h>    /* htons, htonl */
#include <netinet/in.h>   /* sockaddr_in */
#include <sys/socket.h>   /* socket, bind, listen, accept */
#include "protocol.h"
#include "analyzer.h"
#include "store.h"
#include <pthread.h>
#include "stats.h"

typedef struct{

    int fd;
    char ip[INET_ADDRSTRLEN];
}client_arg;

/* port 번호로 연결을 받을 준비가 된 듣는 소켓을 만든다. 실패 -1 */
static int open_listen(int port)
{
    int fd, on = 1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);                 
    if (fd < 0)
        return -1;

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&addr, 0, sizeof(addr));                      
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 16) < 0) {                             /* 영업 시작 */
        close(fd);
        return -1;
    }
    return fd;
}

/*handle함수들은 성공하면 0, 실패 -1임*/

/* ANALYZE: 크래시 정보 해석 → 분석 → 기록 */
static int handle_analyze(const char *body, const char *ip, char *out, size_t outsz)
{
    crash_info ci;

    if (crash_info_decode(body, &ci) < 0)      /* 텍스트 → 구조체 */
        return -1;
    stats_crash(ci.signo);
    if (analyze_crash(&ci, out, outsz) < 0)    /* 분석 결과를 out에 */
        return -1;
    store_save(&ci, out, ip);           /* 기록 저장 */
    return 0;
}

/* BUILD: gcc 에러 텍스트 → 분석 */
static int handle_build(const char *body, char *out, size_t outsz)
{
    return analyze_build(body, out, outsz);
}

/* HISTORY: 최근 n개 목록 */
static int handle_history(const char *body, char *out, size_t outsz)
{
    int n;

    if (sscanf(body, "n=%d", &n) != 1 || n <= 0)
        return -1;
    return store_list(n, out, outsz);
}

/* SHOW: "id=12" → 기록 하나 */
static int handle_show(const char *body, char *out, size_t outsz)
{
    int id;

    if (sscanf(body, "id=%d", &id) != 1)
        return -1;
    return store_show(id, out, outsz);
}


static void *handle_client(void *arg)
{
    client_arg *ca = arg;                 /*arg는 ca..*/
    int fd = ca->fd;                      
    char ip[INET_ADDRSTRLEN];
    char buf[MSG_MAX_BODY + 1];
    char out[MSG_MAX_BODY];
    uint8_t type;
    int r;

    memcpy(ip, ca->ip, sizeof(ip));       
    free(ca);                             
    stats_begin();

    if (recv_msg(fd, &type, buf, sizeof(buf)) < 0) {   
        stats_end(0); /* 0은 실패 */
        close(fd);
        return NULL;
    }

    out[0] = '\0';
    switch (type) {                                     
    case MSG_ANALYZE: r = handle_analyze(buf, ip, out, sizeof(out)); break;
    case MSG_BUILD:   r = handle_build(buf, out, sizeof(out));   break;
    case MSG_HISTORY: r = handle_history(buf, out, sizeof(out)); break;
    case MSG_SHOW:    r = handle_show(buf, out, sizeof(out));    break;
    default:          r = -1;                                    break;
    }

    if (r == 0) {
        send_msg(fd, MSG_RESULT, out, strlen(out));
    } else {
        static const char err[] = "서버가 요청을 처리하지 못했습니다\n";
        send_msg(fd, MSG_ERROR, err, strlen(err));
    }

    stats_end(r == 0);
    close(fd);
    return NULL;
}

int main(int argc, char *argv[])
{
    int listen_fd;
    if (argc < 2) {
        fprintf(stderr, "사용법: crashdoc-server <포트> [--analyzer rule|api|local]\n");
        return 1;
    }

    listen_fd = open_listen(atoi(argv[1]));
    if (listen_fd < 0) {
        perror("open_listen");
        return 1;
    }

    for (;;) {
        int conn_fd;
        pthread_t tid;
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        client_arg *ca;

        conn_fd = accept(listen_fd, (struct sockaddr *)&caddr, &clen);
        if (conn_fd < 0)
            continue;

        ca = malloc(sizeof(*ca));                
        if (ca == NULL) {
            close(conn_fd);
            continue;
        }
        ca->fd = conn_fd;
        inet_ntop(AF_INET, &caddr.sin_addr, ca->ip, sizeof(ca->ip));

        if (pthread_create(&tid, NULL, handle_client, ca) != 0) {
            free(ca);
            close(conn_fd);
            continue;
        }
        pthread_detach(tid);
    }

    close(listen_fd);
    return 0;
}