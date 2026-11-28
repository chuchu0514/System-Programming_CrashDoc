#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "protocol.h"
#include "server.h"

int parse_server_address(const char *text, struct server_address *address)
{
    const char *separator = strrchr(text, ':');
    const char *host = text;
    if (!separator || separator == host)
        return -1;

    size_t host_len = (size_t)(separator - host);
    size_t port_len = strlen(separator + 1);

    /* IPv6 주소는 [주소]:포트 형태로 받는다. */
    if (*host == '[' && host_len > 2 && separator[-1] == ']') {
        ++host;
        host_len -= 2;
    } else if (memchr(host, ':', host_len)) {
        return -1;
    }

    if (host_len >= sizeof(address->host) || !port_len ||
        strspn(separator + 1, "0123456789") != port_len)
        return -1;

    long port = strtol(separator + 1, NULL, 10);
    if (port < 1 || port > 65535)
        return -1;

    memcpy(address->host, host, host_len);
    address->host[host_len] = '\0';
    snprintf(address->port, sizeof(address->port), "%ld", port);
    return 0;
}

static int connect_server(const struct server_address *address)
{
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *addresses;
    if (getaddrinfo(address->host, address->port, &hints, &addresses) != 0)
        return -1;

    /* 연결·송신 시간과 서버 응답 대기 시간은 별도로 둔다. */
    const struct timeval send_timeout = { .tv_sec = 5 };
    const struct timeval response_timeout = { .tv_sec = 610 };
    int fd = -1;
    for (struct addrinfo *entry = addresses; entry; entry = entry->ai_next) {
        fd = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (fd < 0)
            continue;
        if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout,
                       sizeof(send_timeout)) == 0 &&
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &response_timeout,
                       sizeof(response_timeout)) == 0 &&
            connect(fd, entry->ai_addr, entry->ai_addrlen) == 0)
            break;

        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    return fd;
}

/* 요청 종류에 관계없이 연결 하나에서 응답 하나를 받는다. */
int request_server(const struct server_address *address, uint8_t request,
                   const char *body, uint32_t length)
{
    int fd = connect_server(address);
    if (fd < 0) {
        fputs(request == MSG_HISTORY || request == MSG_SHOW
              ? "서버에 연결할 수 없어 기록을 조회하지 못했습니다\n"
              : "서버에 연결할 수 없어 분석을 생략합니다\n", stderr);
        return -1;
    }

    /* 송신 중 연결이 닫혀도 SIGPIPE로 프로세스가 종료되지 않게 한다. */
    struct sigaction ignore = { .sa_handler = SIG_IGN };
    struct sigaction previous;
    sigemptyset(&ignore.sa_mask);
    if (sigaction(SIGPIPE, &ignore, &previous) < 0) {
        perror("sigaction");
        close(fd);
        return -1;
    }
    int sent = send_msg(fd, request, body, length);
    sigaction(SIGPIPE, &previous, NULL);
    if (sent < 0) {
        fprintf(stderr, "%s\n", request == MSG_ANALYZE
                ? "서버에 크래시 정보를 보내지 못했습니다."
                : "서버에 요청을 보내지 못했습니다.");
        close(fd);
        return -1;
    }

    char response[MSG_MAX_BODY + 1];
    uint8_t type;
    int count = recv_msg(fd, &type, response, sizeof(response));
    close(fd);
    if (count < 0) {
        fputs("서버 응답을 받지 못했습니다.\n", stderr);
    } else if (type == MSG_RESULT || type == MSG_ERROR) {
        /* 서버가 보낸 본문은 그대로 출력한다. */
        FILE *output = type == MSG_RESULT ? stdout : stderr;
        if (fwrite(response, 1, (size_t)count, output) == (size_t)count &&
            fflush(output) == 0 && type == MSG_RESULT)
            return 0;
    } else {
        fputs("서버가 알 수 없는 응답을 보냈습니다.\n", stderr);
    }
    return -1;
}

void request_analysis(const struct server_address *address,
                      const char *body, uint32_t length)
{
    request_server(address, MSG_ANALYZE, body, length);
}
