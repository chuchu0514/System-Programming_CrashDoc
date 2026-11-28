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
#include "log.h"
#include <signal.h>


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
    if (store_save(&ci, out, ip) < 0) { /* 분석 결과와 실행 정보 기록 */
        log_msg("기록 저장 실패 %s", ip);
        return -1;
    }
    return 0;
}

/* BUILD: gcc 에러 텍스트 → 분석 */
static int handle_build(const char *body, char *out, size_t outsz)
{
    const char *marker = strstr(body, MSG_BUILD_SOURCE_MARKER);
    char *diagnostics;
    size_t diagnostic_len;
    int result;

    if (marker == NULL)
        return analyze_build(body, out, outsz);
    diagnostic_len = (size_t)(marker - body);
    diagnostics = malloc(diagnostic_len + 1);
    if (diagnostics == NULL)
        return -1;
    memcpy(diagnostics, body, diagnostic_len);
    diagnostics[diagnostic_len] = '\0';
    result = analyze_build_with_source(
        diagnostics, marker + strlen(MSG_BUILD_SOURCE_MARKER), out, outsz);
    free(diagnostics);
    return result;
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
        log_msg("수신 실패 %s", ip);
        stats_end(0);
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
        size_t out_len = strlen(out);
        if (out_len > 0 && out[out_len - 1] != '\n' && out_len < sizeof(out) - 1) {
            out[out_len++] = '\n';
            out[out_len] = '\0';
        }
        if (send_msg(fd, MSG_RESULT, out, (uint32_t)out_len) < 0)
            log_msg("송신 실패 %s (손님이 먼저 끊음)", ip);
    } else {
        static const char err[] = "서버가 요청을 처리하지 못했습니다\n";
        if (send_msg(fd, MSG_ERROR, err, strlen(err)) < 0)
            log_msg("송신 실패 %s (손님이 먼저 끊음)", ip);
    }

    log_msg("처리 %s type=%d → %s", ip, type, r == 0 ? "RESULT" : "ERROR");
    stats_end(r == 0);
    close(fd);
    return NULL;
}

static void usage(void)
{
    fprintf(stderr, "사용법: crashdoc-server <포트> [--no-llm]\n");
}

int main(int argc, char *argv[])
{
    int listen_fd;
    int port;
    int i;
    int use_llm = 1;                        
    signal(SIGPIPE, SIG_IGN);          

    if (argc < 2) {
        usage();
        return 1;
    }

    port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        usage();
        return 1;
    }

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--no-llm") == 0) {
            use_llm = 0;
        } else {
            usage();                       
            return 1;
        }
    }

    analyzer_init(use_llm);

    if (log_open() < 0) {
        perror("log_open");
        return 1;
    }
    log_msg("서버 시작 port=%d llm=%s", port, use_llm ? "on" : "off");

    listen_fd = open_listen(port);

    if (listen_fd < 0) {
        log_msg("open_listen 실패 port=%d", port);
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
        log_msg("접속 %s", ca->ip); 

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
