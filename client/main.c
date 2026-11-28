#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "protocol.h"
#include "run.h"

static int usage(void)
{
    fputs("사용법:\n"
          "  crashdoc run [--server 호스트:포트] <프로그램> [인자...]\n"
          "  crashdoc build [--server 호스트:포트] gcc [인자...]\n"
          "  crashdoc history [--server 호스트:포트] [-n 개수]\n"
          "  crashdoc show [--server 호스트:포트] <번호>\n", stderr);
    return 1;
}

/* 서버의 int 범위에 맞춰 양의 정수만 받는다. */
static int positive_number(const char *text)
{
    if (!*text || text[strspn(text, "0123456789")])
        return 0;
    errno = 0;
    long value = strtol(text, NULL, 10);
    return errno == 0 && value > 0 && value <= INT_MAX ? (int)value : 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        return usage();

    /* 접속 주소는 옵션, 환경변수, 기본값 순으로 선택한다. */
    int first = 2;
    const char *endpoint = getenv("CRASHDOC_SERVER");
    if (first < argc && strcmp(argv[first], "--server") == 0) {
        if (argc < 4)
            return usage();
        endpoint = argv[3];
        first = 4;
    } else if (!endpoint || !*endpoint) {
        endpoint = "127.0.0.1:9000";
    }

    struct server_address address;
    if (parse_server_address(endpoint, &address) < 0) {
        fprintf(stderr, "서버 주소는 호스트:포트 형식이어야 합니다.\n");
        return 1;
    }

    const char *command = argv[1];
    int count = argc - first;
    char **args = argv + first;
    if (strcmp(command, "run") == 0)
        return count > 0 ? run_program(args, &address) : usage();
    if (strcmp(command, "build") == 0)
        return count > 0 && strcmp(args[0], "gcc") == 0
               ? build_program(args, &address) : usage();

    char body[32];
    uint8_t type;
    int length;
    if (strcmp(command, "history") == 0) {
        int limit = 10;
        if (count != 0) {
            if (count != 2 || strcmp(args[0], "-n") != 0)
                return usage();
            limit = positive_number(args[1]);
            if (limit == 0)
                return usage();
        }
        type = MSG_HISTORY;
        length = snprintf(body, sizeof(body), "n=%d", limit);
    } else if (strcmp(command, "show") == 0) {
        int id = count == 1 ? positive_number(args[0]) : 0;
        if (id == 0)
            return usage();
        type = MSG_SHOW;
        length = snprintf(body, sizeof(body), "id=%d", id);
    } else {
        return usage();
    }
    return request_server(&address, type, body, (uint32_t)length) == 0 ? 0 : 1;
}
