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

int main(int argc, char *argv[])
{
    int listen_fd;
    char buf[MSG_MAX_BODY + 1];      
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
        uint8_t type;
        static const char reply[] =
            "원인 추정: (임시 서버) 아직 분석기가 연결되지 않았습니다\n"
            "점검: 서버 연동 테스트용 고정 응답입니다\n";

        conn_fd = accept(listen_fd, NULL, NULL);         
        if (conn_fd < 0)
            continue;

        if (recv_msg(conn_fd, &type, buf, sizeof(buf)) >= 0)
            send_msg(conn_fd, MSG_RESULT, reply, strlen(reply));

        close(conn_fd);                                    
    }

    close(listen_fd);
    return 0;
}